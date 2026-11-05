// Small Windows helpers shared by the server, the text service and the tools.
#pragma once

#include <windows.h>

#include <string>

namespace ProjectKey {

inline std::wstring Widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

inline std::string Narrow(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

// The user's SID as a string, from the process token; also correct inside an
// app container, whose token keeps the user.
std::wstring UserSid();

// One engine server per user: \\.\pipe\ProjectKey-<SID>.
inline std::wstring PipeName() {
  return L"\\\\.\\pipe\\ProjectKey-" + UserSid();
}

// The folder a module (or the executable, for nullptr) was loaded from, with a trailing backslash.
std::wstring ModuleDirectory(HMODULE module);

// Where settings live: HKEY_CURRENT_USER\Software\ProjectKey.
constexpr const wchar_t* kSettingsKey = L"Software\\ProjectKey";

constexpr const wchar_t* kServerExe = L"ProjectKeyServer.exe";
constexpr const wchar_t* kSettingsExe = L"Settings\\ProjectKeySettings.exe";

// Starts the settings window installed in `directory` (with a trailing backslash).
bool LaunchSettings(const std::wstring& directory);

}  // namespace ProjectKey
