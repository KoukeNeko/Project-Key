#include "TextService/TextService.h"

#include <ctffunc.h>

#include "Common/Win.h"
#include "TextService/DisplayAttributes.h"
#include "TextService/Globals.h"
#include "TextService/LangBarButton.h"

namespace ProjectKey {

namespace {

// An edit session that runs a function; it keeps the text service alive until it ran.
class EditSession : public ITfEditSession {
 public:
  EditSession(IUnknown* owner, std::function<void(TfEditCookie)> body) : owner_(owner), body_(std::move(body)) {
    owner_->AddRef();
  }
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (riid == IID_IUnknown || riid == IID_ITfEditSession) {
      *ppv = static_cast<ITfEditSession*>(this);
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
  STDMETHODIMP DoEditSession(TfEditCookie cookie) override {
    body_(cookie);
    return S_OK;
  }

 private:
  ~EditSession() { owner_->Release(); }
  LONG refs_ = 1;
  IUnknown* owner_;
  std::function<void(TfEditCookie)> body_;
};

void SetSelection(TfEditCookie cookie, ITfContext* context, ITfRange* range) {
  TF_SELECTION selection{};
  selection.range = range;
  selection.style.ase = TF_AE_NONE;
  selection.style.fInterimChar = FALSE;
  context->SetSelection(cookie, 1, &selection);
}

bool IsDown(int key) {
  return (GetKeyState(key) & 0x8000) != 0;
}

// What a key types in a US layout, which every Zhuyin layout is drawn on: letters, digits
// and punctuation with Shift; Caps Lock capitalizes letters.
std::string UsCharacter(WPARAM vk, bool shift, bool capsLock) {
  if (vk >= 'A' && vk <= 'Z') {
    char c = static_cast<char>(vk);
    return std::string(1, shift != capsLock ? c : static_cast<char>(c - 'A' + 'a'));
  }
  if (vk >= '0' && vk <= '9') return std::string(1, shift ? ")!@#$%^&*("[vk - '0'] : static_cast<char>(vk));
  if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return std::string(1, static_cast<char>('0' + vk - VK_NUMPAD0));
  struct Pair {
    WPARAM vk;
    char plain, shifted;
  };
  static constexpr Pair kKeys[] = {
      {VK_OEM_1, ';', ':'},     {VK_OEM_PLUS, '=', '+'}, {VK_OEM_COMMA, ',', '<'}, {VK_OEM_MINUS, '-', '_'},
      {VK_OEM_PERIOD, '.', '>'}, {VK_OEM_2, '/', '?'},    {VK_OEM_3, '`', '~'},     {VK_OEM_4, '[', '{'},
      {VK_OEM_5, '\\', '|'},    {VK_OEM_6, ']', '}'},    {VK_OEM_7, '\'', '"'},    {VK_MULTIPLY, '*', '*'},
      {VK_ADD, '+', '+'},       {VK_SUBTRACT, '-', '-'}, {VK_DECIMAL, '.', '.'},   {VK_DIVIDE, '/', '/'},
  };
  for (const Pair& key : kKeys) {
    if (key.vk == vk) return std::string(1, shift ? key.shifted : key.plain);
  }
  return {};
}

}  // namespace

TextService::TextService() {
  AddRefDll();
}

TextService::~TextService() {
  ReleaseDll();
}

STDMETHODIMP TextService::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_INVALIDARG;
  if (riid == IID_IUnknown || riid == IID_ITfTextInputProcessor || riid == IID_ITfTextInputProcessorEx) {
    *ppv = static_cast<ITfTextInputProcessorEx*>(this);
  } else if (riid == IID_ITfThreadMgrEventSink) {
    *ppv = static_cast<ITfThreadMgrEventSink*>(this);
  } else if (riid == IID_ITfThreadFocusSink) {
    *ppv = static_cast<ITfThreadFocusSink*>(this);
  } else if (riid == IID_ITfKeyEventSink) {
    *ppv = static_cast<ITfKeyEventSink*>(this);
  } else if (riid == IID_ITfCompositionSink) {
    *ppv = static_cast<ITfCompositionSink*>(this);
  } else if (riid == IID_ITfDisplayAttributeProvider) {
    *ppv = static_cast<ITfDisplayAttributeProvider*>(this);
  } else if (riid == IID_ITfFunction || riid == IID_ITfFnConfigure) {
    *ppv = static_cast<ITfFnConfigure*>(this);
  } else if (riid == IID_ITfTextLayoutSink) {
    *ppv = static_cast<ITfTextLayoutSink*>(this);
  } else {
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  AddRef();
  return S_OK;
}

STDMETHODIMP_(ULONG) TextService::AddRef() {
  return InterlockedIncrement(&refs_);
}

STDMETHODIMP_(ULONG) TextService::Release() {
  LONG refs = InterlockedDecrement(&refs_);
  if (!refs) delete this;
  return refs;
}

// MARK: Activation

STDMETHODIMP TextService::Activate(ITfThreadMgr* threadMgr, TfClientId clientId) {
  return ActivateEx(threadMgr, clientId, 0);
}

STDMETHODIMP TextService::ActivateEx(ITfThreadMgr* threadMgr, TfClientId clientId, DWORD) {
  threadMgr_ = threadMgr;
  threadMgr_->AddRef();
  clientId_ = clientId;
  ITfSource* source = nullptr;
  if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&source)))) {
    source->AdviseSink(IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this), &threadMgrCookie_);
    source->AdviseSink(IID_ITfThreadFocusSink, static_cast<ITfThreadFocusSink*>(this), &threadFocusCookie_);
    source->Release();
  }
  ITfKeystrokeMgr* keystrokes = nullptr;
  if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&keystrokes)))) {
    keystrokes->AdviseKeyEventSink(clientId_, static_cast<ITfKeyEventSink*>(this), TRUE);
    keystrokes->Release();
  }
  ITfCategoryMgr* categories = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                 reinterpret_cast<void**>(&categories)))) {
    categories->RegisterGUID(kAttributeInput, &inputAtom_);
    categories->RegisterGUID(kAttributeActive, &activeAtom_);
    categories->Release();
  }
  ITfLangBarItemMgr* items = nullptr;
  if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfLangBarItemMgr, reinterpret_cast<void**>(&items)))) {
    langBar_ = new LangBarButton(this);
    items->AddItem(langBar_);
    items->Release();
  }
  candidates_.clicked = [this](const Metrics::PanelHit& hit) { candidateClicked(hit); };
  candidateList_ = new CandidateList;
  candidateList_->picked = [this](int row, int index) { candidateClicked({Metrics::PanelHit::kCandidate, row, index}); };
  candidateList_->aborted = [this] {
    KeyEvent escape;
    escape.key = Key::kEscape;
    if (compositionContext_) request(compositionContext_, {Protocol::Request::kKeyDown, escape});
  };
  candidateList_->shownChanged = [this](bool shown) {
    if (shown) return;  // shown again with the next update, which knows where
    candidates_.hide();
    hint_.hide();
  };
  // Starts the server if needed and learns the mode for the language bar.
  if (std::optional<Protocol::Reply> reply = engine_.send({Protocol::Request::kState})) {
    englishMode_ = reply->englishMode;
    fullWidth_ = reply->fullWidth;
    appearance_ = reply->appearance;
  }
  updateLangBar();
  return S_OK;
}

