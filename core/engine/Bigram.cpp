#include "Bigram.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstring>
#include <fstream>

namespace ProjectKey {

namespace {

template <typename T>
void Write(std::ofstream& out, const T& value) {
  out.write(reinterpret_cast<const char*>(&value), sizeof value);
}

uint32_t Hash(std::string_view word) {  // FNV-1a
  uint32_t hash = 2166136261u;
  for (char c : word) hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
  return hash;
}

}  // namespace

bool Bigram::load(const std::string& path) {
  wordOffsets_.clear();
  slots_.clear();
  pairs_ = triples_ = {};
  // The pair and triple tables stay in the mapped file: the system can drop those pages
  // under memory pressure, which an iOS keyboard needs.
  file_.close();
  if (!file_.open(path.c_str())) return false;
  const char* at = file_.data();
  const char* end = at + file_.length();
  auto take = [&](void* value, size_t size) {
    if (static_cast<size_t>(end - at) < size) return false;
    std::memcpy(value, at, size);
    at += size;
    return true;
  };
  auto table = [&](Table& table) {
    uint32_t count = 0;
    if (!take(&count, sizeof count) || static_cast<size_t>(end - at) / sizeof(Pair) < count) return false;
    table = {at, count};
    at += count * sizeof(Pair);
    return true;
  };
  auto fail = [&] {
    wordOffsets_.clear();
    slots_.clear();
    pairs_ = {};
    file_.close();
    return false;
  };
  char magic[sizeof kMagic];
  if (!take(magic, sizeof magic) || std::memcmp(magic, kMagic, sizeof kMagic) != 0) return fail();
  uint32_t wordCount = 0;
  if (!take(&wordCount, sizeof wordCount)) return fail();
  wordOffsets_.reserve(std::min<size_t>(wordCount, (end - at) / sizeof(uint16_t)));  // a word is at least its length
  for (uint32_t id = 0; id < wordCount; id++) {
    wordOffsets_.push_back(static_cast<uint32_t>(at - file_.data()));
    uint16_t length = 0;
    if (!take(&length, sizeof length) || static_cast<size_t>(end - at) < length) return fail();
    at += length;
  }
  slots_.assign(std::bit_ceil(std::max<size_t>(wordCount, 4) * 2), 0);
  for (uint32_t id = 0; id < wordCount; id++) {
    size_t slot = Hash(wordAt(id)) & (slots_.size() - 1);
    while (slots_[slot]) slot = (slot + 1) & (slots_.size() - 1);
    slots_[slot] = id + 1;
  }
  if (!table(pairs_)) return fail();
  if (!table(triples_)) triples_ = {};  // a bigram-only model
  return true;
}

bool Bigram::save(const std::string& path, const std::vector<std::string>& words, const std::vector<Pair>& pairs,
                  const std::vector<Pair>& triples) {
  std::ofstream out(path, std::ios::binary);
  out.write(kMagic, sizeof kMagic);
  Write(out, static_cast<uint32_t>(words.size()));
  for (const std::string& word : words) {
    Write(out, static_cast<uint16_t>(word.size()));
    out.write(word.data(), static_cast<std::streamsize>(word.size()));
  }
  Write(out, static_cast<uint32_t>(pairs.size()));
  out.write(reinterpret_cast<const char*>(pairs.data()), static_cast<std::streamsize>(pairs.size() * sizeof(Pair)));
  if (!triples.empty()) {
    Write(out, static_cast<uint32_t>(triples.size()));
    out.write(reinterpret_cast<const char*>(triples.data()), static_cast<std::streamsize>(triples.size() * sizeof(Pair)));
  }
  return static_cast<bool>(out);
}

std::string_view Bigram::wordAt(uint32_t id) const {
  const char* entry = file_.data() + wordOffsets_[id];
  uint16_t length;
  std::memcpy(&length, entry, sizeof length);
  return {entry + sizeof length, length};
}

std::optional<uint32_t> Bigram::id(const std::string& word) const {
  if (slots_.empty()) return std::nullopt;
  for (size_t slot = Hash(word) & (slots_.size() - 1); slots_[slot]; slot = (slot + 1) & (slots_.size() - 1)) {
    if (wordAt(slots_[slot] - 1) == word) return slots_[slot] - 1;
  }
  return std::nullopt;
}

namespace {
// Records start at any offset in the file, so they are read byte by byte, not as Pairs.
uint64_t KeyAt(const char* record) {
  uint64_t key;
  std::memcpy(&key, record + offsetof(Bigram::Pair, key), sizeof key);
  return key;
}

std::optional<double> Find(const Bigram::Table& table, uint64_t key) {
  size_t low = 0, high = table.count;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (KeyAt(table.data + middle * sizeof(Bigram::Pair)) < key) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  const char* record = table.data + low * sizeof(Bigram::Pair);
  if (low == table.count || KeyAt(record) != key) return std::nullopt;
  float probability;
  std::memcpy(&probability, record + offsetof(Bigram::Pair, log10Probability), sizeof probability);
  return probability;
}
}  // namespace

std::optional<double> Bigram::log10Probability(uint32_t previous, uint32_t next) const {
  return Find(pairs_, static_cast<uint64_t>(previous) << 32 | next);
}

std::optional<double> Bigram::log10Probability(uint32_t first, uint32_t second, uint32_t next) const {
  return Find(triples_, TripleKey(first, second, next));
}

}  // namespace ProjectKey
