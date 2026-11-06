// Usage: ComposerTest <path to data.txt>
#undef NDEBUG  // the checks are asserts; Release builds (CMake on Linux) would drop them
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "Composer.h"
#include "Controller.h"

using ProjectKey::Composer;
using ProjectKey::Segment;

namespace {

void type(Composer& composer, const std::string& keys) {
  for (char key : keys) {
    auto result = key == ' ' ? composer.completeWithFirstTone() : composer.typeReadingKey(key);
    assert(result == Composer::KeyResult::kHandled);
  }
}

// A scratch file path; also builds on Windows, which has TEMP instead of TMPDIR.
std::string tempPath(const char* name) {
  const char* dir = std::getenv("TMPDIR");
  if (!dir) dir = std::getenv("TEMP");
  return std::string(dir ? dir : "/tmp") + "/" + name;
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
  auto lm = std::make_shared<ProjectKey::Lexicon>();
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

  // A lone symbol is in the text at once as a word it begins; Backspace takes it back out.
  std::string full = composer.text();
  composer.typeReadingKey('r');
  assert(composer.hasPendingReading() && composer.pendingReading().empty() && composer.text() != full);
  assert(composer.backspace() && !composer.hasPendingReading() && composer.text() == full);
  assert(composer.backspace() && composer.text() == full.substr(0, full.size() - std::string("組").size()));

  assert(composer.insertPunctuation('<') && composer.text().ends_with("，"));
  // The punctuation reading _punctuation_* is no syllable typed without a tone.
  assert(composer.insertPunctuation('*') && composer.text().ends_with("＊"));

  // An impossible syllable is rejected without disturbing the composition.
  std::string before = composer.text();
  assert(composer.typeReadingKey('6') == Composer::KeyResult::kHandled);  // tone alone waits
  composer.clearPendingReading();
  assert(composer.text() == before);

  composer.clear();
  assert(composer.isEmpty());
  type(composer, "rup wu0 ");
  assert(composer.text() == "今天");

  // Without tones, a whole syllable is its word at once, and the next syllable begins at a
  // key that cannot extend it (ㄏ after ㄋㄧ); keys in order still build one (ㄍㄨㄛ).
  composer.clear();
  type(composer, "sucl");
  assert(composer.text() == "你好" && composer.readingCount() == 2 && composer.pendingReading().empty());
  composer.clear();
  type(composer, "eji");
  assert(composer.text() == "國" && composer.readingCount() == 1);
  // The open syllable still takes a tone: Space makes it ㄏㄠ, tone 1.
  composer.clear();
  type(composer, "sucl ");
  assert(composer.segments().back().reading.ends_with("ㄏㄠ"));
  // A tone the dictionary lacks leaves the syllable as it was: ㄋㄧ has no first tone.
  composer.clear();
  type(composer, "su");
  assert(composer.completeWithFirstTone() == Composer::KeyResult::kInvalid);
  assert(composer.text() == "你" && composer.hasPendingReading());
  // Backspace edits the open syllable, down to the lone symbol ㄏ.
  composer.clear();
  type(composer, "sucl");
  assert(composer.backspace() && composer.readingCount() == 2 && composer.pendingReading().empty());
  assert(composer.backspace() && composer.text() == "你" && composer.readingCount() == 1);

  // First symbols alone spell a phrase (ㄨㄇ for 我們), also after a whole syllable (ㄐㄧㄣ ㄊ).
  composer.clear();
  type(composer, "ja");
  assert(composer.text() == "我們" && composer.readingCount() == 2);
  composer.clear();
  type(composer, "rup w");
  assert(composer.text() == "今天");
  // Keys typed in English are Zhuyin only when they spell a syllable, which a lone letter does not.
  assert(!composer.isSyllableKeys("x") && composer.isSyllableKeys("su"));

  // Context: alone, ㄌㄧㄝˋ ㄕˊ walks to the phrase 獵食; a bigram model that has
  // seen 第一 列 時 picks the two single characters after 第一.
  composer.clear();
  type(composer, "2u4u xu,4g6");
  assert(composer.text() == "第一獵食");
  std::string path = tempPath("ComposerTest-bigram.bin");
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
  std::string slipPath = tempPath("ComposerTest-slip.bin");
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
  // The rows above and below count too: ㄧㄠˇ (ul3) has ㄠ right above ㄝ.
  Composer above(lm, slipModel);
  type(above, "5k4ek7ul31j4hji4");
  assert(above.text() == "這個也不錯");
  // A key that makes no syllable with a lone symbol starts the next syllable, so first
  // symbols spell 可以 (ㄎㄧ).
  Composer unknown(lm, slipModel);
  type(unknown, "du");
  assert(unknown.text() == "可以" && unknown.readingCount() == 2);
  // After a syllable typed with a tone, such a key that one slip explains stays in the
  // syllable: ㄍㄛ˙ (ei7) is ㄍㄜ˙ (ek7) one key off, not ㄍ and ㄛ˙.
  Composer toned(lm, slipModel);
  type(toned, "5k4ei7");
  assert(toned.text() == "這個" && toned.readingCount() == 2);
  // Text before the caret: after 這個 already in the document, the slip still reads as 也.
  Composer after(lm, slipModel);
  after.setTextBefore("他說這個");
  type(after, "u.31j4hji4");
  assert(after.text() == "也不錯");
  Composer alone(lm, slipModel);
  type(alone, "u.31j4hji4");
  assert(alone.text() != "也不錯");

  // The character model: no word pair knows 候選 then 字, but its characters 選字 are
  // common and 選次 is not, so ㄘˋ (h4) after 候選 reads as the slip ㄗˋ (y4).
  std::string charsPath = tempPath("ComposerTest-chars.bin");
  using Record = ProjectKey::CharModel::Record;
  auto code = [](const char* utf8) { return static_cast<uint64_t>(ProjectKey::CharModel::CodePoints(utf8)[0]); };
  ProjectKey::CharModel::Tables tables;
  for (const char* c : {"候", "字", "次", "選"}) tables.unigrams.push_back({code(c), 0.01f});
  std::sort(tables.unigrams.begin(), tables.unigrams.end(), [](const Record& a, const Record& b) { return a.key < b.key; });
  tables.bigramContexts = {{code("選"), 0.1f}};
  tables.bigrams = {{code("選") << ProjectKey::CharModel::kBits | code("字"), 0.8f}};
  assert(ProjectKey::CharModel::save(charsPath, tables));
  auto chars = std::make_shared<ProjectKey::CharModel>();
  assert(chars->load(charsPath));
  Composer withChars(lm, slipModel, nullptr, Composer::kContextWeight, Composer::kTrigramWeight, Composer::kAdjacentKeyScore, chars, 1);
  type(withChars, "c.4vm03h4");
  assert(withChars.text() == "候選字");
  Composer withoutChars(lm, slipModel);
  type(withoutChars, "c.4vm03h4");
  assert(withoutChars.text() == "候選次");
  std::remove(charsPath.c_str());
  std::remove(slipPath.c_str());

  // The user model: after choosing 列 following 第一 once, the next walk keeps it.
  auto userModel = ProjectKey::NewUserModel();
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

  // Hanyu Pinyin: a letter that cannot go on starts the next syllable, taking the open
  // syllable's last letter when that is what lets it start (ke|neng); ' separates.
  Composer pinyin(lm, nullptr, nullptr, Composer::kContextWeight, Composer::kTrigramWeight, -INFINITY, nullptr,
                  Composer::kCharWeight, true);
  const std::pair<const char*, const char*> typed[] = {{"nihao", "你好"},    {"keneng", "可能"}, {"wo3men", "我們"},
                                                       {"zhongguo", "中國"}, {"yinian", "一年"}, {"fang'an", "方案"},
                                                       {"wm", "我們"}};
  for (const auto& [keys, text] : typed) {
    pinyin.clear();
    type(pinyin, keys);
    assert(pinyin.text() == text);
  }
  pinyin.clear();
  assert(pinyin.typeReadingKey('i') == Composer::KeyResult::kInvalid);  // no syllable starts with i
  type(pinyin, "ni3");
  assert(pinyin.segments().size() == 1 && pinyin.segments()[0].reading == "ㄋㄧˇ");

  std::printf("ComposerTest passed\n");
  return 0;
}
