// Word bigram probabilities P(next | previous) and trigram probabilities
// P(next | first, second), trained by tools/bigram-trainer from segmented
// text. Read-only at runtime.
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
    uint64_t key;  // bigram: previous id << 32 | next id; trigram: TripleKey(first, second, next)
    float log10Probability;
  };

  // File layout, little endian: magic, word count, words as (uint16 length,
  // UTF-8 bytes), pair count, pairs sorted by key, then optionally a triple
  // count and triples (also Pair records) sorted by key.
  // The empty word stands for the start and end of a run of Han characters:
  // P(word | kBoundary) is how often a phrase starts with word, and
  // P(kBoundary | word) how often word ends one.
  static constexpr const char* kBoundary = "";

  static constexpr char kMagic[8] = {'P', 'K', 'B', 'G', '0', '0', '0', '1'};

  static constexpr int kTripleIdBits = 21;
  static uint64_t TripleKey(uint64_t first, uint64_t second, uint64_t next) {
    return first << (2 * kTripleIdBits) | second << kTripleIdBits | next;
  }

  bool load(const std::string& path);
  static bool save(const std::string& path, const std::vector<std::string>& words, const std::vector<Pair>& pairs,
                   const std::vector<Pair>& triples = {});

  bool isLoaded() const { return !pairs_.empty(); }
  size_t pairCount() const { return pairs_.size(); }
  std::optional<uint32_t> id(const std::string& word) const;
  std::optional<double> log10Probability(uint32_t previous, uint32_t next) const;
  std::optional<double> log10Probability(uint32_t first, uint32_t second, uint32_t next) const;

 private:
  std::unordered_map<std::string, uint32_t> ids_;
  std::vector<Pair> pairs_;
  std::vector<Pair> triples_;
};

}  // namespace ProjectKey
