// Composition state for one input session: a Daqian reading buffer feeding a
// McBopomofo reading grid. Platform independent so it can be tested without
// InputMethodKit.
#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Bigram.h"
#include "Mandarin/Mandarin.h"
#include "McBopomofoLM.h"
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

class Composer {
 public:
  // With a context model, each word is scored in the context of the two words
  // before it: log10(weight * context + (1 - weight) * 10^unigram score), where
  // context is P(word | previous), mixed with P(word | two previous) by
  // trigramWeight when the model has that triple.
  // With a user model, choices are remembered by the word before them and the
  // reading, and a remembered word wins the walk the next time.
  // Each syllable also matches the readings one adjacent key away (a slip
  // such as ㄧㄡˇ for ㄧㄝˇ, or a key in the row above or below), scored adjacentKeyScore
  // lower; -infinity turns it off.
  explicit Composer(std::shared_ptr<McBopomofo::McBopomofoLM> lm, std::shared_ptr<const Bigram> bigram = nullptr,
                    std::shared_ptr<McBopomofo::UserOverrideModel> userModel = nullptr,
                    double contextWeight = kContextWeight, double trigramWeight = kTrigramWeight,
                    double adjacentKeyScore = kAdjacentKeyScore);

  static constexpr double kContextWeight = 0.3, kTrigramWeight = 0.5, kAdjacentKeyScore = -3;
  // Ends a grid reading's syllable that was typed without a tone; it reads as any of its tones.
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
  // syllable is in the text as its word right away, and it ends where a key starts
  // the next one.
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
  void clear();

  const std::vector<Segment>& segments() const { return segments_; }
  // The open syllable's symbols while they are not in the text as a word.
  std::string pendingReading() const { return readingShown_ ? "" : reading_.composedString(); }
  std::string text() const;

  // The segment starting at `start`, or, inside a segment, its part from
  // `start` on (with empty text), so a later syllable of a phrase can be chosen.
  std::optional<Segment> segmentFrom(size_t start) const;

  // Candidates for the segment: same-length phrases first, then the shorter
  // phrases that start where the segment starts.
  std::vector<std::string> candidates(const Segment& segment);
  bool choose(const Segment& segment, const std::string& value);

 private:
  KeyResult insertPendingSyllable(const Formosa::Mandarin::BopomofoReadingBuffer& untoned);
  bool startsSyllable(char key) const;
  void showReading();
  void hideReading();
  void endReading();
  bool startsWithSegment(const Segment& segment, const std::string& reading) const;
  void walk();
  double wordScore(uint32_t first, uint32_t second, uint32_t word, double unigramScore) const;

  std::shared_ptr<McBopomofo::McBopomofoLM> lm_;
  // lm_, reading syllables that end in kAnyTone as any of their tones.
  std::shared_ptr<Formosa::Gramambular2::LanguageModel> dictionary_;
  std::shared_ptr<const Bigram> bigram_;
  double contextWeight_;
  double trigramWeight_;
  std::shared_ptr<McBopomofo::UserOverrideModel> userModel_;
  double adjacentKeyScore_;
  Formosa::Gramambular2::ReadingGrid grid_;
  Formosa::Mandarin::BopomofoReadingBuffer reading_;
  bool readingShown_ = false;  // the open syllable is in the grid, toneless, right before the cursor
  std::vector<Segment> segments_;
};

}  // namespace ProjectKey
