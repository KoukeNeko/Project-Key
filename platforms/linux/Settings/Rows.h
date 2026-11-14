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
// install.sh, which also updates whatever it or a package installed. It asks for the sudo
// password, so the settings window copies it for a terminal rather than running it.
constexpr char kInstallCommand[] =
    "curl -fsSL https://github.com/KoukeNeko/Project-Key/releases/latest/download/install.sh | sh";

// $XDG_DATA_HOME/projectkey, which the addon (Sources/ProjectKey.cpp) keeps personal.txt and phrases.txt in too; empty without a home.
inline std::filesystem::path UserData() {
  if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data) return std::filesystem::path(data) / "projectkey";
  if (const char* home = std::getenv("HOME")) return std::filesystem::path(home) / ".local/share/projectkey";
  return {};
}

// Where the models the settings windows download go; the engine uses the newest of them
// and the installed ones (core/engine/Models.h).
inline std::filesystem::path DownloadedModels() {
  std::filesystem::path data = UserData();
  return data.empty() ? data : data / "models";
}

// Where the advanced model (core/engine/AdvancedModel.h) goes, a release per folder as for the
// models; the addon uses the newest (NewestDownload). Nothing is bundled.
inline std::filesystem::path DownloadedAdvanced() {
  std::filesystem::path data = UserData();
  return data.empty() ? data : data / "advanced";
}

// The user's own words, phrases.txt (core/engine Lexicon::loadUserPhrases), or the words never
// offered, exclude-phrases.txt, which the addon reads again on the next composition after a
// save; made first when missing. Empty when it cannot be made.
inline std::filesystem::path MadeUserFile(const std::string& name) {
  std::filesystem::path data = UserData();
  if (data.empty()) return {};
  std::filesystem::path file = data / name;
  std::error_code error;
  if (std::filesystem::exists(file, error)) return file;
  std::filesystem::create_directories(data, error);
  std::ofstream out(file);
  if (name == "phrases.txt") {
    out << "# 每行一個詞：詞、空白、注音\n# 注音的音節以 - 連接，中間不留空白\n# 一聲不標；其他聲調寫在音節後，輕聲寫 ˙（ㄉㄜ˙）\n# 例：小鍵 ㄒㄧㄠˇ-ㄐㄧㄢˋ\n# 存檔後，下一次打字就會用到；# 開頭的行不會讀入\n";
  } else {
    out << "# 每行一個不要出現的詞：詞、空白、注音，格式同自訂詞\n# 選字時按 Delete 也會加到這裡\n# 存檔後，下一次打字就會用到；# 開頭的行不會讀入\n";
  }
  return out.good() ? file : std::filesystem::path();
}

// The folder of the user's files, made first when missing; empty when it cannot be made.
inline std::filesystem::path MadeUserFolder() {
  std::filesystem::path data = UserData();
  std::error_code error;
  if (!data.empty()) std::filesystem::create_directories(data, error);
  return error ? std::filesystem::path() : data;
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

// The newest <prefix>YYYY-MM-DD release with all the files named, when newer than inUse.
inline NewerModels FindNewer(const std::vector<ModelsRelease>& releases, const std::string& prefix,
                             const std::vector<std::string>& required, const std::string& inUse) {
  NewerModels newest;
  std::string newestTag;
  for (const ModelsRelease& release : releases) {
    auto has = [&](const std::string& name) {
      return std::any_of(release.files.begin(), release.files.end(), [&](const ModelsFile& file) { return file.name == name; });
    };
    if (release.tag.rfind(prefix, 0) != 0 || release.draft || release.tag <= newestTag) continue;
    if (!std::all_of(required.begin(), required.end(), has)) continue;
    newestTag = release.tag;
    newest.files = release.files;
  }
  newest.date = newestTag.empty() ? "" : newestTag.substr(prefix.size());
  if (newest.date <= inUse) return {};
  return newest;
}

// The newest models-YYYY-MM-DD release fetch-models.sh published with all three files, when newer
// than the models in use.
inline NewerModels FindNewerModels(const std::vector<ModelsRelease>& releases) {
  return FindNewer(releases, "models-", {"data.txt", "bigram.bin", "chars.bin"}, ProjectKey::ModelsVersion(ModelsInUse()));
}

// The advanced model's version, empty when none is downloaded.
inline std::string AdvancedVersion() {
  std::filesystem::path dir = ProjectKey::NewestDownload(DownloadedAdvanced());
  return dir.empty() ? "" : ProjectKey::ModelsVersion(dir);
}

// The 進階模型 row while nothing newer is offered.
inline std::string InstalledAdvancedText() {
  std::string version = AdvancedVersion();
  return version.empty() ? "未安裝" : version;
}

// The newest advanced-YYYY-MM-DD release, published with its one file (core/engine/AdvancedModel.h),
// when newer than the one downloaded.
constexpr char kAdvancedFile[] = "advanced.gguf";
inline NewerModels FindNewerAdvanced(const std::vector<ModelsRelease>& releases) {
  return FindNewer(releases, "advanced-", {kAdvancedFile}, AdvancedVersion());
}

// 移除's question on the 進階模型 row, as on macOS and Windows.
constexpr char kRemoveAdvancedTitle[] = "移除進階模型？";
constexpr char kRemoveAdvancedBody[] = "打字停頓時，不再用進階模型重選整句。";

// Removes every advanced model downloaded; false when one could not be removed.
inline bool RemoveAdvanced() {
  std::error_code error;
  std::filesystem::remove_all(DownloadedAdvanced(), error);
  return !error;
}

// An empty <folder>/<date>.partial to download into, after removing any download cut short
// before; empty on failure.
inline std::filesystem::path StartModels(const std::string& date, const std::filesystem::path& folder = DownloadedModels()) {
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
    if (entry.path().extension() == ".partial") std::filesystem::remove_all(entry.path(), error);
  }
  std::filesystem::path partial = folder / (date + ".partial");
  return std::filesystem::create_directories(partial, error) ? partial : std::filesystem::path();
}

