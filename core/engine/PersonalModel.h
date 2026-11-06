// What the user typed, learned on the device: counts of the character triples in the
// Han text they committed, which move the character model's probabilities toward the
// user's own phrasing. Kept in one local file of counts, never the sentences themselves.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace ProjectKey {

class PersonalModel {
 public:
  // Counts every run of Han characters in text, its start and end included (CharModel::kBoundary).
  void learn(const std::string& text);
  void clear();
  bool empty() const { return triples_.empty(); }
  // `base`, P(next | first, second) without what the user typed, moved toward how often the
  // user typed next after second, then after first second: each the more, the more often the
  // user typed that context.
  double blend(double base, uint32_t first, uint32_t second, uint32_t next) const;
  // Changes whenever the counts do, so scores kept elsewhere can be dropped.
  uint64_t version() const { return version_; }

  // One "count<TAB>three characters" line per triple, '^' for the boundary. A missing file
  // loads as empty; unreadable lines are skipped.
  bool load(const std::string& path);
  // Written to a temporary file and renamed over the old one; missing folders are made.
  bool save(const std::string& path) const;

  // How many times the user typed a context before what they typed after it counts as much
  // as the model. Tuned on held-out personal text against everyday text by others
  // (tools/corpus-evaluator/results/2026-11-06-sources.md): lower learns faster but costs
  // more on text unlike the user's.
  static constexpr double kConfidence = 32;
  // Above this many triples, the ones typed once are dropped.
  static constexpr size_t kMaxTriples = 200000;

 private:
  void add(uint64_t triple, uint32_t count);
  void rebuild();

  std::unordered_map<uint64_t, uint32_t> triples_;   // a << 42 | b << 21 | c
  std::unordered_map<uint64_t, uint32_t> pairs_;     // b << 21 | c, over every a
  std::unordered_map<uint64_t, uint32_t> contexts_;  // a << 21 | b, over every c
  std::unordered_map<uint32_t, uint32_t> singles_;   // b, over every b c
  uint64_t version_ = 0;
};

}  // namespace ProjectKey
