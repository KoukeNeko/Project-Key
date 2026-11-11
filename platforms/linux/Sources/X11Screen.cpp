// Project Key's windows on X11: override-redirect popups that never take
// focus, placed at the caret in screen coordinates.
#include <cairo-xcb.h>
#include <xcb/xcb.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "Screen.h"

namespace ProjectKey {

namespace {

xcb_atom_t Atom(xcb_connection_t* connection, const std::string& name) {
  xcb_intern_atom_reply_t* reply =
      xcb_intern_atom_reply(connection, xcb_intern_atom(connection, 0, name.size(), name.c_str()), nullptr);
  xcb_atom_t atom = reply ? reply->atom : XCB_ATOM_NONE;
  free(reply);
  return atom;
}

// Xft.dpi from the X resources, as desktops set it for HiDPI screens.
float DpiScale(xcb_connection_t* connection, xcb_screen_t* screen) {
  xcb_get_property_reply_t* reply = xcb_get_property_reply(
      connection, xcb_get_property(connection, 0, screen->root, XCB_ATOM_RESOURCE_MANAGER, XCB_ATOM_STRING, 0, 1 << 16), nullptr);
  float scale = 1;
  if (reply) {
    std::string resources(static_cast<const char*>(xcb_get_property_value(reply)), xcb_get_property_value_length(reply));
    size_t at = resources.find("Xft.dpi:");
    if (at != std::string::npos) {
      double dpi = std::strtod(resources.c_str() + at + 8, nullptr);
      if (dpi >= 48) scale = static_cast<float>(dpi / 96);
    }
    free(reply);
  }
  return scale;
}

class X11Window;

class X11Screen final : public Screen {
 public:
  X11Screen(fcitx::EventLoop& loop, xcb_connection_t* connection, int screenNumber);
  ~X11Screen() override;

  // Project Key's window needs the caret on screen; a relative rect is only within the app's window.
  bool reaches(const fcitx::InputContext& ic) const override {
    return !ic.capabilityFlags().test(fcitx::CapabilityFlag::RelativeRect);
  }
  std::unique_ptr<Window> createWindow(std::function<void()> exposed, std::function<void()> moved) override;
  fcitx::Rect caret(const fcitx::InputContext&, const fcitx::Rect& anchor) const override { return anchor; }
  // shortcut: the whole X screen is the work area, so a panel can overlap a dock or straddle monitors; use RandR and _NET_WORKAREA if that shows.
  int width() const override { return screen_->width_in_pixels; }
  int height() const override { return screen_->height_in_pixels; }
  float scale() const override { return scale_; }
  bool composited() override;
  bool blurs() override;
  void sync() override;

 private:
  friend class X11Window;

  xcb_connection_t* connection_;
  xcb_screen_t* screen_ = nullptr;
  xcb_visualtype_t* visual_ = nullptr;
  uint8_t depth_ = 0;
  xcb_colormap_t colormap_ = 0;
  xcb_atom_t compositorAtom_ = 0, blurAtom_ = 0, opacityAtom_ = 0;
  bool argb_ = false;
  float scale_ = 1;
  std::unique_ptr<fcitx::EventSourceIO> io_;
  std::vector<X11Window*> windows_;
};

class X11Window final : public Window {
 public:
  X11Window(X11Screen& screen, std::function<void()> exposed);
  ~X11Window() override;

  void place(const Frame& to) override;
  void map(fcitx::InputContext&) override;
  void unmap() override;
  void setBlur(bool on) override;
  // KWin dims a window's blur with its opacity; a painted alpha would leave the blur at full strength.
  bool setOpacity(float alpha) override {
    if (!blurred) return false;
    uint32_t value = static_cast<uint32_t>(std::lround(alpha * 0xffffffffu));
    xcb_change_property(screen_.connection_, XCB_PROP_MODE_REPLACE, id_, screen_.opacityAtom_, XCB_ATOM_CARDINAL, 32, 1, &value);
    return true;
  }
  cairo_t* beginPaint() override { return cairo_create(surface_); }
  void endPaint(cairo_t* cr) override {
    cairo_destroy(cr);
    cairo_surface_flush(surface_);
    xcb_flush(screen_.connection_);
  }

