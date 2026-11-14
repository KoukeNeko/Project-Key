#include "AdvancedModel.h"

#include <algorithm>
#include <cmath>
#include <mutex>

#include "llama.h"

namespace ProjectKey {

namespace {

// A composition's best paths (Composer::kSentencePaths) fit one batch: about 20 tokens each.
constexpr int kBatchTokens = 512;
constexpr int kMaxSequences = 16;

}  // namespace

struct AdvancedModel::Llama {
  llama_model* model = nullptr;
  llama_context* context = nullptr;
  llama_batch batch{};
  int vocabTokens = 0;
  ~Llama() {
    if (batch.token) llama_batch_free(batch);
    if (context) llama_free(context);
    if (model) llama_model_free(model);
  }
};

AdvancedModel::AdvancedModel() = default;
AdvancedModel::~AdvancedModel() = default;

bool AdvancedModel::load(const std::string& path, int threads) {
  static std::once_flag backend;
  std::call_once(backend, [] {
    llama_log_set([](ggml_log_level, const char*, void*) {}, nullptr);  // it would write to the app's stderr
    llama_backend_init();
  });
  auto llama = std::make_unique<Llama>();
  llama_model_params modelParams = llama_model_default_params();
  modelParams.n_gpu_layers = 0;
  llama->model = llama_model_load_from_file(path.c_str(), modelParams);
  if (!llama->model) return false;
  llama_context_params contextParams = llama_context_default_params();
  contextParams.n_ctx = kBatchTokens;
  contextParams.n_batch = kBatchTokens;
  contextParams.n_ubatch = kBatchTokens;
  contextParams.n_seq_max = kMaxSequences;
  contextParams.kv_unified = true;  // without it Gemma 4's logits come out wrong for several sequences
  contextParams.n_threads = threads;
  contextParams.n_threads_batch = threads;
  llama->context = llama_init_from_model(llama->model, contextParams);
  if (!llama->context) return false;
  llama->batch = llama_batch_init(kBatchTokens, 0, 1);
  llama->vocabTokens = llama_vocab_n_tokens(llama_model_get_vocab(llama->model));
  llama_ = std::move(llama);
  return true;
}

bool AdvancedModel::isLoaded() const { return llama_ != nullptr; }

std::vector<double> AdvancedModel::log10Probabilities(const std::vector<std::string>& sentences) {
  std::vector<double> result(sentences.size(), NAN);
  if (!llama_) return result;
  const llama_vocab* vocab = llama_model_get_vocab(llama_->model);
  std::vector<std::vector<llama_token>> tokens(sentences.size());
  for (size_t i = 0; i < sentences.size(); i++) {
    tokens[i].resize(sentences[i].size() + 8);
    int n = llama_tokenize(vocab, sentences[i].data(), static_cast<int>(sentences[i].size()), tokens[i].data(),
                           static_cast<int>(tokens[i].size()), /*add_special=*/true, /*parse_special=*/false);
    tokens[i].resize(std::max(n, 0));
  }
  llama_batch& batch = llama_->batch;
  llama_memory_t memory = llama_get_memory(llama_->context);
  for (size_t next = 0; next < sentences.size();) {
    // One sequence per sentence, as many as fit; one too long for a batch stays NaN.
    size_t first = next;
    int used = 0;
    while (next < sentences.size() && static_cast<int>(next - first) < kMaxSequences) {
      int n = static_cast<int>(tokens[next].size());
      if (n < 2 || n > kBatchTokens) {  // nothing to predict, or too long
        if (next == first) first++;
        next++;
        continue;
      }
      if (used + n > kBatchTokens) break;
      for (int p = 0; p < n; p++) {
        batch.token[used + p] = tokens[next][p];
        batch.pos[used + p] = p;
        batch.n_seq_id[used + p] = 1;
        batch.seq_id[used + p][0] = static_cast<llama_seq_id>(next - first);
        batch.logits[used + p] = 1;
      }
      used += n;
      next++;
    }
    if (used == 0) continue;
    batch.n_tokens = used;
    bool decoded = llama_decode(llama_->context, batch) == 0;
    int row = 0;
    for (size_t s = first; s < next && decoded; s++) {
      int n = static_cast<int>(tokens[s].size());
      if (n < 2 || n > kBatchTokens) continue;
      double total = 0;
      for (int p = 0; p + 1 < n; p++) {
        // log P(next token) = its logit - log(sum of exp(logits)).
        const float* logits = llama_get_logits_ith(llama_->context, row + p);
        float top = *std::max_element(logits, logits + llama_->vocabTokens);
        double sum = 0;
        for (int v = 0; v < llama_->vocabTokens; v++) sum += std::exp(static_cast<double>(logits[v] - top));
        total += logits[tokens[s][p + 1]] - top - std::log(sum);
      }
      result[s] = total / std::log(10.0);
      row += n;
    }
    llama_memory_clear(memory, true);
  }
  return result;
}

}  // namespace ProjectKey
