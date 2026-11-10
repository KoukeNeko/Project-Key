// Messages between the text service DLL (in every app) and the engine server,
// over a message-mode named pipe: one request, one response. Little-endian
// fields, length-prefixed UTF-8 strings.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "Controller.h"

namespace ProjectKey::Protocol {

// 2: kShowHint, kHideHint. 3: PanelContent::expanded, KeyEvent::numpad. A text service and server of different versions refuse
// each other's messages rather than misread them.
constexpr uint32_t kVersion = 3;

enum class Request : uint8_t {
  kKeyDown = 1,       // KeyEvent
  kShift = 2,         // bool down
  kCapsLock = 3,
  kOtherModifier = 4,
  kDeactivate = 5,    // focus left: commit
  kReset = 6,         // the app ended the composition
  kClearChoices = 7,  // from the settings app
  kState = 8,         // nothing to do; returns the mode and appearance
  kToggleMode = 9,    // the language bar button
  kShowSettings = 10, // the server starts the settings window, outside any app container
  kReloadModels = 11, // from the settings app, after it downloaded newer models
  kClickCandidate = 12,  // row, index
  kToggleGrid = 13,      // a click on the chevron
  kShowMode = 14,        // a field took focus: show the current mode
};

// What the text service does with the document and windows, in order.
enum class Op : uint8_t {
  kInsertText = 1,
  kSetComposition = 2,
  kShowPanel = 3,
  kHidePanel = 4,
  kIndicator = 5,
  kBeep = 6,
  kShowHint = 7,  // the Tab hint's own window, beside the candidate window
  kHideHint = 8,
};

class Writer {
 public:
  void u8(uint8_t v) { bytes_.push_back(static_cast<char>(v)); }
  void u32(uint32_t v) {
    for (int i = 0; i < 4; i++) u8(static_cast<uint8_t>(v >> (8 * i)));
  }
  void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
  void boolean(bool v) { u8(v ? 1 : 0); }
  void str(const std::string& s) {
    u32(static_cast<uint32_t>(s.size()));
    bytes_ += s;
  }
  void strings(const std::vector<std::string>& v) {
    u32(static_cast<uint32_t>(v.size()));
    for (const std::string& s : v) str(s);
  }
  const std::string& bytes() const { return bytes_; }

 private:
  std::string bytes_;
};

// Reads what Writer wrote; a short or malformed message sets ok() false and reads zeros.
class Reader {
 public:
  Reader(const char* data, size_t size) : data_(data), size_(size) {}
  uint8_t u8() {
    if (pos_ + 1 > size_) return fail(), 0;
    return static_cast<uint8_t>(data_[pos_++]);
  }
  uint32_t u32() {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= static_cast<uint32_t>(u8()) << (8 * i);
    return v;
  }
  int32_t i32() { return static_cast<int32_t>(u32()); }
  bool boolean() { return u8() != 0; }
  std::string str() {
    uint32_t n = u32();
    if (n > size_ - pos_) return fail(), std::string();
    std::string s(data_ + pos_, n);
    pos_ += n;
    return s;
  }
  std::vector<std::string> strings() {
    uint32_t n = u32();
    std::vector<std::string> v;
    for (uint32_t i = 0; i < n && ok_; i++) v.push_back(str());
    return v;
  }
  bool ok() const { return ok_; }
  bool more() const { return pos_ < size_; }

