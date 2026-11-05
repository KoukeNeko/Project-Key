#include "TextService/DisplayAttributes.h"

#include "Common/Layout.h"
#include "TextService/Globals.h"

namespace ProjectKey {

namespace {

TF_DISPLAYATTRIBUTE AttributeFor(REFGUID guid) {
  TF_DISPLAYATTRIBUTE attribute{};
  attribute.crText.type = TF_CT_NONE;
  attribute.crBk.type = TF_CT_NONE;  // a background would clash with dark documents
  attribute.crLine.type = TF_CT_NONE;
  if (guid == kAttributeActive) {
    uint32_t accent = Layout::kAccent;
    attribute.lsStyle = TF_LS_SOLID;
    attribute.fBoldLine = TRUE;
    attribute.crLine.type = TF_CT_COLORREF;
    attribute.crLine.cr = RGB((accent >> 16) & 0xff, (accent >> 8) & 0xff, accent & 0xff);
    attribute.bAttr = TF_ATTR_TARGET_CONVERTED;
  } else {
    attribute.lsStyle = TF_LS_DOT;
    attribute.bAttr = TF_ATTR_INPUT;
  }
  return attribute;
}

class DisplayAttributeInfo : public ITfDisplayAttributeInfo {
 public:
  explicit DisplayAttributeInfo(const GUID& guid) : guid_(guid) { AddRefDll(); }
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (riid == IID_IUnknown || riid == IID_ITfDisplayAttributeInfo) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
  STDMETHODIMP_(ULONG) Release() override {
    LONG refs = InterlockedDecrement(&refs_);
    if (!refs) delete this;
    return refs;
  }
  STDMETHODIMP GetGUID(GUID* guid) override {
    *guid = guid_;
    return S_OK;
  }
  STDMETHODIMP GetDescription(BSTR* description) override {
    *description = SysAllocString(guid_ == kAttributeActive ? L"Project Key: 選字中" : L"Project Key: 組字中");
    return *description ? S_OK : E_OUTOFMEMORY;
  }
  STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* attribute) override {
    *attribute = AttributeFor(guid_);
    return S_OK;
  }
  STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE*) override { return E_NOTIMPL; }
  STDMETHODIMP Reset() override { return S_OK; }

 private:
  ~DisplayAttributeInfo() { ReleaseDll(); }
  LONG refs_ = 1;
  GUID guid_;
};

class DisplayAttributeEnum : public IEnumTfDisplayAttributeInfo {
 public:
  explicit DisplayAttributeEnum(ULONG index = 0) : index_(index) { AddRefDll(); }
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (riid == IID_IUnknown || riid == IID_IEnumTfDisplayAttributeInfo) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
  STDMETHODIMP_(ULONG) Release() override {
    LONG refs = InterlockedDecrement(&refs_);
    if (!refs) delete this;
    return refs;
  }
  STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** result) override {
    *result = new DisplayAttributeEnum(index_);
    return S_OK;
  }
  STDMETHODIMP Next(ULONG count, ITfDisplayAttributeInfo** infos, ULONG* fetched) override {
    static const GUID* const kGuids[] = {&kAttributeInput, &kAttributeActive};
    ULONG n = 0;
    for (; n < count && index_ < 2; n++, index_++) infos[n] = new DisplayAttributeInfo(*kGuids[index_]);
    if (fetched) *fetched = n;
    return n == count ? S_OK : S_FALSE;
  }
  STDMETHODIMP Reset() override {
    index_ = 0;
    return S_OK;
  }
  STDMETHODIMP Skip(ULONG count) override {
    index_ += count;
    return index_ <= 2 ? S_OK : S_FALSE;
  }

 private:
  ~DisplayAttributeEnum() { ReleaseDll(); }
  LONG refs_ = 1;
  ULONG index_;
};

}  // namespace

HRESULT CreateDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** result) {
  if (!result) return E_INVALIDARG;
  *result = nullptr;
  if (guid != kAttributeInput && guid != kAttributeActive) return E_INVALIDARG;
  *result = new DisplayAttributeInfo(guid);
  return S_OK;
}

HRESULT CreateDisplayAttributeEnum(IEnumTfDisplayAttributeInfo** result) {
  if (!result) return E_INVALIDARG;
  *result = new DisplayAttributeEnum();
  return S_OK;
}

}  // namespace ProjectKey