STDMETHODIMP TextService::Deactivate() {
  if (pauseTimer_) KillTimer(nullptr, pauseTimer_);
  pauseTimer_ = 0;
  if (pausedService_ == this) pausedService_ = nullptr;
  if (composition_) request(compositionContext_, {Protocol::Request::kDeactivate});
  candidates_.hide();
  hint_.hide();
  badge_.hide();
  ITfSource* source = nullptr;
  if (threadMgr_ && SUCCEEDED(threadMgr_->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&source)))) {
    source->UnadviseSink(threadMgrCookie_);
    source->UnadviseSink(threadFocusCookie_);
    source->Release();
  }
  threadMgrCookie_ = threadFocusCookie_ = TF_INVALID_COOKIE;
  ITfKeystrokeMgr* keystrokes = nullptr;
  if (threadMgr_ && SUCCEEDED(threadMgr_->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&keystrokes)))) {
    keystrokes->UnadviseKeyEventSink(clientId_);
    keystrokes->Release();
  }
  ITfLangBarItemMgr* items = nullptr;
  if (langBar_ && threadMgr_ && SUCCEEDED(threadMgr_->QueryInterface(IID_ITfLangBarItemMgr, reinterpret_cast<void**>(&items)))) {
    items->RemoveItem(langBar_);
    items->Release();
  }
  if (langBar_) langBar_->detach();
  SafeRelease(langBar_);
  if (candidateList_) {
    candidateList_->end(threadMgr_);
    candidateList_->picked = nullptr;  // an app may hold the element past this
    candidateList_->aborted = nullptr;
    candidateList_->shownChanged = nullptr;
  }
  SafeRelease(candidateList_);
  SafeRelease(composition_);
  SafeRelease(compositionContext_);
  watchLayout(nullptr);
  SafeRelease(threadMgr_);
  engine_.disconnect();
  clientId_ = TF_CLIENTID_NULL;
  return S_OK;
}

