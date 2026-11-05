// Trains word bigrams and trigrams for the macOS input method.
// Usage: train <McBopomofo data.txt> <min pair count> <output> < text
// Cuts each run of Han characters into dictionary words (segment.h), then
// counts adjacent word pairs and triples inside each run, including the run
// boundary (Bigram::kBoundary) at its start and end.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "Bigram.h"
#include "segment.h"

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "usage: " << argv[0] << " <data.txt> <min pair count> <output> < text\n";
    return 2;
  }
  const uint32_t minCount = static_cast<uint32_t>(std::stoul(argv[2]));

  Segment::Lexicon lexicon;
  if (!Segment::LoadLexicon(argv[1], lexicon)) {
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

  std::string line;
  uint64_t tokens = 0, runs = 0;
  std::vector<uint32_t> segmented;
  while (std::getline(std::cin, line)) {
    Segment::ForEachRun(line, lexicon, [&](const std::vector<std::string>& run) {
      // Word ids with the run boundary at both ends.
      segmented.assign({idOf(ProjectKey::Bigram::kBoundary)});
      for (const std::string& word : run) segmented.push_back(idOf(word));
      segmented.push_back(idOf(ProjectKey::Bigram::kBoundary));
      for (size_t k = 1; k < segmented.size(); k++) {
        uint32_t previous = segmented[k - 1], next = segmented[k];
        pairCounts[static_cast<uint64_t>(previous) << 32 | next]++;
        previousCounts[previous]++;
        if (k >= 2) tripleCounts[ProjectKey::Bigram::TripleKey(segmented[k - 2], previous, next)]++;
      }
      tokens += segmented.size() - 2;
      runs++;
    });
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
