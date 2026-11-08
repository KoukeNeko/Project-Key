// The settings window's groups and rows, shared by the Qt window (Qt.cpp, for
// KDE) and the GTK window (Gtk.cpp, for GNOME and other GTK desktops), and
// matching the macOS and Windows settings. Keys and values are the addon's
// config names (Config in Sources/ProjectKey.cpp), which both windows read and
// write through Fcitx5's controller on D-Bus.
#pragma once

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Models.h"

namespace ProjectKey::SettingsRows {

constexpr char kFcitx[] = "org.fcitx.Fcitx5";
constexpr char kConfig[] = "fcitx://config/addon/projectkey";
constexpr char kReleases[] = "https://api.github.com/repos/KoukeNeko/Project-Key/releases/latest";
constexpr char kAllReleases[] = "https://api.github.com/repos/KoukeNeko/Project-Key/releases?per_page=30";
constexpr char kEngineUrl[] = "https://github.com/openvanilla/McBopomofo";

// Where the models the settings windows download go, as UserData() in Sources/ProjectKey.cpp;
// the engine uses the newest of them and the installed ones (core/engine/Models.h).
inline std::filesystem::path DownloadedModels() {
  if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data) return std::filesystem::path(data) / "projectkey/models";
  if (const char* home = std::getenv("HOME")) return std::filesystem::path(home) / ".local/share/projectkey/models";
  return {};
}

inline std::filesystem::path ModelsInUse() {
  return ProjectKey::ModelsDirectory(PROJECTKEY_DATA_DIR, DownloadedModels());
}

// The 選字模型 row while nothing newer is offered.
inline std::string InstalledModelsText() {
  std::filesystem::path dir = ModelsInUse();
  if (!std::filesystem::exists(dir / "bigram.bin") || !std::filesystem::exists(dir / "chars.bin")) return "未安裝";
  std::string version = ProjectKey::ModelsVersion(dir);
  return version.empty() ? "已安裝" : version;
}

struct ModelsFile {
  std::string name, digest, url;  // digest: sha256:<hex>, as GitHub gives it
  long long size = 0;
};

struct ModelsRelease {
  std::string tag;
  bool draft = false;
  std::vector<ModelsFile> files;
};

struct NewerModels {
  std::string date;  // empty when nothing newer is published
  std::vector<ModelsFile> files;
  long long bytes() const {
    long long total = 0;
    for (const ModelsFile& file : files) total += file.size;
    return total;
  }
};

// The newest models-YYYY-MM-DD release fetch-models.sh published with all three files, when newer
// than the models in use.
inline NewerModels FindNewerModels(const std::vector<ModelsRelease>& releases) {
  NewerModels newest;
  std::string newestTag;
  for (const ModelsRelease& release : releases) {
    auto has = [&](const char* name) {
      return std::any_of(release.files.begin(), release.files.end(), [&](const ModelsFile& file) { return file.name == name; });
    };
    if (release.tag.rfind("models-", 0) != 0 || release.draft || release.tag <= newestTag) continue;
    if (!has("data.txt") || !has("bigram.bin") || !has("chars.bin")) continue;
    newestTag = release.tag;
    newest.files = release.files;
  }
  newest.date = newestTag.empty() ? "" : newestTag.substr(7);
  if (newest.date <= ProjectKey::ModelsVersion(ModelsInUse())) return {};
  return newest;
}

// An empty DownloadedModels()/<date>.partial to download into, after removing any download cut
// short before; empty on failure.
inline std::filesystem::path StartModels(const std::string& date) {
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(DownloadedModels(), error)) {
    if (entry.path().extension() == ".partial") std::filesystem::remove_all(entry.path(), error);
  }
  std::filesystem::path partial = DownloadedModels() / (date + ".partial");
  return std::filesystem::create_directories(partial, error) ? partial : std::filesystem::path();
}

// Completes the download: models-version.txt last, then the folder named after it (Models.h).
inline bool FinishModels(const std::string& date) {
  std::filesystem::path partial = DownloadedModels() / (date + ".partial"), done = DownloadedModels() / date;
  if (!(std::ofstream(partial / "models-version.txt") << date << "\n")) return false;
  std::error_code error;
  std::filesystem::remove_all(done, error);
  std::filesystem::rename(partial, done, error);
  return !error;
}

inline void AbandonModels(const std::string& date) {
  std::error_code error;
  std::filesystem::remove_all(DownloadedModels() / (date + ".partial"), error);
}

// The welcome window (--welcome, which projectkey-link suggests after installing): adds Project
// Key to Fcitx5's input methods, waits for the user to switch to it, and offers a box to try
// it in, as on macOS (WelcomeForm.swift) and Windows (WelcomeWindow.xaml).
constexpr char kInputMethod[] = "projectkey";  // Fcitx5's name for it (projectkey.conf)

// 0 add it, 1 switch to it, 2 try it, 3 done.
inline int WelcomeStep(bool added, bool inUse, bool tried) {
  return !added ? 0 : !tried && !inUse ? 1 : !tried ? 2 : 3;
}

struct Row {
  enum Kind { kToggle, kChoice, kClearChoices } kind;
  const char* key;  // empty for kClearChoices
  const char* title;
  const char* detail = nullptr;              // a line under the row
  std::vector<const char*> values = {};      // kChoice: the config's names for its options
  std::vector<const char*> shown = {};       // kChoice: what the list says, when not the names
};

struct Group {
  const char* title;
  std::vector<Row> rows;
};

inline const std::vector<Group>& Groups() {
  static const std::vector<const char*> proofModes = {"永遠", "有可疑字時", "關閉"};
  static const std::vector<Group> groups = {
      {"選字",
       {
           {Row::kChoice, "CandidatePosition", "選字位置", nullptr, {"游標後", "游標前"}, {"游標後（Windows）", "游標前（macOS）"}},
           {Row::kToggle, "NextDoubtful", "選完跳到下一個可疑處"},
           {Row::kToggle, "Abbreviated", "簡拼", "開啟時 ㄨㄇ 為「我們」，可省略聲調連打；關閉時同一般注音"},
           {Row::kToggle, "KeypadPicks", "數字鍵盤選字", "開啟時數字鍵盤的 1–6 直接選字；主鍵盤的 1–6 仍是注音"},
           {Row::kToggle, "RememberChoices", "記住選過的字"},
           {Row::kClearChoices, "", "選字紀錄"},
       }},
      {"外觀",
       {
           {Row::kChoice, "Appearance", "候選視窗", "模糊需要 KDE、GNOME、niri，或開了輸入法模糊的 Hyprland；沒有時是深色。", {"深色", "模糊"}},
           {Row::kChoice, "ProofPreview", "輸入時的整句預覽", nullptr, proofModes},
           {Row::kChoice, "ChoosingProof", "選字時的整句預覽", nullptr, proofModes},
       }},
      {"按鍵",
       {
           {Row::kToggle, "CapsLockToggle", "Caps Lock 切換中英", "開啟時大寫用 Shift；關閉時 Caps Lock 鎖定大寫"},
           {Row::kToggle, "ShiftToggle", "單按 Shift 切換中英"},
           {Row::kToggle, "EnglishToZhuyin", "Tab 英文轉注音"},
           {Row::kToggle, "ZhuyinToEnglish", "Tab 注音轉英文"},
       }},
  };
  return groups;
}

}  // namespace ProjectKey::SettingsRows