// MARK: Focus

// Focus moved to another document: commit, as the macOS version does when the client changes.
STDMETHODIMP TextService::OnSetFocus(ITfDocumentMgr* focus, ITfDocumentMgr*) {
  if (composition_) {
    request(compositionContext_, {Protocol::Request::kDeactivate});
  } else {
    candidates_.hide();
    if (candidateList_) candidateList_->end(threadMgr_);
    hint_.hide();
  }
  // The badge tells which mode the field starts in.
  ITfContext* context = nullptr;
  if (focus && SUCCEEDED(focus->GetTop(&context)) && context) {
    if (!keyboardDisabled(context)) request(context, {Protocol::Request::kShowMode});
    context->Release();
  }
  return S_OK;
}

// The app lost focus to another app.
STDMETHODIMP TextService::OnKillThreadFocus() {
  if (composition_) request(compositionContext_, {Protocol::Request::kDeactivate});
  candidates_.hide();
  if (candidateList_) candidateList_->end(threadMgr_);
  hint_.hide();
  badge_.hide();
  return S_OK;
}

STDMETHODIMP TextService::OnCompositionTerminated(TfEditCookie cookie, ITfComposition* composition) {
  // The app ended the composition (a click elsewhere, say): its text stays, the engine forgets it.
  ITfRange* range = nullptr;
  if (composition && SUCCEEDED(composition->GetRange(&range))) {
    ITfProperty* property = nullptr;
    if (compositionContext_ && SUCCEEDED(compositionContext_->GetProperty(GUID_PROP_ATTRIBUTE, &property))) {
      property->Clear(cookie, range);
      property->Release();
    }
    range->Release();
  }
  if (composition == composition_) {
    SafeRelease(composition_);
    SafeRelease(compositionContext_);
  }
  engine_.send({Protocol::Request::kReset});
  candidates_.hide();
  if (candidateList_) candidateList_->end(threadMgr_);
  hint_.hide();
  return S_OK;
}

// MARK: Keys

bool TextService::keyboardDisabled(ITfContext* context) {
  ITfCompartmentMgr* compartments = nullptr;
  if (FAILED(context->QueryInterface(IID_ITfCompartmentMgr, reinterpret_cast<void**>(&compartments)))) return false;
  bool disabled = false;
  for (const GUID& guid : {GUID_COMPARTMENT_KEYBOARD_DISABLED, GUID_COMPARTMENT_EMPTYCONTEXT}) {
    ITfCompartment* compartment = nullptr;
    if (SUCCEEDED(compartments->GetCompartment(guid, &compartment))) {
      VARIANT value;
      VariantInit(&value);
      if (compartment->GetValue(&value) == S_OK && value.vt == VT_I4 && value.lVal) disabled = true;
      VariantClear(&value);
      compartment->Release();
    }
  }
  compartments->Release();
  return disabled;
}

KeyEvent TextService::keyEvent(WPARAM vk) const {
  KeyEvent event;
  event.shift = IsDown(VK_SHIFT);
  event.control = IsDown(VK_CONTROL);
  event.alt = IsDown(VK_MENU);
  event.win = IsDown(VK_LWIN) || IsDown(VK_RWIN);
  event.capsLock = (GetKeyState(VK_CAPITAL) & 1) != 0;
  event.isP = vk == 'P';
  event.numpad = vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9;
  switch (vk) {
    case VK_SPACE: event.key = Key::kSpace; event.characters = " "; break;
    case VK_RETURN: event.key = Key::kReturn; break;
    case VK_TAB: event.key = Key::kTab; break;
    case VK_BACK: event.key = Key::kBackspace; break;
    case VK_ESCAPE: event.key = Key::kEscape; break;
    case VK_LEFT: event.key = Key::kLeft; break;
    case VK_RIGHT: event.key = Key::kRight; break;
    case VK_UP: event.key = Key::kUp; break;
    case VK_DOWN: event.key = Key::kDown; break;
    case VK_HOME: event.key = Key::kHome; break;
    case VK_END: event.key = Key::kEnd; break;
    case VK_PRIOR: event.key = Key::kPageUp; break;
    case VK_NEXT: event.key = Key::kPageDown; break;
    case VK_DELETE: event.key = Key::kForwardDelete; break;
    default: event.characters = UsCharacter(vk, event.shift, event.capsLock); break;
  }
  return event;
}

