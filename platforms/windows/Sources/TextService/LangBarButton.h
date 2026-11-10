// The 中／英 button Windows shows in the taskbar next to the keyboard
// indicator: it shows ㄅ or A, a click switches, and its menu opens the settings.
#pragma once

#include <ctfutb.h>
#include <msctf.h>

namespace ProjectKey {

class TextService;

class LangBarButton : public ITfLangBarItemButton, public ITfSource {
 public:
  explicit LangBarButton(TextService* service);

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  // ITfLangBarItem
  STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* info) override;
  STDMETHODIMP GetStatus(DWORD* status) override;
  STDMETHODIMP Show(BOOL) override { return E_NOTIMPL; }
  STDMETHODIMP GetTooltipString(BSTR* tooltip) override;

  // ITfLangBarItemButton
  STDMETHODIMP OnClick(TfLBIClick click, POINT point, const RECT* area) override;
  STDMETHODIMP InitMenu(ITfMenu* menu) override;
  STDMETHODIMP OnMenuSelect(UINT id) override;
  STDMETHODIMP GetIcon(HICON* icon) override;
  STDMETHODIMP GetText(BSTR* text) override;

  // ITfSource
  STDMETHODIMP AdviseSink(REFIID riid, IUnknown* sink, DWORD* cookie) override;
  STDMETHODIMP UnadviseSink(DWORD cookie) override;

  void update(bool englishMode, bool fullWidth);
  void detach() { service_ = nullptr; }

 private:
  ~LangBarButton();
  LONG refs_ = 1;
  TextService* service_;
  ITfLangBarItemSink* sink_ = nullptr;
  bool englishMode_ = false;
  bool fullWidth_ = false;
};

}  // namespace ProjectKey