// Completes the download: models-version.txt last, then the folder named after it (Models.h).
inline bool FinishModels(const std::string& date, const std::filesystem::path& folder = DownloadedModels()) {
  std::filesystem::path partial = folder / (date + ".partial"), done = folder / date;
  if (!(std::ofstream(partial / "models-version.txt") << date << "\n")) return false;
  std::error_code error;
  std::filesystem::remove_all(done, error);
  std::filesystem::rename(partial, done, error);
  return !error;
}

inline void AbandonModels(const std::string& date, const std::filesystem::path& folder = DownloadedModels()) {
  std::error_code error;
  std::filesystem::remove_all(folder / (date + ".partial"), error);
}

// Homebrew's prefix when this program runs from its fcitx5-projectkey keg, which updates with
// `brew upgrade`; empty when Project Key was installed otherwise, which offers kInstallCommand.
inline std::filesystem::path HomebrewPrefix() {
  std::error_code error;
  std::string self = std::filesystem::read_symlink("/proc/self/exe", error).string();
  size_t cellar = self.find("/Cellar/fcitx5-projectkey/");
  if (error || cellar == std::string::npos) return {};
  std::filesystem::path prefix = self.substr(0, cellar);
  return std::filesystem::exists(prefix / "bin/brew", error) ? prefix : std::filesystem::path();
}

inline std::string ShellQuoted(const std::string& text) {
  std::string quoted = "'";
  for (char c : text) quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return quoted + "'";
}

// The update as a shell command: Homebrew builds the newest release from source (a few minutes)
// and projectkey-link links its files where Fcitx5 and GNOME read them. What they print goes to
// ~/.cache/projectkey-update.log, for when it fails.
inline std::string UpdateCommand(const std::filesystem::path& prefix) {
  const char* cache = std::getenv("XDG_CACHE_HOME");
  const char* home = std::getenv("HOME");
  std::filesystem::path log = cache && *cache ? std::filesystem::path(cache) : std::filesystem::path(home ? home : "/tmp") / ".cache";
  return "mkdir -p " + ShellQuoted(log.string()) + " && { " + ShellQuoted((prefix / "bin/brew").string()) +
         " upgrade fcitx5-projectkey && " + ShellQuoted((prefix / "bin/projectkey-link").string()) + "; } > " +
         ShellQuoted((log / "projectkey-update.log").string()) + " 2>&1";
}

// The settings window of the version just installed, to open once the update is done.
inline std::string UpdatedSettings(const std::filesystem::path& prefix) {
  return (prefix / "opt/fcitx5-projectkey/bin/projectkey-settings").string();
}

// The welcome window (--welcome, which projectkey-link suggests after installing): adds Project
// Key to Fcitx5's input methods, waits for the user to switch to it, and offers a box to try
// it in, as on macOS (WelcomeForm.swift) and Windows (WelcomeWindow.xaml).
constexpr char kInputMethod[] = "projectkey";  // Fcitx5's name for it (projectkey.conf)

// 0 add it, 1 switch to it, 2 try it, 3 done.
inline int WelcomeStep(bool added, bool inUse, bool tried) {
  return !added ? 0 : !tried && !inUse ? 1 : !tried ? 2 : 3;
}

