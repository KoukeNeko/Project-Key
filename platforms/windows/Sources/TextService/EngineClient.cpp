#include "TextService/EngineClient.h"

#include "Common/Win.h"
#include "TextService/Globals.h"

namespace ProjectKey {

namespace {

constexpr DWORD kTimeoutMs = 500;
constexpr DWORD kLaunchWaitMs = 3000;
constexpr size_t kMaxReply = 1 << 20;  // far beyond any real reply; a bigger one is not the server's

bool InAppContainer() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  DWORD isAppContainer = 0, size = sizeof(isAppContainer);
  GetTokenInformation(token, TokenIsAppContainer, &isAppContainer, size, &size);
  CloseHandle(token);
  return isAppContainer != 0;
}

// Whether this process runs as the user signs in: medium integrity, not elevated.
bool PlainUser() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  TOKEN_ELEVATION elevation{};
  DWORD size = sizeof(elevation);
  bool plain = GetTokenInformation(token, TokenElevation, &elevation, size, &size) && !elevation.TokenIsElevated;
  BYTE label[64];
  if (plain && GetTokenInformation(token, TokenIntegrityLevel, label, sizeof(label), &size)) {
    PSID sid = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(label)->Label.Sid;
    DWORD rid = *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
    plain = rid == SECURITY_MANDATORY_MEDIUM_RID;
  } else {
    plain = false;
  }
  CloseHandle(token);
  return plain;
}

// Starts the server next to this DLL. Only from a process with the user's plain
// token: from an app container it would inherit the sandbox, from an elevated or
// low-integrity app that app's rights. The installer also starts it at sign-in.
bool LaunchServer() {
  if (InAppContainer() || !PlainUser()) return false;
  std::wstring path = ModuleDirectory(gInstance) + kServerExe;
  STARTUPINFOW startup{sizeof(startup)};
  PROCESS_INFORMATION process{};
  std::wstring command = L"\"" + path + L"\"";
  if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr,
                      nullptr, &startup, &process)) {
    return false;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

}  // namespace

bool EngineClient::connect() {
  if (pipe_ != INVALID_HANDLE_VALUE) return true;
  std::wstring name = PipeName();
  for (int attempt = 0; attempt < 2; attempt++) {
    // The server may identify this client but not act as it.
    pipe_ = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                        FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
    if (pipe_ != INVALID_HANDLE_VALUE) break;
    DWORD error = GetLastError();
    if (error == ERROR_PIPE_BUSY) {
      if (!WaitNamedPipeW(name.c_str(), kTimeoutMs)) return false;
      continue;
    }
    // No server: start one, at most every few seconds, and wait for its pipe.
    if (error != ERROR_FILE_NOT_FOUND || attempt > 0) return false;
    DWORD now = GetTickCount();
    if (lastLaunch_ && now - lastLaunch_ < kLaunchWaitMs) return false;
    lastLaunch_ = now;
    if (!LaunchServer()) return false;
    for (DWORD waited = 0; waited < kLaunchWaitMs && !WaitNamedPipeW(name.c_str(), 50); waited += 50) Sleep(50);
  }
  if (pipe_ == INVALID_HANDLE_VALUE) return false;
  // Keys go only to the installed server: any process of this user could serve a pipe of
  // that name. A sandbox that cannot read another process's image (an app container)
  // has only the pipe's own access rules.
  ULONG server = 0;
  std::wstring image = GetNamedPipeServerProcessId(pipe_, &server) ? ProcessImage(server) : L"?";
  if (!image.empty() && !SamePath(image, ModuleDirectory(gInstance) + kServerExe)) {
    disconnect();
    return false;
  }
  newSession_ = true;
  DWORD mode = PIPE_READMODE_MESSAGE;
  SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr);
  if (!event_) event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  return true;
}

void EngineClient::disconnect() {
  if (pipe_ != INVALID_HANDLE_VALUE) CloseHandle(pipe_);
  pipe_ = INVALID_HANDLE_VALUE;
}

std::optional<std::string> EngineClient::transact(const std::string& request) {
  enum Result { kDone, kMore, kFailed };
  // Waits for an overlapped operation, at most kTimeoutMs.
  auto finish = [&](BOOL ok, OVERLAPPED& overlapped, DWORD& transferred) {
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
      if (WaitForSingleObject(event_, kTimeoutMs) != WAIT_OBJECT_0) {
        CancelIoEx(pipe_, &overlapped);
        GetOverlappedResult(pipe_, &overlapped, &transferred, TRUE);
        return kFailed;
      }
      ok = GetOverlappedResult(pipe_, &overlapped, &transferred, FALSE);
    }
    if (ok) return kDone;
    return GetLastError() == ERROR_MORE_DATA ? kMore : kFailed;
  };
  constexpr DWORD kChunk = 16384;
  std::string reply(kChunk, '\0');
  OVERLAPPED overlapped{};
  overlapped.hEvent = event_;
  ResetEvent(event_);
  DWORD read = 0;
  Result result = finish(TransactNamedPipe(pipe_, const_cast<char*>(request.data()), static_cast<DWORD>(request.size()),
                                           reply.data(), kChunk, &read, &overlapped),
                         overlapped, read);
  size_t total = read;
  // A reply longer than the buffer: read the rest of the message.
  while (result == kMore) {
    if (total + kChunk > kMaxReply) {
      CancelIoEx(pipe_, nullptr);
      return std::nullopt;
    }
    reply.resize(total + kChunk);
    overlapped = {};
    overlapped.hEvent = event_;
    ResetEvent(event_);
    read = 0;
    result = finish(ReadFile(pipe_, reply.data() + total, kChunk, &read, &overlapped), overlapped, read);
    total += read;
  }
  if (result == kFailed) return std::nullopt;
  reply.resize(total);
  return reply;
}

std::optional<Protocol::Reply> EngineClient::send(const Protocol::Message& message) {
  std::string request = Protocol::WriteMessage(message);
  // A server restarted since the last request drops the old connection: reconnect once.
  for (int attempt = 0; attempt < 2; attempt++) {
    if (!connect()) return std::nullopt;
    std::optional<std::string> bytes = transact(request);
    if (bytes) {
      Protocol::Reply reply;
      if (Protocol::ReadReply(*bytes, reply)) return reply;
    }
    disconnect();
  }
  return std::nullopt;
}

}  // namespace ProjectKey
