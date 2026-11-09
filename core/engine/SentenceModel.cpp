#include "SentenceModel.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <mutex>
#include <thread>

namespace ProjectKey {

namespace {

constexpr char kMagic[8] = {'P', 'K', 'S', 'M', '0', '0', '0', '1'};
constexpr uint32_t kStart = 2, kEnd = 3;  // [CLS] and [SEP], as tools/lm-trainer/prepare.py wraps sentences

float HalfToFloat(uint16_t h) {
  uint32_t sign = (h & 0x8000u) << 16, exponent = (h >> 10) & 0x1f, mantissa = h & 0x3ff, bits;
  if (exponent == 0) {
    if (mantissa == 0) {
      bits = sign;
    } else {  // subnormal: normalize it
      exponent = 127 - 15 + 1;
      while (!(mantissa & 0x400)) {
        mantissa <<= 1;
        exponent--;
      }
      bits = sign | exponent << 23 | (mantissa & 0x3ff) << 13;
    }
  } else if (exponent == 31) {
    bits = sign | 0x7f800000u | mantissa << 13;
  } else {
    bits = sign | (exponent + 127 - 15) << 23 | mantissa << 13;
  }
  float f;
  std::memcpy(&f, &bits, sizeof f);
  return f;
}

// Runs task(0..count-1) on the calling thread and a few that wait for work between calls.
class Pool {
 public:
  static Pool& Shared() {
    static Pool pool(std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 1, 0, 7));
    return pool;
  }
  void run(int count, const std::function<void(int)>& task) {
    if (workers_.empty() || count <= 1) {
      for (int i = 0; i < count; i++) task(i);
      return;
    }
    std::unique_lock<std::mutex> lock(mutex_);
    task_ = &task;
    count_ = count;
    next_ = 0;
    done_ = 0;
    generation_++;
    wake_.notify_all();
    lock.unlock();
    work();
    lock.lock();
    finished_.wait(lock, [&] { return done_ == count_; });
    task_ = nullptr;
  }
  ~Pool() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
    }
    wake_.notify_all();
    for (std::thread& worker : workers_) worker.join();
  }

 private:
  explicit Pool(int threads) {
    for (int i = 0; i < threads; i++) {
      workers_.emplace_back([this] {
        unsigned seen = 0;
        for (;;) {
          {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] { return stopping_ || generation_ != seen; });
            if (stopping_) return;
            seen = generation_;
          }
          work();
        }
      });
    }
  }
  void work() {
    for (;;) {
      int i = next_++;
      if (i >= count_) return;
      (*task_)(i);
      std::lock_guard<std::mutex> lock(mutex_);
      if (++done_ == count_) finished_.notify_all();
    }
  }
  std::vector<std::thread> workers_;
  std::mutex mutex_;
  std::condition_variable wake_, finished_;
  // Atomic: a worker late from the call before may read them while the next call sets them.
  std::atomic<const std::function<void(int)>*> task_{nullptr};
  std::atomic<int> next_{0}, count_{0};
  int done_ = 0;
  unsigned generation_ = 0;
  bool stopping_ = false;
};

#if defined(_MSC_VER)
#define PK_RESTRICT __restrict
#else
#define PK_RESTRICT __restrict__
#endif

// y[0..n) += a * row[0..n), the loop every matrix product here spends its time in.
void AddScaled(float* PK_RESTRICT y, const float* PK_RESTRICT row, float a, int n) {
  for (int i = 0; i < n; i++) y[i] += a * row[i];
}

// y[b][n] = bias[n] + sum_k x[b][k] * w[k][n], in blocks of columns spread over the pool: a
// block of every row of y stays in cache while each row of w is read once for all rows of x.
void MatMul(const float* x, int rows, int in, const std::vector<float>& w, const std::vector<float>& bias, int out,
            float* y) {
  constexpr int kBlock = 256;
  int blocks = (out + kBlock - 1) / kBlock;
  Pool::Shared().run(blocks, [&](int block) {
    int from = block * kBlock, to = std::min(out, from + kBlock);
    for (int b = 0; b < rows; b++) std::memcpy(y + b * out + from, bias.data() + from, (to - from) * sizeof(float));
    for (int k = 0; k < in; k++) {
      const float* row = w.data() + static_cast<size_t>(k) * out;
      for (int b = 0; b < rows; b++) AddScaled(y + b * out + from, row + from, x[b * in + k], to - from);
    }
  });
}

void LayerNorm(const float* x, int rows, int n, const std::vector<float>& w, const std::vector<float>& b, float* y) {
  for (int r = 0; r < rows; r++) {
    const float* xr = x + r * n;
    float mean = 0, var = 0;
    for (int i = 0; i < n; i++) mean += xr[i];
    mean /= n;
    for (int i = 0; i < n; i++) var += (xr[i] - mean) * (xr[i] - mean);
    float scale = 1 / std::sqrt(var / n + 1e-5f);
    for (int i = 0; i < n; i++) y[r * n + i] = (xr[i] - mean) * scale * w[i] + b[i];
  }
}

}  // namespace

