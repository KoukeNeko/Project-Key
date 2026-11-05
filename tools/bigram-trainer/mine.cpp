// Finds words the dictionary lacks: a dictionary word and a single character
// that keep appearing together, such as 氣象 + 署 or 簽 + 核. Without them the
// input method has to pick that character alone, with nothing to go on.
// Usage: mine <data.txt> <min count> < text > candidates.tsv
// Cuts the text as train.cpp does (segment.h), then prints tab-separated lines:
//   new <word> <count> <association> <first>|<second>  a pair of 2 to 5
//       characters not in the dictionary, one of them a single character;
//       association is the pair's log10 pointwise mutual information
//   known <word> <reading> <count> <score>  a dictionary word of 2 or more
//       characters, for calibrating scores from counts
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "segment.h"

namespace {

constexpr size_t kMaxNewWordLength = 5;

size_t CharCount(const std::string& text) {
  return std::count_if(text.begin(), text.end(), [](char c) { return (c & 0xc0) != 0x80; });
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: " << argv[0] << " <data.txt> <min count> < text\n";
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
  std::vector<uint64_t> counts;
  std::unordered_map<uint64_t, uint32_t> pairCounts;
  auto idOf = [&](const std::string& word) {
    auto [it, inserted] = ids.emplace(word, static_cast<uint32_t>(words.size()));
    if (inserted) {
      words.push_back(word);
      counts.push_back(0);
    }
    return it->second;
  };
  std::string line;
  uint64_t tokens = 0;
  while (std::getline(std::cin, line)) {
    Segment::ForEachRun(line, lexicon, [&](const std::vector<std::string>& run) {
      uint32_t previous = 0;
      for (size_t k = 0; k < run.size(); k++) {
        uint32_t id = idOf(run[k]);
        counts[id]++;
        if (k >= 1) pairCounts[static_cast<uint64_t>(previous) << 32 | id]++;
        previous = id;
      }
      tokens += run.size();
    });
  }

  const double total = static_cast<double>(tokens);
  for (const auto& [key, count] : pairCounts) {
    const std::string &first = words[key >> 32], &second = words[static_cast<uint32_t>(key)];
    std::string word = first + second;
    // Both parts in the dictionary, for the reading; a character the dictionary lacks stands alone.
    if (count < minCount || !lexicon.count(first) || !lexicon.count(second)) continue;
    if (std::min(CharCount(first), CharCount(second)) != 1 || CharCount(word) > kMaxNewWordLength || lexicon.count(word)) {
      continue;
    }
    double association = std::log10(count * total / (counts[key >> 32] * counts[static_cast<uint32_t>(key)]));
    std::printf("new\t%s\t%u\t%.3f\t%s|%s\n", word.c_str(), count, association, first.c_str(), second.c_str());
  }
  for (size_t id = 0; id < words.size(); id++) {
    auto it = lexicon.find(words[id]);
    if (it == lexicon.end() || CharCount(words[id]) < 2) continue;
    std::printf("known\t%s\t%s\t%llu\t%.3f\n", words[id].c_str(), it->second.reading.c_str(),
                static_cast<unsigned long long>(counts[id]), it->second.score);
  }
  std::fprintf(stderr, "%llu tokens, %zu distinct words\n", static_cast<unsigned long long>(tokens), words.size());
  return 0;
}
