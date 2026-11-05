// The engine server: one process per user that holds the language models, the
// choices the user made and the 中/英 switch, so every app shares them, and
// writes settings outside any app container. The text service in each app
// connects over a named pipe; each connection gets its own Controller.
#include <windows.h>
#include <objbase.h>
#include <sddl.h>
#include <spellcheck.h>
#include <wrl/client.h>

#include <memory>
#include <mutex>
#include <thread>

#include "Common/Layout.h"
#include "Common/Protocol.h"
#include "Common/Win.h"
#include "Controller.h"

using Microsoft::WRL::ComPtr;
using namespace ProjectKey;

namespace {

// Settings as DWORD values under HKCU\Software\ProjectKey, named as on macOS.
class RegistrySettings : public Settings {
 public:
  std::optional<int> stored(Setting setting) const override {
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, Widen(Name(setting)).c_str(), RRF_RT_REG_DWORD, nullptr, &value, &size) !=
        ERROR_SUCCESS) {
      return std::nullopt;
    }
    return static_cast<int>(value);
  }
  void set(Setting setting, int value) override {
    DWORD v = static_cast<DWORD>(value);
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, Widen(Name(setting)).c_str(), REG_DWORD, &v, sizeof(v));
  }
};

// Choices the user made, shared by every input session. Kept in memory only,
// with McBopomofo's capacity and 1.5-hour half-life, as on macOS.
McBopomofo::UserOverrideModel NewUserModel() {
  return McBopomofo::UserOverrideModel(500, 5400.0);
}

// Records what the controller does as reply operations.
class Recorder : public Client {
 public:
  Protocol::Reply reply;
  std::string context;

  void insertText(const std::string& text) override { add({Protocol::Op::kInsertText, text}); }
  void setComposition(const Composition& composition) override {
    Protocol::Operation o;
    o.op = Protocol::Op::kSetComposition;
    o.composition = composition;
    add(std::move(o));
  }
  void showPanel(const PanelContent& content) override {
    Protocol::Operation o;
    o.op = Protocol::Op::kShowPanel;
    o.panel = content;
    add(std::move(o));
  }
  void hidePanel() override { add({Protocol::Op::kHidePanel}); }
  void showIndicator(const std::string& label) override { add({Protocol::Op::kIndicator, label}); }
  void beep() override { add({Protocol::Op::kBeep}); }
  std::string textBeforeCaret() override { return context; }

 private:
  void add(Protocol::Operation operation) { reply.operations.push_back(std::move(operation)); }
};

std::mutex gLock;  // the shared environment; held for each request
Environment gEnvironment;
RegistrySettings gSettings;
std::unique_ptr<Layout::Fonts> gFonts;
ComPtr<ISpellChecker> gSpellChecker;

bool IsEnglish(const std::string& text) {
  if (!gSpellChecker) return false;
  std::wstring wide = Widen(text);
  ComPtr<IEnumSpellingError> errors;
  if (FAILED(gSpellChecker->Check(wide.c_str(), &errors))) return false;
  ComPtr<ISpellingError> error;
  return errors->Next(&error) == S_FALSE;  // no misspelled word
}

// Requests are a key and at most a little text before the caret; anything
// bigger is not from the text service, and reading it all could exhaust memory.
constexpr size_t kMaxRequestBytes = 65536;

bool ReadMessage(HANDLE pipe, std::string& message) {
  message.clear();
  char buffer[4096];
  for (;;) {
    DWORD read = 0;
    BOOL ok = ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr);
    message.append(buffer, read);
    if (ok) return true;
    if (GetLastError() != ERROR_MORE_DATA || message.size() > kMaxRequestBytes) return false;
  }
}

// Whether the other end of the pipe is the installed settings window. Apps in a
// sandbox can connect too, but cannot pose as it.
bool FromSettings(HANDLE pipe) {
  ULONG client = 0;
  return GetNamedPipeClientProcessId(pipe, &client) && SamePath(ProcessImage(client), ModuleDirectory(nullptr) + kSettingsExe);
}

