#include "CharModel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <optional>

#include "Utf8.h"

namespace ProjectKey {

namespace {

// A character the text never had.
constexpr double kUnseen = 1e-8;

void WriteTable(std::ofstream& out, const std::vector<CharModel::Record>& records) {
  uint32_t count = static_cast<uint32_t>(records.size());
  out.write(reinterpret_cast<const char*>(&count), sizeof count);
  out.write(reinterpret_cast<const char*>(records.data()), static_cast<std::streamsize>(count * sizeof(CharModel::Record)));
}

// Records start at any offset in the file, so they are read byte by byte.
uint64_t KeyAt(const char* record) {
  uint64_t key;
  std::memcpy(&key, record + offsetof(CharModel::Record, key), sizeof key);
  return key;
}

// The first record in [low, high) whose key is not below `key`.
size_t LowerBound(const CharModel::Table& table, uint64_t key, size_t low, size_t high) {
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (KeyAt(table.data + middle * sizeof(CharModel::Record)) < key) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  return low;
}

std::optional<double> Find(const CharModel::Table& table, uint64_t key, size_t low = 0, size_t high = SIZE_MAX) {
  high = std::min(high, table.count);
  size_t at = LowerBound(table, key, low, high);
  const char* record = table.data + at * sizeof(CharModel::Record);
  if (at == high || KeyAt(record) != key) return std::nullopt;
  float value;
  std::memcpy(&value, record + offsetof(CharModel::Record, value), sizeof value);
  return value;
}

// [first, last) of the records whose key, shifted right by `shift`, is `prefix`.
void RangeOf(const CharModel::Table& table, uint64_t prefix, int shift, uint32_t range[2]) {
  range[0] = static_cast<uint32_t>(LowerBound(table, prefix << shift, 0, table.count));
  range[1] = static_cast<uint32_t>(LowerBound(table, (prefix + 1) << shift, range[0], table.count));
}

}  // namespace

bool CharModel::load(const std::string& path) {
  unigrams_ = bigramContexts_ = bigrams_ = trigramContexts_ = trigrams_ = {};
  // The tables stay in the mapped file: the system can drop those pages under memory
  // pressure, which an iOS keyboard needs.
  file_.close();
  if (!file_.open(path.c_str())) return false;
  const char* at = file_.data();
  const char* end = at + file_.length();
  bool ok = file_.length() >= sizeof kMagic && std::memcmp(at, kMagic, sizeof kMagic) == 0;
  at += sizeof kMagic;
  for (Table* table : {&unigrams_, &bigramContexts_, &bigrams_, &trigramContexts_, &trigrams_}) {
    uint32_t count = 0;
    ok = ok && static_cast<size_t>(end - at) >= sizeof count;
    if (ok) std::memcpy(&count, at, sizeof count);
    ok = ok && static_cast<size_t>(end - at - sizeof count) / sizeof(Record) >= count;
    if (!ok) break;
    *table = {at + sizeof count, count};
    at += sizeof count + count * sizeof(Record);
  }
  if (!ok) {
    unigrams_ = bigramContexts_ = bigrams_ = trigramContexts_ = trigrams_ = {};
    file_.close();
    return false;
  }
  entries_.clear();
  entries_.reserve(unigrams_.count);
  for (size_t i = 0; i < unigrams_.count; i++) {
    const char* record = unigrams_.data + i * sizeof(Record);
    uint32_t c = static_cast<uint32_t>(KeyAt(record));
    Entry& entry = entries_[c];
    std::memcpy(&entry.probability, record + offsetof(Record, value), sizeof entry.probability);
    entry.backoff = static_cast<float>(Find(bigramContexts_, c).value_or(-1));
    RangeOf(bigrams_, c, kBits, entry.bigrams);
    RangeOf(trigramContexts_, c, kBits, entry.trigramContexts);
    RangeOf(trigrams_, c, 2 * kBits, entry.trigrams);
  }
  // The start of a run is no character the text has, but it begins pairs and triples.
  Entry& boundary = entries_[kBoundary];
  boundary.backoff = static_cast<float>(Find(bigramContexts_, kBoundary).value_or(-1));
  RangeOf(bigrams_, kBoundary, kBits, boundary.bigrams);
  RangeOf(trigramContexts_, kBoundary, kBits, boundary.trigramContexts);
  RangeOf(trigrams_, kBoundary, 2 * kBits, boundary.trigrams);
  return true;
}

bool CharModel::save(const std::string& path, const Tables& tables) {
  std::ofstream out(path, std::ios::binary);
  out.write(kMagic, sizeof kMagic);
  for (const std::vector<Record>* table : {&tables.unigrams, &tables.bigramContexts, &tables.bigrams, &tables.trigramContexts, &tables.trigrams}) {
    WriteTable(out, *table);
  }
  return static_cast<bool>(out);
}

double CharModel::probability(uint32_t next) const {
  auto it = entries_.find(next);
  return it != entries_.end() && it->second.probability > 0 ? it->second.probability : kUnseen;
}

double CharModel::probability(uint32_t second, uint32_t next) const {
  auto it = entries_.find(second);
  if (it == entries_.end() || it->second.backoff < 0) return probability(next);
  const Entry& entry = it->second;
  double kept = Find(bigrams_, static_cast<uint64_t>(second) << kBits | next, entry.bigrams[0], entry.bigrams[1]).value_or(0);
  return kept + entry.backoff * probability(next);
}

double CharModel::probability(uint32_t first, uint32_t second, uint32_t next) const {
  auto it = entries_.find(first);
  if (it == entries_.end()) return probability(second, next);
  const Entry& entry = it->second;
  uint64_t context = static_cast<uint64_t>(first) << kBits | second;
  std::optional<double> backoff = Find(trigramContexts_, context, entry.trigramContexts[0], entry.trigramContexts[1]);
  if (!backoff) return probability(second, next);
  double kept = Find(trigrams_, context << kBits | next, entry.trigrams[0], entry.trigrams[1]).value_or(0);
  return kept + *backoff * probability(second, next);
}

std::vector<uint32_t> CharModel::CodePoints(const std::string& utf8) {
  std::vector<uint32_t> result;
  for (size_t i = 0; i < utf8.size();) result.push_back(NextCodePoint(utf8, i));
  return result;
}

}  // namespace ProjectKey
