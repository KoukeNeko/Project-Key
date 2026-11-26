// The engine server: one process per user that holds the language models, the
// choices the user made and the 中/英 switch, so every app shares them, and
// writes settings outside any app container. The text service in each app
// connects over a named pipe; each connection gets its own Controller.
#include <windows.h>
#include <objbase.h>
#include <sddl.h>
#include <spellcheck.h>
#include <winhttp.h>
#include <winver.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>

#include "Common/Layout.h"
#include "Common/Protocol.h"
#include "Common/Win.h"
#ifdef PROJECTKEY_ADVANCED_MODEL
#include "Microphone.h"
#include "SpeechModel.h"
#endif
#include "Controller.h"
#include "Models.h"
#include "Update.h"

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
    reply.columns = content.columns;
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
// Each app's text service holds a connection, but so may any app in a sandbox, without limit: past
// this many, new ones are turned away rather than each taking a thread.
constexpr int kMaxConnections = 256;
std::atomic<int> gConnections{0};
// kShowSettings from any app, a sandboxed one included: at most one launch per interval.
constexpr std::chrono::seconds kSettingsInterval{2};
std::atomic<std::chrono::steady_clock::rep> gSettingsLaunched{0};
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

// A newer release than this one, for the language bar's menu; empty for none.
std::mutex gUpdateLock;
std::string gUpdate;

// ProjectKey.dll's product version, as the settings app shows it; empty without one.
std::string InstalledVersion() {
  std::wstring path = ModuleDirectory(nullptr) + L"ProjectKey.dll";
  DWORD size = GetFileVersionInfoSizeW(path.c_str(), nullptr);
  std::vector<char> data(size);
  VS_FIXEDFILEINFO* info = nullptr;
  UINT length = 0;
  if (!size || !GetFileVersionInfoW(path.c_str(), 0, size, data.data()) ||
      !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info) {
    return {};
  }
  return std::to_string(HIWORD(info->dwProductVersionMS)) + "." + std::to_string(LOWORD(info->dwProductVersionMS)) + "." +
         std::to_string(HIWORD(info->dwProductVersionLS));
}

// GitHub's latest-release JSON; empty when it cannot be had.
std::string FetchLatestRelease() {
  constexpr size_t kMaxReleaseBytes = 1 << 20;  // the JSON is a few kilobytes; more is not GitHub's
  std::wstring url = Widen(kLatestRelease);
  URL_COMPONENTS parts{sizeof(parts)};
  parts.dwHostNameLength = parts.dwUrlPathLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) return {};
  std::wstring host(parts.lpszHostName, parts.dwHostNameLength), path(parts.lpszUrlPath, parts.dwUrlPathLength);
  std::string body;
  HINTERNET session = WinHttpOpen(L"ProjectKey", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  HINTERNET connection = session ? WinHttpConnect(session, host.c_str(), parts.nPort, 0) : nullptr;
  HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                 : nullptr;
  DWORD status = 0, size = sizeof(status);
  if (request && WinHttpSendRequest(request, L"Accept: application/vnd.github+json", static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
      WinHttpReceiveResponse(request, nullptr) &&
      WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                          WINHTTP_NO_HEADER_INDEX) &&
      status == 200) {
    char buffer[8192];
    DWORD read = 0;
    while (body.size() < kMaxReleaseBytes && WinHttpReadData(request, buffer, sizeof(buffer), &read) && read > 0) body.append(buffer, read);
  }
  if (request) WinHttpCloseHandle(request);
  if (connection) WinHttpCloseHandle(connection);
  if (session) WinHttpCloseHandle(session);
  return body;
}

// At start and every kUpdateCheckInterval while the setting is on; one that fails, offline at sign-in
// say, is retried within the hour.
void CheckForUpdates() {
  std::string current = InstalledVersion();
  if (current.empty()) return;
  std::optional<std::chrono::steady_clock::time_point> checked;
  for (;; std::this_thread::sleep_for(std::chrono::hours(1))) {
    auto now = std::chrono::steady_clock::now();
    if (!gSettings.flag(Setting::kCheckForUpdates) || (checked && now - *checked < kUpdateCheckInterval)) continue;
    std::string release = FetchLatestRelease();
    if (release.empty()) continue;
    checked = now;
    std::lock_guard lock(gUpdateLock);
    gUpdate = NewerRelease(release, current).value_or("");
  }
}