// TSF normally asks OnTestKeyDown first and calls OnKeyDown only for keys the
// test took, but some apps (Chromium among them) call OnKeyDown and OnKeyUp
// directly. Each key is handled once, in whichever call comes first.
STDMETHODIMP TextService::OnTestKeyDown(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten) {
  *eaten = FALSE;
  pending_.reset();
  testedDown_ = wParam;
  keyDown(context, wParam, lParam, eaten, true);
  return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten) {
  *eaten = FALSE;
  if (pending_ && pendingKey_ == wParam) {
    Protocol::Reply reply = std::move(*pending_);
    pending_.reset();
    testedDown_ = 0;
    if (context) apply(context, reply);
    *eaten = reply.handled;
    return S_OK;
  }
  if (testedDown_ == wParam) {  // the test already handled it and left it to the app
    testedDown_ = 0;
    return S_OK;
  }
  keyDown(context, wParam, lParam, eaten, false);
  return S_OK;
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext* context, WPARAM wParam, LPARAM, BOOL* eaten) {
  *eaten = FALSE;
  testedUp_ = wParam;
  keyUp(context, wParam);
  return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext* context, WPARAM wParam, LPARAM, BOOL* eaten) {
  *eaten = FALSE;
  if (testedUp_ == wParam) {
    testedUp_ = 0;
    return S_OK;
  }
  keyUp(context, wParam);
  return S_OK;
}

void TextService::keyDown(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten, bool testing) {
  if (!context || keyboardDisabled(context)) return;
  bool repeat = (lParam & (1 << 30)) != 0;
  switch (wParam) {
    case VK_SHIFT:
      if (!repeat) request(context, {Protocol::Request::kShift, {}, true});
      return;
    case VK_CAPITAL:
      if (!repeat) request(context, {Protocol::Request::kCapsLock});
      return;
    case VK_CONTROL:
    case VK_MENU:
    case VK_LWIN:
    case VK_RWIN:
      if (!repeat) engine_.send({Protocol::Request::kOtherModifier});
      return;
  }
  Protocol::Message message{Protocol::Request::kKeyDown, keyEvent(wParam)};
  // In 英, Tab may turn the Zhuyin just typed into a composition; the engine checks it is still there.
  // Apps reached through the IMM32 compatibility layer expose no text, so there Tab stays a Tab.
  // In 中, the key that may start a composition sends the text before the caret too: the
  // composition is ranked after the Han characters there, as on macOS and Linux.
  bool typing = !message.key.characters.empty() && !message.key.control && !message.key.alt && !message.key.win;
  if ((englishMode_ && wParam == VK_TAB) || (!englishMode_ && !composition_ && typing)) message.context = textBeforeCaret(context);
  std::optional<Protocol::Reply> reply = engine_.send(message);
  if (!reply) return;
  // In 英, the preview offers to turn Zhuyin into a composition, which needs the
  // text before the caret; documents of apps reached through the IMM32
  // compatibility layer (transitory) do not show it, so the offer would be empty.
  // Before the reply is kept for OnKeyDown, which applies it as it is.
  TF_STATUS status{};
  if (englishMode_ && SUCCEEDED(context->GetStatus(&status)) && (status.dwStaticFlags & TS_SS_TRANSITORY)) {
    std::erase_if(reply->operations, [](const Protocol::Operation& o) { return o.op == Protocol::Op::kShowPanel && !o.panel.keyHint.empty(); });
  }
  *eaten = reply->handled;
  if (reply->handled) waitForPause();
  if (reply->handled && testing) {
    // TSF calls OnKeyDown next; the document is changed there.
    pending_ = std::move(reply);
    pendingKey_ = wParam;
    return;
  }
  apply(context, *reply);  // e.g. the commit before a shortcut the app handles
}

void TextService::keyUp(ITfContext* context, WPARAM wParam) {
  if (wParam != VK_SHIFT || !context || keyboardDisabled(context)) return;
  request(context, {Protocol::Request::kShift, {}, false});
}

void TextService::candidateClicked(const Metrics::PanelHit& hit) {
  if (!compositionContext_) return;
  Protocol::Message message{hit.kind == Metrics::PanelHit::kChevron ? Protocol::Request::kToggleGrid : Protocol::Request::kClickCandidate};
  message.row = hit.row;
  message.index = hit.index;
  request(compositionContext_, message);
}

