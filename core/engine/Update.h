// The input method's own check for a newer release (Setting::kCheckForUpdates): each platform
// fetches kLatestRelease at start and every kUpdateCheckInterval, in the background, and its
// input method menu offers what NewerRelease finds; the settings window updates to it.
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace ProjectKey {

inline constexpr char kLatestRelease[] = "https://api.github.com/repos/KoukeNeko/Project-Key/releases/latest";
inline constexpr std::chrono::hours kUpdateCheckInterval{24};

// "0.10.1" as {0, 10, 1}; empty for anything else, such as "0.10.1-beta".
inline std::vector<unsigned long> VersionNumbers(const std::string& version) {
  std::vector<unsigned long> numbers{0};
  for (char c : version) {
    if (c == '.') {
      numbers.push_back(0);
    } else if (c >= '0' && c <= '9') {
      numbers.back() = numbers.back() * 10 + static_cast<unsigned long>(c - '0');
    } else {
      return {};
    }
  }
  return numbers;
}

// The version of GitHub's latest-release JSON ("tag_name": "v0.10.1" gives 0.10.1) when it is newer
// than current, compared number by number; nothing when it is not, or the JSON has no such tag.
inline std::optional<std::string> NewerRelease(const std::string& release, const std::string& current) {
  size_t key = release.find("\"tag_name\"");
  if (key == std::string::npos) return std::nullopt;
  size_t open = release.find('"', release.find(':', key) + 1);
  size_t close = open == std::string::npos ? open : release.find('"', open + 1);
  if (close == std::string::npos) return std::nullopt;
  std::string latest = release.substr(open + 1, close - open - 1);
  if (!latest.empty() && latest[0] == 'v') latest.erase(0, 1);
  std::vector<unsigned long> newer = VersionNumbers(latest), older = VersionNumbers(current);
  if (newer.empty() || older.empty() || newer <= older) return std::nullopt;
  return latest;
}

}  // namespace ProjectKey
