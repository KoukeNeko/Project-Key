#include "Bigram.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace ProjectKey {

namespace {

template <typename T>
bool Read(std::ifstream& in, T& value) {
  return static_cast<bool>(in.read(reinterpret_cast<char*>(&value), sizeof value));
}

template <typename T>
void Write(std::ofstream& out, const T& value) {
  out.write(reinterpret_cast<const char*>(&value), sizeof value);
}

}  // namespace

bool Bigram::load(const std::string& path) {
  ids_.clear();
  pairs_.clear();
  std::ifstream in(path, std::ios::binary);
  char magic[sizeof kMagic];
  if (!in.read(magic, sizeof magic) || std::memcmp(magic, kMagic, sizeof kMagic) != 0) return false;
  uint32_t wordCount = 0;
  if (!Read(in, wordCount)) return false;
  for (uint32_t id = 0; id < wordCount; id++) {
    uint16_t length = 0;
    if (!Read(in, length)) return false;
    std::string word(length, '\0');
    if (!in.read(word.data(), length)) return false;
    ids_.emplace(std::move(word), id);
  }
  uint32_t pairCount = 0;
  if (!Read(in, pairCount)) return false;
  pairs_.resize(pairCount);
  if (!in.read(reinterpret_cast<char*>(pairs_.data()), static_cast<std::streamsize>(pairCount * sizeof(Pair)))) {
    ids_.clear();
    pairs_.clear();
    return false;
  }
  return true;
}

bool Bigram::save(const std::string& path, const std::vector<std::string>& words, const std::vector<Pair>& pairs) {
  std::ofstream out(path, std::ios::binary);
  out.write(kMagic, sizeof kMagic);
  Write(out, static_cast<uint32_t>(words.size()));
  for (const std::string& word : words) {
    Write(out, static_cast<uint16_t>(word.size()));
    out.write(word.data(), static_cast<std::streamsize>(word.size()));
  }
  Write(out, static_cast<uint32_t>(pairs.size()));
  out.write(reinterpret_cast<const char*>(pairs.data()), static_cast<std::streamsize>(pairs.size() * sizeof(Pair)));
  return static_cast<bool>(out);
}

std::optional<double> Bigram::log10Probability(const std::string& previous, const std::string& next) const {
  auto p = ids_.find(previous);
  auto n = ids_.find(next);
  if (p == ids_.end() || n == ids_.end()) return std::nullopt;
  uint64_t key = static_cast<uint64_t>(p->second) << 32 | n->second;
  auto it = std::lower_bound(pairs_.begin(), pairs_.end(), key, [](const Pair& pair, uint64_t k) { return pair.key < k; });
  if (it == pairs_.end() || it->key != key) return std::nullopt;
  return it->log10Probability;
}

}  // namespace ProjectKey