void TextService::toggleMode() {
  ITfDocumentMgr* document = nullptr;
  ITfContext* context = nullptr;
  if (threadMgr_ && SUCCEEDED(threadMgr_->GetFocus(&document)) && document) {
    document->GetTop(&context);
    document->Release();
  }
  std::optional<Protocol::Reply> reply = context ? request(context, {Protocol::Request::kToggleMode})
                                                 : engine_.send({Protocol::Request::kToggleMode});
  if (!context && reply) {
    englishMode_ = reply->englishMode;
    fullWidth_ = reply->fullWidth;
    updateLangBar();
  }
  SafeRelease(context);
}

// MARK: The advanced model

namespace {
constexpr UINT kPauseMs = 300;  // how long the typing pauses before the model chooses
constexpr UINT kPollMs = 100;   // how often the server is asked for its choice meanwhile
}  // namespace

thread_local TextService* TextService::pausedService_ = nullptr;

void TextService::waitForPause() {
  ranking_ = false;  // the server drops a choice made for the composition before this key
  startPauseTimer(kPauseMs);
}

void TextService::startPauseTimer(UINT milliseconds) {
  if (pauseTimer_) KillTimer(nullptr, pauseTimer_);
  pausedService_ = this;
  pauseTimer_ = SetTimer(nullptr, 0, milliseconds, &TextService::OnPauseTimer);
}

void CALLBACK TextService::OnPauseTimer(HWND, UINT, UINT_PTR, DWORD) {
  TextService* self = pausedService_;
  if (!self) return;
  KillTimer(nullptr, self->pauseTimer_);
  self->pauseTimer_ = 0;
  if (!self->compositionContext_) return;
  std::optional<Protocol::Reply> reply =
      self->request(self->compositionContext_, {self->ranking_ ? Protocol::Request::kRanked : Protocol::Request::kPause});
  self->ranking_ = reply && reply->handled;
  if (self->ranking_) self->startPauseTimer(kPollMs);
}

// MARK: Applying replies

std::optional<Protocol::Reply> TextService::request(ITfContext* context, const Protocol::Message& message) {
  std::optional<Protocol::Reply> reply = engine_.send(message);
  if (reply && context) apply(context, *reply);
  return reply;
}

HRESULT TextService::editSession(ITfContext* context, DWORD flags, std::function<void(TfEditCookie)> body, bool syncOnly) {
  auto* session = new EditSession(static_cast<ITfTextInputProcessorEx*>(this), std::move(body));
  HRESULT result = E_FAIL;
  HRESULT hr = context->RequestEditSession(clientId_, session, TF_ES_SYNC | flags, &result);
  if (!syncOnly && (FAILED(hr) || result == TF_E_SYNCHRONOUS)) {
    hr = context->RequestEditSession(clientId_, session, TF_ES_ASYNCDONTCARE | flags, &result);
  }
  session->Release();
  return FAILED(hr) ? hr : result;
}

