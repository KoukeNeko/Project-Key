#include "TextService/CandidateList.h"

#include <algorithm>

#include "Common/Win.h"
#include "TextService/Globals.h"

namespace ProjectKey {

namespace {

// {6C4D6E3F-7B0A-4E59-9A2E-3F1B8D5C2A71}: this element, for GetGUID.
constexpr GUID kCandidateListGuid = {0x6c4d6e3f, 0x7b0a, 0x4e59, {0x9a, 0x2e, 0x3f, 0x1b, 0x8d, 0x5c, 0x2a, 0x71}};

UINT CountOf(const PanelContent& content) {
  UINT count = 0;
  for (const auto& row : content.rows) count += static_cast<UINT>(row.size());
  return count;
}

}  // namespace

CandidateList::CandidateList() {
  AddRefDll();
}

CandidateList::~CandidateList() {
  SafeRelease(document_);
  ReleaseDll();
}

STDMETHODIMP CandidateList::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_INVALIDARG;
  if (riid == IID_IUnknown || riid == IID_ITfUIElement || riid == IID_ITfCandidateListUIElement ||
      riid == IID_ITfCandidateListUIElementBehavior) {
    *ppv = static_cast<ITfCandidateListUIElementBehavior*>(this);
    AddRef();
    return S_OK;
  }
  *ppv = nullptr;
  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CandidateList::AddRef() {
  return InterlockedIncrement(&refs_);
}

STDMETHODIMP_(ULONG) CandidateList::Release() {
  LONG refs = InterlockedDecrement(&refs_);
  if (refs == 0) delete this;
  return refs;
}

STDMETHODIMP CandidateList::GetDescription(BSTR* description) {
  if (!description) return E_INVALIDARG;
  *description = SysAllocString(L"Project Key 候選字");
  return *description ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CandidateList::GetGUID(GUID* guid) {
  if (!guid) return E_INVALIDARG;
  *guid = kCandidateListGuid;
  return S_OK;
}

STDMETHODIMP CandidateList::Show(BOOL show) {
  shown_ = show != FALSE;
  if (shownChanged) shownChanged(shown_);
  return S_OK;
}

STDMETHODIMP CandidateList::IsShown(BOOL* shown) {
  if (!shown) return E_INVALIDARG;
  *shown = shown_;
  return S_OK;
}

STDMETHODIMP CandidateList::GetUpdatedFlags(DWORD* flags) {
  if (!flags) return E_INVALIDARG;
  // Each update may change everything: a new row, page or selection.
  *flags = TF_CLUIE_DOCUMENTMGR | TF_CLUIE_COUNT | TF_CLUIE_SELECTION | TF_CLUIE_STRING | TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;
  return S_OK;
}

STDMETHODIMP CandidateList::GetDocumentMgr(ITfDocumentMgr** document) {
  if (!document) return E_INVALIDARG;
  *document = document_;
  if (!document_) return E_FAIL;
  document_->AddRef();
  return S_OK;
}

STDMETHODIMP CandidateList::GetCount(UINT* count) {
  if (!count) return E_INVALIDARG;
  *count = CountOf(content_);
  return S_OK;
}

STDMETHODIMP CandidateList::GetSelection(UINT* index) {
  if (!index) return E_INVALIDARG;
  *index = selection_;
  return S_OK;
}

STDMETHODIMP CandidateList::GetString(UINT index, BSTR* text) {
  if (!text) return E_INVALIDARG;
  std::optional<std::pair<int, int>> place = at(index);
  if (!place) return E_INVALIDARG;
  *text = SysAllocString(Widen(content_.rows[place->first][place->second]).c_str());
  return *text ? S_OK : E_OUTOFMEMORY;
}

// A page is a row of the grid, as the candidate window shows them.
STDMETHODIMP CandidateList::GetPageIndex(UINT* starts, UINT size, UINT* pages) {
  if (!pages) return E_INVALIDARG;
  *pages = static_cast<UINT>(content_.rows.size());
  if (!starts) return S_OK;
  if (size < *pages) return E_INVALIDARG;
  UINT start = 0;
  for (size_t row = 0; row < content_.rows.size(); row++) {
    starts[row] = start;
    start += static_cast<UINT>(content_.rows[row].size());
  }
  return S_OK;
}

STDMETHODIMP CandidateList::GetCurrentPage(UINT* page) {
  if (!page) return E_INVALIDARG;
  *page = static_cast<UINT>(std::max(0, content_.selectedRow));
  return S_OK;
}

STDMETHODIMP CandidateList::SetSelection(UINT index) {
  if (!at(index)) return E_INVALIDARG;
  selection_ = index;
  return S_OK;
}

STDMETHODIMP CandidateList::Finalize() {
  std::optional<std::pair<int, int>> place = at(selection_);
  if (place && picked) picked(place->first, place->second);
  return S_OK;
}

STDMETHODIMP CandidateList::Abort() {
  if (aborted) aborted();
  return S_OK;
}

std::optional<std::pair<int, int>> CandidateList::at(UINT index) const {
  for (size_t row = 0; row < content_.rows.size(); row++) {
    if (index < content_.rows[row].size()) return std::pair(static_cast<int>(row), static_cast<int>(index));
    index -= static_cast<UINT>(content_.rows[row].size());
  }
  return std::nullopt;
}

bool CandidateList::present(ITfThreadMgr* threadMgr, ITfContext* context, const PanelContent* content) {
  if (!content || content->rows.empty()) {
    end(threadMgr);
    return true;  // the proof row alone, or nothing: the input method's own window
  }
  content_ = *content;
  // The selected row's selection; while typing (no selected row), the guess in the first row.
  UINT selection = 0;
  int row = std::max(0, content_.selectedRow);
  for (int i = 0; i < row && i < static_cast<int>(content_.rows.size()); i++) selection += static_cast<UINT>(content_.rows[i].size());
  selection_ = at(selection + std::max(0, content_.selection)) ? selection + std::max(0, content_.selection) : 0;
  ITfDocumentMgr* document = nullptr;
  if (context) context->GetDocumentMgr(&document);
  SafeRelease(document_);
  document_ = document;
  ITfUIElementMgr* elements = nullptr;
  if (!threadMgr || FAILED(threadMgr->QueryInterface(IID_ITfUIElementMgr, reinterpret_cast<void**>(&elements)))) return true;
  if (id_ == TF_INVALID_UIELEMENTID) {
    BOOL show = TRUE;
    if (FAILED(elements->BeginUIElement(this, &show, &id_))) id_ = TF_INVALID_UIELEMENTID;
    shown_ = show != FALSE;
  } else {
    elements->UpdateUIElement(id_);
  }
  elements->Release();
  return shown_;
}

void CandidateList::end(ITfThreadMgr* threadMgr) {
  if (id_ == TF_INVALID_UIELEMENTID) return;
  ITfUIElementMgr* elements = nullptr;
  if (threadMgr && SUCCEEDED(threadMgr->QueryInterface(IID_ITfUIElementMgr, reinterpret_cast<void**>(&elements)))) {
    elements->EndUIElement(id_);
    elements->Release();
  }
  id_ = TF_INVALID_UIELEMENTID;
  shown_ = true;
  content_ = {};
  SafeRelease(document_);
}

}  // namespace ProjectKey