bool SentenceModel::load(const std::string& path) {
  layers_ = 0;
  std::ifstream in(path, std::ios::binary);
  char magic[sizeof kMagic];
  uint32_t header[5];
  if (!in.read(magic, sizeof magic) || std::memcmp(magic, kMagic, sizeof kMagic) != 0) return false;
  if (!in.read(reinterpret_cast<char*>(header), sizeof header)) return false;
  int vocab = header[0], layers = header[1], heads = header[2], embd = header[3], context = header[4];
  if (vocab <= 4 || layers <= 0 || heads <= 0 || embd <= 0 || embd % heads || context <= 1 || vocab > 1 << 20 ||
      layers > 64 || embd > 4096 || context > 4096) {
    return false;
  }
  std::vector<uint32_t> codePoints(vocab);
  if (!in.read(reinterpret_cast<char*>(codePoints.data()), vocab * sizeof(uint32_t))) return false;
  bool ok = true;
  auto take = [&](size_t count) {
    std::vector<uint16_t> half(count);
    ok = ok && in.read(reinterpret_cast<char*>(half.data()), count * sizeof(uint16_t));
    std::vector<float> values(count);
    for (size_t i = 0; i < count; i++) values[i] = HalfToFloat(half[i]);
    return values;
  };
  size_t e = embd;
  std::vector<float> embedding = take(vocab * e);
  positionEmbedding_ = take(context * e);
  blocks_.clear();
  for (int l = 0; l < layers && ok; l++) {
    Layer layer;
    layer.ln1w = take(e), layer.ln1b = take(e);
    layer.attnW = take(e * 3 * e), layer.attnB = take(3 * e);
    layer.projW = take(e * e), layer.projB = take(e);
    layer.ln2w = take(e), layer.ln2b = take(e);
    layer.fcW = take(e * 4 * e), layer.fcB = take(4 * e);
    layer.fcProjW = take(4 * e * e), layer.fcProjB = take(e);
    blocks_.push_back(std::move(layer));
  }
  lnfW_ = take(e), lnfB_ = take(e);
  if (!ok) return false;
  tokenEmbedding_.resize(embedding.size());  // transposed, so MatMul reads it row by row
  for (size_t v = 0; v < static_cast<size_t>(vocab); v++) {
    for (size_t i = 0; i < e; i++) tokenEmbedding_[i * vocab + v] = embedding[v * e + i];
  }
  zeros_.assign(vocab, 0);
  ids_.clear();
  for (int i = 0; i < vocab; i++) {
    if (codePoints[i]) ids_.emplace(codePoints[i], i);
  }
  vocab_ = vocab, heads_ = heads, embd_ = embd, context_ = context, layers_ = layers;
  return true;
}

std::optional<std::vector<uint32_t>> SentenceModel::encode(const std::vector<uint32_t>& codePoints) const {
  std::vector<uint32_t> tokens;
  tokens.reserve(codePoints.size());
  for (uint32_t c : codePoints) {
    auto it = ids_.find(c);
    if (it == ids_.end()) return std::nullopt;
    tokens.push_back(it->second);
  }
  return tokens;
}

