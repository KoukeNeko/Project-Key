// Composition state for one input session: a Daqian reading buffer feeding a
// McBopomofo reading grid. Platform independent so it can be tested without
// InputMethodKit.
#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Bigram.h"
#include "CharModel.h"
#include "Mandarin/Mandarin.h"
#include "McBopomofoLM.h"
#include "MemoryMappedFile.h"
#include "ParselessPhraseDB.h"
#include "PersonalModel.h"
#include "UserOverrideModel.h"
#include "gramambular2/reading_grid.h"

namespace ProjectKey {

struct Segment {
  std::string text;
  std::string reading;  // joined with the grid's separator; a syllable typed without a tone ends in kAnyTone
  size_t start = 0;     // in readings
  size_t length = 0;  // in readings
  bool doubtful = false;
};

// McBopomofo's language model, which also finds data.txt's rows by the start of a reading.
class Lexicon : public McBopomofo::McBopomofoLM {
 public:
  // Hides McBopomofoLM's own, which loads only the exact lookups.
  void loadLanguageModel(const char* path);
  // The user's own words, one "word reading" line each (小鍵 ㄒㄧㄠˇ-ㄐㄧㄢˋ), '#' starting a
  // comment; they win over the dictionary's words of the same reading. A missing file has none.
  void loadUserPhrases(const std::string& path);
  // The "reading word score" rows whose reading starts with `prefix`, then the user's
  // readings that do, each a row of the reading alone.
  std::vector<std::string_view> rowsStartingWith(const std::string& prefix) const;

 private:
  McBopomofo::MemoryMappedFile file_;
  std::unique_ptr<McBopomofo::ParselessPhraseDB> rows_;
  std::string userPhrasesText_;            // the user phrases file, which McBopomofo's points into
  std::vector<std::string> userReadings_;  // sorted, unique
};

class Composer {
 public:
  // With a context model, each word is scored in the context of the two words
  // before it: log10(weight * context + (1 - weight) * 10^unigram score), where
  // context is P(word | previous), mixed with P(word | two previous) by
  // trigramWeight when the model has that triple.
  // With a user model, choices are remembered by the word before them and the
  // reading, and a remembered word wins the walk the next time.
  // Without abbreviated, a lone symbol is no syllable of its own and a Zhuyin key edits the open
  // syllable (ㄅ then ㄆ is ㄆ) until it is a whole one; then a key that cannot extend it starts
  // the next (ㄖㄣㄨㄟ for 認為), and the open syllable shows as its symbols until it ends.
  explicit Composer(std::shared_ptr<Lexicon> lm, std::shared_ptr<const Bigram> bigram = nullptr,
                    std::shared_ptr<McBopomofo::UserOverrideModel> userModel = nullptr,
                    double contextWeight = kContextWeight, double trigramWeight = kTrigramWeight,
                    std::shared_ptr<const CharModel> chars = nullptr,
                    double charWeight = kCharWeight,
                    std::shared_ptr<const PersonalModel> personal = nullptr, bool abbreviated = true);

  static constexpr double kContextWeight = 0.3, kTrigramWeight = 0.5;
  // How much the character model's log10 probability adds to a word's score (it only works with a Bigram).
  static constexpr double kCharWeight = 0.2;
  // Ends a grid reading's syllable that was typed without a tone; it reads as any of its tones,
  // and, when abbreviated, a lone symbol as any syllable it begins (ㄋ for 你 or 那).
  static constexpr char kAnyTone = '*';

  enum class KeyResult { kHandled, kInvalid };

  bool isEmpty() const { return grid_.length() == 0 && reading_.isEmpty(); }
  // A syllable is open: a tone key or Space completes it.
  bool hasPendingReading() const { return !reading_.isEmpty(); }
  bool isReadingKey(char key) const { return reading_.isValidKey(key); }
  // Whether `keys` type exactly one dictionary syllable: each key sets its own
  // component (none replaces another, as in English words), ending with a tone
  // key, a space for the first tone, or neither for any tone.
  bool isSyllableKeys(const std::string& keys) const;

