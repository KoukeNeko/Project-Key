#include "Composer.h"

#include <algorithm>
#include <cmath>

namespace ProjectKey {

using Formosa::Gramambular2::ReadingGrid;
using Formosa::Mandarin::BopomofoKeyboardLayout;

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
const std::string kBoundary = Bigram::kBoundary;
}  // namespace

Composer::Composer(std::shared_ptr<McBopomofo::McBopomofoLM> lm, std::shared_ptr<const Bigram> bigram,
                   double contextWeight)
    : lm_(lm),
      bigram_(bigram && bigram->isLoaded() ? std::move(bigram) : nullptr),
      contextWeight_(contextWeight),
      grid_(lm),
      reading_(BopomofoKeyboardLayout::StandardLayout()) {}

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

double Composer::wordScore(const std::string* previous, const std::string& word, double unigramScore) const {
  if (!bigram_) return unigramScore;
  std::optional<double> context = previous ? bigram_->log10Probability(*previous, word) : std::nullopt;
  if (!context) return unigramScore + std::log10(1 - contextWeight_);
  return std::log10(contextWeight_ * std::pow(10, *context) + (1 - contextWeight_) * std::pow(10, unigramScore));
}

// Viterbi over (node, word) choices. Without a bigram model only each node's
// top word can win, which is exactly gramambular2's own walk.
void Composer::walk() {
  struct Choice {
    size_t start, length;
    ReadingGrid::NodePtr node;
    std::string value;
    double unigramScore;
    double best = -INFINITY;
    int back = -1;
  };
  const std::vector<ReadingGrid::Span>& spans = grid_.spans();
  const size_t n = spans.size();
  std::vector<Choice> choices;
  std::vector<std::vector<int>> endingAt(n + 1);
  for (size_t start = 0; start < n; start++) {
    for (size_t length = 1; length <= spans[start].maxLength(); length++) {
      const ReadingGrid::NodePtr& node = spans[start].nodeOf(length);
      if (!node) continue;
      auto add = [&](std::string value, double score) {
        endingAt[start + length].push_back(static_cast<int>(choices.size()));
        choices.push_back({start, length, node, std::move(value), score});
      };
      if (node->isOverridden() || !bigram_) {
        add(node->value(), node->score());
      } else {
        const auto& unigrams = node->unigrams();
        for (size_t k = 0; k < std::min(kWordsPerNode, unigrams.size()); k++) add(unigrams[k].value(), unigrams[k].score());
      }
    }
  }
  // Choices are in start order, so every predecessor is final before it is used.
  for (Choice& choice : choices) {
    if (choice.start == 0) {
      choice.best = wordScore(&kBoundary, choice.value, choice.unigramScore);
      continue;
    }
    for (int previous : endingAt[choice.start]) {
      double score = choices[previous].best + wordScore(&choices[previous].value, choice.value, choice.unigramScore);
      if (score > choice.best) {
        choice.best = score;
        choice.back = previous;
      }
    }
  }
  std::vector<int> path;
  int last = -1;
  double lastScore = -INFINITY;
  for (int end : endingAt[n]) {
    double score = choices[end].best + wordScore(&choices[end].value, kBoundary, kEndScore);
    if (score > lastScore) {
      lastScore = score;
      last = end;
    }
  }
  for (int at = last; at >= 0; at = choices[at].back) path.insert(path.begin(), at);

  // A word is doubtful when another word of the same node scores within the gap
  // in the same context.
  segments_.clear();
  for (size_t i = 0; i < path.size(); i++) {
    const Choice& choice = choices[path[i]];
    const std::string* previous = i > 0 ? &choices[path[i - 1]].value : &kBoundary;
    const Choice* next = i + 1 < path.size() ? &choices[path[i + 1]] : nullptr;
    auto local = [&](const std::string& value, double unigramScore) {
      return wordScore(previous, value, unigramScore) +
             (next ? wordScore(&value, next->value, next->unigramScore) : wordScore(&value, kBoundary, kEndScore));
    };
    bool doubtful = false;
    if (!choice.node->isOverridden()) {
      double chosen = local(choice.value, choice.unigramScore);
      const auto& unigrams = choice.node->unigrams();
      for (size_t k = 0; k < std::min(kWordsPerNode, unigrams.size()) && !doubtful; k++) {
        if (unigrams[k].value() != choice.value) doubtful = chosen - local(unigrams[k].value(), unigrams[k].score()) < kDoubtfulScoreGap;
      }
    }
    segments_.push_back({choice.value, choice.node->reading(), choice.start, choice.length, doubtful});
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
