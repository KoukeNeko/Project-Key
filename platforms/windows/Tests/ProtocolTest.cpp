// The pipe protocol between the text service and the engine server: a reply
// survives the round trip, and every truncation of it, as a misbehaving peer
// might send, is rejected without reading past the end.
#undef NDEBUG
#include <cassert>
#include <cstdio>

#include "Common/Protocol.h"

using namespace ProjectKey;

int main() {
  Protocol::Reply reply;
  reply.handled = true;
  reply.englishMode = true;
  reply.appearance = 2;
  Protocol::Operation show;
  show.op = Protocol::Op::kShowPanel;
  show.panel.segments = {"我們", "好"};
  show.panel.doubtful = {false, true};
  show.panel.rows = {{"好", "號"}, {"耗"}};
  show.panel.pageLabel = "1/2";
  Protocol::Operation compose;
  compose.op = Protocol::Op::kSetComposition;
  compose.composition.text = "我們好";
  compose.composition.caret = 3;
  Protocol::Operation hint;
  hint.op = Protocol::Op::kShowHint;
  hint.panel.segments = {"ja"};
  hint.panel.replacedText = "我ㄇ";
  hint.panel.keyHint = "Tab";
  reply.operations = {{Protocol::Op::kInsertText, "ㄅ"}, compose, show, hint, {Protocol::Op::kHideHint}};
  std::string bytes = Protocol::WriteReply(reply);
  Protocol::Reply read;
  assert(Protocol::ReadReply(bytes, read) && read.handled && read.englishMode && read.appearance == 2);
  assert(read.operations.size() == 5 && read.operations[0].text == "ㄅ");
  assert(read.operations[1].composition.text == "我們好" && read.operations[1].composition.caret == 3);
  assert(read.operations[2].panel.rows == show.panel.rows && read.operations[2].panel.doubtful == show.panel.doubtful);
  assert(read.operations[3].op == Protocol::Op::kShowHint && read.operations[3].panel.replacedText == "我ㄇ");
  assert(read.operations[4].op == Protocol::Op::kHideHint);
  for (size_t length = 0; length < bytes.size(); length++) {
    Protocol::Reply partial;
    assert(!Protocol::ReadReply(bytes.substr(0, length), partial));
  }
  Protocol::Message message{Protocol::Request::kShift, {}, true, "abc"};
  bytes = Protocol::WriteMessage(message);
  Protocol::Message readMessage;
  assert(Protocol::ReadMessage(bytes, readMessage) && readMessage.type == Protocol::Request::kShift && readMessage.context == "abc");
  for (size_t length = 0; length < bytes.size(); length++) assert(!Protocol::ReadMessage(bytes.substr(0, length), readMessage));

  std::printf("ProtocolTest passed\n");
  return 0;
}
