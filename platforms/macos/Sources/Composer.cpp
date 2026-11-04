#include "Composer.h"

#include <algorithm>

namespace ProjectKey {

using Formosa::Gramambular2::ReadingGrid;
using Formosa::Mandarin::BopomofoKeyboardLayout;

namespace {
// Scores are log10 probabilities. When the walked phrase leads the next phrase
// with the same reading by less than this, the engine is guessing and the
// proof line marks the segment.
constexpr double kDoubtfulScoreGap = 0.5;
}  // namespace

Composer::Composer(std::shared_ptr<McBopomofo::McBopomofoLM> lm)
    : lm_(lm), grid_(lm), reading_(BopomofoKeyboardLayout::StandardLayout()) {}

Composer::KeyResult Composer::typeReadingKey(char key) {
  if (!reading_.combineKey(key)) return KeyResult::kInvalid;
  if (reading_.hasToneMarker() && !reading_.hasToneMarkerOnly()) return insertPendingSyllable();
  return KeyResult::kHandled;
}

Composer::KeyResult Composer::completeWithFirstTone() {
  if (reading_.isEmpty()) return KeyResult::kInvalid;
  return insertPendingSyllable();
}

Composer::KeyResult Composer::insertPendingSyllable() {
  std::string syllable = reading_.composedString();
  reading_.clear();
  if (!lm_->hasUnigrams(syllable) || !grid_.insertReading(syllable)) return KeyResult::kInvalid;
  walk();
  return KeyResult::kHandled;
}

bool Composer::insertPunctuation(char key) {
  for (const std::string& prefix : {"_punctuation_Standard_", "_punctuation_"}) {
    std::string reading = prefix + std::string(1, key);
    if (lm_->hasUnigrams(reading)) {
      reading_.clear();
      grid_.insertReading(reading);
      walk();
      return true;
    }
  }
  return false;
}

bool Composer::backspace() {
  if (!reading_.isEmpty()) {
    reading_.backspace();
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

void Composer::clear() {
  reading_.clear();
  grid_.clear();
  segments_.clear();
}

std::string Composer::text() const {
  std::string result;
  for (const Segment& segment : segments_) result += segment.text;
  return result;
}

void Composer::walk() {
  segments_.clear();
  size_t start = 0;
  for (const ReadingGrid::NodePtr& node : grid_.walk().nodes) {
    const auto& unigrams = node->unigrams();
    bool close = unigrams.size() > 1 && unigrams[0].score() - unigrams[1].score() < kDoubtfulScoreGap;
    segments_.push_back({node->value(), node->reading(), start, node->spanningLength(), close && !node->isOverridden()});
    start += node->spanningLength();
  }
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
  for (const ReadingGrid::Candidate& candidate : grid_.candidatesAt(segment.start)) {
    if (candidate.value != value || !startsWithSegment(segment, candidate.reading)) continue;
    if (grid_.overrideCandidate(segment.start, candidate)) {
      walk();
      return true;
    }
  }
  return false;
}

}  // namespace ProjectKey