 private:
  void fail() { ok_ = false; pos_ = size_; }
  const char* data_;
  size_t size_;
  size_t pos_ = 0;
  bool ok_ = true;
};

inline void WriteKey(Writer& w, const KeyEvent& e) {
  w.u8(static_cast<uint8_t>(e.key));
  w.str(e.characters);
  w.u8(static_cast<uint8_t>(int{e.shift} | int{e.control} << 1 | int{e.alt} << 2 | int{e.win} << 3 | int{e.capsLock} << 4 |
                            int{e.isP} << 5 | int{e.numpad} << 6));
}

inline KeyEvent ReadKey(Reader& r) {
  KeyEvent e;
  e.key = static_cast<Key>(r.u8());
  e.characters = r.str();
  uint8_t flags = r.u8();
  e.shift = flags & 1;
  e.control = flags & 2;
  e.alt = flags & 4;
  e.win = flags & 8;
  e.capsLock = flags & 16;
  e.isP = flags & 32;
  e.numpad = flags & 64;
  return e;
}

inline void WriteComposition(Writer& w, const Composition& c) {
  w.str(c.text);
  w.u32(c.caret);
  w.u32(c.activeStart);
  w.u32(c.activeLength);
  w.u32(c.replaceBefore);
}

inline Composition ReadComposition(Reader& r) {
  Composition c;
  c.text = r.str();
  c.caret = r.u32();
  c.activeStart = r.u32();
  c.activeLength = r.u32();
  c.replaceBefore = r.u32();
  return c;
}

inline void WritePanel(Writer& w, const PanelContent& p) {
  w.strings(p.segments);
  w.u32(static_cast<uint32_t>(p.doubtful.size()));
  for (bool d : p.doubtful) w.boolean(d);
  w.i32(p.activeSegment);
  w.str(p.pendingReading);
  w.u32(p.caret);
  w.boolean(p.showsProof);
  w.str(p.keyHint);
  w.str(p.replacedText);
  w.u32(static_cast<uint32_t>(p.rows.size()));
  for (const auto& row : p.rows) w.strings(row);
  w.i32(p.selectedRow);
  w.i32(p.selection);
  w.str(p.pageLabel);
  w.boolean(p.expanded);
}

inline PanelContent ReadPanel(Reader& r) {
  PanelContent p;
  p.segments = r.strings();
  uint32_t n = r.u32();
  for (uint32_t i = 0; i < n && r.ok(); i++) p.doubtful.push_back(r.boolean());
  p.doubtful.resize(p.segments.size());
  p.activeSegment = r.i32();
  p.pendingReading = r.str();
  p.caret = r.u32();
  p.showsProof = r.boolean();
  p.keyHint = r.str();
  p.replacedText = r.str();
  uint32_t rows = r.u32();
  for (uint32_t i = 0; i < rows && r.ok(); i++) p.rows.push_back(r.strings());
  p.selectedRow = r.i32();
  p.selection = r.i32();
  p.pageLabel = r.str();
  p.expanded = r.boolean();
  return p;
}

struct Message {
  Request type = Request::kState;
  KeyEvent key;         // kKeyDown
  bool down = false;    // kShift
  std::string context;  // kKeyDown of Tab in 英: text before the caret, for turning English typed there into Zhuyin
  int row = 0, index = 0;  // kClickCandidate
};

inline std::string WriteMessage(const Message& m) {
  Writer w;
  w.u32(kVersion);
  w.u8(static_cast<uint8_t>(m.type));
  WriteKey(w, m.key);
  w.boolean(m.down);
  w.str(m.context);
  w.i32(m.row);
  w.i32(m.index);
  return w.bytes();
}

inline bool ReadMessage(const std::string& bytes, Message& m) {
  Reader r(bytes.data(), bytes.size());
  if (r.u32() != kVersion) return false;
  m.type = static_cast<Request>(r.u8());
  m.key = ReadKey(r);
  m.down = r.boolean();
  m.context = r.str();
  // Absent from apps still running a text service from before clicks, which kVersion must not turn away.
  if (r.more()) {
    m.row = r.i32();
    m.index = r.i32();
  }
  return r.ok();
}

struct Operation {
  Op op = Op::kBeep;
  std::string text;          // kInsertText, kIndicator
  Composition composition;   // kSetComposition
  PanelContent panel;        // kShowPanel, kShowHint
};

// Whether the key was taken, the state the text service shows, and what to do.
struct Reply {
  bool handled = false;
  bool englishMode = false;
  int appearance = 0;  // Setting::kAppearance
  int columns = kGridColumns;  // PanelContent::columns of every panel here; sent last, so a text service without it still reads the reply
  std::vector<Operation> operations;
};

inline std::string WriteReply(const Reply& reply) {
  Writer w;
  w.u32(kVersion);
  w.boolean(reply.handled);
  w.boolean(reply.englishMode);
  w.i32(reply.appearance);
  w.u32(static_cast<uint32_t>(reply.operations.size()));
  for (const Operation& o : reply.operations) {
    w.u8(static_cast<uint8_t>(o.op));
    switch (o.op) {
      case Op::kInsertText:
      case Op::kIndicator: w.str(o.text); break;
      case Op::kSetComposition: WriteComposition(w, o.composition); break;
      case Op::kShowPanel:
      case Op::kShowHint: WritePanel(w, o.panel); break;
      default: break;
    }
  }
  w.i32(reply.columns);
  return w.bytes();
}

inline bool ReadReply(const std::string& bytes, Reply& reply) {
  Reader r(bytes.data(), bytes.size());
  if (r.u32() != kVersion) return false;
  reply.handled = r.boolean();
  reply.englishMode = r.boolean();
  reply.appearance = r.i32();
  uint32_t n = r.u32();
  for (uint32_t i = 0; i < n && r.ok(); i++) {
    Operation o;
    o.op = static_cast<Op>(r.u8());
    switch (o.op) {
      case Op::kInsertText:
      case Op::kIndicator: o.text = r.str(); break;
      case Op::kSetComposition: o.composition = ReadComposition(r); break;
      case Op::kShowPanel:
      case Op::kShowHint: o.panel = ReadPanel(r); break;
      default: break;
    }
    reply.operations.push_back(std::move(o));
  }
  if (r.more()) reply.columns = r.i32();  // absent from a server from before 9 columns
  for (Operation& o : reply.operations) o.panel.columns = reply.columns;
  return r.ok();
}

}  // namespace ProjectKey::Protocol
