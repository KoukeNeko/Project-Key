// The TSF text input processor: forwards keys to the engine server, applies
// what it answers to the document, and shows the candidate window. One
// instance per UI thread that activates the input method.
#pragma once

#include <windows.h>
#include <msctf.h>
#include <ctffunc.h>

#include <functional>
#include <memory>
#include <optional>

#include "Common/Protocol.h"
#include "TextService/CandidateWindow.h"
#include "TextService/EngineClient.h"

namespace ProjectKey {

class LangBarButton;

class TextService : public ITfTextInputProcessorEx,
                    public ITfThreadMgrEventSink,
                    public ITfThreadFocusSink,
                    public ITfKeyEventSink,
                    public ITfCompositionSink,
                    public ITfDisplayAttributeProvider,
                    public ITfFnConfigure,
                    public ITfTextLayoutSink {
 public:
  TextService();

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  // ITfTextInputProcessor(Ex)
  STDMETHODIMP Activate(ITfThreadMgr* threadMgr, TfClientId clientId) override;
  STDMETHODIMP ActivateEx(ITfThreadMgr* threadMgr, TfClientId clientId, DWORD flags) override;
  STDMETHODIMP Deactivate() override;

  // ITfThreadMgrEventSink
  STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
  STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
  STDMETHODIMP OnSetFocus(ITfDocumentMgr* focus, ITfDocumentMgr* previous) override;
  STDMETHODIMP OnPushContext(ITfContext*) override { return S_OK; }
  STDMETHODIMP OnPopContext(ITfContext*) override { return S_OK; }

  // ITfThreadFocusSink
  STDMETHODIMP OnSetThreadFocus() override { return S_OK; }
  STDMETHODIMP OnKillThreadFocus() override;

  // ITfKeyEventSink
  STDMETHODIMP OnSetFocus(BOOL) override { return S_OK; }
  STDMETHODIMP OnTestKeyDown(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
  STDMETHODIMP OnKeyDown(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
  STDMETHODIMP OnTestKeyUp(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
  STDMETHODIMP OnKeyUp(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
  STDMETHODIMP OnPreservedKey(ITfContext*, REFGUID, BOOL* eaten) override {
    *eaten = FALSE;
    return S_OK;
  }

  // ITfCompositionSink
  STDMETHODIMP OnCompositionTerminated(TfEditCookie cookie, ITfComposition* composition) override;

  // ITfDisplayAttributeProvider
  STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** result) override;
  STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** result) override;

  // ITfFnConfigure: the Options button in Windows Settings.
  STDMETHODIMP GetDisplayName(BSTR* name) override;
  STDMETHODIMP Show(HWND parent, LANGID langid, REFGUID profile) override;

  // ITfTextLayoutSink: apps like Chromium report positions only once laid out.
  STDMETHODIMP OnLayoutChange(ITfContext* context, TfLayoutCode code, ITfContextView* view) override;

  // From the language bar button.
  void toggleMode();
  bool englishMode() const { return englishMode_; }
  // Asks the server to open the settings window: an app in a sandbox could
  // only start it inside the sandbox.
  void showSettings();

 private:
  ~TextService();

  // Sends a request and applies the reply to `context`; nullopt without a server.
  std::optional<Protocol::Reply> request(ITfContext* context, const Protocol::Message& message);
  void apply(ITfContext* context, const Protocol::Reply& reply);
  // Runs `body` in an edit session on `context`, synchronously when the app allows it, else later unless syncOnly.
  HRESULT editSession(ITfContext* context, DWORD flags, std::function<void(TfEditCookie)> body, bool syncOnly = false);
  void insertText(TfEditCookie cookie, ITfContext* context, const std::wstring& text);
  void setComposition(TfEditCookie cookie, ITfContext* context, const Composition& composition);
  void endComposition(TfEditCookie cookie);
  void setAttribute(TfEditCookie cookie, ITfContext* context, ITfRange* range, TfGuidAtom atom);
  RECT anchorRect(TfEditCookie cookie, ITfContext* context, HWND* window);
  std::string textBeforeCaret(ITfContext* context);
  bool keyboardDisabled(ITfContext* context);
  std::optional<KeyEvent> keyEvent(WPARAM wParam, LPARAM lParam) const;
  void keyDown(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten, bool testing);
  void keyUp(ITfContext* context, WPARAM wParam);
  void updateLangBar();

  LONG refs_ = 1;
  ITfThreadMgr* threadMgr_ = nullptr;
  TfClientId clientId_ = TF_CLIENTID_NULL;
  DWORD threadMgrCookie_ = TF_INVALID_COOKIE;
  DWORD threadFocusCookie_ = TF_INVALID_COOKIE;
  ITfComposition* composition_ = nullptr;
  ITfContext* compositionContext_ = nullptr;
  TfGuidAtom inputAtom_ = TF_INVALID_GUIDATOM, activeAtom_ = TF_INVALID_GUIDATOM;
  EngineClient engine_;
  ModeBadge badge_;
  CandidateWindow candidates_{badge_};
  LangBarButton* langBar_ = nullptr;
  bool englishMode_ = false;
  int appearance_ = 0;
  // OnTestKeyDown already sent this key; OnKeyDown applies the reply.
  std::optional<Protocol::Reply> pending_;
  WPARAM pendingKey_ = 0;
  // The document whose layout changes move the windows.
  void watchLayout(ITfContext* context);
  ITfContext* layoutContext_ = nullptr;
  DWORD layoutCookie_ = TF_INVALID_COOKIE;
  // The last real position the windows were placed at, and the document window it was in.
  RECT lastAnchor_{};
  HWND lastAnchorWindow_ = nullptr;
  // The keys the test calls handled, so OnKeyDown and OnKeyUp skip them.
  WPARAM testedDown_ = 0, testedUp_ = 0;
};

}  // namespace ProjectKey
