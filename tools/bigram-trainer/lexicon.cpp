// Compiles data.txt into lexicon.bin (core/engine/LexiconFile.h) and checks every reading
// reads back the same.
// Usage: lexicon <data.txt> <lexicon.bin>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "LexiconFile.h"

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: " << argv[0] << " <data.txt> <lexicon.bin>\n";
    return 2;
  }
  std::ifstream in(argv[1], std::ios::binary);
  if (!in) {
    std::cerr << "cannot read " << argv[1] << "\n";
    return 1;
  }
  std::vector<ProjectKey::LexiconFile::Row> rows;
  std::map<std::string, std::vector<std::pair<std::string, float>>> expected;
  for (std::string line; std::getline(in, line);) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream columns(line);
    std::string reading, word, score;
    if (!(columns >> reading >> word >> score)) continue;
    rows.push_back({reading, word, std::stof(score)});
    expected[reading].emplace_back(word, rows.back().score);
  }
  if (!ProjectKey::LexiconFile::save(argv[2], rows)) {
    std::cerr << "cannot write " << argv[2] << "\n";
    return 1;
  }

  ProjectKey::LexiconFile file;
  if (!file.load(argv[2])) {
    std::cerr << "cannot load " << argv[2] << "\n";
    return 1;
  }
  size_t index = 0;
  const std::vector<std::string> all = file.readingsStartingWith("");
  for (const auto& [reading, words] : expected) {
    std::string lines;
    if (index >= all.size() || all[index++] != reading || !file.appendLines(reading, lines)) {
      std::cerr << "cannot read back " << reading << "\n";
      return 1;
    }
    std::istringstream back(lines);
    std::string key, word;
    double score;
    for (const auto& [expectedWord, expectedScore] : words) {
      if (!(back >> key >> word >> score) || key != reading || word != expectedWord ||
          static_cast<float>(score) != expectedScore) {
        std::cerr << "row differs for " << reading << " " << expectedWord << "\n";
        return 1;
      }
    }
  }
  if (index != all.size()) {
    std::cerr << "extra readings\n";
    return 1;
  }
  std::fprintf(stderr, "%zu rows, %zu readings checked\n", rows.size(), all.size());
  return 0;
}
