// Usage: ComposerTest <path to data.txt>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

  // Context: alone, ㄌㄧㄝˋ ㄕˊ walks to the phrase 獵食; a bigram model that has
  // seen 第一 列 時 picks the two single characters after 第一.
  composer.clear();
  type(composer, "2u4u xu,4g6");
  assert(composer.text() == "第一獵食");
  std::string path = std::string(std::getenv("TMPDIR") ?: "/tmp") + "/ComposerTest-bigram.bin";
  // Ids: 0 第一, 1 列, 2 時; keys are previous << 32 | next, sorted.
  assert(ProjectKey::Bigram::save(path, {"第一", "列", "時"}, {{0ull << 32 | 1, -0.5f}, {1ull << 32 | 2, -0.5f}}));
  auto bigram = std::make_shared<ProjectKey::Bigram>();
  assert(bigram->load(path));
  Composer contextual(lm, bigram);
  type(contextual, "2u4u xu,4g6");
  assert(contextual.text() == "第一列時");
  std::remove(path.c_str());

  // Adjacent keys: ㄧㄡˇ (u.3) is one key off ㄧㄝˇ (u,3); with context that
  // knows 這個 也 不 (McBopomofo has no phrase 不錯), the slip comes out as 也.
  std::string slipPath = std::string(std::getenv("TMPDIR") ?: "/tmp") + "/ComposerTest-slip.bin";
  // Ids: 0 這個, 1 也, 2 不.
  assert(ProjectKey::Bigram::save(slipPath, {"這個", "也", "不"}, {{0ull << 32 | 1, -0.1f}, {1ull << 32 | 2, -0.1f}}));
  auto slipModel = std::make_shared<ProjectKey::Bigram>();
  assert(slipModel->load(slipPath));
  Composer slipping(lm, slipModel);
  type(slipping, "5k4ek7u.31j4hji4");
  assert(slipping.text() == "這個也不錯");
  Composer exact(lm, slipModel, nullptr, 0.3, 0.5, -INFINITY);
  type(exact, "5k4ek7u.31j4hji4");
  assert(exact.text() != "這個也不錯");
  std::remove(slipPath.c_str());

  // The user model: after choosing 列 following 第一 once, the next walk keeps it.
  auto userModel = std::make_shared<McBopomofo::UserOverrideModel>(500, 5400.0);
  Composer learning(lm, nullptr, userModel);
  type(learning, "2u4u xu,4g6");
  const ProjectKey::Segment* hunt = nullptr;
  for (const auto& segment : learning.segments()) {
    if (segment.text == "獵食") hunt = &segment;
  }
  assert(hunt && learning.choose(*hunt, "列"));
  assert(learning.text().rfind("第一列", 0) == 0);
  learning.clear();
  type(learning, "2u4u xu,4g6");
  assert(learning.text().rfind("第一列", 0) == 0);
  // Other contexts are unaffected.
  learning.clear();
  type(learning, "xu,4g6");
  assert(learning.text() == "獵食");

  std::printf("ComposerTest passed\n");
  return 0;
}
