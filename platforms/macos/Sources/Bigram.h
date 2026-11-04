// Word bigram probabilities P(next | previous), trained by
// tools/bigram-trainer from segmented text. Read-only at runtime.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ProjectKey {

class Bigram {
 public:
  struct Pair {
    uint64_t key;  // previous id << 32 | next id
    float log10Probability;
  };

  // File layout, little endian: magic, word count, words as (uint16 length,
  // UTF-8 bytes), pair count, pairs sorted by key.
  // The empty word stands for the start and end of a run of Han characters:
  // P(word | kBoundary) is how often a phrase starts with word, and
  // P(kBoundary | word) how often word ends one.
  static constexpr const char* kBoundary = "";

  static constexpr char kMagic[8] = {'P', 'K', 'B', 'G', '0', '0', '0', '1'};

  bool load(const std::string& path);
  static bool save(const std::string& path, const std::vector<std::string>& words, const std::vector<Pair>& pairs);

  bool isLoaded() const { return !pairs_.empty(); }
  size_t pairCount() const { return pairs_.size(); }
  std::optional<double> log10Probability(const std::string& previous, const std::string& next) const;

 private:
  std::unordered_map<std::string, uint32_t> ids_;
  std::vector<Pair> pairs_;
};

}  // namespace ProjectKey
