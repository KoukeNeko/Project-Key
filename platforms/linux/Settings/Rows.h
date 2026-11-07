// The settings window's groups and rows, shared by the Qt window (Qt.cpp, for
// KDE) and the GTK window (Gtk.cpp, for GNOME and other GTK desktops), and
// matching the macOS and Windows settings. Keys and values are the addon's
// config names (Config in Sources/ProjectKey.cpp), which both windows read and
// write through Fcitx5's controller on D-Bus.
#pragma once

#include <vector>

namespace ProjectKey::SettingsRows {

constexpr char kFcitx[] = "org.fcitx.Fcitx5";
constexpr char kConfig[] = "fcitx://config/addon/projectkey";
constexpr char kReleases[] = "https://api.github.com/repos/KoukeNeko/Project-Key/releases/latest";
constexpr char kEngineUrl[] = "https://github.com/openvanilla/McBopomofo";

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
           {Row::kChoice, "Appearance", "候選視窗", "模糊需要 KWin 的模糊特效；沒有時是深色。", {"深色", "模糊"}},
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
