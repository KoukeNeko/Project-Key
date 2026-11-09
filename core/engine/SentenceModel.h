// A small character-level GPT-2 (tools/lm-trainer) that scores whole sentences, so the
// composer can rerank its best paths by how the sentence reads as a whole rather than by the
// two words before each word. Read-only after load; the per-composer Cache holds the work
// done for sentences seen before, which on the next key are mostly the same sentences again.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ProjectKey {

class SentenceModel {
 public:
  // File layout: tools/lm-trainer/export.py.
  bool load(const std::string& path);
  bool isLoaded() const { return layers_ > 0; }

  // Token ids for Han characters, or nullopt when one is outside the vocabulary.
  std::optional<std::vector<uint32_t>> encode(const std::vector<uint32_t>& codePoints) const;

  struct Node {
    std::vector<float> keys, values;  // per layer, this position's attention keys and values
    std::vector<float> output;        // final hidden state, which predicts the next token
    float logSum = 0;                 // log of the softmax normalizer over that prediction
    const Node* parent = nullptr;
    int position = 0;
    unsigned used = 0;  // the call that last read it
  };
  // Prefix of token ids -> its last position's state. Past kCacheSize, the nodes the last call
  // did not read go (with their descendants, which it did not read either).
  struct Cache {
    std::unordered_map<std::u32string, Node> nodes;
    unsigned call = 0;
    size_t computed = 0;  // nodes run, for measurements
    void clear() { nodes.clear(); }
  };
  static constexpr size_t kCacheSize = 256;  // about 5 MB with the 384-wide, 6-layer model; one call reads fewer

  // log10 P(sentence, then its end) for each encoded sentence.
  std::vector<double> log10Probabilities(const std::vector<std::vector<uint32_t>>& sentences, Cache& cache) const;

 private:
  void run(std::vector<Node*>& nodes, const std::vector<uint32_t>& tokens) const;

  int vocab_ = 0, layers_ = 0, heads_ = 0, embd_ = 0, context_ = 0;
  std::unordered_map<uint32_t, uint32_t> ids_;  // code point -> token id
  struct Layer {
    std::vector<float> ln1w, ln1b, attnW, attnB, projW, projB, ln2w, ln2b, fcW, fcB, fcProjW, fcProjB;
  };
  std::vector<float> positionEmbedding_, lnfW_, lnfB_;
  std::vector<float> tokenEmbedding_, zeros_;  // [embd][vocab], as the output layer reads it, and a zero bias for it
  std::vector<Layer> blocks_;
};

}  // namespace ProjectKey
