// Usage: ComposerTest <path to data.txt>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>

#include "../Sources/Composer.h"

using ProjectKey::Composer;
using ProjectKey::Segment;

namespace {

void type(Composer& composer, const std::string& keys) {
  for (char key : keys) {
    auto result = key == ' ' ? composer.completeWithFirstTone() : composer.typeReadingKey(key);
    assert(result == Composer::KeyResult::kHandled);
  }
}

const Segment* find(const Composer& composer, const std::string& reading) {
  for (const Segment& segment : composer.segments()) {
    if (segment.reading == reading) return &segment;
  }
  return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <data.txt>\n", argv[0]);
    return 2;
  }
  auto lm = std::make_shared<McBopomofo::McBopomofoLM>();
  lm->loadLanguageModel(argv[1]);
  assert(lm->isDataModelLoaded());
  Composer composer(lm);

  // 我們把舊的程式重構成模組
  type(composer, "ji3ap7183ru.42k7t/6g4tj/6e.4t/6ai6yj3");
  for (const Segment& segment : composer.segments()) {
    std::printf("%s%s ", segment.text.c_str(), segment.doubtful ? "?" : "");
  }
  std::printf("\n");
  assert(!composer.hasPendingReading());

  // The engine cannot tell 程式 from 城市 without context, so it must flag it.
  const Segment* program = find(composer, "ㄔㄥˊ-ㄕˋ");
  assert(program && program->doubtful);
  auto values = composer.candidates(*program);
  auto at = [&](const std::string& value) { return std::find(values.begin(), values.end(), value) - values.begin(); };
  assert(at("程式") < 2 && at("城市") < 2);
  // Single characters starting here follow the two-character phrases;
  // nothing that starts at the next syllable (式) is offered.
  assert(at("程") > at("程式") && at("程") < (long)values.size());
  assert(at("式") == (long)values.size());

  Segment chosen = *program;
  assert(composer.choose(chosen, "程式"));
  assert(composer.text().find("的程式") != std::string::npos);
  assert(!find(composer, "ㄔㄥˊ-ㄕˋ")->doubtful);  // a user choice is never doubtful

  // A pending reading is not part of the text until its tone completes it.
  composer.typeReadingKey('r');
  assert(composer.hasPendingReading() && composer.pendingReading() == "ㄐ");
  assert(composer.backspace() && !composer.hasPendingReading());
  std::string full = composer.text();
  assert(composer.backspace() && composer.text() == full.substr(0, full.size() - std::string("組").size()));

  assert(composer.insertPunctuation('<') && composer.text().ends_with("，"));

  // An impossible syllable is rejected without disturbing the composition.
  std::string before = composer.text();
  assert(composer.typeReadingKey('6') == Composer::KeyResult::kHandled);  // tone alone waits
  composer.clearPendingReading();
  assert(composer.text() == before);

  composer.clear();
  assert(composer.isEmpty());
  type(composer, "rup wu0 ");
  assert(composer.text() == "今天");

  std::printf("ComposerTest passed\n");
  return 0;
}
