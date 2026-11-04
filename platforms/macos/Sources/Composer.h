// Composition state for one input session: a Daqian reading buffer feeding a
// McBopomofo reading grid. Platform independent so it can be tested without
// InputMethodKit.
#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "Bigram.h"
#include "Mandarin/Mandarin.h"
#include "McBopomofoLM.h"
#include "gramambular2/reading_grid.h"

namespace ProjectKey {

struct Segment {
  std::string text;
  std::string reading;  // joined with the grid's separator
  size_t start = 0;     // in readings
  size_t length = 0;  // in readings
  bool doubtful = false;
};

class Composer {
 public:
  // With a bigram model, each word is scored in the context of the word before
  // it: log10(weight * P(word | previous) + (1 - weight) * 10^unigram score).
  explicit Composer(std::shared_ptr<McBopomofo::McBopomofoLM> lm, std::shared_ptr<const Bigram> bigram = nullptr,
                    double contextWeight = 0.3);

  enum class KeyResult { kHandled, kInvalid };

  bool isEmpty() const { return grid_.length() == 0 && reading_.isEmpty(); }
  bool hasPendingReading() const { return !reading_.isEmpty(); }
  bool isReadingKey(char key) const { return reading_.isValidKey(key); }

  // Adds a Daqian key; a tone key completes the syllable.
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
  void setCursor(size_t cursor) { grid_.setCursor(std::min(cursor, grid_.length())); }
  void clearPendingReading() { reading_.clear(); }
  void clear();

  const std::vector<Segment>& segments() const { return segments_; }
  std::string pendingReading() const { return reading_.composedString(); }
  std::string text() const;

  // Candidates for the segment: same-length phrases first, then the shorter
  // phrases that start where the segment starts.
  std::vector<std::string> candidates(const Segment& segment);
  bool choose(const Segment& segment, const std::string& value);

 private:
  KeyResult insertPendingSyllable();
  bool startsWithSegment(const Segment& segment, const std::string& reading) const;
  void walk();
  double wordScore(const std::string* previous, const std::string& word, double unigramScore) const;

  std::shared_ptr<McBopomofo::McBopomofoLM> lm_;
  std::shared_ptr<const Bigram> bigram_;
  double contextWeight_;
  Formosa::Gramambular2::ReadingGrid grid_;
  Formosa::Mandarin::BopomofoReadingBuffer reading_;
  std::vector<Segment> segments_;
};

}  // namespace ProjectKey
