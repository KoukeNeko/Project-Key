// data.txt in less space (lexicon.bin): its readings as a finite-state transducer, a minimal
// automaton whose paths number the readings, so readings sharing a start or an end share
// states and a prefix can be walked, as typing without tones needs. Read-only after load.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "MemoryMappedFile.h"

namespace ProjectKey {

class LexiconFile {
 public:
  struct Row {
    std::string reading, word;
    float score;
  };

  // rows sorted by the bytes of reading, as data.txt is.
  // File layout, little endian: magic, uint32 symbol, state, arc, reading counts and row bytes;
  // the symbols (code points, ascending); per state its first arc (one more for the end) and
  // how many readings go through it, the top bit if one ends there; per arc its symbol index
  // (uint8), then its target; per reading where its rows start (one more for the end); the rows
  // as (uint8 length, UTF-8 word, float32 score).
  static bool save(const std::string& path, const std::vector<Row>& rows);
  bool load(const std::string& path);
  bool isLoaded() const { return readingCount_ > 0; }

  // Appends reading's rows as data.txt's lines; false when it has none.
  bool appendLines(const std::string& reading, std::string& lines) const;
  // The readings that start with prefix, in the order of data.txt.
  std::vector<std::string> readingsStartingWith(const std::string& prefix) const;

 private:
  uint32_t u32(const char* table, size_t index) const;
  std::optional<uint32_t> targetOf(uint32_t state, uint8_t symbol) const;
  std::optional<uint8_t> symbolOf(uint32_t codePoint) const;
  void collect(uint32_t state, std::string& reading, std::vector<std::string>& readings) const;

  McBopomofo::MemoryMappedFile file_;
  std::vector<uint32_t> symbols_;
  const char* firstArcs_ = nullptr;
  const char* counts_ = nullptr;
  const char* arcSymbols_ = nullptr;
  const char* arcTargets_ = nullptr;
  const char* rowStarts_ = nullptr;
  const char* rows_ = nullptr;
  uint32_t readingCount_ = 0;
};

}  // namespace ProjectKey
