// The candidates as a TSF UI element: an app that draws the input method's UI itself (a full-screen
// game, the touch keyboard) reads them from here, and the candidate window stays hidden. Other apps
// let the candidate window show as usual.
#pragma once

#include <windows.h>
#include <msctf.h>

#include <functional>
#include <optional>

#include "Controller.h"

namespace ProjectKey {

class CandidateList : public ITfCandidateListUIElementBehavior {
 public:
  CandidateList();

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  // ITfUIElement
  STDMETHODIMP GetDescription(BSTR* description) override;
  STDMETHODIMP GetGUID(GUID* guid) override;
  STDMETHODIMP Show(BOOL show) override;
  STDMETHODIMP IsShown(BOOL* shown) override;

  // ITfCandidateListUIElement
  STDMETHODIMP GetUpdatedFlags(DWORD* flags) override;
  STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** document) override;
  STDMETHODIMP GetCount(UINT* count) override;
  STDMETHODIMP GetSelection(UINT* index) override;
  STDMETHODIMP GetString(UINT index, BSTR* text) override;
  STDMETHODIMP GetPageIndex(UINT* starts, UINT size, UINT* pages) override;
  STDMETHODIMP SetPageIndex(UINT*, UINT) override { return E_NOTIMPL; }
  STDMETHODIMP GetCurrentPage(UINT* page) override;

  // ITfCandidateListUIElementBehavior
  STDMETHODIMP SetSelection(UINT index) override;
  STDMETHODIMP Finalize() override;
  STDMETHODIMP Abort() override;

  // The candidates of `content` (none ends the element) for the document of `context`. True when
  // the input method draws them itself; false when the app does.
  bool present(ITfThreadMgr* threadMgr, ITfContext* context, const PanelContent* content);
  void end(ITfThreadMgr* threadMgr);

  // What the app's own UI does: picks PanelContent::rows[row][index], or takes the candidates back.
  std::function<void(int row, int index)> picked;
  std::function<void()> aborted;
  // The app hid or showed the candidate window (Show).
  std::function<void(bool shown)> shownChanged;

 private:
  ~CandidateList();
  // The row and index of the `index`th candidate across the rows.
  std::optional<std::pair<int, int>> at(UINT index) const;

  LONG refs_ = 1;
  PanelContent content_;
  UINT selection_ = 0;
  ITfDocumentMgr* document_ = nullptr;
  DWORD id_ = TF_INVALID_UIELEMENTID;
  bool shown_ = true;
};

}  // namespace ProjectKey
