#include "LexiconFile.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <unordered_map>

#include "CharModel.h"

namespace ProjectKey {

namespace {

constexpr char kMagic[8] = {'P', 'K', 'L', 'X', '0', '0', '0', '1'};
constexpr uint32_t kEndsHere = 1u << 31;

template <typename T>
void Write(std::ofstream& out, const T& value) {
  out.write(reinterpret_cast<const char*>(&value), sizeof value);
}

void AppendUtf8(std::string& out, uint32_t c) {
  if (c < 0x80) {
    out += static_cast<char>(c);
  } else if (c < 0x800) {
    out += static_cast<char>(0xC0 | c >> 6);
    out += static_cast<char>(0x80 | (c & 0x3F));
  } else if (c < 0x10000) {
    out += static_cast<char>(0xE0 | c >> 12);
    out += static_cast<char>(0x80 | (c >> 6 & 0x3F));
    out += static_cast<char>(0x80 | (c & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | c >> 18);
    out += static_cast<char>(0x80 | (c >> 12 & 0x3F));
    out += static_cast<char>(0x80 | (c >> 6 & 0x3F));
    out += static_cast<char>(0x80 | (c & 0x3F));
  }
}

// The automaton as it is built, by adding the sorted readings and merging each finished state
// with an equal one (Daciuk et al., 2000).
struct State {
  std::vector<std::pair<uint8_t, uint32_t>> arcs;
  bool ends = false;
};

std::string SignatureOf(const State& state) {
  std::string signature(1, state.ends ? '1' : '0');
  for (const auto& [symbol, target] : state.arcs) {
    signature += static_cast<char>(symbol);
    signature.append(reinterpret_cast<const char*>(&target), sizeof target);
  }
  return signature;
}

}  // namespace

bool LexiconFile::save(const std::string& path, const std::vector<Row>& rows) {
  std::set<uint32_t> used;
  for (const Row& row : rows) {
    for (uint32_t c : CharModel::CodePoints(row.reading)) used.insert(c);
  }
  if (used.size() > 256) return false;  // symbols are uint8
  const std::vector<uint32_t> symbols(used.begin(), used.end());
  auto symbolIndex = [&](uint32_t c) {
    return static_cast<uint8_t>(std::lower_bound(symbols.begin(), symbols.end(), c) - symbols.begin());
  };

  std::vector<State> states(1);
  std::vector<uint32_t> open{0};  // the states along the previous reading
  std::unordered_map<std::string, uint32_t> finished;
  auto finishDownTo = [&](size_t depth) {
    for (size_t at = open.size() - 1; at > depth; at--) {
      std::string signature = SignatureOf(states[open[at]]);
      auto [it, inserted] = finished.emplace(std::move(signature), open[at]);
      if (!inserted) states[open[at - 1]].arcs.back().second = it->second;
    }
    open.resize(depth + 1);
  };
  std::vector<std::string> readings;
  std::string blob;
  std::vector<uint32_t> rowStarts;
  std::vector<uint8_t> previous;
  for (const Row& row : rows) {
    if (row.word.size() > 255) return false;
    if (readings.empty() || readings.back() != row.reading) {
      if (!readings.empty() && row.reading <= readings.back()) return false;  // not sorted
      std::vector<uint8_t> current;
      for (uint32_t c : CharModel::CodePoints(row.reading)) current.push_back(symbolIndex(c));
      size_t common = 0;
      while (common < current.size() && common < previous.size() && current[common] == previous[common]) common++;
      finishDownTo(common);
      for (size_t i = common; i < current.size(); i++) {
        states.emplace_back();
        uint32_t added = static_cast<uint32_t>(states.size() - 1);
        states[open.back()].arcs.emplace_back(current[i], added);
        open.push_back(added);
      }
      states[open.back()].ends = true;
      previous = std::move(current);
      readings.push_back(row.reading);
      rowStarts.push_back(static_cast<uint32_t>(blob.size()));
    }
    blob += static_cast<char>(row.word.size());
    blob += row.word;
    blob.append(reinterpret_cast<const char*>(&row.score), sizeof row.score);
  }
  finishDownTo(0);
  rowStarts.push_back(static_cast<uint32_t>(blob.size()));

  // Number the states left, the root first, and count the readings through each.
  std::unordered_map<uint32_t, uint32_t> ids{{0, 0}};
  std::vector<uint32_t> order{0};
  for (size_t i = 0; i < order.size(); i++) {
    for (const auto& [symbol, target] : states[order[i]].arcs) {
      if (ids.emplace(target, static_cast<uint32_t>(order.size())).second) order.push_back(target);
    }
  }
  std::map<uint32_t, uint32_t> counts;
  auto countOf = [&](auto&& self, uint32_t state) -> uint32_t {
    auto known = counts.find(state);
    if (known != counts.end()) return known->second;
    uint32_t total = states[state].ends ? 1 : 0;
    for (const auto& [symbol, target] : states[state].arcs) total += self(self, target);
    return counts[state] = total;
  };

  std::ofstream out(path, std::ios::binary);
  uint32_t arcCount = 0;
  for (uint32_t state : order) arcCount += static_cast<uint32_t>(states[state].arcs.size());
  out.write(kMagic, sizeof kMagic);
  for (uint32_t value : {static_cast<uint32_t>(symbols.size()), static_cast<uint32_t>(order.size()), arcCount,
                         static_cast<uint32_t>(readings.size()), static_cast<uint32_t>(blob.size())}) {
    Write(out, value);
  }
  for (uint32_t symbol : symbols) Write(out, symbol);
  uint32_t firstArc = 0;
  for (uint32_t state : order) {
    Write(out, firstArc);
    firstArc += static_cast<uint32_t>(states[state].arcs.size());
  }
  Write(out, firstArc);
  for (uint32_t state : order) Write(out, countOf(countOf, state) | (states[state].ends ? kEndsHere : 0));
  for (uint32_t state : order) {
    for (const auto& [symbol, target] : states[state].arcs) Write(out, symbol);
  }
  for (uint32_t state : order) {
    for (const auto& [symbol, target] : states[state].arcs) Write(out, ids.at(target));
  }
  for (uint32_t start : rowStarts) Write(out, start);
  out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
  return static_cast<bool>(out);
}

bool LexiconFile::load(const std::string& path) {
  readingCount_ = 0;
  symbols_.clear();
  file_.close();
  if (!file_.open(path.c_str())) return false;
  const char* at = file_.data();
  const size_t length = file_.length();
  uint32_t header[5];
  if (length < sizeof kMagic + sizeof header || std::memcmp(at, kMagic, sizeof kMagic) != 0) {
    file_.close();
    return false;
  }
  std::memcpy(header, at + sizeof kMagic, sizeof header);
  at += sizeof kMagic + sizeof header;
  const auto [symbolCount, stateCount, arcCount, readingCount, rowBytes] =
      std::array<uint64_t, 5>{header[0], header[1], header[2], header[3], header[4]};
  const uint64_t expected = sizeof kMagic + sizeof header + 4 * symbolCount + 4 * (stateCount + 1) + 4 * stateCount +
                            arcCount + 4 * arcCount + 4 * (readingCount + 1) + rowBytes;
  if (expected != length || symbolCount > 256 || stateCount == 0 || readingCount == 0) {
    file_.close();
    return false;
  }
  symbols_.resize(symbolCount);
  std::memcpy(symbols_.data(), at, 4 * symbolCount);
  at += 4 * symbolCount;
  firstArcs_ = at, at += 4 * (stateCount + 1);
  counts_ = at, at += 4 * stateCount;
  arcSymbols_ = at, at += arcCount;
  arcTargets_ = at, at += 4 * arcCount;
  rowStarts_ = at, at += 4 * (readingCount + 1);
  rows_ = at;
  readingCount_ = static_cast<uint32_t>(readingCount);
  return true;
}

// The tables start at any offset in the file, so they are read byte by byte.
uint32_t LexiconFile::u32(const char* table, size_t index) const {
  uint32_t value;
  std::memcpy(&value, table + index * sizeof value, sizeof value);
  return value;
}

std::optional<uint8_t> LexiconFile::symbolOf(uint32_t codePoint) const {
  auto it = std::lower_bound(symbols_.begin(), symbols_.end(), codePoint);
  if (it == symbols_.end() || *it != codePoint) return std::nullopt;
  return static_cast<uint8_t>(it - symbols_.begin());
}

std::optional<uint32_t> LexiconFile::targetOf(uint32_t state, uint8_t symbol) const {
  for (uint32_t arc = u32(firstArcs_, state), end = u32(firstArcs_, state + 1); arc < end; arc++) {
    if (static_cast<uint8_t>(arcSymbols_[arc]) == symbol) return u32(arcTargets_, arc);
  }
  return std::nullopt;
}

bool LexiconFile::appendLines(const std::string& reading, std::string& lines) const {
  if (!isLoaded()) return false;
  uint32_t state = 0, index = 0;
  for (uint32_t c : CharModel::CodePoints(reading)) {
    std::optional<uint8_t> symbol = symbolOf(c);
    if (!symbol) return false;
    if (u32(counts_, state) & kEndsHere) index++;
    std::optional<uint32_t> next;
    for (uint32_t arc = u32(firstArcs_, state), end = u32(firstArcs_, state + 1); arc < end; arc++) {
      uint32_t target = u32(arcTargets_, arc);
      if (static_cast<uint8_t>(arcSymbols_[arc]) == *symbol) {
        next = target;
        break;
      }
      index += u32(counts_, target) & ~kEndsHere;  // the readings before this one, as they sort first
    }
    if (!next) return false;
    state = *next;
  }
  if (!(u32(counts_, state) & kEndsHere)) return false;
  const char* row = rows_ + u32(rowStarts_, index);
  const char* end = rows_ + u32(rowStarts_, index + 1);
  while (row < end) {
    size_t length = static_cast<uint8_t>(*row++);
    lines.append(reading).append(1, ' ').append(row, length).append(1, ' ');
    row += length;
    float score;
    std::memcpy(&score, row, sizeof score);
    row += sizeof score;
    char number[32];
    std::snprintf(number, sizeof number, "%.9g\n", score);  // nine digits read back as the same float
    lines += number;
  }
  return true;
}

void LexiconFile::collect(uint32_t state, std::string& reading, std::vector<std::string>& readings) const {
  if (u32(counts_, state) & kEndsHere) readings.push_back(reading);
  for (uint32_t arc = u32(firstArcs_, state), end = u32(firstArcs_, state + 1); arc < end; arc++) {
    size_t length = reading.size();
    AppendUtf8(reading, symbols_[static_cast<uint8_t>(arcSymbols_[arc])]);
    collect(u32(arcTargets_, arc), reading, readings);
    reading.resize(length);
  }
}

std::vector<std::string> LexiconFile::readingsStartingWith(const std::string& prefix) const {
  std::vector<std::string> readings;
  if (!isLoaded()) return readings;
  uint32_t state = 0;
  for (uint32_t c : CharModel::CodePoints(prefix)) {
    std::optional<uint8_t> symbol = symbolOf(c);
    std::optional<uint32_t> next = symbol ? targetOf(state, *symbol) : std::nullopt;
    if (!next) return readings;
    state = *next;
  }
  std::string reading = prefix;
  collect(state, reading, readings);
  return readings;
}

}  // namespace ProjectKey