// 恢復原廠設定's question, as on macOS (SettingsForm.swift) and Windows (MainWindow.xaml.cs). 恢復設定
// asks the addon for its defaults (ResetSettings on /projectkey); 一併刪除資料 also removes the user's
// files and has the addon forget the choices it holds (ClearChoices). The downloaded models stay.
constexpr char kRestoreTitle[] = "恢復原廠設定？";
constexpr char kRestoreBody[] = "設定將改回預設值。「一併刪除資料」另刪除自訂詞、排除詞與選字紀錄，無法復原；下載的選字模型保留。";

// 一併刪除資料's files: phrases.txt, exclude-phrases.txt and personal.txt. False when one could not be removed.
inline bool RemoveUserData() {
  std::filesystem::path data = UserData();
  if (data.empty()) return true;
  bool removed = true;
  for (const char* name : {"phrases.txt", "exclude-phrases.txt", "personal.txt"}) {
    std::error_code error;
    std::filesystem::remove(data / name, error);  // false, without an error, when it is not there
    if (error) removed = false;
  }
  return removed;
}

struct Row {
  enum Kind { kToggle, kChoice, kClearChoices, kEditPhrases, kShowFolder, kRestore } kind;
  const char* key;  // empty for kClearChoices, kShowFolder and kRestore; the file's name for kEditPhrases
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
           {Row::kChoice, "GridColumns", "每列欄數", nullptr, {"6", "7", "8", "9"}},
           {Row::kToggle, "NextDoubtful", "選完跳到下一個可疑處"},
           {Row::kToggle, "OmitTones", "省略聲調", "開啟時拼完整音節可直接打下一個，ㄕㄉㄜ 為「是的」；關閉時按到已有注音的位置會換掉它，ㄋㄧ 再按 ㄨ 為 ㄋㄨ"},
           {Row::kToggle, "Abbreviated", "簡拼", "開啟時 ㄨㄇ 為「我們」，可省略聲調連打；關閉時同一般注音"},
           {Row::kToggle, "FuzzySounds", "近似音", "開啟時，ㄓ/ㄗ、ㄔ/ㄘ、ㄕ/ㄙ、ㄣ/ㄥ、ㄢ/ㄤ 的字也會列出，排在原音之後"},
           {Row::kToggle, "AssociatedPhrases", "聯想詞", "開啟時，送出後列出常接在後面的詞，如「輸入」之後的「法」，按 Shift+數字鍵選取"},
           {Row::kToggle, "KeypadPicks", "數字鍵盤選字", "開啟時數字鍵盤直接選字；主鍵盤的數字仍是注音"},
           {Row::kToggle, "RememberChoices", "記住選過的字"},
           {Row::kClearChoices, "", "選字紀錄"},
           {Row::kEditPhrases, "phrases.txt", "自訂詞", "每行一個詞和注音，如 小鍵 ㄒㄧㄠˇ-ㄐㄧㄢˋ"},
           {Row::kEditPhrases, "exclude-phrases.txt", "排除詞", "不再出現的詞，格式同自訂詞；選字時按 Delete 也會加入"},
           {Row::kShowFolder, "", "資料夾", "自訂詞、排除詞與學到的字串，複製檔案即可備份"},
       }},
      {"外觀",
       {
           {Row::kChoice, "Appearance", "候選視窗", "模糊需要 KDE、GNOME、niri，或開了輸入法模糊的 Hyprland。", {"自動", "淺色", "深色", "模糊"}},
           {Row::kChoice, "ProofPreview", "輸入時的整句預覽", nullptr, proofModes},
           {Row::kChoice, "ChoosingProof", "選字時的整句預覽", nullptr, proofModes},
       }},
      {"按鍵",
       {
           {Row::kChoice, "KeyboardLayout", "鍵盤排列", nullptr, {"大千", "倚天", "許氏", "倚天 26 鍵", "IBM"}},
           {Row::kToggle, "CapsLockToggle", "Caps Lock 切換中英", "開啟時大寫用 Shift；關閉時 Caps Lock 鎖定大寫"},
           {Row::kToggle, "ShiftToggle", "單按 Shift 切換中英"},
           {Row::kToggle, "FullWidthToggle", "Shift+Space 切換全形"},
           {Row::kToggle, "EnglishToZhuyin", "Tab 英文轉注音"},
           {Row::kToggle, "ZhuyinToEnglish", "Tab 注音轉英文"},
       }},
      {"重設", {{Row::kRestore, "", "恢復原廠設定"}}},
  };
  return groups;
}

}  // namespace ProjectKey::SettingsRows
