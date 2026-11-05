#include "TextService/LangBarButton.h"

#include <d2d1.h>
#include <olectl.h>
#include <dwrite.h>
#include <wrl/client.h>

#include "Common/Layout.h"
#include "TextService/Globals.h"
#include "TextService/TextService.h"

using Microsoft::WRL::ComPtr;

namespace ProjectKey {

namespace {

constexpr DWORD kSinkCookie = 1;
// The taskbar's input mode item; the SDK documents it but declares it in no header.
constexpr GUID kInputModeItem = {0x2c77a81e, 0x41cc, 0x4178, {0xa3, 0xa7, 0x5f, 0x8a, 0x98, 0x75, 0x68, 0xe6}};
constexpr UINT kMenuSettings = 1;

// Whether the taskbar is light, where the icon has to be dark.
bool LightTaskbar() {
  DWORD light = 0, size = sizeof(light);
  RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"SystemUsesLightTheme",
               RRF_RT_REG_DWORD, nullptr, &light, &size);
  return light != 0;
}

// The label drawn as a small-icon-sized HICON.
HICON MakeIcon(const wchar_t* label) {
  int side = GetSystemMetrics(SM_CXSMICON);
  BITMAPINFO info{};
  info.bmiHeader = {sizeof(BITMAPINFOHEADER), side, -side, 1, 32, BI_RGB};
  void* bits = nullptr;
  HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  HDC dc = CreateCompatibleDC(nullptr);
  HGDIOBJ old = SelectObject(dc, color);
  ComPtr<ID2D1Factory> factory;
  ComPtr<ID2D1DCRenderTarget> target;
  ComPtr<IDWriteFactory> dwrite;
  ComPtr<IDWriteTextFormat> format;
  D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
  RECT bounds{0, 0, side, side};
  if (SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf())) &&
      SUCCEEDED(factory->CreateDCRenderTarget(&properties, &target)) && SUCCEEDED(target->BindDC(dc, &bounds)) &&
      SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()))) &&
      SUCCEEDED(dwrite->CreateTextFormat(Layout::kTextFont, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                         DWRITE_FONT_STRETCH_NORMAL, side * 0.8f, L"zh-TW", &format))) {
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    ComPtr<ID2D1SolidColorBrush> brush;
    float level = LightTaskbar() ? 0.f : 1.f;
    target->CreateSolidColorBrush(D2D1::ColorF(level, level, level, 1), &brush);
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    target->BeginDraw();
    target->Clear(D2D1::ColorF(0, 0, 0, 0));
    target->DrawTextW(label, static_cast<UINT32>(wcslen(label)), format.Get(), D2D1::RectF(0, 0, static_cast<float>(side), static_cast<float>(side)),
                      brush.Get());
    target->EndDraw();
  }
  SelectObject(dc, old);
  DeleteDC(dc);
  HBITMAP mask = CreateBitmap(side, side, 1, 1, nullptr);
  ICONINFO iconInfo{TRUE, 0, 0, mask, color};
  HICON icon = CreateIconIndirect(&iconInfo);
  DeleteObject(mask);
  DeleteObject(color);
  return icon;
}

}  // namespace

LangBarButton::LangBarButton(TextService* service) : service_(service) {
  AddRefDll();
}

LangBarButton::~LangBarButton() {
  if (sink_) sink_->Release();
  ReleaseDll();
}

STDMETHODIMP LangBarButton::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_INVALIDARG;
  if (riid == IID_IUnknown || riid == IID_ITfLangBarItem || riid == IID_ITfLangBarItemButton) {
    *ppv = static_cast<ITfLangBarItemButton*>(this);
  } else if (riid == IID_ITfSource) {
    *ppv = static_cast<ITfSource*>(this);
  } else {
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  AddRef();
  return S_OK;
}

STDMETHODIMP_(ULONG) LangBarButton::AddRef() {
  return InterlockedIncrement(&refs_);
}

STDMETHODIMP_(ULONG) LangBarButton::Release() {
  LONG refs = InterlockedDecrement(&refs_);
  if (!refs) delete this;
  return refs;
}

STDMETHODIMP LangBarButton::GetInfo(TF_LANGBARITEMINFO* info) {
  info->clsidService = kClsid;
  info->guidItem = kInputModeItem;
  info->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
  info->ulSort = 0;
  wcscpy_s(info->szDescription, L"中／英");
  return S_OK;
}

STDMETHODIMP LangBarButton::GetStatus(DWORD* status) {
  *status = 0;
  return S_OK;
}

STDMETHODIMP LangBarButton::GetTooltipString(BSTR* tooltip) {
  *tooltip = SysAllocString(englishMode_ ? L"英文（按 Shift 切換）" : L"中文（按 Shift 切換）");
  return *tooltip ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LangBarButton::OnClick(TfLBIClick click, POINT point, const RECT*) {
  if (click == TF_LBI_CLK_LEFT) {
    if (service_) service_->toggleMode();
    return S_OK;
  }
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, kMenuSettings, L"設定…");
  HWND owner = GetForegroundWindow();
  UINT chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, point.x, point.y, 0, owner, nullptr);
  DestroyMenu(menu);
  if (chosen) OnMenuSelect(chosen);
  return S_OK;
}

STDMETHODIMP LangBarButton::InitMenu(ITfMenu* menu) {
  menu->AddMenuItem(kMenuSettings, 0, nullptr, nullptr, L"設定…", 4, nullptr);
  return S_OK;
}

STDMETHODIMP LangBarButton::OnMenuSelect(UINT id) {
  if (id == kMenuSettings) TextService::ShowSettings();
  return S_OK;
}

STDMETHODIMP LangBarButton::GetIcon(HICON* icon) {
  *icon = MakeIcon(englishMode_ ? L"A" : L"ㄅ");
  return *icon ? S_OK : E_FAIL;
}

STDMETHODIMP LangBarButton::GetText(BSTR* text) {
  *text = SysAllocString(englishMode_ ? L"英" : L"中");
  return *text ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LangBarButton::AdviseSink(REFIID riid, IUnknown* sink, DWORD* cookie) {
  if (riid != IID_ITfLangBarItemSink) return CONNECT_E_CANNOTCONNECT;
  if (sink_) return CONNECT_E_ADVISELIMIT;
  if (FAILED(sink->QueryInterface(IID_ITfLangBarItemSink, reinterpret_cast<void**>(&sink_)))) {
    sink_ = nullptr;
    return E_NOINTERFACE;
  }
  *cookie = kSinkCookie;
  return S_OK;
}

STDMETHODIMP LangBarButton::UnadviseSink(DWORD cookie) {
  if (cookie != kSinkCookie || !sink_) return CONNECT_E_NOCONNECTION;
  sink_->Release();
  sink_ = nullptr;
  return S_OK;
}

void LangBarButton::update(bool englishMode) {
  englishMode_ = englishMode;
  if (sink_) sink_->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP);
}

}  // namespace ProjectKey
