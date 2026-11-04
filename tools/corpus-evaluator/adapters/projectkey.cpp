// Engine adapter for Project Key's macOS composer: McBopomofo's grid and
// dictionary, plus the context model when one is given.
// Usage: projectkey <data.txt> [bigram.bin] [context weight] [trigram weight] [adjacent key score]
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>

#include "Composer.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " <data.txt> [bigram.bin] [context weight] [trigram weight] [adjacent key score]\n";
    return 2;
  }
  auto model = std::make_shared<McBopomofo::McBopomofoLM>();
  model->loadLanguageModel(argv[1]);
  if (!model->isDataModelLoaded()) {
    std::cerr << "cannot load " << argv[1] << "\n";
    return 1;
  }
  std::shared_ptr<ProjectKey::Bigram> bigram;
  if (argc >= 3) {
    bigram = std::make_shared<ProjectKey::Bigram>();
    if (!bigram->load(argv[2])) {
      std::cerr << "cannot load " << argv[2] << "\n";
      return 1;
    }
  }
  ProjectKey::Composer composer(model, bigram, nullptr, argc >= 4 ? std::stod(argv[3]) : ProjectKey::Composer::kContextWeight, argc >= 5 ? std::stod(argv[4]) : ProjectKey::Composer::kTrigramWeight,
                                argc >= 6 ? std::stod(argv[5]) : ProjectKey::Composer::kAdjacentKeyScore);

  std::string line;
  while (std::getline(std::cin, line)) {
    composer.clear();
    for (size_t i = 0; i < line.size(); i++) {
      auto start = std::chrono::steady_clock::now();
      if (line[i] == ' ') {
        composer.completeWithFirstTone();
      } else {
        composer.typeReadingKey(line[i]);
      }
      std::string text = composer.text();
      std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
      std::printf("%s%.3f", i == 0 ? "" : ",", elapsed.count());
    }
    std::printf("\t%s\n", composer.text().c_str());
    std::fflush(stdout);
  }
  return 0;
}
