// Cuts each run of Han characters into McBopomofo words by maximizing the sum
// of the dictionary's unigram scores, the same scores the input method walks
// with. Shared by train.cpp and mine.cpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Segment {

constexpr size_t kMaxWordLength = 8;  // ReadingGrid::kMaximumSpanLength
constexpr double kUnknownCharScore = -13;

struct Entry {
  double score;         // the word's best score over its readings
  std::string reading;  // the reading with that score, syllables joined by '-'
};
using Lexicon = std::unordered_map<std::string, Entry>;

// Reads McBopomofo's data.txt; false when it has no words.
inline bool LoadLexicon(const char* path, Lexicon& lexicon) {
  std::ifstream data(path);
  std::string line;
  while (std::getline(data, line)) {
    if (line.empty() || line[0] == '#' || line[0] == '_') continue;
    std::istringstream fields(line);
    std::string reading, word;
    double score;
    if (!(fields >> reading >> word >> score)) continue;
    auto [it, inserted] = lexicon.emplace(word, Entry{score, reading});
    if (!inserted && score > it->second.score) it->second = {score, reading};
  }
  return !lexicon.empty();
}

inline bool IsHan(char32_t c) {
  return (c >= 0x3400 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff) || (c >= 0x20000 && c <= 0x2ffff);
}

// Calls visit(words) for each run of Han characters in a line of UTF-8, the run
// cut into lexicon words; a character the lexicon lacks stands alone.
template <typename Visit>
void ForEachRun(const std::string& line, const Lexicon& lexicon, Visit visit) {
  std::vector<char32_t> points;
  std::vector<std::string> bytes;
  for (size_t i = 0; i < line.size();) {
    unsigned char lead = line[i];
    size_t length = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
    if (i + length > line.size()) break;
    char32_t point = length == 1 ? lead : lead & (0xff >> (length + 1));
    for (size_t k = 1; k < length; k++) point = point << 6 | (line[i + k] & 0x3f);
    points.push_back(point);
    bytes.push_back(line.substr(i, length));
    i += length;
  }
  std::vector<std::string> words;
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
        double score = it != lexicon.end() ? it->second.score : length == 1 ? kUnknownCharScore : -INFINITY;
        if (score == -INFINITY) continue;
        if (best[i] + score > best[i + length]) {
          best[i + length] = best[i] + score;
          from[i + length] = i;
        }
      }
    }
    words.clear();
    for (size_t at = n; at > 0; at = from[at]) {
      std::string word;
      for (size_t k = from[at]; k < at; k++) word += bytes[begin + k];
      words.push_back(std::move(word));
    }
    std::reverse(words.begin(), words.end());
    visit(words);
    begin = end;
  }
}

}  // namespace Segment
