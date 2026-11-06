// Trains the character trigram model (core/engine/CharModel.h).
// Usage: chars <min count> <output> < text
// Counts characters, pairs and triples inside each run of Han characters, its
// start and end included, and keeps the pairs and triples seen at least
// <min count> times. Discounts and backoff weights use every count.
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "CharModel.h"

namespace {

using ProjectKey::CharModel;
constexpr double kDiscount = 0.75;

// Sorted n-gram keys -> the kept n-grams' discounted probabilities, and the backoff
// weight of each context that keeps any.
void Estimate(std::vector<uint64_t>& keys, uint64_t minCount, std::vector<CharModel::Record>& kept,
              std::vector<CharModel::Record>& contexts) {
  std::sort(keys.begin(), keys.end());
  for (size_t i = 0; i < keys.size();) {
    uint64_t context = keys[i] >> CharModel::kBits;
    uint64_t total = 0, distinct = 0;
    std::vector<std::pair<uint64_t, uint64_t>> counts;
    size_t j = i;
    for (; j < keys.size() && keys[j] >> CharModel::kBits == context;) {
      size_t k = j;
      while (k < keys.size() && keys[k] == keys[j]) k++;
      counts.emplace_back(keys[j], k - j);
      total += k - j;
      distinct++;
      j = k;
    }
    size_t before = kept.size();
    for (const auto& [key, count] : counts) {
      if (count >= minCount) kept.push_back({key, static_cast<float>((count - kDiscount) / total)});
    }
    // The backoff weight is what the discounts free, D * distinct / total. The dropped n-grams'
    // share goes nowhere: giving it to the backoff too lets unseen continuations (選次 after 候選)
    // score high enough that a one-key slip (選字) no longer wins.
    // A context that keeps nothing backs off entirely, as one never seen does.
    if (kept.size() > before) contexts.push_back({context, static_cast<float>(kDiscount * distinct / total)});
    i = j;
  }
  std::vector<uint64_t>().swap(keys);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: " << argv[0] << " <min count> <output> < text\n";
    return 2;
  }
  const uint64_t minCount = std::stoull(argv[1]);
  std::unordered_map<uint32_t, uint64_t> unigrams;
  std::vector<uint64_t> bigrams, trigrams;
  std::vector<uint32_t> run;
  auto endRun = [&] {
    if (run.empty()) return;
    run.insert(run.begin(), CharModel::kBoundary);
    run.push_back(CharModel::kBoundary);
    for (size_t k = 1; k < run.size(); k++) {
      unigrams[run[k]]++;
      bigrams.push_back(static_cast<uint64_t>(run[k - 1]) << CharModel::kBits | run[k]);
      if (k >= 2) {
        trigrams.push_back(static_cast<uint64_t>(run[k - 2]) << (2 * CharModel::kBits) |
                           static_cast<uint64_t>(run[k - 1]) << CharModel::kBits | run[k]);
      }
    }
    run.clear();
  };
  std::string line;
  while (std::getline(std::cin, line)) {
    for (uint32_t c : CharModel::CodePoints(line)) {
      if (CharModel::IsHan(c)) {
        run.push_back(c);
      } else {
        endRun();
      }
    }
    endRun();
  }

  CharModel::Tables tables;
  uint64_t total = 0;
  for (const auto& [c, count] : unigrams) total += count;
  for (const auto& [c, count] : unigrams) tables.unigrams.push_back({c, static_cast<float>(static_cast<double>(count) / total)});
  std::sort(tables.unigrams.begin(), tables.unigrams.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
  Estimate(bigrams, minCount, tables.bigrams, tables.bigramContexts);
  Estimate(trigrams, minCount, tables.trigrams, tables.trigramContexts);
  if (!CharModel::save(argv[2], tables)) {
    std::cerr << "cannot write " << argv[2] << "\n";
    return 1;
  }
  std::cerr << tables.unigrams.size() << " characters, " << tables.bigrams.size() << " pairs, " << tables.trigrams.size()
            << " triples\n";
  return 0;
}
