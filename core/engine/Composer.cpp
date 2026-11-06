#include "Composer.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <cmath>
#include <cstring>

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

// The dictionary, reading a syllable that ends in Composer::kAnyTone as any of its tones,
// and, when abbreviated, a lone symbol that ends in it as any syllable it begins.
class TonelessModel : public LanguageModel {
 public:
  TonelessModel(std::shared_ptr<Lexicon> lm, bool abbreviated) : lm_(std::move(lm)), abbreviated_(abbreviated) {}

  std::vector<Unigram> getUnigrams(const std::string& reading) override {
    size_t marker = reading.find(Composer::kAnyTone);
    // Punctuation readings such as _punctuation_* are not syllables.
    if (marker == std::string::npos || reading[0] == '_') return lm_->getUnigrams(reading);
    auto cached = cache_.find(reading);
    if (cached != cache_.end()) return cached->second;
    std::vector<Typed> typed;
    for (size_t start = 0;;) {
      size_t end = reading.find(kSeparator, start);
      typed.push_back(Typed::Of(reading.substr(start, end - start), abbreviated_));
      if (end == std::string::npos) break;
      start = end + 1;
    }
    std::vector<Unigram> unigrams;
    std::unordered_map<std::string, size_t> indexOf;  // by value
    std::string_view previous;
    // Every match starts with what was typed before the first kAnyTone; a reading's rows are adjacent.
    for (std::string_view row : lm_->rowsStartingWith(reading.substr(0, marker))) {
      std::string_view key = row.substr(0, row.find(' '));
      if (key == previous) continue;
      previous = key;
      if (!Matches(typed, key)) continue;
      std::string toned(key);
      for (Unigram& unigram : lm_->getUnigrams(toned)) {
        // The dictionary also lists each lone symbol as itself (ㄉ for ㄉ), which is no syllable;
        // the candidates offer the Zhuyin anyway.
        if (unigram.value() == toned) continue;
        auto [at, added] = indexOf.emplace(unigram.value(), unigrams.size());
        if (added) {
          unigrams.push_back(std::move(unigram));
        } else if (unigram.score() > unigrams[at->second].score()) {
          unigrams[at->second] = std::move(unigram);
        }
      }
    }
    std::stable_sort(unigrams.begin(), unigrams.end(), [](const Unigram& a, const Unigram& b) { return a.score() > b.score(); });
    if (cache_.size() >= kCacheSize) cache_.clear();
    return cache_.emplace(reading, std::move(unigrams)).first->second;
  }

  bool hasUnigrams(const std::string& reading) override {
    if (reading.find(Composer::kAnyTone) == std::string::npos) return lm_->hasUnigrams(reading);
    return !getUnigrams(reading).empty();
  }

 private:
  static constexpr size_t kCacheSize = 4096;

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
  bool abbreviated_;
  std::unordered_map<std::string, std::vector<Unigram>> cache_;
};


}  // namespace

void Lexicon::loadLanguageModel(const char* path) {
  McBopomofoLM::loadLanguageModel(path);
  if (!path) return;
  rows_.reset();
  file_.close();
  if (file_.open(path)) {
    rows_ = std::make_unique<McBopomofo::ParselessPhraseDB>(file_.data(), file_.length(), /*validate_pragma=*/true);
  }
}

std::vector<std::string_view> Lexicon::rowsStartingWith(const std::string& prefix) const {
  return rows_ ? rows_->findRows(prefix) : std::vector<std::string_view>{};
}

Composer::Composer(std::shared_ptr<Lexicon> lm, std::shared_ptr<const Bigram> bigram,
                   std::shared_ptr<McBopomofo::UserOverrideModel> userModel, double contextWeight,
                   double trigramWeight, std::shared_ptr<const CharModel> chars,
                   double charWeight, std::shared_ptr<const PersonalModel> personal, bool abbreviated)
    : lm_(lm),
      dictionary_(std::make_shared<TonelessModel>(lm, abbreviated)),
      bigram_(bigram && bigram->isLoaded() ? std::move(bigram) : nullptr),
      contextWeight_(contextWeight),
      trigramWeight_(trigramWeight),
      chars_(bigram_ && chars && chars->isLoaded() ? std::move(chars) : nullptr),
      charWeight_(charWeight),
      personal_(std::move(personal)),
      userModel_(std::move(userModel)),
      abbreviated_(abbreviated),
      grid_(dictionary_),
      reading_(BopomofoKeyboardLayout::StandardLayout()) {}

