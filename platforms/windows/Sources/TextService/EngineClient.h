// The text service's connection to the engine server. Requests block the
// app's thread, so each waits at most a fraction of a second; without a
// server, keys go to the app as if no input method were active.
#pragma once

#include <windows.h>

#include <optional>

#include "Common/Protocol.h"

namespace ProjectKey {

class EngineClient {
 public:
  ~EngineClient() { disconnect(); }
  std::optional<Protocol::Reply> send(const Protocol::Message& message);
  void disconnect();

 private:
  bool connect();
  std::optional<std::string> transact(const std::string& request);

  HANDLE pipe_ = INVALID_HANDLE_VALUE;
  HANDLE event_ = nullptr;
  DWORD lastLaunch_ = 0;
};

}  // namespace ProjectKey
