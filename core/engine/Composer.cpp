#include "Composer.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#include "InputMacro.h"
#include "Utf8.h"

namespace ProjectKey {

using Formosa::Gramambular2::LanguageModel;
using Formosa::Gramambular2::ReadingGrid;
using Formosa::Mandarin::BopomofoKeyboardLayout;
using Formosa::Mandarin::BopomofoSyllable;

namespace {
// Scores are log10 probabilities. When the walked phrase leads the next phrase
// with the same reading by less than this, the engine is guessing and the
// proof line marks the segment.
constexpr double kDoubtfulScoreGap = 0.5;
// Lower-ranked words a node can contribute when context picks the path.
constexpr size_t kWordsPerNode = 10;
// Unigram fallback for the composition's end: about 19% of the words in the
// bigram training text end a run of Han characters.
constexpr double kEndScore = -0.72;
// Words outside the context model, and the missing word before the first.
constexpr uint32_t kNoWord = UINT32_MAX;
// The longest word before the composition that the context model is asked about.
constexpr size_t kWordBeforeLength = 4;
// Character model log10 probabilities kept between walks.
constexpr size_t kCharCacheSize = 1 << 16;
// P(c | a b) for every character when there is no character model, before what the user typed.
constexpr double kNoCharModel = 1e-4;

double Now() {
  return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// The user model's key: the word before (empty at the start) and the reading.
std::string UserKey(const std::string& previousReading, const std::string& previousValue, const std::string& reading) {
  return (previousReading.empty() ? "" : "(" + previousReading + "," + previousValue + ")") + "|" + reading;
}
constexpr char kSeparator = '-';  // ReadingGrid's default

// Sounds some speakers do not tell apart: 翹舌 (ㄓㄗ, ㄔㄘ, ㄕㄙ) and 前後鼻音 (ㄣㄥ, ㄢㄤ).
// A word that needs one swapped is offered, below the words of the sound as typed.
constexpr double kFuzzyPenalty = 1.5;  // log10: the sound as typed is about 30 times likelier
constexpr size_t kMaxFuzzyReadings = 16;
constexpr std::pair<std::string_view, std::string_view> kFuzzyInitials[] = {{"ㄓ", "ㄗ"}, {"ㄔ", "ㄘ"}, {"ㄕ", "ㄙ"}};
constexpr std::pair<std::string_view, std::string_view> kFuzzyFinals[] = {{"ㄣ", "ㄥ"}, {"ㄢ", "ㄤ"}};

// A syllable, with each swap made or not, tone (or kAnyTone) kept.
std::vector<std::string> FuzzySyllables(const std::string& syllable) {
  size_t cut = syllable.size();
  for (const std::string_view tone : {"ˊ", "ˇ", "ˋ", "˙", "*"}) {
    if (syllable.ends_with(tone)) cut -= tone.size();
  }
  std::string tail = syllable.substr(cut);
  std::vector<std::string> alternatives{syllable.substr(0, cut)};
  for (const auto& [a, b] : kFuzzyInitials) {
    for (size_t i = 0, n = alternatives.size(); i < n; i++) {
      std::string_view from = alternatives[i].starts_with(a) ? a : alternatives[i].starts_with(b) ? b : "";
      if (!from.empty()) alternatives.push_back(std::string(from == a ? b : a) + alternatives[i].substr(from.size()));
    }
  }
  for (const auto& [a, b] : kFuzzyFinals) {
    for (size_t i = 0, n = alternatives.size(); i < n; i++) {
      std::string_view from = alternatives[i].ends_with(a) ? a : alternatives[i].ends_with(b) ? b : "";
      if (!from.empty()) alternatives.push_back(alternatives[i].substr(0, alternatives[i].size() - from.size()) + std::string(from == a ? b : a));
    }
  }
  for (std::string& alternative : alternatives) alternative += tail;
  return alternatives;
}

// The grid reading with some of its syllables swapped for their near sounds, not itself. None
// when there are more than kMaxFuzzyReadings of them: a long run of such syllables is no word.
std::vector<std::string> FuzzyReadings(const std::string& reading) {
  std::vector<std::vector<std::string>> syllables;
  size_t count = 1;
  for (size_t start = 0;;) {
    size_t end = reading.find(kSeparator, start);
    syllables.push_back(FuzzySyllables(reading.substr(start, end - start)));
    count *= syllables.back().size();
    if (end == std::string::npos) break;
    start = end + 1;
  }
  if (count == 1 || count > kMaxFuzzyReadings) return {};
  std::vector<std::string> readings{""};
  for (size_t i = 0; i < syllables.size(); i++) {
    std::vector<std::string> next;
    for (const std::string& prefix : readings) {
      for (const std::string& syllable : syllables[i]) next.push_back(i ? prefix + kSeparator + syllable : syllable);
    }
    readings = std::move(next);
  }
  readings.erase(readings.begin());  // the first is the reading as typed
  return readings;
}

// The dictionary, reading a syllable that ends in Composer::kAnyTone as any of its tones,
// and, when abbreviated, a lone symbol that ends in it as any syllable it begins.
class TonelessModel : public LanguageModel {
 public:
  TonelessModel(std::shared_ptr<Lexicon> lm, bool abbreviated, bool fuzzy)
      : lm_(std::move(lm)), abbreviated_(abbreviated), fuzzy_(fuzzy) {}

  std::vector<Unigram> getUnigrams(const std::string& reading) override {
    if (!fuzzy_ || reading.empty() || reading[0] == '_') return lookup(reading);
    auto cached = fuzzyCache_.find(reading);
    if (cached != fuzzyCache_.end()) return cached->second;
    std::vector<Unigram> unigrams = lookup(reading);
    std::unordered_set<std::string> values;
    for (const Unigram& unigram : unigrams) values.insert(unigram.value());
    for (const std::string& variant : FuzzyReadings(reading)) {
      for (const Unigram& unigram : lookup(variant)) {
        if (values.insert(unigram.value()).second) unigrams.emplace_back(unigram.value(), unigram.score() - kFuzzyPenalty, unigram.rawValue());
      }
    }
    std::stable_sort(unigrams.begin(), unigrams.end(), [](const Unigram& a, const Unigram& b) { return a.score() > b.score(); });
    if (fuzzyCache_.size() >= kCacheSize) fuzzyCache_.clear();
    if (std::any_of(unigrams.begin(), unigrams.end(), IsMacro)) macroReadings_.push_back(reading);
    return fuzzyCache_.emplace(reading, std::move(unigrams)).first->second;
  }

  std::vector<Unigram> lookup(const std::string& reading) {
    // Punctuation readings such as _punctuation_* are not syllables.
    if (reading.find(Composer::kAnyTone) == std::string::npos || reading[0] == '_') return lm_->getUnigrams(reading);
    auto cached = cache_.find(reading);
    if (cached != cache_.end()) return cached->second;
    std::vector<Unigram> unigrams;
    std::unordered_set<std::string> values;
    for (const auto& [key, unigram] : matches(reading)) {
      if (values.insert(unigram.value()).second) unigrams.push_back(unigram);  // the best, as matches() comes best first
    }
    if (cache_.size() >= kCacheSize) cache_.clear();
    return cache_.emplace(reading, std::move(unigrams)).first->second;
  }

  // Every dictionary word that a reading with kAnyTone in it matches, with the word's own
  // reading, best first.
  const std::vector<std::pair<std::string, Unigram>>& matches(const std::string& reading) {
    auto cached = matchCache_.find(reading);
    if (cached != matchCache_.end()) return cached->second;
    std::vector<Typed> typed;
    for (size_t start = 0;;) {
      size_t end = reading.find(kSeparator, start);
      typed.push_back(Typed::Of(reading.substr(start, end - start), abbreviated_));
      if (end == std::string::npos) break;
      start = end + 1;
    }
    std::vector<std::pair<std::string, Unigram>> found;
    std::string_view previous;
    // Every match starts with what was typed before the first kAnyTone; a reading's rows are adjacent.
    for (std::string_view row : lm_->rowsStartingWith(reading.substr(0, reading.find(Composer::kAnyTone)))) {
      std::string_view key = row.substr(0, row.find(' '));
      if (key == previous) continue;
      previous = key;
      if (!Matches(typed, key)) continue;
      std::string toned(key);
      for (Unigram& unigram : lm_->getUnigrams(toned)) {
        // The dictionary also lists each lone symbol as itself (ㄉ for ㄉ), which is no syllable;
        // the candidates offer the Zhuyin anyway.
        if (unigram.value() != toned) found.emplace_back(toned, std::move(unigram));
      }
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.second.score() > b.second.score(); });
    if (matchCache_.size() >= kCacheSize) matchCache_.clear();
    if (std::any_of(found.begin(), found.end(), [](const auto& match) { return IsMacro(match.second); })) macroReadings_.push_back(reading);
    return matchCache_.emplace(reading, std::move(found)).first->second;
  }

  bool hasUnigrams(const std::string& reading) override {
    if (!fuzzy_ && reading.find(Composer::kAnyTone) == std::string::npos) return lm_->hasUnigrams(reading);
    return !getUnigrams(reading).empty();
  }

  // Drops the cached words of the readings with a macro (InputMacro.h) among them, so a new
  // composition gets the date and time anew rather than those of the first lookup.
  void forgetMacros() {
    for (const std::string& reading : macroReadings_) {
      fuzzyCache_.erase(reading);
      cache_.erase(reading);
      matchCache_.erase(reading);
    }
    macroReadings_.clear();
  }

 private:
  static constexpr size_t kCacheSize = 4096;

  static bool IsMacro(const Unigram& unigram) { return unigram.rawValue().starts_with(kMacroPrefix); }

  // A grid reading's syllable: exact, without a tone, or a lone symbol without a tone.
  struct Typed {
    std::string symbols;
    bool anyTone = false, anySyllable = false;

    static Typed Of(const std::string& syllable, bool abbreviated) {
      if (syllable.empty() || syllable.back() != Composer::kAnyTone) return {syllable};
      std::string symbols = syllable.substr(0, syllable.size() - 1);
      BopomofoSyllable parsed = BopomofoSyllable::FromComposedString(symbols);
      int parts = !!parsed.consonantComponent() + !!parsed.middleVowelComponent() + !!parsed.vowelComponent();
      return {symbols, true, abbreviated && parts == 1};
    }

    bool matches(std::string_view syllable) const {
      if (!anyTone) return syllable == symbols;
      if (syllable.substr(0, symbols.size()) != symbols) return false;
      std::string_view rest = syllable.substr(symbols.size());
      return anySyllable || rest.empty() || rest == "ˊ" || rest == "ˇ" || rest == "ˋ" || rest == "˙";
    }
  };

  // Whether the dictionary reading `key` has the typed syllables, one for one.
  static bool Matches(const std::vector<Typed>& typed, std::string_view key) {
    size_t start = 0;
    for (size_t i = 0; i < typed.size(); i++) {
      size_t end = key.find(kSeparator, start);
      bool last = i + 1 == typed.size();
      if ((end == std::string_view::npos) != last) return false;
      if (!typed[i].matches(key.substr(start, last ? std::string_view::npos : end - start))) return false;
      start = end + 1;
    }
    return true;
  }

  std::shared_ptr<Lexicon> lm_;
  bool abbreviated_, fuzzy_;
  std::unordered_map<std::string, std::vector<Unigram>> fuzzyCache_;
  std::unordered_map<std::string, std::vector<Unigram>> cache_;
  std::unordered_map<std::string, std::vector<std::pair<std::string, Unigram>>> matchCache_;
  std::vector<std::string> macroReadings_;
};

// A grid reading with each syllable in any tone: ㄨㄟˇ-ㄗㄠˋ as ㄨㄟ*-ㄗㄠ*.
std::string AnyTone(const std::string& reading) {
  std::string result;
  for (size_t start = 0;;) {
    size_t end = reading.find(kSeparator, start);
    std::string syllable = reading.substr(start, end - start);
    if (syllable.empty() || syllable.back() != Composer::kAnyTone) {
      for (const std::string tone : {"ˊ", "ˇ", "ˋ", "˙"}) {
        if (syllable.ends_with(tone)) syllable.erase(syllable.size() - tone.size());
      }
      syllable += Composer::kAnyTone;
    }
    if (start > 0) result += kSeparator;
    result += syllable;
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return result;
}


}  // namespace

Lexicon::Lexicon() {
  setMacroConverter([](const std::string& value) { return ConvertMacro(value); });
}

void Lexicon::loadLanguageModel(const char* path) {
  McBopomofoLM::loadLanguageModel(path);
  if (!path) return;
  rows_.reset();
  file_.close();
  if (file_.open(path)) {
    rows_ = std::make_unique<McBopomofo::ParselessPhraseDB>(file_.data(), file_.length(), /*validate_pragma=*/true);
  }
}

namespace {
// Read, not mapped as McBopomofo maps it: on Windows a mapped file cannot be saved over, so the
// user could not edit it while Project Key runs.
// A byte order mark, which some editors start a UTF-8 file with, would join the first word.
std::string ReadUserFile(const std::string& path) {
  std::ifstream file(Path(path), std::ios::binary);
  std::string text(std::istreambuf_iterator<char>(file), {});
  if (text.rfind("\xEF\xBB\xBF", 0) == 0) text.erase(0, 3);
  return text;
}
}  // namespace

void Lexicon::loadUserPhrases(const std::string& path, const std::string& excludedPath) {
  userPhrasesText_ = ReadUserFile(path);
  McBopomofoLM::loadUserPhrases(userPhrasesText_.data(), userPhrasesText_.size());  // which keeps pointing into the text
  excludedPhrasesText_ = ReadUserFile(excludedPath);
  McBopomofoLM::loadExcludedPhrases(excludedPhrasesText_.data(), excludedPhrasesText_.size());
  userReadings_.clear();
  std::istringstream lines(userPhrasesText_);
  std::string line;
  while (std::getline(lines, line)) {
    std::istringstream columns(line);
    std::string word, reading;
    if (!(columns >> word >> reading) || word[0] == '#') continue;
    userReadings_.push_back(reading);
  }
  std::sort(userReadings_.begin(), userReadings_.end());
  userReadings_.erase(std::unique(userReadings_.begin(), userReadings_.end()), userReadings_.end());
}

namespace {
// Below any word of the dictionary, so an emoji is offered but never the default.
constexpr double kEmojiScore = -10;
constexpr char kEmojiGroups[] = "_emoji_groups", kEmojiList[] = "_emoji_list_";

std::vector<std::string> Split(const std::string& text, char separator) {
  std::vector<std::string> parts;
  std::istringstream stream(text);
  std::string part;
  while (std::getline(stream, part, separator)) parts.push_back(part);
  return parts;
}
}  // namespace

void Lexicon::loadEmoji(const std::string& path) {
  emojiGroups_.clear();
  emojiByReading_.clear();
  std::istringstream lines(ReadUserFile(path));
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();  // a file checked out or saved on Windows
    std::vector<std::string> fields = Split(line, '\t');
    if (fields.size() != 3) continue;
    if (fields[0] == "G") emojiGroups_.emplace_back(fields[1], Split(fields[2], ' '));
    if (fields[0] == "K") emojiByReading_[fields[1]] = Split(fields[2], ' ');
  }
}

std::vector<Formosa::Gramambular2::LanguageModel::Unigram> Lexicon::getUnigrams(const std::string& key) {
  using Unigram = Formosa::Gramambular2::LanguageModel::Unigram;
  std::vector<Unigram> unigrams;
  // The table's lists, in their order: a score that falls by a little each.
  auto list = [&unigrams](const std::vector<std::string>& values) {
    for (size_t i = 0; i < values.size(); i++) unigrams.emplace_back(values[i], -1e-4 * static_cast<double>(i));
    return unigrams;
  };
  if (key == kEmojiGroups) {
    std::vector<std::string> names;
    for (const auto& group : emojiGroups_) names.push_back(group.first);
    return list(names);
  }
  if (key.rfind(kEmojiList, 0) == 0) {
    size_t group = std::strtoul(key.c_str() + sizeof(kEmojiList) - 1, nullptr, 10);
    return group < emojiGroups_.size() ? list(emojiGroups_[group].second) : unigrams;
  }
  unigrams = McBopomofoLM::getUnigrams(key);
  auto offered = emojiByReading_.find(key);
  if (offered == emojiByReading_.end()) return unigrams;
  for (const std::string& emoji : offered->second) {
    bool known = std::any_of(unigrams.begin(), unigrams.end(), [&emoji](const Unigram& unigram) { return unigram.value() == emoji; });
    if (!known) unigrams.emplace_back(emoji, kEmojiScore);
  }
  return unigrams;
}

bool Lexicon::hasUnigrams(const std::string& key) {
  if (key == kEmojiGroups) return !emojiGroups_.empty();
  if (key.rfind(kEmojiList, 0) == 0) return std::strtoul(key.c_str() + sizeof(kEmojiList) - 1, nullptr, 10) < emojiGroups_.size();
  return McBopomofoLM::hasUnigrams(key) || emojiByReading_.count(key);
}

std::vector<std::string_view> Lexicon::rowsStartingWith(const std::string& prefix) const {
  std::vector<std::string_view> rows = rows_ ? rows_->findRows(prefix) : std::vector<std::string_view>{};
  for (auto it = std::lower_bound(userReadings_.begin(), userReadings_.end(), prefix);
       it != userReadings_.end() && it->compare(0, prefix.size(), prefix) == 0; ++it) {
    rows.push_back(*it);
  }
  return rows;
}

Composer::Composer(std::shared_ptr<Lexicon> lm, std::shared_ptr<const Bigram> bigram,
                   std::shared_ptr<McBopomofo::UserOverrideModel> userModel, double contextWeight,
                   double trigramWeight, std::shared_ptr<const CharModel> chars,
                   double charWeight, std::shared_ptr<const PersonalModel> personal, bool abbreviated, bool omitTones, bool fuzzy,
                   const BopomofoKeyboardLayout* layout)
    : lm_(lm),
      dictionary_(std::make_shared<TonelessModel>(lm, abbreviated, fuzzy)),
      bigram_(bigram && bigram->isLoaded() ? std::move(bigram) : nullptr),
      contextWeight_(contextWeight),
      trigramWeight_(trigramWeight),
      chars_(bigram_ && chars && chars->isLoaded() ? std::move(chars) : nullptr),
      charWeight_(charWeight),
      personal_(std::move(personal)),
      userModel_(std::move(userModel)),
      abbreviated_(abbreviated),
      omitTones_(omitTones),
      fuzzy_(fuzzy),
      grid_(dictionary_),
      reading_(layout) {}

Composer::KeyResult Composer::typeReadingKey(char key) {
  if (!reading_.isValidKey(key)) return KeyResult::kInvalid;
  bool shown = readingShown_;
  if ((readingShown_ || !abbreviated_) && !reading_.isEmpty() && startsSyllable(key)) {
    if (!readingShown_) showReading(/*ending=*/true);  // without abbreviated, the syllable goes in as its word now
    shown = shown || readingShown_;
    endReading();
  }
  Formosa::Mandarin::BopomofoReadingBuffer untoned = reading_;
  hideReading();
  reading_.combineKey(key);
  if (reading_.hasToneMarker() && !reading_.hasToneMarkerOnly()) return insertPendingSyllable(untoned);
  showReading();
  if (shown || readingShown_) walk();
  return KeyResult::kHandled;
}

Composer::KeyResult Composer::completeWithFirstTone() {
  if (reading_.isEmpty()) return KeyResult::kInvalid;
  Formosa::Mandarin::BopomofoReadingBuffer untoned = reading_;
  hideReading();
  return insertPendingSyllable(untoned);
}

// `untoned` is the reading before its tone: when the dictionary lacks that tone, a whole
// syllable or a lone symbol goes back as it was, still shown as its word; anything else is
// dropped, except without abbreviated, where the symbols stay to be fixed.
Composer::KeyResult Composer::insertPendingSyllable(const Formosa::Mandarin::BopomofoReadingBuffer& untoned) {
  std::string syllable = reading_.syllable().composedString();
  if (joinPrevious(reading_.syllable())) {
    reading_.clear();
    walk();
    return KeyResult::kHandled;
  }
  reading_.clear();
  bool inserted = grid_.insertReading(syllable);
  if (!inserted) {
    reading_ = untoned;
    showReading();
    if (!readingShown_ && abbreviated_) reading_.clear();
  }
  walk();
  return inserted ? KeyResult::kHandled : KeyResult::kInvalid;
}

// Symbols typed out of order, without abbreviated (ㄧㄚㄐ for ㄐㄧㄚ, ㄛㄨㄗˋ for ㄗㄨㄛˋ): a whole syllable
// ends the open one before a symbol that goes before its own, so each of these began a syllable. When
// the tone comes, the toneless syllables just before it (at most two, as a syllable has three places)
// join the toned one, if their symbols fill different places, together make a syllable with words, and
// as typed make no word (ㄩㄌㄜˋ stays 娛樂). Not on each key, which would undo toneless typing (ㄖㄣㄨㄟ).
bool Composer::joinPrevious(const BopomofoSyllable& toned) {
  if (abbreviated_) return false;
  auto hasWord = [this](const std::string& reading) {
    for (const auto& unigram : dictionary_->getUnigrams(reading)) {
      if (unigram.value() != reading) return true;  // the dictionary's lone symbols list themselves
    }
    return false;
  };
  const std::vector<std::string>& readings = grid_.readings();
  size_t cursor = grid_.cursor();
  for (size_t count = std::min<size_t>(2, cursor); count > 0; count--) {
    BopomofoSyllable joined = toned;
    std::string typed = toned.composedString();
    bool fits = true;
    for (size_t i = cursor; fits && i > cursor - count; i--) {
      const std::string& previous = readings[i - 1];
      fits = previous.size() > 1 && previous.back() == kAnyTone && previous[0] != '_' && previous.find(kSeparator) == std::string::npos;
      if (!fits) break;
      BopomofoSyllable symbols = BopomofoSyllable::FromComposedString(previous.substr(0, previous.size() - 1));
      fits = !(symbols.hasConsonant() && joined.hasConsonant()) && !(symbols.hasMiddleVowel() && joined.hasMiddleVowel()) &&
             !(symbols.hasVowel() && joined.hasVowel());
      joined = symbols + joined;
      typed = previous + kSeparator + typed;
    }
    if (!fits || hasWord(typed) || !hasWord(joined.composedString())) continue;
    for (size_t i = 0; i < count; i++) grid_.deleteReadingBeforeCursor();
    return grid_.insertReading(joined.composedString());
  }
  return false;
}

// With a whole syllable open and no tone typed, whether `key` begins the next syllable:
// it goes before the symbol the syllable ends in (the order is consonant, medial, vowel), or
// in its place if tones are omitted, or it makes a syllable the dictionary lacks.
bool Composer::startsSyllable(char key) const {
  // Without abbreviated, only once the open syllable is a whole one (ㄖㄣ, not ㄅ): until then
  // each key edits it (ㄅ then ㄆ is ㄆ).
  if (!abbreviated_ && !dictionary_->hasUnigrams(reading_.syllable().composedString() + kAnyTone)) return false;
  const BopomofoKeyboardLayout* layout = reading_.keyboardLayout();
  BopomofoSyllable open = reading_.syllable();
  // The open syllable with the key, as the reading would take it: in 許氏 and 倚天 26 鍵 a key's
  // symbol depends on the others (j is ㄓ alone, ㄐ before ㄧ, ˋ after a whole syllable).
  BopomofoSyllable joined = layout->syllableFromKeySequence(layout->keySequenceFromSyllable(open) + key);
  if (joined.hasToneMarker()) return false;
  BopomofoSyllable typed = layout->syllableFromKeySequence(std::string(1, key));
  auto lastPlace = [](const BopomofoSyllable& s) { return s.hasVowel() ? 3 : s.hasMiddleVowel() ? 2 : s.hasConsonant() ? 1 : 0; };
  // In the place the syllable ends in, as an ordinary Zhuyin input method: it replaces what is there.
  if (!abbreviated_ && !omitTones_ && lastPlace(typed) == lastPlace(open)) return false;
  if (lastPlace(typed) <= lastPlace(open)) return true;
  return !dictionary_->hasUnigrams(joined.composedString() + kAnyTone);
}

// Puts the open syllable in the grid, toneless, when the dictionary has it, so its word shows.
// Without abbreviated only when `ending` it: until then it shows as its symbols, as the word
// and its candidates would change with each key.
void Composer::showReading(bool ending) {
  if (reading_.isEmpty() || reading_.hasToneMarker() || (!abbreviated_ && !ending)) return;
  std::string reading = reading_.syllable().composedString() + kAnyTone;
  readingShown_ = dictionary_->hasUnigrams(reading) && grid_.insertReading(reading);
}

// Takes the open syllable out of the grid, to change it.
void Composer::hideReading() {
  if (readingShown_) grid_.deleteReadingBeforeCursor();
  readingShown_ = false;
}

// Closes the open syllable; a shown one stays in the grid without a tone.
void Composer::endReading() {
  readingShown_ = false;
  reading_.clear();
}

void Composer::showSymbols() {
  if (!readingShown_) return;
  hideReading();  // the symbols stay open, so pendingReading() has them
  walk();
}

void Composer::setCursor(size_t cursor) {
  endReading();
  grid_.setCursor(std::min(cursor, grid_.length()));
}

void Composer::clearPendingReading() {
  bool shown = readingShown_;
  hideReading();
  reading_.clear();
  if (shown) walk();
}

void Composer::endOpenSyllable() {
  if (readingShown_ || reading_.isEmpty()) return;
  showReading(/*ending=*/true);
  if (!readingShown_) return;  // symbols that make no syllable stay as they are
  endReading();
  walk();
}

bool Composer::insertPunctuation(char key) {
  // The layout's own first, as McBopomofo: its Zhuyin keys leave other keys for punctuation.
  for (const std::string& prefix : {"_punctuation_" + reading_.keyboardLayout()->name() + "_", std::string("_punctuation_")}) {
    if (insertSymbols(prefix + std::string(1, key))) return true;
  }
  return false;
}

bool Composer::insertSymbols(const std::string& reading) {
  if (!lm_->hasUnigrams(reading)) return false;
  if (!readingShown_) showReading(/*ending=*/true);  // the open syllable goes in as its word
  endReading();
  grid_.insertReading(reading);
  walk();
  return true;
}

bool Composer::isSyllableKeys(const std::string& keys) const {
  if (keys.empty()) return false;
  std::string typed = keys.back() == ' ' ? keys.substr(0, keys.size() - 1) : keys;
  if (typed.empty() || typed.find(' ') != std::string::npos) return false;
  const BopomofoKeyboardLayout* layout = reading_.keyboardLayout();
  BopomofoSyllable syllable = layout->syllableFromKeySequence(typed);
  bool hasTone = syllable.hasToneMarker(), space = keys.back() == ' ';
  if (hasTone && space) return false;  // a tone key ends it, a space for the first tone, or neither for any tone
  if (!hasTone && !space && typed.size() == 1) return false;  // a lone symbol only stands for a syllable
  std::string reading = syllable.composedString() + (hasTone || space ? "" : std::string(1, kAnyTone));
  return layout->keySequenceFromSyllable(syllable) == typed && dictionary_->hasUnigrams(reading);
}

bool Composer::isToneKey(char key) const {
  std::vector<BopomofoSyllable::Component> components = reading_.keyboardLayout()->keyToComponents(key);
  return components.size() == 1 && BopomofoSyllable(components[0]).hasToneMarker();
}

bool Composer::backspace() {
  if (!reading_.isEmpty()) {
    bool shown = readingShown_;
    hideReading();
    reading_.backspace();
    showReading();
    if (shown || readingShown_) walk();
    return true;
  }
  if (grid_.length() == 0) return false;
  grid_.deleteReadingBeforeCursor();
  walk();
  return true;
}

bool Composer::deleteForward() {
  if (!grid_.deleteReadingAfterCursor()) return false;
  walk();
  return true;
}

void Composer::setTextBefore(const std::string& text) {
  // Back over the trailing Han characters, a UTF-8 sequence at a time.
  size_t start = text.size();
  for (size_t count = 0; start > 0 && count < kWordBeforeLength; count++) {
    size_t at = start - 1;
    while (at > 0 && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80) at--;
    size_t end = at;
    uint32_t c = NextCodePoint(text, end);
    if (end != start || !CharModel::IsHan(c)) break;  // a malformed sequence ends the run too
    start = at;
  }
  hanBefore_ = text.substr(start);
  walk();
}

void Composer::setSentenceModel(std::shared_ptr<const SentenceModel> sentences) {
  sentences_ = sentences && sentences->isLoaded() ? std::move(sentences) : nullptr;
  sentenceCache_.clear();
  walk();
}

std::optional<Composer::RankingRequest> Composer::rankingRequest() const {
  if (rankable_.size() < 2 || !ranked_.empty()) return std::nullopt;
  RankingRequest request{{}, {}, version_};
  for (const auto& [text, walkScore] : rankable_) {
    request.sentences.push_back(text);
    request.walkScores.push_back(walkScore);
  }
  return request;
}

bool Composer::applyRanking(const RankingRequest& request, const std::vector<double>& log10Probabilities) {
  if (request.version != version_ || !ranked_.empty() || log10Probabilities.size() != request.sentences.size()) return false;
  std::optional<size_t> best;
  for (size_t i = 0; i < request.sentences.size(); i++) {
    if (std::isnan(log10Probabilities[i])) continue;
    double total = request.walkScores[i] + kAdvancedWeight * log10Probabilities[i];
    if (!best || total > request.walkScores[*best] + kAdvancedWeight * log10Probabilities[*best]) best = i;
  }
  if (!best) return false;
  std::string before = text();
  ranked_ = request.sentences[*best];
  rewalking_ = true;
  walk();
  rewalking_ = false;
  return text() != before;
}

void Composer::clear() {
  hanBefore_.clear();
  endReading();
  grid_.clear();
  segments_.clear();
  rankable_.clear();
  ranked_.clear();
  version_++;
  static_cast<TonelessModel&>(*dictionary_).forgetMacros();
}

std::string Composer::text() const {
  std::string result;
  for (const Segment& segment : segments_) result += segment.text;
  return result;
}

double Composer::wordScore(uint32_t first, uint32_t second, uint32_t word, double unigramScore) const {
  std::optional<double> pair =
      second != kNoWord && word != kNoWord ? bigram_->log10Probability(second, word) : std::nullopt;
  if (!pair) return unigramScore + std::log10(1 - contextWeight_);
  double context = std::pow(10, *pair);
  // A triple implies its pair, so only pairs the model knows need the lookup.
  if (std::optional<double> triple = first != kNoWord ? bigram_->log10Probability(first, second, word) : std::nullopt) {
    context = trigramWeight_ * std::pow(10, *triple) + (1 - trigramWeight_) * context;
  }
  return std::log10(contextWeight_ * context + (1 - contextWeight_) * std::pow(10, unigramScore));
}

// Viterbi over (node, word) choices, keeping one path per choice and the choice
// before it so each word sees the two words before it. Without a context model
// only each node's top word can win (other words only when the user picked
// them), which is exactly gramambular2's own walk.
void Composer::walk() {
  struct State {
    int previous;  // choice before this one, -1 at the start
    double best;
    int back;  // index into the previous choice's states
  };
  struct Choice {
    size_t start, length;
    ReadingGrid::NodePtr node;
    std::string value;
    uint32_t id;
    double unigramScore;
    std::vector<State> states;
    std::vector<uint32_t> chars;  // code points, with a character model
  };
  // Backspaced away, the composition ends as clear() ends it.
  if (grid_.length() == 0) static_cast<TonelessModel&>(*dictionary_).forgetMacros();
  if (!rewalking_) {
    version_++;
    ranked_.clear();
  }
  rankable_.clear();
  const std::vector<ReadingGrid::Span>& spans = grid_.spans();
  const size_t n = spans.size();
  const uint32_t boundary = bigram_ ? bigram_->id(Bigram::kBoundary).value_or(kNoWord) : kNoWord;
  auto idOf = [&](const std::string& value) { return bigram_ ? bigram_->id(value).value_or(kNoWord) : kNoWord; };
  // The word before the first: the start of a phrase, or the longest known word ending the Han text before.
  uint32_t opening = hanBefore_.empty() ? boundary : kNoWord;
  for (size_t at = 0; at < hanBefore_.size() && opening == kNoWord;) {
    opening = idOf(hanBefore_.substr(at));
    do at++; while (at < hanBefore_.size() && (hanBefore_[at] & 0xC0) == 0x80);
  }
  auto score = [&](uint32_t first, uint32_t second, uint32_t word, double unigramScore) {
    return bigram_ ? wordScore(first, second, word, unigramScore) : unigramScore;
  };
  // The character model's weighted log10 P(chars) after the words `beforeLast` (null at the
  // start) and `last`; the Han text before the composition stands for missing words.
  const std::vector<uint32_t> openingChars = scoresChars() ? CharModel::CodePoints(hanBefore_) : std::vector<uint32_t>{};
  if (personal_ && personal_->version() != personalVersion_) {
    charCache_.clear();
    personalVersion_ = personal_->version();
  }
  static const std::vector<uint32_t> kEndChars{CharModel::kBoundary};
  auto charScore = [&](const std::vector<uint32_t>* beforeLast, const std::vector<uint32_t>& last, const std::vector<uint32_t>& chars) {
    if (!scoresChars()) return 0.0;
    uint32_t first = CharModel::kBoundary, second = CharModel::kBoundary;
    for (const std::vector<uint32_t>* part : {beforeLast, &last}) {
      if (!part) continue;
      for (uint32_t c : *part) {
        first = second;
        second = c;
      }
    }
    double total = 0;
    for (uint32_t next : chars) {
      // Each key walks the whole composition again, through the same characters in the same places.
      uint64_t key = (static_cast<uint64_t>(first) << CharModel::kBits | second) << CharModel::kBits | next;
      auto cached = charCache_.find(key);
      if (cached == charCache_.end()) {
        if (charCache_.size() >= kCharCacheSize) charCache_.clear();
        // Without the shipped model, every character starts out equally likely and only what
        // the user typed tells them apart.
        double p = chars_ ? chars_->probability(first, second, next) : kNoCharModel;
        if (personal_) p = personal_->blend(p, first, second, next);
        cached = charCache_.emplace(key, std::log10(p)).first;
      }
      total += cached->second;
      first = second;
      second = next;
    }
    return charWeight_ * total;
  };
  std::vector<Choice> choices;
  std::vector<std::vector<int>> endingAt(n + 1);
  for (size_t start = 0; start < n; start++) {
    for (size_t length = 1; length <= spans[start].maxLength(); length++) {
      const ReadingGrid::NodePtr& node = spans[start].nodeOf(length);
      if (!node) continue;
      const std::string& reading = node->reading();
      // Punctuation ends a phrase, as in the text the models learned from, and scores as the
      // composition's end does; the words after it start a phrase, as after punctuation
      // before the composition (setTextBefore).
      bool punctuation = reading[0] == '_';
      auto add = [&](std::string value, double unigramScore) {
        endingAt[start + length].push_back(static_cast<int>(choices.size()));
        uint32_t id = punctuation ? boundary : idOf(value);
        std::vector<uint32_t> chars = !scoresChars() ? std::vector<uint32_t>{}
                                      : punctuation  ? kEndChars
                                                     : CharModel::CodePoints(value);
        if (punctuation) unigramScore = kEndScore;
        choices.push_back({start, length, node, std::move(value), id, unigramScore, {}, std::move(chars)});
      };
      if (node->isOverridden() || (!bigram_ && !userModel_ && !personal_)) {
        add(node->value(), node->score());
      } else {
        const auto& unigrams = node->unigrams();
        for (size_t k = 0; k < std::min(kWordsPerNode, unigrams.size()); k++) add(unigrams[k].value(), unigrams[k].score());
      }
    }
  }
  // A word the user picked after the same word is certain: probability 1.
  const double now = Now();
  auto learned = [&](int previous, const Choice& choice) {
    if (!userModel_ || choice.node->isOverridden()) return false;
    const std::string empty;
    const Choice* before = previous >= 0 ? &choices[previous] : nullptr;
    std::string key = UserKey(before ? before->node->reading() : empty, before ? before->value : empty, choice.node->reading());
    return userModel_->suggest(key, now).candidate == choice.value;
  };
  // Choices are in start order, so every predecessor is final before it is used.
  for (Choice& choice : choices) {
    if (choice.start == 0) {
      double first = learned(-1, choice) ? 0
                                         : score(kNoWord, opening, choice.id, choice.unigramScore) +
                                               charScore(nullptr, openingChars, choice.chars);
      choice.states.push_back({-1, first, -1});
      continue;
    }
    for (int previous : endingAt[choice.start]) {
      const Choice& before = choices[previous];
      State state{previous, -INFINITY, -1};
      bool certain = learned(previous, choice);
      for (size_t k = 0; k < before.states.size(); k++) {
        int beforeLast = before.states[k].previous;
        uint32_t first = beforeLast >= 0 ? choices[beforeLast].id : opening;
        double total = before.states[k].best;
        if (!certain) {
          total += score(first, before.id, choice.id, choice.unigramScore) +
                   charScore(beforeLast >= 0 ? &choices[beforeLast].chars : &openingChars, before.chars, choice.chars);
        }
        if (total > state.best) {
          state.best = total;
          state.back = static_cast<int>(k);
        }
      }
      choice.states.push_back(state);
    }
  }
  int last = -1, lastState = -1;
  double lastScore = -INFINITY;
  for (int end : endingAt[n]) {
    for (size_t k = 0; k < choices[end].states.size(); k++) {
      const State& state = choices[end].states[k];
      uint32_t first = state.previous >= 0 ? choices[state.previous].id : opening;
      double total = state.best + score(first, choices[end].id, boundary, kEndScore) +
                     charScore(state.previous >= 0 ? &choices[state.previous].chars : &openingChars, choices[end].chars, kEndChars);
      if (total > lastScore) {
        lastScore = total;
        last = end;
        lastState = static_cast<int>(k);
      }
    }
  }
  std::vector<int> path;
  for (int at = last, k = lastState; at >= 0;) {
    path.insert(path.begin(), at);
    const State& state = choices[at].states[k];
    at = state.previous;
    k = state.back;
  }

  // With a sentence model, the best whole path through each choice is a candidate sentence, and
  // the best kSentencePaths of them are reranked by how each reads as a whole: the walk sees only
  // two words back, so a rare pair can win (網液態 for 網頁太). A sentence with a character outside
  // the model's vocabulary keeps its walk score only when it is the walk's own.
  if ((sentences_ || advancedRanking_) && n > 1) {  // one syllable has no sentence to read
    std::vector<int> slot(choices.size());  // a choice's index in endingAt[its end]: its state in each successor
    for (size_t e = 0; e <= n; e++) {
      for (size_t j = 0; j < endingAt[e].size(); j++) slot[endingAt[e][j]] = static_cast<int>(j);
    }
    std::vector<std::vector<int>> startingAt(n + 1);
    for (size_t c = 0; c < choices.size(); c++) startingAt[choices[c].start].push_back(static_cast<int>(c));
    // Backward: the best rest of the path after each state, and the choice it goes on with.
    std::vector<std::vector<double>> rest(choices.size());
    std::vector<std::vector<int>> following(choices.size());
    for (int c = static_cast<int>(choices.size()) - 1; c >= 0; c--) {
      const Choice& choice = choices[c];
      rest[c].assign(choice.states.size(), -INFINITY);
      following[c].assign(choice.states.size(), -1);
      size_t end = choice.start + choice.length;
      for (size_t k = 0; k < choice.states.size(); k++) {
        int p = choice.states[k].previous;
        uint32_t first = p >= 0 ? choices[p].id : opening;
        const std::vector<uint32_t>* beforeLast = p >= 0 ? &choices[p].chars : &openingChars;
        if (end == n) {
          rest[c][k] = score(first, choice.id, boundary, kEndScore) + charScore(beforeLast, choice.chars, kEndChars);
          continue;
        }
        for (int next : startingAt[end]) {
          double step = learned(c, choices[next]) ? 0
                        : score(first, choice.id, choices[next].id, choices[next].unigramScore) +
                              charScore(beforeLast, choice.chars, choices[next].chars);
          if (step + rest[next][slot[c]] > rest[c][k]) {
            rest[c][k] = step + rest[next][slot[c]];
            following[c][k] = next;
          }
        }
      }
    }
    std::vector<std::pair<double, std::vector<int>>> paths;
    for (size_t c = 0; c < choices.size(); c++) {
      size_t k = 0;
      for (size_t j = 1; j < choices[c].states.size(); j++) {
        if (choices[c].states[j].best + rest[c][j] > choices[c].states[k].best + rest[c][k]) k = j;
      }
      double total = choices[c].states[k].best + rest[c][k];
      if (!std::isfinite(total)) continue;
      std::vector<int> through;
      for (int at = static_cast<int>(c), state = static_cast<int>(k); at >= 0;) {
        through.insert(through.begin(), at);
        const State& s = choices[at].states[state];
        at = s.previous;
        state = s.back;
      }
      for (int at = static_cast<int>(c), state = static_cast<int>(k); following[at][state] >= 0;) {
        int next = following[at][state];
        state = slot[at];
        at = next;
        through.push_back(at);
      }
      paths.emplace_back(total, std::move(through));
    }
    std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    paths.erase(std::unique(paths.begin(), paths.end(), [](const auto& a, const auto& b) { return a.second == b.second; }),
                paths.end());
    if (paths.size() > kSentencePaths) paths.resize(kSentencePaths);
    std::vector<std::string> texts;
    for (const auto& [total, through] : paths) {
      std::string text;
      for (int at : through) text += choices[at].value;
      texts.push_back(text);
      if (advancedRanking_) rankable_.emplace_back(text, total);
    }
    auto chosen = std::find(texts.begin(), texts.end(), ranked_);
    if (!ranked_.empty() && chosen != texts.end()) {
      path = paths[chosen - texts.begin()].second;
    } else if (sentences_) {
      std::vector<std::vector<uint32_t>> encoded;
      std::vector<size_t> scored;  // index into paths
      for (size_t i = 0; i < paths.size(); i++) {
        std::vector<uint32_t> codePoints;
        for (int at : paths[i].second) {
          for (uint32_t c : CharModel::CodePoints(choices[at].value)) codePoints.push_back(c);
        }
        std::optional<std::vector<uint32_t>> tokens = sentences_->encode(codePoints);
        if (!tokens) {
          if (paths[i].second == path) break;  // the walk's own: no sentence to compare it with
          continue;
        }
        encoded.push_back(std::move(*tokens));
        scored.push_back(i);
      }
      bool walkScored = std::any_of(scored.begin(), scored.end(), [&](size_t i) { return paths[i].second == path; });
      if (walkScored && scored.size() > 1) {
        std::vector<double> lm = sentences_->log10Probabilities(encoded, sentenceCache_);
        size_t best = 0;
        for (size_t j = 1; j < scored.size(); j++) {
          if (paths[scored[j]].first + kSentenceWeight * lm[j] > paths[scored[best]].first + kSentenceWeight * lm[best]) best = j;
        }
        path = paths[scored[best]].second;
      }
    }
  }

  // A word is doubtful when another word of the same node scores within the gap
  // in the same context.
  segments_.clear();
  auto idAt = [&](long i) {
    return i == -1 ? opening : i < 0 ? kNoWord : i >= static_cast<long>(path.size()) ? boundary : choices[path[i]].id;
  };
  for (size_t i = 0; i < path.size(); i++) {
    const Choice& choice = choices[path[i]];
    long at = static_cast<long>(i);
    uint32_t first = i > 0 ? idAt(at - 2) : kNoWord, previous = idAt(at - 1);
    bool hasNext = i + 1 < path.size();
    const Choice* next = hasNext ? &choices[path[i + 1]] : nullptr;
    const std::vector<uint32_t>& lastChars = i >= 1 ? choices[path[i - 1]].chars : openingChars;
    const std::vector<uint32_t>* beforeLastChars = i >= 2 ? &choices[path[i - 2]].chars : i == 1 ? &openingChars : nullptr;
    const std::vector<uint32_t>& afterNextChars = i + 2 < path.size() ? choices[path[i + 2]].chars : kEndChars;
    auto local = [&](uint32_t word, double unigramScore, const std::vector<uint32_t>& chars) {
      double total = score(first, previous, word, unigramScore) + charScore(beforeLastChars, lastChars, chars);
      total += score(previous, word, idAt(at + 1), next ? next->unigramScore : kEndScore) +
               charScore(&lastChars, chars, next ? next->chars : kEndChars);
      if (next) {
        total += score(word, next->id, idAt(at + 2), i + 2 < path.size() ? choices[path[i + 2]].unigramScore : kEndScore) +
                 charScore(&chars, next->chars, afterNextChars);
      }
      return total;
    };
    bool doubtful = false;
    if (!choice.node->isOverridden() && !learned(i > 0 ? path[i - 1] : -1, choice)) {
      double chosen = local(choice.id, choice.unigramScore, choice.chars);
      const auto& unigrams = choice.node->unigrams();
      for (size_t k = 0; k < std::min(kWordsPerNode, unigrams.size()) && !doubtful; k++) {
        if (unigrams[k].value() != choice.value) {
          const std::string& value = unigrams[k].value();
          std::vector<uint32_t> chars = scoresChars() ? CharModel::CodePoints(value) : std::vector<uint32_t>{};
          doubtful = chosen - local(idOf(value), unigrams[k].score(), chars) < kDoubtfulScoreGap;
        }
      }
    }
    segments_.push_back({choice.value, choice.node->reading(), choice.start, choice.length, doubtful});
  }
}

std::optional<Segment> Composer::segmentFrom(size_t start) const {
  for (const Segment& segment : segments_) {
    if (start < segment.start || start >= segment.start + segment.length) continue;
    if (start == segment.start) return segment;
    std::string reading = segment.reading;
    for (size_t skip = start - segment.start; skip > 0; skip--) {
      reading.erase(0, reading.find(grid_.readingSeparator()) + grid_.readingSeparator().size());
    }
    return Segment{"", reading, start, segment.start + segment.length - start, false};
  }
  return std::nullopt;
}

std::vector<std::pair<size_t, ReadingGrid::Candidate>> Composer::choices(const Segment& segment, bool fromEnd) {
  std::vector<std::pair<size_t, ReadingGrid::Candidate>> found;
  const std::vector<std::string>& readings = grid_.readings();
  // The words for readings [from, to): the grid's for what was typed, then the dictionary's for
  // the same syllables in other tones (偽造 for ㄨㄟˇ-ㄗㄠˋ), which the walk never takes on its own.
  auto add = [&](size_t from, size_t to) {
    std::string typed = readings[from];
    for (size_t i = from + 1; i < to; i++) typed += grid_.readingSeparator() + readings[i];
    for (const ReadingGrid::Candidate& candidate : grid_.candidatesAt(from)) {
      if (candidate.reading == typed) found.emplace_back(from, candidate);
    }
    std::string anyTone = AnyTone(typed);
    if (typed.find('_') != std::string::npos || anyTone == typed) return;  // punctuation, or already in any tone
    for (const auto& [reading, unigram] : static_cast<TonelessModel&>(*dictionary_).matches(anyTone)) {
      if (reading != typed) found.emplace_back(from, ReadingGrid::Candidate(reading, unigram.value(), unigram.rawValue()));
    }
  };
  size_t end = segment.start + segment.length;
  if (!fromEnd) {
    for (size_t to = end; to > segment.start; to--) add(segment.start, to);
    return found;
  }
  // Before the cursor: the longer words that end where the segment ends, longest first; the
  // segment's own; then the shorter ones, as ㄗˋ of ㄉㄚˇ-ㄗˋ.
  size_t longest = end > ReadingGrid::kMaximumSpanLength ? end - ReadingGrid::kMaximumSpanLength : 0;
  for (size_t from = longest; from < segment.start; from++) add(from, end);
  for (size_t from = segment.start; from < end; from++) add(from, end);
  return found;
}

// Puts `reading` in the grid in place of readings [start, start + length), as a word read in
// other tones than were typed is chosen.
void Composer::replaceReadings(size_t start, size_t length, const std::string& reading) {
  size_t cursor = grid_.cursor();
  grid_.setCursor(start + length);
  for (size_t i = 0; i < length; i++) grid_.deleteReadingBeforeCursor();
  const std::string& separator = grid_.readingSeparator();
  for (size_t from = 0;;) {
    size_t to = reading.find(separator, from);
    grid_.insertReading(reading.substr(from, to - from));
    if (to == std::string::npos) break;
    from = to + separator.size();
  }
  grid_.setCursor(std::min(cursor, grid_.length()));  // a syllable with no words of its own does not go in
}

std::vector<std::string> Composer::candidates(const Segment& segment, bool fromEnd) {
  std::vector<std::string> values;
  for (const auto& [at, candidate] : choices(segment, fromEnd)) {
    if (std::find(values.begin(), values.end(), candidate.value) == values.end()) values.push_back(candidate.value);
  }
  return values;
}

std::optional<std::pair<std::string, std::string>> Composer::wordIn(size_t from, size_t to) const {
  const std::vector<std::string>& readings = grid_.readings();
  if (from >= to || to > readings.size()) return std::nullopt;
  std::string word, reading;
  for (size_t i = from; i < to; i++) {
    // Without a tone it stands for several; punctuation (_punctuation_…) is no part of a word.
    if (readings[i].find(kAnyTone) != std::string::npos || readings[i][0] == '_') return std::nullopt;
    reading += (i > from ? grid_.readingSeparator() : "") + readings[i];
  }
  for (const Segment& segment : segments_) {
    if (segment.start + segment.length <= from || segment.start >= to) continue;
    if (fuzzy_) {  // a word of a near sound is listed under what was typed, which is not its reading
      std::string own;
      for (size_t i = segment.start; i < segment.start + segment.length; i++) {
        own += (i > segment.start ? grid_.readingSeparator() : "") + readings[i];
      }
      std::vector<Formosa::Gramambular2::LanguageModel::Unigram> words = lm_->getUnigrams(own);
      if (std::none_of(words.begin(), words.end(), [&](const auto& unigram) { return unigram.value() == segment.text; })) return std::nullopt;
    }
    std::vector<size_t> starts;  // where each character begins
    for (size_t at = 0; at < segment.text.size();) {
      starts.push_back(at);
      NextCodePoint(segment.text, at);
    }
    starts.push_back(segment.text.size());
    if (starts.size() - 1 != segment.length) return std::nullopt;
    size_t first = std::max(from, segment.start) - segment.start, last = std::min(to, segment.start + segment.length) - segment.start;
    word += segment.text.substr(starts[first], starts[last] - starts[first]);
  }
  return std::make_pair(word, reading);
}

std::string Composer::readingOf(const Segment& segment, const std::string& value, bool fromEnd) {
  for (const auto& [start, candidate] : choices(segment, fromEnd)) {
    if (candidate.value != value) continue;
    if (fuzzy_) {  // a word of a near sound is listed under what was typed, which is not its reading
      std::vector<Formosa::Gramambular2::LanguageModel::Unigram> own = lm_->getUnigrams(candidate.reading);
      if (std::none_of(own.begin(), own.end(), [&](const auto& unigram) { return unigram.value() == value; })) return "";
    }
    return candidate.reading;
  }
  return "";
}

void Composer::reloadWords() {
  std::vector<std::string> readings = grid_.readings();
  size_t cursor = grid_.cursor();
  // A new dictionary, as the old one keeps the words it looked up.
  dictionary_ = std::make_shared<TonelessModel>(std::static_pointer_cast<Lexicon>(lm_), abbreviated_, fuzzy_);
  grid_ = Formosa::Gramambular2::ReadingGrid(dictionary_);
  // A syllable whose every word was just excluded does not go back in, so the cursor counts those that do.
  size_t kept = 0;
  for (size_t i = 0; i < readings.size(); i++) {
    if (grid_.insertReading(readings[i]) && i < cursor) kept++;
  }
  grid_.setCursor(kept);
  walk();
}

bool Composer::choose(const Segment& segment, const std::string& value, bool fromEnd) {
  // Found before walk() rebuilds segments_, which `segment` may belong to.
  for (const auto& [start, candidate] : choices(segment, fromEnd)) {
    if (candidate.value != value) continue;
    size_t length = std::count(candidate.reading.begin(), candidate.reading.end(), kSeparator) + 1;
    std::string typed = grid_.readings()[start];
    for (size_t i = start + 1; i < start + length; i++) typed += grid_.readingSeparator() + grid_.readings()[i];
    if (typed != candidate.reading) replaceReadings(start, length, candidate.reading);
    if (grid_.overrideCandidate(start, candidate)) {
      endReading();  // a word chosen ends the syllable being typed
      walk();
      if (userModel_) {
        std::string previousReading, previousValue;
        for (const Segment& before : segments_) {
          if (before.start + before.length == start) {
            previousReading = before.reading;
            previousValue = before.text;
          }
        }
        userModel_->observe(UserKey(previousReading, previousValue, candidate.reading), value, Now());
      }
      return true;
    }
  }
  return false;
}

}  // namespace ProjectKey
