// Trains word bigrams and trigrams for the macOS input method.
// Usage: train <McBopomofo data.txt> <min pair count> <output> < text
// Segments each run of Han characters into dictionary words by maximizing the
// sum of McBopomofo unigram scores, the same scores the input method walks
// with, then counts adjacent word pairs and triples inside each run, including
// the run boundary (Bigram::kBoundary) at its start and end.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "Bigram.h"

namespace {

constexpr size_t kMaxWordLength = 8;  // ReadingGrid::kMaximumSpanLength
constexpr double kUnknownCharScore = -13;

bool IsHan(char32_t c) {
  return (c >= 0x3400 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff) || (c >= 0x20000 && c <= 0x2ffff);
}

// Splits UTF-8 into code points, keeping each one's bytes.
void Decode(const std::string& text, std::vector<char32_t>& points, std::vector<std::string>& bytes) {
  points.clear();
  bytes.clear();
  for (size_t i = 0; i < text.size();) {
    unsigned char lead = text[i];
    size_t length = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
    if (i + length > text.size()) break;
    char32_t point = length == 1 ? lead : lead & (0xff >> (length + 1));
    for (size_t k = 1; k < length; k++) point = point << 6 | (text[i + k] & 0x3f);
    points.push_back(point);
    bytes.push_back(text.substr(i, length));
    i += length;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "usage: " << argv[0] << " <data.txt> <min pair count> <output> < text\n";
    return 2;
  }
  const uint32_t minCount = static_cast<uint32_t>(std::stoul(argv[2]));

  std::unordered_map<std::string, double> lexicon;
  {
    std::ifstream data(argv[1]);
    std::string line;
    while (std::getline(data, line)) {
      if (line.empty() || line[0] == '#' || line[0] == '_') continue;
      std::istringstream fields(line);
      std::string reading, word;
      double score;
      if (!(fields >> reading >> word >> score)) continue;
      auto [it, inserted] = lexicon.emplace(word, score);
      if (!inserted) it->second = std::max(it->second, score);
    }
  }
  if (lexicon.empty()) {
    std::cerr << "cannot read " << argv[1] << "\n";
    return 1;
  }

  std::unordered_map<std::string, uint32_t> ids;
  std::vector<std::string> words;
  std::unordered_map<uint64_t, uint32_t> pairCounts, tripleCounts;
  std::vector<uint32_t> previousCounts;  // times each word is followed by another word
  auto idOf = [&](const std::string& word) {
    auto [it, inserted] = ids.emplace(word, static_cast<uint32_t>(words.size()));
    if (inserted) {
      if (words.size() >> ProjectKey::Bigram::kTripleIdBits) {
        std::cerr << "too many distinct words for trigram keys\n";
        std::exit(1);
      }
      words.push_back(word);
      previousCounts.push_back(0);
    }
    return it->second;
  };

  std::vector<char32_t> points;
  std::vector<std::string> bytes;
  std::string line;
  uint64_t tokens = 0, runs = 0;
  while (std::getline(std::cin, line)) {
    Decode(line, points, bytes);
    for (size_t begin = 0; begin < points.size();) {
      if (!IsHan(points[begin])) {
        begin++;
        continue;
      }
      size_t end = begin;
      while (end < points.size() && IsHan(points[end])) end++;
      // Viterbi over the run [begin, end).
      size_t n = end - begin;
      std::vector<double> best(n + 1, -INFINITY);
      std::vector<size_t> from(n + 1, 0);
      best[0] = 0;
      for (size_t i = 0; i < n; i++) {
        if (best[i] == -INFINITY) continue;
        std::string word;
        for (size_t length = 1; length <= kMaxWordLength && i + length <= n; length++) {
          word += bytes[begin + i + length - 1];
          auto it = lexicon.find(word);
          double score = it != lexicon.end() ? it->second : length == 1 ? kUnknownCharScore : -INFINITY;
          if (score == -INFINITY) continue;
          if (best[i] + score > best[i + length]) {
            best[i + length] = best[i] + score;
            from[i + length] = i;
          }
        }
      }
      // Reversed word ids with the run boundary at both ends.
      std::vector<uint32_t> segmented{idOf(ProjectKey::Bigram::kBoundary)};
      for (size_t at = n; at > 0; at = from[at]) {
        std::string word;
        for (size_t k = from[at]; k < at; k++) word += bytes[begin + k];
        segmented.push_back(idOf(word));
      }
      segmented.push_back(idOf(ProjectKey::Bigram::kBoundary));
      for (size_t k = segmented.size(); k-- > 1;) {
        uint32_t previous = segmented[k], next = segmented[k - 1];  // segmented is reversed
        pairCounts[static_cast<uint64_t>(previous) << 32 | next]++;
        previousCounts[previous]++;
        if (k + 1 < segmented.size()) tripleCounts[ProjectKey::Bigram::TripleKey(segmented[k + 1], previous, next)]++;
      }
      tokens += segmented.size() - 2;
      runs++;
      begin = end;
    }
  }

  // Keep pairs seen at least minCount times; renumber the words they use.
  std::vector<std::pair<uint64_t, uint32_t>> kept;
  for (const auto& [key, count] : pairCounts) {
    if (count >= minCount) kept.emplace_back(key, count);
  }
  std::unordered_map<uint32_t, uint32_t> remap;
  std::vector<std::string> keptWords;
  auto newId = [&](uint32_t id) {
    auto [it, inserted] = remap.emplace(id, static_cast<uint32_t>(keptWords.size()));
    if (inserted) keptWords.push_back(words[id]);
    return it->second;
  };
  std::vector<ProjectKey::Bigram::Pair> pairs;
  pairs.reserve(kept.size());
  for (const auto& [key, count] : kept) {
    uint32_t previous = static_cast<uint32_t>(key >> 32), next = static_cast<uint32_t>(key);
    double probability = static_cast<double>(count) / previousCounts[previous];
    pairs.push_back({static_cast<uint64_t>(newId(previous)) << 32 | newId(next), static_cast<float>(std::log10(probability))});
  }
  // P(next | first, second): a pair's count is how often it is followed by a
  // word, except a pair ending at the boundary, which has no triples.
  constexpr uint64_t kIdMask = (1ull << ProjectKey::Bigram::kTripleIdBits) - 1;
  std::vector<ProjectKey::Bigram::Pair> triples;
  for (const auto& [key, count] : tripleCounts) {
    if (count < minCount) continue;
    uint32_t first = static_cast<uint32_t>(key >> (2 * ProjectKey::Bigram::kTripleIdBits));
    uint32_t second = static_cast<uint32_t>(key >> ProjectKey::Bigram::kTripleIdBits & kIdMask);
    uint32_t next = static_cast<uint32_t>(key & kIdMask);
    double probability = static_cast<double>(count) / pairCounts[static_cast<uint64_t>(first) << 32 | second];
    triples.push_back({ProjectKey::Bigram::TripleKey(newId(first), newId(second), newId(next)),
                       static_cast<float>(std::log10(probability))});
  }
  std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
  std::sort(triples.begin(), triples.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
  if (!ProjectKey::Bigram::save(argv[3], keptWords, pairs, triples)) {
    std::cerr << "cannot write " << argv[3] << "\n";
    return 1;
  }
  std::fprintf(stderr, "%llu runs, ", static_cast<unsigned long long>(runs));
  std::fprintf(stderr, "%llu tokens, %zu distinct pairs, kept %zu pairs over %zu words, kept %zu of %zu triples\n",
               static_cast<unsigned long long>(tokens), pairCounts.size(), pairs.size(), keptWords.size(),
               triples.size(), tripleCounts.size());
  return 0;
}