  X11Screen& screen_;
  xcb_window_t id_ = 0;
  cairo_surface_t* surface_ = nullptr;
  std::function<void()> exposed_;
};

X11Screen::X11Screen(fcitx::EventLoop& loop, xcb_connection_t* connection, int screenNumber) : connection_(connection) {
  xcb_screen_iterator_t screens = xcb_setup_roots_iterator(xcb_get_setup(connection_));
  for (int i = 0; i < screenNumber && screens.rem; i++) xcb_screen_next(&screens);
  screen_ = screens.data;

  // A 32-bit visual lets a compositor show the rounded corners; the root visual otherwise.
  for (auto depths = xcb_screen_allowed_depths_iterator(screen_); depths.rem; xcb_depth_next(&depths)) {
    for (auto visuals = xcb_depth_visuals_iterator(depths.data); visuals.rem; xcb_visualtype_next(&visuals)) {
      if (depths.data->depth == 32 && visuals.data->_class == XCB_VISUAL_CLASS_TRUE_COLOR && !argb_) {
        visual_ = visuals.data;
        depth_ = 32;
        argb_ = true;
      }
      if (!visual_ && visuals.data->visual_id == screen_->root_visual) {
        visual_ = visuals.data;
        depth_ = screen_->root_depth;
      }
    }
  }
  if (argb_) {
    colormap_ = xcb_generate_id(connection_);
    xcb_create_colormap(connection_, XCB_COLORMAP_ALLOC_NONE, colormap_, screen_->root, visual_->visual_id);
  } else {
    colormap_ = screen_->default_colormap;
  }
  compositorAtom_ = Atom(connection_, "_NET_WM_CM_S" + std::to_string(screenNumber));
  blurAtom_ = Atom(connection_, "_KDE_NET_WM_BLUR_BEHIND_REGION");
  opacityAtom_ = Atom(connection_, "_NET_WM_WINDOW_OPACITY");
  scale_ = DpiScale(connection_, screen_);

  io_ = loop.addIOEvent(xcb_get_file_descriptor(connection_), fcitx::IOEventFlag::In, [this](fcitx::EventSourceIO* source, int, fcitx::IOEventFlags) {
    sync();
    // The X server went away: its socket stays readable with nothing to read, which would wake
    // this every time round the loop.
    if (xcb_connection_has_error(connection_)) source->setEnabled(false);
    return true;
  });
  xcb_flush(connection_);
}

X11Screen::~X11Screen() {
  io_.reset();
  xcb_disconnect(connection_);
}

std::unique_ptr<Window> X11Screen::createWindow(std::function<void()> exposed, std::function<void()>) {
  return std::make_unique<X11Window>(*this, std::move(exposed));
}

void X11Screen::sync() {
  while (xcb_generic_event_t* event = xcb_poll_for_event(connection_)) {
    uint8_t type = event->response_type & ~0x80;
    if (type == XCB_EXPOSE) {
      xcb_window_t id = reinterpret_cast<xcb_expose_event_t*>(event)->window;
      for (X11Window* window : windows_) {
        if (window->id_ == id) window->exposed_();
      }
    } else if (type == XCB_BUTTON_RELEASE) {
      auto* button = reinterpret_cast<xcb_button_release_event_t*>(event);
      for (X11Window* window : windows_) {
        if (window->id_ == button->event && button->detail == XCB_BUTTON_INDEX_1 && window->clicked) window->clicked(button->event_x, button->event_y);
      }
    } else if (type == XCB_MOTION_NOTIFY) {
      auto* motion = reinterpret_cast<xcb_motion_notify_event_t*>(event);
      for (X11Window* window : windows_) {
        if (window->id_ == motion->event && window->moved) window->moved(motion->event_x, motion->event_y);
      }
    } else if (type == XCB_LEAVE_NOTIFY) {
      auto* leave = reinterpret_cast<xcb_leave_notify_event_t*>(event);
      for (X11Window* window : windows_) {
        if (window->id_ == leave->event && window->moved) window->moved(-1, -1);
      }
    }
    free(event);
  }
}

bool X11Screen::composited() {
  if (!argb_) return false;
  xcb_get_selection_owner_reply_t* reply =
      xcb_get_selection_owner_reply(connection_, xcb_get_selection_owner(connection_, compositorAtom_), nullptr);
  bool owned = reply && reply->owner != XCB_NONE;
  free(reply);
  return owned;
}

bool X11Screen::blurs() {
  if (!composited()) return false;
  // KWin's blur effect, while it is on, sets this property on the root window.
  xcb_get_property_reply_t* reply =
      xcb_get_property_reply(connection_, xcb_get_property(connection_, 0, screen_->root, blurAtom_, XCB_GET_PROPERTY_TYPE_ANY, 0, 0), nullptr);
  bool offered = reply && reply->type != XCB_NONE;
  free(reply);
  return offered;
}

X11Window::X11Window(X11Screen& screen, std::function<void()> exposed) : screen_(screen), exposed_(std::move(exposed)) {
  xcb_connection_t* connection = screen_.connection_;
  id_ = xcb_generate_id(connection);
  uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP;
  uint32_t values[] = {0, 0, 1, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                           XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_LEAVE_WINDOW, screen_.colormap_};
  xcb_create_window(connection, screen_.depth_, id_, screen_.screen_->root, 0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    screen_.visual_->visual_id, mask, values);
  // Compositors treat it as a popup: no decoration, focus or taskbar entry.
  xcb_atom_t type = Atom(connection, "_NET_WM_WINDOW_TYPE"), popup = Atom(connection, "_NET_WM_WINDOW_TYPE_POPUP_MENU");
  xcb_change_property(connection, XCB_PROP_MODE_REPLACE, id_, type, XCB_ATOM_ATOM, 32, 1, &popup);
  surface_ = cairo_xcb_surface_create(connection, id_, screen_.visual_, 1, 1);
  frame.width = frame.height = 1;
  screen_.windows_.push_back(this);
}

X11Window::~X11Window() {
  std::erase(screen_.windows_, this);
  cairo_surface_destroy(surface_);
  xcb_destroy_window(screen_.connection_, id_);
  xcb_flush(screen_.connection_);
}

void X11Window::place(const Frame& to) {
  int width = std::max(to.width, 1), height = std::max(to.height, 1);
  uint32_t values[] = {static_cast<uint32_t>(to.x), static_cast<uint32_t>(to.y), static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                       XCB_STACK_MODE_ABOVE};
  xcb_configure_window(screen_.connection_, id_,
                       XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
                           XCB_CONFIG_WINDOW_STACK_MODE,
                       values);
  bool resized = width != frame.width || height != frame.height;
  if (resized) cairo_xcb_surface_set_size(surface_, width, height);
  frame = {to.x, to.y, width, height};
  if (resized && blurred) setBlur(true);
}

void X11Window::map(fcitx::InputContext&) {
  if (mapped) return;
  xcb_map_window(screen_.connection_, id_);
  mapped = true;
}

void X11Window::unmap() {
  if (!mapped) return;
  xcb_unmap_window(screen_.connection_, id_);
  mapped = false;
  xcb_flush(screen_.connection_);
}

// The region KWin blurs: the window's rounded rectangle, one row of pixels at
// a time through the corners, so no blur shows outside them.
void X11Window::setBlur(bool on) {
  bool was = blurred;
  blurred = on;
  if (!on) {
    xcb_delete_property(screen_.connection_, id_, screen_.blurAtom_);
    // Without blur the alpha is painted, so a fade's last opacity must not stay on the window.
    if (was) xcb_delete_property(screen_.connection_, id_, screen_.opacityAtom_);
    return;
  }
  int radius = static_cast<int>(std::lround(kWindowRadius * screen_.scale_));
  radius = std::min({radius, frame.width / 2, frame.height / 2});
  std::vector<uint32_t> rects;
  auto add = [&](int x, int y, int w, int h) {
    rects.insert(rects.end(), {static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(w), static_cast<uint32_t>(h)});
  };
  for (int y = 0; y < radius; y++) {
    double dy = radius - y - 0.5;
    int inset = static_cast<int>(std::lround(radius - std::sqrt(radius * radius - dy * dy)));
    add(inset, y, frame.width - 2 * inset, 1);
    add(inset, frame.height - 1 - y, frame.width - 2 * inset, 1);
  }
  add(0, radius, frame.width, frame.height - 2 * radius);
  xcb_change_property(screen_.connection_, XCB_PROP_MODE_REPLACE, id_, screen_.blurAtom_, XCB_ATOM_CARDINAL, 32, rects.size(), rects.data());
}

}  // namespace

std::unique_ptr<Screen> OpenX11Screen(fcitx::EventLoop& loop) {
  int screenNumber = 0;
  xcb_connection_t* connection = xcb_connect(nullptr, &screenNumber);
  if (xcb_connection_has_error(connection)) {
    xcb_disconnect(connection);
    return nullptr;
  }
  return std::make_unique<X11Screen>(loop, connection, screenNumber);
}

}  // namespace ProjectKey
