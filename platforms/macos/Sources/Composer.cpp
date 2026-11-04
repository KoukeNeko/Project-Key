#include "Composer.h"

#include <algorithm>
#include <chrono>
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
// Words outside the context model, and the missing word before the first.
constexpr uint32_t kNoWord = UINT32_MAX;

double Now() {
  return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// The user model's key: the word before (empty at the start) and the reading.
std::string UserKey(const std::string& previousReading, const std::string& previousValue, const std::string& reading) {
  return (previousReading.empty() ? "" : "(" + previousReading + "," + previousValue + ")") + "|" + reading;
}
}  // namespace

Composer::Composer(std::shared_ptr<McBopomofo::McBopomofoLM> lm, std::shared_ptr<const Bigram> bigram,
                   std::shared_ptr<McBopomofo::UserOverrideModel> userModel, double contextWeight,
                   double trigramWeight)
    : lm_(lm),
      bigram_(bigram && bigram->isLoaded() ? std::move(bigram) : nullptr),
      contextWeight_(contextWeight),
      trigramWeight_(trigramWeight),
      userModel_(std::move(userModel)),
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
  };
  const std::vector<ReadingGrid::Span>& spans = grid_.spans();
  const size_t n = spans.size();
  const uint32_t boundary = bigram_ ? bigram_->id(Bigram::kBoundary).value_or(kNoWord) : kNoWord;
  auto idOf = [&](const std::string& value) { return bigram_ ? bigram_->id(value).value_or(kNoWord) : kNoWord; };
  auto score = [&](uint32_t first, uint32_t second, uint32_t word, double unigramScore) {
    return bigram_ ? wordScore(first, second, word, unigramScore) : unigramScore;
  };
  std::vector<Choice> choices;
  std::vector<std::vector<int>> endingAt(n + 1);
  for (size_t start = 0; start < n; start++) {
    for (size_t length = 1; length <= spans[start].maxLength(); length++) {
      const ReadingGrid::NodePtr& node = spans[start].nodeOf(length);
      if (!node) continue;
      auto add = [&](std::string value, double unigramScore) {
        endingAt[start + length].push_back(static_cast<int>(choices.size()));
        uint32_t id = idOf(value);
        choices.push_back({start, length, node, std::move(value), id, unigramScore, {}});
      };
      if (node->isOverridden() || (!bigram_ && !userModel_)) {
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
      double first = learned(-1, choice) ? 0 : score(kNoWord, boundary, choice.id, choice.unigramScore);
      choice.states.push_back({-1, first, -1});
      continue;
    }
    for (int previous : endingAt[choice.start]) {
      const Choice& before = choices[previous];
      State state{previous, -INFINITY, -1};
      bool certain = learned(previous, choice);
      for (size_t k = 0; k < before.states.size(); k++) {
        uint32_t first = before.states[k].previous >= 0 ? choices[before.states[k].previous].id : boundary;
        double total = before.states[k].best + (certain ? 0 : score(first, before.id, choice.id, choice.unigramScore));
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
      uint32_t first = state.previous >= 0 ? choices[state.previous].id : boundary;
      double total = state.best + score(first, choices[end].id, boundary, kEndScore);
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
  auto idAt = [&](long i) { return i < 0 || i >= static_cast<long>(path.size()) ? boundary : choices[path[i]].id; };
  for (size_t i = 0; i < path.size(); i++) {
    const Choice& choice = choices[path[i]];
    long at = static_cast<long>(i);
    uint32_t first = i > 0 ? idAt(at - 2) : kNoWord, previous = idAt(at - 1);
    bool hasNext = i + 1 < path.size();
    const Choice* next = hasNext ? &choices[path[i + 1]] : nullptr;
    auto local = [&](uint32_t word, double unigramScore) {
      double total = score(first, previous, word, unigramScore);
      total += score(previous, word, idAt(at + 1), next ? next->unigramScore : kEndScore);
      if (next) total += score(word, next->id, idAt(at + 2), i + 2 < path.size() ? choices[path[i + 2]].unigramScore : kEndScore);
      return total;
    };
    bool doubtful = false;
    if (!choice.node->isOverridden() && !learned(i > 0 ? path[i - 1] : -1, choice)) {
      double chosen = local(choice.id, choice.unigramScore);
      const auto& unigrams = choice.node->unigrams();
      for (size_t k = 0; k < std::min(kWordsPerNode, unigrams.size()) && !doubtful; k++) {
        if (unigrams[k].value() != choice.value) {
          doubtful = chosen - local(idOf(unigrams[k].value()), unigrams[k].score()) < kDoubtfulScoreGap;
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