// Switches to the newest of the installed models and those the settings app downloaded.
bool UseNewestModels() {
  return UseNewestModels(gEnvironment, Narrow(ModuleDirectory(nullptr)), UserData().empty() ? "" : Narrow(UserData() + L"models"));
}

// Dictation (Request::kDictationKey, kDictated): the microphone records while the right Alt is held
// in whichever app has it, and the speech model (core/engine/SpeechModel.h) the settings app
// downloads transcribes on a thread of its own. Built without llama.cpp, there is never a model.
class Dictation {
 public:
  // A connection's recording; text is set once it is transcribed.
  struct Job {
    std::vector<float> samples;
    std::vector<std::string> words;
    std::mutex lock;
    std::optional<std::string> text;
  };
  static Dictation& Shared() {
    static Dictation* dictation = new Dictation();  // its thread runs as long as the process
    return *dictation;
  }
  bool loaded() const { return sampleRate_ > 0; }
  void reload() {
    std::lock_guard lock(mutex_);
    reload_ = true;
    wake_.notify_all();
  }
  // false when the microphone cannot be opened.
  bool record() {
#ifdef PROJECTKEY_ADVANCED_MODEL
    std::lock_guard lock(microphoneLock_);
    return loaded() && microphone_.start(sampleRate_);
#else
    return false;
#endif
  }
  std::vector<float> stop() {
#ifdef PROJECTKEY_ADVANCED_MODEL
    std::lock_guard lock(microphoneLock_);
    return microphone_.stop();
#else
    return {};
#endif
  }
  // Answers the job unless its connection has dropped it by then.
  void ask(const std::shared_ptr<Job>& job) {
    std::lock_guard lock(mutex_);
    jobs_.push_back(job);
    wake_.notify_all();
  }

 private:
  Dictation() { std::thread([this] { run(); }).detach(); }
  void run() {
    for (;;) {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [&] { return reload_ || !jobs_.empty(); });
      if (reload_) {
        reload_ = false;
        lock.unlock();
        sampleRate_ = load();
        continue;
      }
      std::shared_ptr<Job> job = jobs_.front().lock();
      jobs_.pop_front();
      lock.unlock();
      if (!job) continue;
      std::string text;
#ifdef PROJECTKEY_ADVANCED_MODEL
      if (model_) text = model_->transcribe(job->samples, job->words);
#endif
      std::lock_guard answered(job->lock);
      job->text = std::move(text);
    }
  }
  // The model's sample rate, 0 for none.
  int load() {
#ifdef PROJECTKEY_ADVANCED_MODEL
    model_.reset();
    if (UserData().empty()) return 0;
    std::filesystem::path folder = UserData() + L"speech";
    std::filesystem::path dir = NewestDownload(folder);
    if (dir.empty()) return 0;
    auto model = std::make_unique<SpeechModel>();
    int threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 1, 1, 8);
    if (!model->load(Narrow(dir.wstring()), threads)) {
      Log(L"cannot load the speech model in " + dir.wstring());
      return 0;
    }
    model_ = std::move(model);
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
      if (entry.path() != dir && entry.path().extension() != L".partial") std::filesystem::remove_all(entry.path(), error);
    }
    return model_->sampleRate();
#else
    return 0;
#endif
  }

  std::atomic<int> sampleRate_{0};
  std::mutex mutex_;
  std::condition_variable wake_;
  bool reload_ = false;
  std::deque<std::weak_ptr<Job>> jobs_;
#ifdef PROJECTKEY_ADVANCED_MODEL
  std::mutex microphoneLock_;
  Microphone microphone_;
  std::unique_ptr<SpeechModel> model_;  // only the thread touches it