Composer::KeyResult Composer::typeReadingKey(char key) {
  if (!reading_.isValidKey(key)) return KeyResult::kInvalid;
  bool shown = readingShown_;
  if (readingShown_ && startsSyllable(key)) endReading();
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

// With a whole syllable open and no tone typed, whether `key` begins the next syllable:
// it goes in the place of a symbol already there or before one (the order is
// consonant, medial, vowel), or it makes a syllable the dictionary lacks.
bool Composer::startsSyllable(char key) const {
  if (!abbreviated_) return false;
  BopomofoSyllable typed = BopomofoKeyboardLayout::StandardLayout()->syllableFromKeySequence(std::string(1, key));
  if (typed.hasToneMarker()) return false;
  auto lastPlace = [](const BopomofoSyllable& s) { return s.hasVowel() ? 3 : s.hasMiddleVowel() ? 2 : s.hasConsonant() ? 1 : 0; };
  BopomofoSyllable open = reading_.syllable();
  if (lastPlace(typed) <= lastPlace(open)) return true;
  std::string joined = (open + typed).composedString();
  return !dictionary_->hasUnigrams(joined + kAnyTone);
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

bool Composer::insertPunctuation(char key) {
  for (const std::string& prefix : {"_punctuation_Standard_", "_punctuation_"}) {
    std::string reading = prefix + std::string(1, key);
    if (lm_->hasUnigrams(reading)) {
      if (!readingShown_) showReading(/*ending=*/true);  // the open syllable goes in as its word
      endReading();
      grid_.insertReading(reading);
      walk();
      return true;
    }
  }
  return false;
}

bool Composer::isSyllableKeys(const std::string& keys) const {
  if (keys.empty()) return false;
  std::string typed = keys.back() == ' ' ? keys.substr(0, keys.size() - 1) : keys;
  if (typed.empty() || typed.find(' ') != std::string::npos) return false;
  const BopomofoKeyboardLayout* layout = BopomofoKeyboardLayout::StandardLayout();
  BopomofoSyllable syllable = layout->syllableFromKeySequence(typed);
  bool hasTone = syllable.hasToneMarker(), space = keys.back() == ' ';
  if (hasTone && space) return false;  // a tone key ends it, a space for the first tone, or neither for any tone
  if (!hasTone && !space && typed.size() == 1) return false;  // a lone symbol only stands for a syllable
  std::string reading = syllable.composedString() + (hasTone || space ? "" : std::string(1, kAnyTone));
  return layout->keySequenceFromSyllable(syllable) == typed && dictionary_->hasUnigrams(reading);
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

void Composer::clear() {
  hanBefore_.clear();
  endReading();
  grid_.clear();
  segments_.clear();
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

// candidatesAt() returns every node overlapping the location; keep the ones
// that begin where the segment begins: its own reading or a prefix of it.
bool Composer::startsWithSegment(const Segment& segment, const std::string& reading) const {
  return reading == segment.reading || segment.reading.rfind(reading + grid_.readingSeparator(), 0) == 0;
}

std::vector<std::string> Composer::candidates(const Segment& segment) {
  std::vector<std::string> values;
  for (const ReadingGrid::Candidate& candidate : grid_.candidatesAt(segment.start)) {
    if (startsWithSegment(segment, candidate.reading) &&
        std::find(values.begin(), values.end(), candidate.value) == values.end()) {
      values.push_back(candidate.value);
    }
  }
  return values;
}

bool Composer::choose(const Segment& segment, const std::string& value) {
  const size_t start = segment.start;  // walk() rebuilds segments_, which `segment` may belong to
  for (const ReadingGrid::Candidate& candidate : grid_.candidatesAt(segment.start)) {
    if (candidate.value != value || !startsWithSegment(segment, candidate.reading)) continue;
    if (grid_.overrideCandidate(segment.start, candidate)) {
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
