#include "Common/Win.h"

#include <sddl.h>

#include <memory>

namespace ProjectKey {

std::wstring UserSid() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return L"unknown";
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  auto buffer = std::make_unique<BYTE[]>(size);
  std::wstring result = L"unknown";
  if (GetTokenInformation(token, TokenUser, buffer.get(), size, &size)) {
    LPWSTR sid = nullptr;
    if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.get())->User.Sid, &sid)) {
      result = sid;
      LocalFree(sid);
    }
  }
  CloseHandle(token);
  return result;
}

std::wstring ModuleDirectory(HMODULE module) {
  wchar_t path[MAX_PATH * 2];
  DWORD n = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
  std::wstring dir(path, n);
  return dir.substr(0, dir.find_last_of(L'\\') + 1);
}

}  // namespace ProjectKey