  // Adds a Daqian key; a tone key completes the syllable. Without a tone, a whole
  // syllable, or a lone symbol, is in the text as its word right away, and it ends
  // where a key starts the next one.
  KeyResult typeReadingKey(char key);
  // Completes the pending syllable with tone 1 (the space bar).
  KeyResult completeWithFirstTone();
  // Inserts a punctuation reading such as "<" for "，". False if unknown.
  bool insertPunctuation(char key);
  // Removes the last reading key, or the syllable before the cursor.
  bool backspace();
  // Removes the syllable after the cursor.
  bool deleteForward();

  // Position between syllables, 0...readingCount(). New syllables go here.
  size_t cursor() const { return grid_.cursor(); }
  size_t readingCount() const { return grid_.length(); }
  void setCursor(size_t cursor);
  void clearPendingReading();
  // Shows the open syllable as its symbols rather than as a word, as Enter sends it.
  void showSymbols();
  // Puts a whole syllable still open without a tone, and shown as its symbols, in as its word,
  // as a key starting the next syllable would.
  void endOpenSyllable();
  // The app's text before the caret where the composition starts. When it ends in a Han
  // character the first word follows the word there instead of starting a phrase.
  void setTextBefore(const std::string& text);
  void clear();

  const std::vector<Segment>& segments() const { return segments_; }
  // The open syllable's symbols while they are not in the text as a word.
  std::string pendingReading() const { return readingShown_ ? "" : reading_.composedString(); }
  // The open syllable's symbols while its word is in the text, just before the cursor.
  std::string shownReading() const { return readingShown_ ? reading_.composedString() : ""; }
  std::string text() const;

  // The segment starting at `start`, or, inside a segment, its part from
  // `start` on (with empty text), so a later syllable of a phrase can be chosen.
  std::optional<Segment> segmentFrom(size_t start) const;

  // Candidates for the segment: same-length phrases first, then the shorter
  // phrases that start where the segment starts, or, fromEnd, the longer phrases
  // that end where it ends, then its own and the shorter ones (for the word before
  // the cursor: 打字, then 字 and the like for its ㄗˋ). Each reading's words come
  // first, then those of the same syllables in other tones (偽造 for ㄨㄟˇ-ㄗㄠˋ).
  std::vector<std::string> candidates(const Segment& segment, bool fromEnd = false);
  // Puts `value`, one of candidates(segment, fromEnd), in the grid where it stands.
  bool choose(const Segment& segment, const std::string& value, bool fromEnd = false);

 private:
  KeyResult insertPendingSyllable(const Formosa::Mandarin::BopomofoReadingBuffer& untoned);
  bool startsSyllable(char key) const;
  void showReading(bool ending = false);
  void hideReading();
  void endReading();
  void replaceReadings(size_t start, size_t length, const std::string& reading);
  // candidates() with where each one starts, in its order.
  std::vector<std::pair<size_t, Formosa::Gramambular2::ReadingGrid::Candidate>> choices(const Segment& segment, bool fromEnd);
  void walk();
  double wordScore(uint32_t first, uint32_t second, uint32_t word, double unigramScore) const;

  std::shared_ptr<McBopomofo::McBopomofoLM> lm_;
  // lm_, reading syllables that end in kAnyTone as described there.
  std::shared_ptr<Formosa::Gramambular2::LanguageModel> dictionary_;
  std::shared_ptr<const Bigram> bigram_;
  double contextWeight_;
  double trigramWeight_;
  std::shared_ptr<const CharModel> chars_;
  double charWeight_;
  // What the user typed; it scores characters even without chars_.
  std::shared_ptr<const PersonalModel> personal_;
  uint64_t personalVersion_ = 0;  // personal_'s version when charCache_ was filled
  bool scoresChars() const { return chars_ || personal_; }
  // The character model's log10 P(c | a b) by a << 42 | b << 21 | c.
  std::unordered_map<uint64_t, double> charCache_;
  std::shared_ptr<McBopomofo::UserOverrideModel> userModel_;
  bool abbreviated_;
  Formosa::Gramambular2::ReadingGrid grid_;
  Formosa::Mandarin::BopomofoReadingBuffer reading_;
  bool readingShown_ = false;  // the open syllable is in the grid, toneless, right before the cursor
  std::string hanBefore_;      // the Han characters right before the composition, at most kWordBeforeLength
  std::vector<Segment> segments_;
};

}  // namespace ProjectKey
