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
  // Without 簡拼, a lone symbol is no syllable, and each key edits the open syllable until it is a
  // whole one (ㄅ then ㄆ is ㄆ); then a key that cannot extend it starts the next, so syllables
  // chain without tones: ㄨ then ㄇ is a word for ㄨ and an open ㄇ, never 我們.
  Composer plain(lm, nullptr, nullptr, Composer::kContextWeight, Composer::kTrigramWeight,
                 nullptr, Composer::kCharWeight, nullptr, /*abbreviated=*/false);
  type(plain, "ja");
  assert(plain.readingCount() == 1 && plain.pendingReading() == "ㄇ" && plain.text() != "我");
  plain.clear();
  type(plain, "1q");
  assert(plain.readingCount() == 0 && plain.pendingReading() == "ㄆ");
  plain.clear();
  type(plain, "suc");
  assert(plain.readingCount() == 1 && plain.pendingReading() == "ㄏ");
  // ㄖㄣㄨㄟ: the open ㄨㄟ shows as its symbols until it ends, here as Tab ends it, as its word.
  plain.clear();
  type(plain, "bpjo");
  assert(plain.readingCount() == 1 && plain.pendingReading() == "ㄨㄟ");
  plain.endOpenSyllable();
  assert(plain.readingCount() == 2 && !plain.hasPendingReading() && ProjectKey::Utf16Length(plain.text()) == 2);
  // A syllable still ends with a tone key or Space: ㄋㄧˇ ㄏㄠˇ is 你好.
  plain.clear();
  type(plain, "su3cl3");
  assert(plain.text() == "你好");
  // The open syllable is no word until it ends: a tone the dictionary lacks keeps its symbols,
  // and punctuation puts it in as its word.
  plain.clear();
  type(plain, "su");
  assert(plain.readingCount() == 0 && plain.pendingReading() == "ㄋㄧ");
  assert(plain.completeWithFirstTone() == Composer::KeyResult::kInvalid && plain.pendingReading() == "ㄋㄧ");
  assert(plain.insertPunctuation('<') && plain.text() == "你，" && !plain.hasPendingReading());
  composer.clear();
  type(composer, "1q");
  assert(composer.readingCount() == 2);
  // For the word before the cursor, the shorter candidates end where it ends: 打字 offers 自
  // for its ㄗˋ, not 打, and choosing it changes that syllable alone.
  composer.clear();
  type(composer, "283y4");
  const Segment typed = composer.segments().back();
  assert(typed.text == "打字" && typed.length == 2);
  std::vector<std::string> fromEnd = composer.candidates(typed, /*fromEnd=*/true);
  assert(fromEnd[0] == "打字" && std::find(fromEnd.begin(), fromEnd.end(), "自") != fromEnd.end());
  assert(std::find(fromEnd.begin(), fromEnd.end(), "打") == fromEnd.end());
  std::vector<std::string> fromStart = composer.candidates(typed);
  assert(std::find(fromStart.begin(), fromStart.end(), "打") != fromStart.end());
  assert(composer.choose(typed, "自", /*fromEnd=*/true) && composer.text().ends_with("自") && composer.readingCount() == 2);
  // Other tones: ㄨㄟˇ ㄗㄠˋ walks as typed, but the word before the cursor also offers 偽造,
  // which the dictionary reads ㄨㄟˋ-ㄗㄠˋ, and choosing it takes that reading.
  composer.clear();
  type(composer, "jo3yl4");
  assert(composer.text() != "偽造" && composer.readingCount() == 2);
  const Segment toned = composer.segments().back();
  std::vector<std::string> offered = composer.candidates(toned, /*fromEnd=*/true);
  assert(std::find(offered.begin(), offered.end(), "偽造") != offered.end());
  assert(composer.choose(toned, "偽造", /*fromEnd=*/true) && composer.text() == "偽造" && composer.readingCount() == 2);
  // A syllable's own tone comes first: ㄨㄟˇ offers 為 (ㄨㄟˊ) after its own words.
  composer.clear();
  type(composer, "jo3");
  const Segment wei = composer.segments().back();
  std::vector<std::string> weis = composer.candidates(wei, /*fromEnd=*/true);
  auto own = std::find(weis.begin(), weis.end(), wei.text), other = std::find(weis.begin(), weis.end(), "為");
  assert(own != weis.end() && other != weis.end() && own < other);
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

  // A key that makes no syllable with a lone symbol starts the next syllable, so first
  // symbols spell 可以 (ㄎㄧ).
  Composer unknown(lm);
  type(unknown, "du");
  assert(unknown.text() == "可以" && unknown.readingCount() == 2);

  // Text before the caret: a model that knows only 第一 then 列 reads ㄌㄧㄝˋ ㄕˊ as 列 and
  // a character after 第一 already in the document, and as 獵食 without it.
  std::string contextPath = tempPath("ComposerTest-context.bin");
  assert(ProjectKey::Bigram::save(contextPath, {"第一", "列"}, {{0ull << 32 | 1, -0.5f}}));
  auto contextModel = std::make_shared<ProjectKey::Bigram>();
  assert(contextModel->load(contextPath));
  Composer after(lm, contextModel);
  after.setTextBefore("他說第一");
  type(after, "xu,4g6");
  assert(after.text().starts_with("列"));
  Composer alone(lm, contextModel);
  type(alone, "xu,4g6");
  assert(alone.text() == "獵食");
  // Punctuation in the composition starts a phrase, as punctuation before it does: a model
  // that knows only phrases starting with 列 reads ㄌㄧㄝˋ ㄕˊ as 列 and a character after 第一，.
  std::string startPath = tempPath("ComposerTest-start.bin");
  // Ids: 0 the phrase boundary, 1 列.
  assert(ProjectKey::Bigram::save(startPath, {ProjectKey::Bigram::kBoundary, "列"}, {{0ull << 32 | 1, -0.5f}}));
  auto startModel = std::make_shared<ProjectKey::Bigram>();
  assert(startModel->load(startPath));
  Composer punctuated(lm, startModel);
  type(punctuated, "2u4u ");
  assert(punctuated.insertPunctuation('<'));
  type(punctuated, "xu,4g6");
  assert(punctuated.text().starts_with("第一，列"));
  std::remove(startPath.c_str());

  // The character model: no word pair knows 候選 then ㄗˋ (y4), which reads as 字 alone; a
  // character model where 選 is mostly followed by 自 makes it 自.
  std::string charsPath = tempPath("ComposerTest-chars.bin");
  using Record = ProjectKey::CharModel::Record;
  auto code = [](const char* utf8) { return static_cast<uint64_t>(ProjectKey::CharModel::CodePoints(utf8)[0]); };
  ProjectKey::CharModel::Tables tables;
  for (const char* c : {"候", "自", "選"}) tables.unigrams.push_back({code(c), 0.01f});
  std::sort(tables.unigrams.begin(), tables.unigrams.end(), [](const Record& a, const Record& b) { return a.key < b.key; });
  tables.bigramContexts = {{code("選"), 0.1f}};
  tables.bigrams = {{code("選") << ProjectKey::CharModel::kBits | code("自"), 0.8f}};
  assert(ProjectKey::CharModel::save(charsPath, tables));
  auto chars = std::make_shared<ProjectKey::CharModel>();
  assert(chars->load(charsPath));
  Composer withChars(lm, contextModel, nullptr, Composer::kContextWeight, Composer::kTrigramWeight, chars, 1);
  type(withChars, "c.4vm03y4");
  assert(withChars.text() == "候選自");
  Composer withoutChars(lm, contextModel);
  type(withoutChars, "c.4vm03y4");
  assert(withoutChars.text() == "候選字");
  std::remove(charsPath.c_str());
  std::remove(contextPath.c_str());

  // What the user typed: with no context or character model, 程式 learned from committed
  // text beats the dictionary's 城市.
  auto personal = std::make_shared<ProjectKey::PersonalModel>();
  Composer unlearned(lm, nullptr, nullptr, Composer::kContextWeight, Composer::kTrigramWeight,
                     nullptr, Composer::kCharWeight, personal);
  type(unlearned, "ji3ap7183ru.42k7t/6g4tj/6e.4t/6ai6yj3");
  assert(unlearned.text().find("的程式") == std::string::npos);
  for (int i = 0; i < 50; i++) personal->learn("我們把舊的程式重構成模組。");
  Composer learned(lm, nullptr, nullptr, Composer::kContextWeight, Composer::kTrigramWeight,
                   nullptr, Composer::kCharWeight, personal);
  type(learned, "ji3ap7183ru.42k7t/6g4tj/6e.4t/6ai6yj3");
  assert(learned.text().find("的程式") != std::string::npos);
  // Saved and loaded, the counts score the same; a missing file loads as empty.
  std::string personalPath = tempPath("ComposerTest-personal/personal.txt");
  assert(personal->save(personalPath));
  ProjectKey::PersonalModel loaded;
  assert(loaded.load(personalPath) && !loaded.empty());
  auto c = [](const char* utf8) { return ProjectKey::CharModel::CodePoints(utf8)[0]; };
  assert(loaded.blend(1e-4, c("的"), c("程"), c("式")) == personal->blend(1e-4, c("的"), c("程"), c("式")));
  assert(personal->blend(1e-4, c("的"), c("程"), c("式")) > 0.5);
  std::remove(personalPath.c_str());
  assert(loaded.load(personalPath) && loaded.empty());

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

  std::printf("ComposerTest passed\n");
  return 0;
}
