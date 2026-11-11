// DLL entry points: the class factory TSF uses to create the text service, and
// registration (regsvr32) as a Traditional Chinese keyboard input method.
#include <windows.h>
#include <msctf.h>
#include <olectl.h>

#include <string>

#include "Common/Win.h"
#include "TextService/Globals.h"
#include "TextService/TextService.h"

namespace ProjectKey {

HINSTANCE gInstance = nullptr;
namespace {
LONG gDllRefs = 0;
}

void AddRefDll() {
  InterlockedIncrement(&gDllRefs);
}

void ReleaseDll() {
  InterlockedDecrement(&gDllRefs);
}

namespace {

class ClassFactory : public IClassFactory {
 public:
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override {
    AddRefDll();
    return 2;
  }
  STDMETHODIMP_(ULONG) Release() override {
    ReleaseDll();
    return 1;
  }
  STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (outer) return CLASS_E_NOAGGREGATION;
    auto* service = new (std::nothrow) TextService();
    if (!service) return E_OUTOFMEMORY;
    HRESULT hr = service->QueryInterface(riid, ppv);
    service->Release();
    return hr;
  }
  STDMETHODIMP LockServer(BOOL lock) override {
    lock ? AddRefDll() : ReleaseDll();
    return S_OK;
  }
};

ClassFactory gFactory;

std::wstring GuidString(REFGUID guid) {
  wchar_t text[64];
  StringFromGUID2(guid, text, 64);
  return text;
}

// Capabilities registered with TSF.
const GUID* const kCategories[] = {
    &GUID_TFCAT_TIP_KEYBOARD,
    &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
    &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,  // Store apps and the Start menu
    &GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
    &GUID_TFCAT_TIPCAP_UIELEMENTENABLED,  // CandidateList: apps that draw the candidates themselves
};

HRESULT RegisterServer() {
  std::wstring key = L"Software\\Classes\\CLSID\\" + GuidString(kClsid);
  wchar_t module[MAX_PATH * 2];
  DWORD n = GetModuleFileNameW(gInstance, module, static_cast<DWORD>(std::size(module)));
  std::wstring dll(module, n);
  auto set = [](const std::wstring& subkey, const wchar_t* name, const std::wstring& value) {
    return RegSetKeyValueW(HKEY_LOCAL_MACHINE, subkey.c_str(), name, REG_SZ, value.c_str(),
                           static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
  };
  if (set(key, nullptr, kDisplayName) != ERROR_SUCCESS) return SELFREG_E_CLASS;
  set(key + L"\\InprocServer32", nullptr, dll);
  set(key + L"\\InprocServer32", L"ThreadingModel", L"Apartment");

  ITfInputProcessorProfileMgr* profiles = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr,
                                reinterpret_cast<void**>(&profiles));
  if (FAILED(hr)) return hr;
  hr = profiles->RegisterProfile(kClsid, kLangId, kProfile, kDisplayName, static_cast<ULONG>(wcslen(kDisplayName)), dll.c_str(),
                                 static_cast<ULONG>(dll.size()), 0, nullptr, 0, TRUE, 0);
  profiles->Release();
  if (FAILED(hr)) return hr;

  ITfCategoryMgr* categories = nullptr;
  hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, reinterpret_cast<void**>(&categories));
  if (FAILED(hr)) return hr;
  for (const GUID* category : kCategories) {
    hr = categories->RegisterCategory(kClsid, *category, kClsid);
    if (FAILED(hr)) break;
  }
  categories->Release();
  return hr;
}

HRESULT UnregisterServer() {
  ITfCategoryMgr* categories = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                 reinterpret_cast<void**>(&categories)))) {
    for (const GUID* category : kCategories) categories->UnregisterCategory(kClsid, *category, kClsid);
    categories->Release();
  }
  ITfInputProcessorProfileMgr* profiles = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr,
                                 reinterpret_cast<void**>(&profiles)))) {
    profiles->UnregisterProfile(kClsid, kLangId, kProfile, 0);
    profiles->Release();
  }
  RegDeleteTreeW(HKEY_LOCAL_MACHINE, (L"Software\\Classes\\CLSID\\" + GuidString(kClsid)).c_str());
  return S_OK;
}

}  // namespace

}  // namespace ProjectKey

using namespace ProjectKey;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    gInstance = instance;
    DisableThreadLibraryCalls(instance);
  }
  return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
  if (!ppv) return E_INVALIDARG;
  *ppv = nullptr;
  if (clsid != kClsid) return CLASS_E_CLASSNOTAVAILABLE;
  return gFactory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow() {
  return gDllRefs ? S_FALSE : S_OK;
}

STDAPI DllRegisterServer() {
  HRESULT hr = RegisterServer();
  if (FAILED(hr)) UnregisterServer();
  return hr;
}

STDAPI DllUnregisterServer() {
  return UnregisterServer();
}