void TextService::apply(ITfContext* context, const Protocol::Reply& reply) {
  if (reply.englishMode != englishMode_ || reply.fullWidth != fullWidth_) {
    englishMode_ = reply.englishMode;
    fullWidth_ = reply.fullWidth;
    updateLangBar();
  }
  appearance_ = reply.appearance;
  // The reply came from a new server session (the server restarted, or a request
  // timed out): it knows nothing of the composition in the document, so that
  // text stays as typed instead of being replaced by the new session's.
  bool orphaned = engine_.takeNewSession() && composition_;
  bool needsDocument = orphaned;
  for (const Protocol::Operation& o : reply.operations) {
    needsDocument |= o.op == Protocol::Op::kInsertText || o.op == Protocol::Op::kSetComposition || o.op == Protocol::Op::kShowPanel ||
                     o.op == Protocol::Op::kIndicator;
  }
  // The windows follow the composition, so they update after the document does.
  auto showWindows = [this, reply, context](const RECT* anchor, HWND window) {
    const Protocol::Operation* panel = nullptr;
    const Protocol::Operation* hint = nullptr;
    const Protocol::Operation* indicator = nullptr;
    for (const Protocol::Operation& o : reply.operations) {
      if (o.op == Protocol::Op::kShowPanel || o.op == Protocol::Op::kHidePanel) panel = &o;
      if (o.op == Protocol::Op::kShowHint || o.op == Protocol::Op::kHideHint) hint = &o;
      if (o.op == Protocol::Op::kIndicator) indicator = &o;
      if (o.op == Protocol::Op::kBeep) MessageBeep(MB_OK);
    }
    HWND owner = window ? GetAncestor(window, GA_ROOT) : GetForegroundWindow();
    // An app that draws the candidates itself (CandidateList) keeps the window hidden.
    bool drawn = panel && panel->op == Protocol::Op::kShowPanel && anchor &&
                 (!candidateList_ || candidateList_->present(threadMgr_, composition_ ? compositionContext_ : context, &panel->panel));
    if (drawn) {
      candidates_.show(panel->panel, *anchor, owner, appearance_);
    } else if (panel) {
      if (candidateList_ && panel->op == Protocol::Op::kHidePanel) candidateList_->end(threadMgr_);
      candidates_.hide();
      hint_.hide();
    }
    // Placed from the candidate window, so after it.
    if (hint && hint->op == Protocol::Op::kShowHint && candidates_.visible()) {
      hint_.showBeside(hint->panel, candidates_, owner, appearance_);
    } else if (hint) {
      hint_.hide();
    }
    if (indicator && anchor) {
      RECT frame = candidates_.frame();
      badge_.show(Widen(indicator->text), *anchor, owner, candidates_.visible() ? &frame : nullptr, appearance_);
    }
  };
  if (!needsDocument || !context) {
    showWindows(nullptr, nullptr);
    return;
  }
  editSession(composition_ ? compositionContext_ : context, TF_ES_READWRITE, [this, context, reply, showWindows, orphaned](TfEditCookie cookie) {
    // An app may run the session later, after Deactivate let go of the composition and sinks.
    if (!threadMgr_) return;
    if (orphaned && composition_) keepComposition(cookie);
    ITfContext* target = composition_ ? compositionContext_ : context;
    target->AddRef();
    watchLayout(target);
    for (const Protocol::Operation& o : reply.operations) {
      if (o.op == Protocol::Op::kInsertText) {
        insertText(cookie, target, Widen(o.text));
      } else if (o.op == Protocol::Op::kSetComposition) {
        setComposition(cookie, target, o.composition);
      }
    }
    HWND window = nullptr;
    RECT anchor = anchorRect(cookie, target, &window);
    showWindows(&anchor, window);
    target->Release();
  });
}

void TextService::watchLayout(ITfContext* context) {
  if (context == layoutContext_) return;
  ITfSource* source = nullptr;
  if (layoutContext_ && SUCCEEDED(layoutContext_->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&source)))) {
    source->UnadviseSink(layoutCookie_);
    source->Release();
  }
  SafeRelease(layoutContext_);
  layoutCookie_ = TF_INVALID_COOKIE;
  if (!context || FAILED(context->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&source)))) return;
  if (SUCCEEDED(source->AdviseSink(IID_ITfTextLayoutSink, static_cast<ITfTextLayoutSink*>(this), &layoutCookie_))) {
    layoutContext_ = context;
    layoutContext_->AddRef();
  }
  source->Release();
}

// The document laid out again: the composition or caret may be somewhere else,
// or (in Chromium) known only now.
STDMETHODIMP TextService::OnLayoutChange(ITfContext* context, TfLayoutCode code, ITfContextView*) {
  if (code == TF_LC_DESTROY) {
    if (context == layoutContext_) watchLayout(nullptr);  // let go of the closed view's document
    return S_OK;
  }
  if (!context || (!candidates_.visible() && !badge_.visible())) return S_OK;
  editSession(context, TF_ES_READ, [this, context](TfEditCookie cookie) {
    HWND window = nullptr;
    RECT anchor = anchorRect(cookie, context, &window);
    if (anchor.bottom <= anchor.top) return;
    HWND owner = window ? GetAncestor(window, GA_ROOT) : GetForegroundWindow();
    if (candidates_.visible() && candidates_.content()) {
      PanelContent content = *candidates_.content();  // show() replaces it
      candidates_.show(content, anchor, owner, appearance_);
    }
    if (hint_.visible() && hint_.content()) {
      PanelContent content = *hint_.content();
      hint_.showBeside(content, candidates_, owner, appearance_);
    }
    RECT frame = candidates_.frame();
    badge_.follow(anchor, candidates_.visible() ? &frame : nullptr);
  });
  return S_OK;
}

void TextService::setAttribute(TfEditCookie cookie, ITfContext* context, ITfRange* range, TfGuidAtom atom) {
  ITfProperty* property = nullptr;
  if (FAILED(context->GetProperty(GUID_PROP_ATTRIBUTE, &property))) return;
  if (atom == TF_INVALID_GUIDATOM) {
    property->Clear(cookie, range);
  } else {
    VARIANT value;
    value.vt = VT_I4;
    value.lVal = static_cast<LONG>(atom);
    property->SetValue(cookie, range, &value);
  }
  property->Release();
}

