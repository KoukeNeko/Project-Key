// Where Project Key's windows go (Panel.cpp draws them). On X11 they are
// override-redirect windows placed at the caret (X11Screen.cpp). On Wayland the
// compositor places them at the caret itself, as input method popups
// (WaylandScreen.cpp); on GNOME, which has none, a GNOME Shell extension shows
// them (GnomeScreen.cpp).
#pragma once

#include <cairo.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/rect.h>
#include <fcitx/inputcontext.h>

#include <functional>
#include <memory>

namespace fcitx {
class Instance;
}

namespace ProjectKey {

// The windows' corners (platforms/windows/Sources/Common/Layout.h), which a blur follows.
constexpr float kWindowRadius = 8;

// In pixels, from where Screen::caret says.
struct Frame {
  int x = 0, y = 0, width = 0, height = 0;
};

class Window {
 public:
  virtual ~Window() = default;
  virtual void place(const Frame& frame) = 0;
  // Shows the window at ic's caret, with what it paints next.
  virtual void map(fcitx::InputContext& ic) = 0;
  virtual void unmap() = 0;
  // Blurs what is behind the window's rounded rectangle.
  virtual void setBlur(bool on) = 0;
  // A context covering the window from the frame's origin; nullptr when it
  // cannot be painted now, in which case it calls exposed once it can.
  virtual cairo_t* beginPaint() = 0;
  virtual void endPaint(cairo_t* cr) = 0;
  // The compositor put the window above the caret, as the screen ends below it.
  virtual bool above() const { return false; }

  Frame frame;
  bool mapped = false;
  bool blurred = false;
};

class Screen {
 public:
  virtual ~Screen() = default;
  // Whether a window can go at ic's caret.
  virtual bool reaches(const fcitx::InputContext& ic) const = 0;
  // exposed: the window needs painting again; moved: placing again (above() changed).
  virtual std::unique_ptr<Window> createWindow(std::function<void()> exposed, std::function<void()> moved) = 0;
  // What frames are measured from, given the caret in the context's coordinates:
  // the screen's origin on X11, the caret on Wayland, where the compositor finds it.
  virtual fcitx::Rect caret(const fcitx::InputContext& ic, const fcitx::Rect& anchor) const = 0;
  // The area windows stay within, in the same coordinates.
  virtual int width() const = 0;
  virtual int height() const = 0;
  virtual float scale() const = 0;
  // Whether a window's corners and alpha show through.
  virtual bool composited() = 0;
  // Whether the compositor blurs behind a window that asks.
  virtual bool blurs() = 0;
  // Handles what the display sent, so a window shows at once.
  virtual void sync() {}
  // Whether windows slide and resize in steps; each step is a frame to deliver.
  virtual bool animates() const { return true; }
};

std::unique_ptr<Screen> OpenX11Screen(fcitx::EventLoop& loop);
std::unique_ptr<Screen> OpenWaylandScreen(fcitx::Instance* instance);
std::unique_ptr<Screen> OpenGnomeScreen(fcitx::Instance* instance);

}  // namespace ProjectKey