// Runs each node's position; a parent is in the cache already or earlier in the batch, whose keys
// and values for a layer are all in place before any node attends in that layer.
void SentenceModel::run(std::vector<Node*>& nodes, const std::vector<uint32_t>& tokens) const {
  const int rows = static_cast<int>(nodes.size()), e = embd_, headSize = e / heads_;
  std::vector<float> x(rows * e), a(rows * e), qkv(rows * 3 * e), attended(rows * e), out(rows * e), hidden(rows * 4 * e);
  for (int r = 0; r < rows; r++) {
    Node& node = *nodes[r];
    node.keys.assign(static_cast<size_t>(layers_) * e, 0);
    node.values.assign(static_cast<size_t>(layers_) * e, 0);
    for (int i = 0; i < e; i++) {
      x[r * e + i] = tokenEmbedding_[static_cast<size_t>(i) * vocab_ + tokens[r]] + positionEmbedding_[node.position * e + i];
    }
  }
  std::vector<float> weights(context_);
  for (int l = 0; l < layers_; l++) {
    const Layer& layer = blocks_[l];
    LayerNorm(x.data(), rows, e, layer.ln1w, layer.ln1b, a.data());
    MatMul(a.data(), rows, e, layer.attnW, layer.attnB, 3 * e, qkv.data());
    for (int r = 0; r < rows; r++) {
      std::memcpy(&nodes[r]->keys[l * e], &qkv[r * 3 * e + e], e * sizeof(float));
      std::memcpy(&nodes[r]->values[l * e], &qkv[r * 3 * e + 2 * e], e * sizeof(float));
    }
    for (int r = 0; r < rows; r++) {
      const float* q = &qkv[r * 3 * e];
      for (int h = 0; h < heads_; h++) {
        // Positions 0..this one, walking the parents back.
        float most = -std::numeric_limits<float>::infinity();
        int count = 0;
        for (const Node* at = nodes[r]; at; at = at->parent, count++) {
          float dot = 0;
          for (int i = 0; i < headSize; i++) dot += q[h * headSize + i] * at->keys[l * e + h * headSize + i];
          weights[count] = dot / std::sqrt(static_cast<float>(headSize));
          most = std::max(most, weights[count]);
        }
        float sum = 0;
        for (int k = 0; k < count; k++) sum += weights[k] = std::exp(weights[k] - most);
        float* o = &attended[r * e + h * headSize];
        std::fill(o, o + headSize, 0.0f);
        int k = 0;
        for (const Node* at = nodes[r]; at; at = at->parent, k++) {
          float weight = weights[k] / sum;
          for (int i = 0; i < headSize; i++) o[i] += weight * at->values[l * e + h * headSize + i];
        }
      }
    }
    MatMul(attended.data(), rows, e, layer.projW, layer.projB, e, out.data());
    for (int i = 0; i < rows * e; i++) x[i] += out[i];
    LayerNorm(x.data(), rows, e, layer.ln2w, layer.ln2b, a.data());
    MatMul(a.data(), rows, e, layer.fcW, layer.fcB, 4 * e, hidden.data());
    for (float& v : hidden) {  // GPT-2's tanh approximation of GELU ("gelu_new")
      v = 0.5f * v * (1 + std::tanh(0.7978845608f * (v + 0.044715f * v * v * v)));
    }
    MatMul(hidden.data(), rows, 4 * e, layer.fcProjW, layer.fcProjB, e, out.data());
    for (int i = 0; i < rows * e; i++) x[i] += out[i];
  }
  LayerNorm(x.data(), rows, e, lnfW_, lnfB_, a.data());
  // The normalizer over the whole vocabulary: every logit, against the transposed embedding.
  std::vector<float> all(static_cast<size_t>(rows) * vocab_);
  MatMul(a.data(), rows, e, tokenEmbedding_, zeros_, vocab_, all.data());
  for (int r = 0; r < rows; r++) {
    const float* logits = &all[static_cast<size_t>(r) * vocab_];
    float most = *std::max_element(logits, logits + vocab_);
    double sum = 0;
    for (int v = 0; v < vocab_; v++) sum += std::exp(logits[v] - most);
    nodes[r]->logSum = static_cast<float>(most + std::log(sum));
    nodes[r]->output.assign(a.begin() + r * e, a.begin() + (r + 1) * e);
  }
}

std::vector<double> SentenceModel::log10Probabilities(const std::vector<std::vector<uint32_t>>& sentences,
                                                      Cache& cache) const {
  std::vector<double> result(sentences.size(), 0.0);
  if (!isLoaded()) return result;
  cache.call++;
  // Every prefix, [CLS] first, is a node; build the missing ones depth by depth, batched.
  size_t longest = 0;
  for (const auto& s : sentences) longest = std::max(longest, s.size());
  longest = std::min(longest + 1, static_cast<size_t>(context_));  // positions: [CLS] and the characters
  std::vector<std::u32string> prefixes(sentences.size(), std::u32string(1, kStart));
  // All missing nodes in one batch, parents first: a layer needs only its own input and the
  // keys and values its ancestors left in the same layer, so each weight matrix is read once.
  std::vector<Node*> batch;
  std::vector<uint32_t> tokens;
  for (size_t depth = 0; depth < longest; depth++) {
    std::vector<std::u32string> added;
    for (size_t s = 0; s < sentences.size(); s++) {
      if (depth > sentences[s].size()) continue;
      if (depth > 0) prefixes[s] += static_cast<char32_t>(sentences[s][depth - 1]);
      if (cache.nodes.count(prefixes[s]) || std::find(added.begin(), added.end(), prefixes[s]) != added.end()) continue;
      added.push_back(prefixes[s]);
    }
    for (const std::u32string& prefix : added) {
      Node& node = cache.nodes[prefix];
      node.position = static_cast<int>(depth);
      node.parent = depth > 0 ? &cache.nodes.at(prefix.substr(0, prefix.size() - 1)) : nullptr;
      batch.push_back(&node);
      tokens.push_back(prefix.back());
    }
  }
  if (!batch.empty()) run(batch, tokens);
  cache.computed += batch.size();
  // Sum log P(next | prefix) along each sentence, its end included.
  for (size_t s = 0; s < sentences.size(); s++) {
    std::u32string prefix(1, kStart);
    double total = 0;
    size_t usable = std::min(sentences[s].size(), static_cast<size_t>(context_) - 1);
    for (size_t i = 0; i <= usable; i++) {
      Node& node = cache.nodes.at(prefix);
      node.used = cache.call;
      uint32_t next = i < usable ? sentences[s][i] : kEnd;
      float dot = 0;
      for (int k = 0; k < embd_; k++) dot += node.output[k] * tokenEmbedding_[static_cast<size_t>(k) * vocab_ + next];
      total += dot - node.logSum;
      if (i < usable) prefix += static_cast<char32_t>(next);
    }
    result[s] = total / std::log(10.0);
  }
  // Unread nodes have only unread descendants, so no kept node points at a removed one.
  if (cache.nodes.size() > kCacheSize) std::erase_if(cache.nodes, [&](const auto& entry) { return entry.second.used != cache.call; });
  return result;
}

}  // namespace ProjectKey