void TextService::insertText(TfEditCookie cookie, ITfContext* context, const std::wstring& text) {
  if (composition_) {
    // Committing: the composition's text becomes plain text and the caret goes after it.
    ITfRange* range = nullptr;
    if (SUCCEEDED(composition_->GetRange(&range))) {
      range->SetText(cookie, 0, text.c_str(), static_cast<LONG>(text.size()));
      setAttribute(cookie, compositionContext_, range, TF_INVALID_GUIDATOM);
      range->Collapse(cookie, TF_ANCHOR_END);
      SetSelection(cookie, compositionContext_, range);
      range->Release();
    }
    endComposition(cookie);
    return;
  }
  if (text.empty()) return;
  ITfInsertAtSelection* insert = nullptr;
  if (FAILED(context->QueryInterface(IID_ITfInsertAtSelection, reinterpret_cast<void**>(&insert)))) return;
  ITfRange* range = nullptr;
  if (SUCCEEDED(insert->InsertTextAtSelection(cookie, 0, text.c_str(), static_cast<LONG>(text.size()), &range)) && range) {
    range->Collapse(cookie, TF_ANCHOR_END);
    SetSelection(cookie, context, range);
    range->Release();
  }
  insert->Release();
}

// Ends the composition, leaving its text in the document as plain text.
void TextService::keepComposition(TfEditCookie cookie) {
  ITfRange* range = nullptr;
  if (SUCCEEDED(composition_->GetRange(&range))) {
    setAttribute(cookie, compositionContext_, range, TF_INVALID_GUIDATOM);
    range->Release();
  }
  endComposition(cookie);
}

void TextService::endComposition(TfEditCookie cookie) {
  if (!composition_) return;
  composition_->EndComposition(cookie);
  SafeRelease(composition_);
  SafeRelease(compositionContext_);
}

void TextService::setComposition(TfEditCookie cookie, ITfContext* context, const Composition& composition) {
  std::wstring text = Widen(composition.text);
  if (text.empty()) {
    if (!composition_) return;
    ITfRange* range = nullptr;
    if (SUCCEEDED(composition_->GetRange(&range))) {
      range->SetText(cookie, 0, L"", 0);
      range->Release();
    }
    endComposition(cookie);
    return;
  }
  if (!composition_) {
    ITfInsertAtSelection* insert = nullptr;
    ITfContextComposition* compositions = nullptr;
    ITfRange* range = nullptr;
    if (SUCCEEDED(context->QueryInterface(IID_ITfInsertAtSelection, reinterpret_cast<void**>(&insert))) &&
        SUCCEEDED(insert->InsertTextAtSelection(cookie, TF_IAS_QUERYONLY, nullptr, 0, &range)) && range &&
        SUCCEEDED(context->QueryInterface(IID_ITfContextComposition, reinterpret_cast<void**>(&compositions)))) {
      // The composition takes the place of the English typed before it.
      if (composition.replaceBefore) {
        LONG shifted = 0;
        range->ShiftStart(cookie, -static_cast<LONG>(composition.replaceBefore), &shifted, nullptr);
      }
      if (SUCCEEDED(compositions->StartComposition(cookie, range, static_cast<ITfCompositionSink*>(this), &composition_)) &&
          composition_) {
        compositionContext_ = context;
        compositionContext_->AddRef();
      }
    }
    SafeRelease(range);
    SafeRelease(compositions);
    SafeRelease(insert);
    if (!composition_) return;
  }
  ITfRange* range = nullptr;
  if (FAILED(composition_->GetRange(&range))) return;
  range->SetText(cookie, 0, text.c_str(), static_cast<LONG>(text.size()));
  setAttribute(cookie, compositionContext_, range, inputAtom_);
  LONG shifted = 0;
  if (composition.activeLength) {
    ITfRange* active = nullptr;
    if (SUCCEEDED(range->Clone(&active))) {
      active->Collapse(cookie, TF_ANCHOR_START);
      active->ShiftEnd(cookie, composition.activeStart + composition.activeLength, &shifted, nullptr);
      active->ShiftStart(cookie, composition.activeStart, &shifted, nullptr);
      setAttribute(cookie, compositionContext_, active, activeAtom_);
      active->Release();
    }
  }
  ITfRange* caret = nullptr;
  if (SUCCEEDED(range->Clone(&caret))) {
    caret->Collapse(cookie, TF_ANCHOR_START);
    caret->ShiftEnd(cookie, composition.caret, &shifted, nullptr);
    caret->Collapse(cookie, TF_ANCHOR_END);
    SetSelection(cookie, compositionContext_, caret);
    caret->Release();
  }
  range->Release();
}

