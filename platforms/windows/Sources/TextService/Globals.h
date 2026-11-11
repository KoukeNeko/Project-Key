// Identity of the text service and the DLL's shared state.
#pragma once

#include <windows.h>
#include <msctf.h>

namespace ProjectKey {

// {A11BBC41-85CF-449F-96B0-F1C5DBDB2213}
constexpr CLSID kClsid = {0xa11bbc41, 0x85cf, 0x449f, {0x96, 0xb0, 0xf1, 0xc5, 0xdb, 0xdb, 0x22, 0x13}};
// {570A6239-8C24-43D1-A2E9-C7C45C7512DA}: the Traditional Chinese profile.
constexpr GUID kProfile = {0x570a6239, 0x8c24, 0x43d1, {0xa2, 0xe9, 0xc7, 0xc4, 0x5c, 0x75, 0x12, 0xda}};
// {BB1095E2-5616-4CE1-9A39-718FCD4ED7BA}: the composition's underline.
constexpr GUID kAttributeInput = {0xbb1095e2, 0x5616, 0x4ce1, {0x9a, 0x39, 0x71, 0x8f, 0xcd, 0x4e, 0xd7, 0xba}};
// {FF6F1085-C855-4DFD-A945-AE5DD572FEDC}: the segment being chosen.
constexpr GUID kAttributeActive = {0xff6f1085, 0xc855, 0x4dfd, {0xa9, 0x45, 0xae, 0x5d, 0xd5, 0x72, 0xfe, 0xdc}};

constexpr LANGID kLangId = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL);  // zh-TW
constexpr const wchar_t* kDisplayName = L"Project Key";

extern HINSTANCE gInstance;

// Objects alive in the DLL, for DllCanUnloadNow.
void AddRefDll();
void ReleaseDll();

template <typename T>
void SafeRelease(T*& p) {
  if (p) p->Release();
  p = nullptr;
}

}  // namespace ProjectKey
