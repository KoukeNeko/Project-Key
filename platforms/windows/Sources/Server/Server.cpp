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
  void showHint(const PanelContent& content) override {
    Protocol::Operation o;
    o.op = Protocol::Op::kShowHint;
    o.panel = content;
    add(std::move(o));
  }
  void hideHint() override { add({Protocol::Op::kHideHint}); }
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

void Log(const std::wstring& message) {
  OutputDebugStringW((L"Project Key: " + message + L"\n").c_str());
}

// %LOCALAPPDATA%\Project Key\, where personal.txt and downloaded models go; empty without it.
std::wstring UserData() {
  wchar_t local[MAX_PATH];
  DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
  return length > 0 && length < MAX_PATH ? std::wstring(local, length) + L"\\Project Key\\" : L"";
}

// Switches to the newest of the installed models and those the settings app downloaded.
bool UseNewestModels() {
  return UseNewestModels(gEnvironment, Narrow(ModuleDirectory(nullptr)), UserData().empty() ? "" : Narrow(UserData() + L"models"));
}

// Whether the other end of the pipe is the installed settings window. Apps in a
// sandbox can connect too, but cannot pose as it.
bool FromSettings(HANDLE pipe) {
  ULONG client = 0;
  return GetNamedPipeClientProcessId(pipe, &client) && SamePath(ProcessImage(client), ModuleDirectory(nullptr) + kSettingsExe);
}

// Whether the other end of the pipe runs outside an app container at medium integrity or
// above. Only those rank by and teach what the user typed: anything sandboxed could read
// it back from the candidate order. Unknown counts as sandboxed.
bool FromPlainApp(HANDLE pipe) {
  ULONG client = 0;
  if (!GetNamedPipeClientProcessId(pipe, &client)) return false;
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, client);
  if (!process) return false;
  HANDLE token = nullptr;
  bool opened = OpenProcessToken(process, TOKEN_QUERY, &token);
  CloseHandle(process);
  if (!opened) return false;
  DWORD isAppContainer = 1, size = sizeof(isAppContainer);
  BYTE label[64];
  bool plain = GetTokenInformation(token, TokenIsAppContainer, &isAppContainer, size, &size) && !isAppContainer &&
               GetTokenInformation(token, TokenIntegrityLevel, label, sizeof(label), &size);
  if (plain) {
    PSID sid = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(label)->Label.Sid;
    plain = *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1) >= static_cast<DWORD>(SECURITY_MANDATORY_MEDIUM_RID);
  }
  CloseHandle(token);
  return plain;
}

void Serve(HANDLE pipe) {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  Recorder recorder;
  bool personal = FromPlainApp(pipe);
  std::unique_ptr<Controller> controller;
  {
    std::lock_guard lock(gLock);
    controller = std::make_unique<Controller>(gEnvironment, recorder, personal);
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
          if (FromSettings(pipe)) ClearChoices(gEnvironment);
          break;
        case Protocol::Request::kReloadModels:
          if (FromSettings(pipe) && !UseNewestModels()) {
            Log(L"cannot load the downloaded models");
          }
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
// the text service runs inside every app. App containers may read and write it
// (0x12019b: FILE_GENERIC_READ | FILE_GENERIC_WRITE without FILE_CREATE_PIPE_INSTANCE)
// but not serve an instance of their own; the text service also checks which
// program serves it.
SECURITY_ATTRIBUTES* PipeSecurity() {
  static SECURITY_ATTRIBUTES attributes{sizeof(attributes)};
  static PSECURITY_DESCRIPTOR descriptor = [] {
    std::wstring sddl =
        L"D:(A;;GA;;;" + UserSid() + L")(A;;GA;;;SY)(A;;0x12019b;;;AC)(A;;0x12019b;;;S-1-15-2-2)S:(ML;;NW;;;LW)";
    PSECURITY_DESCRIPTOR sd = nullptr;
    ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr);
    return sd;
  }();
  attributes.lpSecurityDescriptor = descriptor;
  return &attributes;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  std::wstring sid = UserSid();
  HANDLE instance = CreateMutexW(nullptr, TRUE, (L"Local\\ProjectKeyServer-" + sid).c_str());
  if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  // The installed models, or newer ones the settings app downloaded.
  if (!UseNewestModels()) {
    Log(L"cannot load data.txt");
    return 1;
  }
  gEnvironment.userModel = NewUserModel();
  // What the user typed, learned on this PC only, beside other per-user app data.
  if (!UserData().empty()) gEnvironment.personalPath = Narrow(UserData() + L"personal.txt");
  gEnvironment.personal = std::make_shared<PersonalModel>();
  if (!gEnvironment.personalPath.empty()) gEnvironment.personal->load(gEnvironment.personalPath);
  gEnvironment.settings = &gSettings;
  gEnvironment.isEnglish = IsEnglish;
  gEnvironment.hintWindow = true;
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