void Serve(HANDLE pipe) {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  Recorder recorder;
  std::unique_ptr<Controller> controller;
  {
    std::lock_guard lock(gLock);
    controller = std::make_unique<Controller>(gEnvironment, recorder);
  }
  std::string bytes;
  while (ReadMessage(pipe, bytes)) {
    Protocol::Message message;
    if (!Protocol::ReadMessage(bytes, message)) break;
    std::string reply;
    {
      std::lock_guard lock(gLock);
      recorder.reply = {};
      recorder.context = message.context;
      switch (message.type) {
        case Protocol::Request::kKeyDown: recorder.reply.handled = controller->handleKey(message.key); break;
        case Protocol::Request::kShift: controller->handleShift(message.down); break;
        case Protocol::Request::kCapsLock: controller->handleCapsLock(); break;
        case Protocol::Request::kOtherModifier: controller->handleOtherModifier(); break;
        case Protocol::Request::kDeactivate: controller->deactivate(); break;
        case Protocol::Request::kReset: controller->reset(); break;
        case Protocol::Request::kClearChoices:
          if (FromSettings(pipe)) *gEnvironment.userModel = NewUserModel();
          break;
        case Protocol::Request::kToggleMode: controller->toggleMode(); break;
        case Protocol::Request::kShowSettings: LaunchSettings(ModuleDirectory(nullptr)); break;
        case Protocol::Request::kState: break;
      }
      recorder.reply.englishMode = gEnvironment.englishMode;
      recorder.reply.appearance = gSettings.get(Setting::kAppearance);
      reply = Protocol::WriteReply(recorder.reply);
    }
    DWORD written = 0;
    if (!WriteFile(pipe, reply.data(), static_cast<DWORD>(reply.size()), &written, nullptr)) break;
  }
  {
    std::lock_guard lock(gLock);
    controller.reset();
  }
  DisconnectNamedPipe(pipe);
  CloseHandle(pipe);
  CoUninitialize();
}

// The pipe admits this user from any integrity level and app container, as
// the text service runs inside every app.
SECURITY_ATTRIBUTES* PipeSecurity() {
  static SECURITY_ATTRIBUTES attributes{sizeof(attributes)};
  static PSECURITY_DESCRIPTOR descriptor = [] {
    std::wstring sddl = L"D:(A;;GA;;;" + UserSid() + L")(A;;GA;;;SY)(A;;GA;;;AC)(A;;GA;;;S-1-15-2-2)S:(ML;;NW;;;LW)";
    PSECURITY_DESCRIPTOR sd = nullptr;
    ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr);
    return sd;
  }();
  attributes.lpSecurityDescriptor = descriptor;
  return &attributes;
}

void Log(const std::wstring& message) {
  OutputDebugStringW((L"Project Key: " + message + L"\n").c_str());
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  std::wstring sid = UserSid();
  HANDLE instance = CreateMutexW(nullptr, TRUE, (L"Local\\ProjectKeyServer-" + sid).c_str());
  if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  std::wstring dir = ModuleDirectory(nullptr);
  gEnvironment.lm = std::make_shared<Lexicon>();
  gEnvironment.lm->loadLanguageModel(Narrow(dir + L"data.txt").c_str());
  if (!gEnvironment.lm->isDataModelLoaded()) {
    Log(L"cannot load " + dir + L"data.txt");
    return 1;
  }
  // Context ranking model from tools/bigram-trainer; without it, unigrams only.
  auto bigram = std::make_shared<Bigram>();
  if (GetFileAttributesW((dir + L"bigram.bin").c_str()) != INVALID_FILE_ATTRIBUTES) {
    if (bigram->load(Narrow(dir + L"bigram.bin"))) {
      gEnvironment.bigram = bigram;
    } else {
      Log(L"cannot load " + dir + L"bigram.bin");
    }
  }
  gEnvironment.userModel = std::make_shared<McBopomofo::UserOverrideModel>(NewUserModel());
  gEnvironment.settings = &gSettings;
  gEnvironment.isEnglish = IsEnglish;
  gFonts = std::make_unique<Layout::Fonts>();
  gEnvironment.columnsSpannedBy = [](const std::string& candidate) { return gFonts->ok() ? gFonts->span(Widen(candidate)) : 1; };
  ComPtr<ISpellCheckerFactory> spelling;
  if (SUCCEEDED(CoCreateInstance(__uuidof(SpellCheckerFactory), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&spelling)))) {
    spelling->CreateSpellChecker(L"en-US", &gSpellChecker);
  }

  std::wstring name = PipeName();
  DWORD first = FILE_FLAG_FIRST_PIPE_INSTANCE;  // no one else may own the name
  for (;;) {
    HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | first,
                                   PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                   PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, PipeSecurity());
    if (pipe == INVALID_HANDLE_VALUE) {
      Log(L"cannot create " + name);
      return 1;
    }
    first = 0;
    if (ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) {
      std::thread(Serve, pipe).detach();
    } else {
      CloseHandle(pipe);
    }
  }
  CloseHandle(instance);
  return 0;
}
