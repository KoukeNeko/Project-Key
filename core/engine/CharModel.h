// Character trigram probabilities P(next | two characters before), trained by
// tools/bigram-trainer from the same text as Bigram. Word pairs the text never
// shows (候選 then 字) still get a probability from their characters (選字).
// Read-only at runtime.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "MemoryMappedFile.h"

namespace ProjectKey {

class CharModel {
 public:
#pragma pack(push, 4)
  struct Record {
    // Code points of 21 bits: c, b << 21 | c or a << 42 | b << 21 | c, or a context without its last.
    uint64_t key;
    float value;  // a probability, or for a context its backoff weight
  };
#pragma pack(pop)

  // File layout, little endian: magic, then five tables of (uint32 count, Records sorted
  // by key): character probabilities, bigram contexts, bigrams, trigram contexts, trigrams.
  // Probabilities are interpolated absolute discounting: P(c | a b) is the trigram's
  // discounted probability plus its context's backoff weight times P(c | b), and likewise
  // down to P(c). Code point 0 stands for the start and end of a run of Han characters.
  static constexpr char kMagic[8] = {'P', 'K', 'C', 'H', '0', '0', '0', '1'};
  static constexpr int kBits = 21;
  static constexpr uint32_t kBoundary = 0;

  struct Tables {
    std::vector<Record> unigrams, bigramContexts, bigrams, trigramContexts, trigrams;
  };

  bool load(const std::string& path);
  static bool save(const std::string& path, const Tables& tables);

  // Records in the mapped file.
  struct Table {
    const char* data = nullptr;
    size_t count = 0;
  };

  bool isLoaded() const { return unigrams_.count > 0; }
  // P(next | first, second).
  double probability(uint32_t first, uint32_t second, uint32_t next) const;
  // log10 P(chars | first, second), each character given the two before it.
  double log10Probability(uint32_t first, uint32_t second, const std::vector<uint32_t>& chars) const;
  static std::vector<uint32_t> CodePoints(const std::string& utf8);

 private:
  double probability(uint32_t second, uint32_t next) const;
  double probability(uint32_t next) const;

  // Per character: its probability, its backoff weight as a bigram context, and where the
  // records it begins are, so a lookup searches those instead of a whole table.
  struct Entry {
    float probability = 0;
    float backoff = -1;  // none: the character keeps no pairs
    uint32_t bigrams[2] = {0, 0}, trigramContexts[2] = {0, 0}, trigrams[2] = {0, 0};
  };

  McBopomofo::MemoryMappedFile file_;
  Table unigrams_, bigramContexts_, bigrams_, trigramContexts_, trigrams_;
  std::unordered_map<uint32_t, Entry> entries_;
};

}  // namespace ProjectKey
