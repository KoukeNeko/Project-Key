#include "PersonalModel.h"

#include <filesystem>
#include <fstream>
#include <vector>

#include "CharModel.h"

namespace ProjectKey {

namespace {

constexpr int kBits = CharModel::kBits;
constexpr uint64_t kMask = (uint64_t{1} << kBits) - 1;
constexpr char kBoundaryMark = '^';

uint64_t Key(uint64_t a, uint64_t b) { return a << kBits | b; }

// Paths are UTF-8; Windows would read a plain std::string in the ANSI code page.
std::filesystem::path PathOf(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

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

}  // namespace

void PersonalModel::learn(const std::string& text) {
  uint64_t learned = 0;
  std::vector<uint32_t> run;
  auto endRun = [&] {
    if (run.empty()) return;
    run.insert(run.begin(), {CharModel::kBoundary, CharModel::kBoundary});
    run.push_back(CharModel::kBoundary);
    for (size_t k = 2; k < run.size(); k++) add(Key(Key(run[k - 2], run[k - 1]), run[k]), 1);
    learned += run.size() - 2;
    run.clear();
  };
  for (uint32_t c : CharModel::CodePoints(text)) {
    if (CharModel::IsHan(c)) {
      run.push_back(c);
    } else {
      endRun();
    }
  }
  endRun();
  sinceHalving_ += learned;
  if (sinceHalving_ >= kHalfLife) halve();
  if (triples_.size() > kMaxTriples) {
    // shortcut: drops everything typed once, however recent; a typing history this large
    // would want counts that decay with age instead.
    std::erase_if(triples_, [](const auto& entry) { return entry.second <= 1; });
    rebuild();
  }
  version_++;
}

void PersonalModel::halve() {
  for (auto& entry : triples_) entry.second /= 2;
  std::erase_if(triples_, [](const auto& entry) { return entry.second == 0; });
  rebuild();
  sinceHalving_ = 0;
}

void PersonalModel::clear() {
  sinceHalving_ = 0;
  triples_.clear();
  rebuild();
  version_++;
}

void PersonalModel::add(uint64_t triple, uint32_t count) {
  uint64_t a = triple >> (2 * kBits), b = triple >> kBits & kMask, c = triple & kMask;
  triples_[triple] += count;
  pairs_[Key(b, c)] += count;
  contexts_[Key(a, b)] += count;
  singles_[static_cast<uint32_t>(b)] += count;
}

void PersonalModel::rebuild() {
  std::unordered_map<uint64_t, uint32_t> triples;
  triples.swap(triples_);
  pairs_.clear();
  contexts_.clear();
  singles_.clear();
  for (const auto& [triple, count] : triples) add(triple, count);
}

double PersonalModel::blend(double base, uint32_t first, uint32_t second, uint32_t next) const {
  auto count = [](const auto& map, auto key) -> double {
    auto it = map.find(key);
    return it == map.end() ? 0 : it->second;
  };
  double p = base;
  if (double seen = count(singles_, second)) {
    double weight = seen / (seen + kConfidence);
    p = weight * count(pairs_, Key(second, next)) / seen + (1 - weight) * p;
  }
  if (double seen = count(contexts_, Key(first, second))) {
    double weight = seen / (seen + kConfidence);
    p = weight * count(triples_, Key(Key(first, second), next)) / seen + (1 - weight) * p;
  }
  return p;
}

bool PersonalModel::load(const std::string& path) {
  triples_.clear();
  sinceHalving_ = 0;
  std::ifstream in(PathOf(path));
  std::string line;
  while (std::getline(in, line)) {
    size_t tab = line.find('\t');
    if (tab == std::string::npos) continue;
    if (line[0] == '#') {
      sinceHalving_ = std::strtoull(line.c_str() + tab + 1, nullptr, 10);
      continue;
    }
    unsigned long count = std::strtoul(line.c_str(), nullptr, 10);
    std::vector<uint32_t> chars;
    std::string text = line.substr(tab + 1);
    for (uint32_t c : CharModel::CodePoints(text)) chars.push_back(c == static_cast<uint32_t>(kBoundaryMark) ? CharModel::kBoundary : c);
    if (count == 0 || chars.size() != 3) continue;
    triples_[Key(Key(chars[0], chars[1]), chars[2])] += static_cast<uint32_t>(count);
  }
  rebuild();
  version_++;
  return !in.bad();
}

bool PersonalModel::save(const std::string& path) const {
  std::string text = "#\t" + std::to_string(sinceHalving_) + "\n";
  for (const auto& [triple, count] : triples_) {
    text += std::to_string(count);
    text += '\t';
    for (uint64_t c : {triple >> (2 * kBits), triple >> kBits & kMask, triple & kMask}) {
      if (c == CharModel::kBoundary) {
        text += kBoundaryMark;
      } else {
        AppendUtf8(text, static_cast<uint32_t>(c));
      }
    }
    text += '\n';
  }
  std::filesystem::path temporary = PathOf(path + ".tmp");
  std::error_code error;
  std::filesystem::create_directories(temporary.parent_path(), error);
  // What the user typed is for the user only. With the folder closed first, no one else can open
  // the file before it is; on Windows these only clear read-only, and the per-user folder's ACL does it.
  using std::filesystem::perms;
  std::filesystem::permissions(temporary.parent_path(), perms::owner_all, error);
  {
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    std::filesystem::permissions(temporary, perms::owner_read | perms::owner_write, error);
    if (error) return false;
    out << text;
    out.close();  // flushes: a disk that fills up on the last block must not put a cut file in place
    if (!out) return false;
  }
  std::filesystem::rename(temporary, PathOf(path), error);  // replaces the old file, on Windows too
  return !error;
}

}  // namespace ProjectKey
