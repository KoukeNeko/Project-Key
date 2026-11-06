#include "TextService/EngineClient.h"

#include "Common/Win.h"
#include "TextService/Globals.h"

namespace ProjectKey {

namespace {

constexpr DWORD kTimeoutMs = 500;
constexpr DWORD kLaunchWaitMs = 3000;

bool InAppContainer() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  DWORD isAppContainer = 0, size = sizeof(isAppContainer);
  GetTokenInformation(token, TokenIsAppContainer, &isAppContainer, size, &size);
  CloseHandle(token);
  return isAppContainer != 0;
}

// Starts the server next to this DLL. Not from an app container: the server
// would inherit the sandbox. The installer also starts it at sign-in.
bool LaunchServer() {
  if (InAppContainer()) return false;
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
    pipe_ = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
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
