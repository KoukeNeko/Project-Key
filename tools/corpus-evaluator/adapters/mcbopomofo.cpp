// Engine adapter: reads one Daqian key sequence per line on stdin, writes
// "<per-key ms, comma separated>\t<converted text>" per line on stdout.
// Usage: mcbopomofo <path to McBopomofo data.txt>
// Drives the engine the same way McBopomofo's EngineProfile.cpp does.
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>

#include "Mandarin/Mandarin.h"
#include "McBopomofoLM.h"
#include "gramambular2/reading_grid.h"

using Formosa::Gramambular2::ReadingGrid;
using Formosa::Mandarin::BopomofoKeyboardLayout;
using Formosa::Mandarin::BopomofoReadingBuffer;

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " <data.txt>\n";
    return 2;
  }
  auto model = std::make_shared<McBopomofo::McBopomofoLM>();
  model->loadLanguageModel(argv[1]);
  if (!model->isDataModelLoaded()) {
    std::cerr << "cannot load " << argv[1] << "\n";
    return 1;
  }
  ReadingGrid grid(model);
  BopomofoReadingBuffer reading(BopomofoKeyboardLayout::StandardLayout());

  std::string line;
  while (std::getline(std::cin, line)) {
    grid.clear();
    reading.clear();
    std::string text;
    for (size_t i = 0; i < line.size(); i++) {
      char key = line[i];
      auto start = std::chrono::steady_clock::now();
      // Daqian tone keys end a syllable; space is tone 1 and is not itself a reading key.
      bool endsSyllable = key == ' ' || key == '6' || key == '3' || key == '4' || key == '7';
      if (key != ' ') reading.combineKey(key);
      if (endsSyllable && !reading.isEmpty()) {
        grid.insertReading(reading.composedString());
        reading.clear();
        text.clear();
        for (const std::string& value : grid.walk().valuesAsStrings()) text += value;
      }
      std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
      std::printf("%s%.3f", i == 0 ? "" : ",", elapsed.count());
    }
    std::printf("\t%s\n", text.c_str());
    std::fflush(stdout);
  }
  return 0;
}