// The composition's first line in screen coordinates, or the caret's without one.
RECT TextService::anchorRect(TfEditCookie cookie, ITfContext* context, HWND* window) {
  RECT rect{}, field{};
  ITfContextView* view = nullptr;
  if (SUCCEEDED(context->GetActiveView(&view))) {
    view->GetWnd(window);
    view->GetScreenExt(&field);
    ITfRange* range = nullptr;
    LONG shifted = 0;
    bool caretAfter = false;  // measured the character before the caret
    if (composition_ && SUCCEEDED(composition_->GetRange(&range))) {
      range->Collapse(cookie, TF_ANCHOR_START);
      range->ShiftEnd(cookie, 1, &shifted, nullptr);
    } else {
      TF_SELECTION selection{};
      ULONG fetched = 0;
      if (SUCCEEDED(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) && fetched) {
        range = selection.range;
        // Chromium answers an empty range with a zero-height rect at the
        // screen's corner; the character before the caret has a real one.
        range->Collapse(cookie, TF_ANCHOR_START);
        range->ShiftStart(cookie, -1, &shifted, nullptr);
        caretAfter = shifted == -1;
      }
    }
    if (range) {
      BOOL clipped = FALSE;
      if (FAILED(view->GetTextExt(cookie, range, &rect, &clipped))) rect = {};
      if (caretAfter) rect.left = rect.right;
      range->Release();
    }
    view->Release();
  }
  // A zero-height rect is no position: keep the last real one in the same window.
  if (rect.bottom > rect.top) {
    lastAnchor_ = rect;
    lastAnchorWindow_ = window ? *window : nullptr;
    return rect;
  }
  if (lastAnchor_.bottom > lastAnchor_.top && window && *window == lastAnchorWindow_) return lastAnchor_;
  rect = {};
  // Apps without a text layout: the system caret.
  GUITHREADINFO info{sizeof(info)};
  if (GetGUIThreadInfo(GetCurrentThreadId(), &info) && info.hwndCaret) {
    rect = info.rcCaret;
    MapWindowPoints(info.hwndCaret, nullptr, reinterpret_cast<POINT*>(&rect), 2);
    if (window && !*window) *window = info.hwndCaret;
  }
  // Nothing knows the caret: the text field itself (in Chromium, the focused element).
  if (rect.bottom <= rect.top && field.bottom > field.top) rect = {field.left, field.top, field.left, field.bottom};
  return rect;
}

std::string TextService::textBeforeCaret(ITfContext* context) {
  std::wstring text;
  editSession(context, TF_ES_READ, [&text, context](TfEditCookie cookie) {
    TF_SELECTION selection{};
    ULONG fetched = 0;
    if (FAILED(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) || !fetched) return;
    ITfRange* range = selection.range;
    range->Collapse(cookie, TF_ANCHOR_START);
    LONG shifted = 0;
    range->ShiftStart(cookie, -64, &shifted, nullptr);
    wchar_t buffer[64];
    ULONG length = 0;
    if (SUCCEEDED(range->GetText(cookie, 0, buffer, 64, &length))) text.assign(buffer, length);
    range->Release();
  }, true);  // the result is needed now
  return Narrow(text);
}

// MARK: Display attributes

STDMETHODIMP TextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** result) {
  return CreateDisplayAttributeEnum(result);
}

STDMETHODIMP TextService::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** result) {
  return CreateDisplayAttributeInfo(guid, result);
}

// MARK: Settings

STDMETHODIMP TextService::GetDisplayName(BSTR* name) {
  if (!name) return E_INVALIDARG;
  *name = SysAllocString(kDisplayName);
  return *name ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP TextService::Show(HWND, LANGID, REFGUID) {
  showSettings();
  return S_OK;
}

void TextService::showSettings() {
  // The user just clicked here; let the window the server starts come to the front.
  AllowSetForegroundWindow(ASFW_ANY);
  if (!engine_.send({Protocol::Request::kShowSettings})) LaunchSettings(ModuleDirectory(gInstance));
}

void TextService::updateLangBar() {
  if (langBar_) langBar_->update(englishMode_, fullWidth_);
}

}  // namespace ProjectKey