#endif
};

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
  std::shared_ptr<Dictation::Job> dictated;      // what it recorded, while it is transcribed
  std::string bytes;
  while (ReadMessage(pipe, bytes)) {
    Protocol::Message message;
    if (!Protocol::ReadMessage(bytes, message)) break;
    std::string reply;
    bool showSettings = false;
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
          if (!FromSettings(pipe)) break;
          if (!UseNewestModels()) Log(L"cannot load the downloaded models");
          Dictation::Shared().reload();  // after the speech model was downloaded or removed too
          break;
        case Protocol::Request::kDictationKey:
          if (!Dictation::Shared().loaded() || !gSettings.flag(Setting::kDictation)) {
            if (message.down) controller->handleOtherModifier();  // an ordinary Alt
            break;
          }
          recorder.reply.handled = message.down;
          if (message.down) {
            if (controller->dictationKey(true) && !Dictation::Shared().record()) controller->handleOtherModifier();  // takes the badge away
          } else if (controller->dictationKey(false)) {
            dictated = std::make_shared<Dictation::Job>();
            dictated->samples = Dictation::Shared().stop();
            dictated->words = DictationWords(gEnvironment);
            Dictation::Shared().ask(dictated);
            recorder.reply.handled = true;
          } else {
            Dictation::Shared().stop();  // a tap, or a hold another key broke
          }
          break;
        case Protocol::Request::kDictated:
          if (dictated) {
            std::optional<std::string> text;
            {
              std::lock_guard answered(dictated->lock);
              text = dictated->text;
            }
            if (text) {
              controller->insertDictation(*text);
              dictated.reset();
            } else {
              recorder.reply.handled = true;
            }
          }
          break;
        case Protocol::Request::kPause:
        case Protocol::Request::kRanked:
          break;  // retired: leaving both unhandled tells older text services to stop asking
        case Protocol::Request::kToggleMode: controller->toggleMode(); break;
        case Protocol::Request::kToggleSimplified:
          if (message.down && !gSettings.flag(Setting::kSimplifiedToggle)) break;  // the shortcut is off: the app's Ctrl+Shift+F
          controller->toggleSimplified();
          recorder.reply.handled = true;
          break;
        case Protocol::Request::kToggleFullWidth: controller->toggleFullWidth(); break;
        case Protocol::Request::kClickCandidate: controller->clickCandidate(message.row, message.index); break;
        case Protocol::Request::kToggleGrid: controller->toggleGrid(); break;
        case Protocol::Request::kShowMode: controller->showMode(); break;
        case Protocol::Request::kShowSettings: showSettings = true; break;  // after the lock, which every app's keys wait on
        case Protocol::Request::kState: break;
      }
      recorder.reply.englishMode = gEnvironment.englishMode;
      recorder.reply.fullWidth = gEnvironment.fullWidth;
      recorder.reply.simplified = gSettings.flag(Setting::kSimplified);
      recorder.reply.appearance = gSettings.get(Setting::kAppearance);
      if (gSettings.flag(Setting::kCheckForUpdates)) {  // turning it off takes the menu item away at once
        std::lock_guard update(gUpdateLock);
        recorder.reply.update = gUpdate;
      }
      reply = Protocol::WriteReply(recorder.reply);
    }
    if (showSettings) {
      auto now = std::chrono::steady_clock::now().time_since_epoch().count();
      auto last = gSettingsLaunched.load();
      if (now - last >= std::chrono::duration_cast<std::chrono::steady_clock::duration>(kSettingsInterval).count() &&
          gSettingsLaunched.compare_exchange_strong(last, now)) {
        LaunchSettings(ModuleDirectory(nullptr));
      }
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
  gConnections--;
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
  Dictation::Shared().reload();
  gEnvironment.userModel = NewUserModel();
  // What the user typed, learned on this PC only, beside other per-user app data.
  if (!UserData().empty()) {
    gEnvironment.personalPath = Narrow(UserData() + L"personal.txt");
    gEnvironment.userPhrasesPath = Narrow(UserData() + L"phrases.txt");  // edited from the settings app
  }
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
  std::thread(CheckForUpdates).detach();

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
    if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
      CloseHandle(pipe);
      continue;
    }
    if (gConnections >= kMaxConnections) {
      DisconnectNamedPipe(pipe);
      CloseHandle(pipe);
      Sleep(50);  // a client that keeps reconnecting does not keep this loop busy
      continue;
    }
    gConnections++;
    try {
      std::thread(Serve, pipe).detach();
    } catch (const std::system_error&) {  // out of threads: this client goes without, the others keep theirs
      gConnections--;
      DisconnectNamedPipe(pipe);
      CloseHandle(pipe);
      Log(L"cannot start a thread for a connection");
    }
  }
  CloseHandle(instance);
  return 0;
}
