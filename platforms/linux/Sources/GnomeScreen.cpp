// Project Key's windows on GNOME (Wayland). Mutter gives an input method no way to
// place a window at the caret, so a GNOME Shell extension shows them
// (gnome-shell/project-key@koukeneko.github.io): each window is painted into memory,
// written to a file in XDG_RUNTIME_DIR and handed to the extension with its place on
// screen. The extension sends back where the focused window and its monitor's work
// area are, which turn an app's caret (relative to its window) into those coordinates.
#include <cairo.h>
#include <dbus_public.h>
#include <fcitx-utils/dbus/bus.h>
#include <fcitx-utils/dbus/objectvtable.h>
#include <fcitx-utils/dbus/servicewatcher.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>
#include <fcntl.h>
#include <unistd.h>
#include <xcb/xcb.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "Screen.h"

namespace ProjectKey {

namespace {

constexpr char kExtension[] = "org.projectkey.Panel";

// The extension's interfaces, version 3: Geometry also says how wide the stage is when the
// focused window is an X11 one, which turns its caret into stage units. An extension from
// before an update, until the next login, has an older version, so the two do not meet and
// Fcitx5's panel stands in (problem() says to log in).
constexpr char kExtensionInterface[] = "org.projectkey.Panel3";
constexpr char kHostInterface[] = "org.projectkey.PanelHost3";

// Where the extension says the focused window and its monitor's work area are, in
// GNOME's logical coordinates, and the scale that makes them pixels: the monitor's own,
// such as 1.25 at 125%.
struct Geometry {
  int originX = 0, originY = 0;
  int areaX = 0, areaY = 0, areaWidth = 0, areaHeight = 0;
  double scale = 1;
  // The stage's width in logical pixels while the focused window is an X11 one, else 0.
  int x11StageWidth = 0;
};

class GnomeScreen;

// What the extension calls on Fcitx5's bus name, at /projectkey/panel.
class PanelHost : public fcitx::dbus::ObjectVTable<PanelHost> {
 public:
  explicit PanelHost(GnomeScreen& screen) : screen_(screen) {}
  void geometry(int32_t originX, int32_t originY, int32_t areaX, int32_t areaY, int32_t areaWidth, int32_t areaHeight,
                double scale, int32_t x11StageWidth);
  void clicked(const std::string& id, int32_t x, int32_t y);  // in pixels from the window's corner
  void moved(const std::string& id, int32_t x, int32_t y);    // likewise; (-1, -1) as the pointer leaves

 private:
  GnomeScreen& screen_;
  FCITX_OBJECT_VTABLE_METHOD(geometry, "Geometry", "iiiiiidi", "");
  FCITX_OBJECT_VTABLE_METHOD(clicked, "Clicked", "sii", "");
  FCITX_OBJECT_VTABLE_METHOD(moved, "Moved", "sii", "");
};

class GnomeWindow;

class GnomeScreen final : public Screen {
 public:
  explicit GnomeScreen(fcitx::dbus::Bus* bus) : bus_(bus), host_(*this), watcher_(*bus) {
    bus_->addObjectVTable("/projectkey/panel", kHostInterface, host_);
    entry_ = watcher_.watchService(kExtension, [this](const std::string&, const std::string&, const std::string& owner) {
      known_ = false;
      if (!owner.empty()) call("Refresh").send();  // the geometry, for this addon loaded after it last went out
    });
  }

  ~GnomeScreen() override { xcb_disconnect(x11_); }
  // Until the extension has said where things are, Fcitx5's panel stands in.
  bool reaches(const fcitx::InputContext&) const override { return known_; }
  std::unique_ptr<Window> createWindow(std::function<void()>, std::function<void()>) override;
  // In pixels from the work area's corner. Apps GNOME Shell relays (IBus) give the caret
  // on screen; apps on Fcitx5's own D-Bus frontend (GTK and Qt under Wayland) give it in
  // their window, which the extension says is the focused one. X11 apps (XWayland) give it
  // in X pixels, xScale_ to a stage unit.
  fcitx::Rect caret(const fcitx::InputContext& ic, const fcitx::Rect& anchor) const override {
    double s = geometry_.scale;
    bool relative = ic.capabilityFlags().test(fcitx::CapabilityFlag::RelativeRect);
    double unit = relative ? 1 : xScale_;
    int originX = relative ? geometry_.originX : 0, originY = relative ? geometry_.originY : 0;
    int left = static_cast<int>(std::lround((originX + anchor.left() / unit - geometry_.areaX) * s));
    int top = static_cast<int>(std::lround((originY + anchor.top() / unit - geometry_.areaY) * s));
    return fcitx::Rect(left, top, left + static_cast<int>(std::lround(anchor.width() / unit * s)),
                       top + static_cast<int>(std::lround(anchor.height() / unit * s)));
  }
  int width() const override { return static_cast<int>(std::lround(geometry_.areaWidth * geometry_.scale)); }
  int height() const override { return static_cast<int>(std::lround(geometry_.areaHeight * geometry_.scale)); }
  float scale() const override { return static_cast<float>(geometry_.scale); }
  bool composited() override { return true; }
  bool blurs() override { return true; }  // Shell.BlurEffect, in the extension
  std::string problem() const override {
    if (known_) return {};
    return "GNOME Shell 擴充功能「Project Key」沒有在執行：在「擴充功能」App 開啟它"
           "（或 gnome-extensions enable project-key@koukeneko.github.io），再重新登入";
  }

  void setGeometry(const Geometry& geometry) {
    known_ = geometry.areaWidth > 0 && geometry.areaHeight > 0 && geometry.scale > 0;
    if (!known_) return;
    geometry_ = geometry;
    double rootWidth = geometry.x11StageWidth > 0 ? xRootWidth() : 0;
    xScale_ = rootWidth > 0 ? rootWidth / geometry.x11StageWidth : 1;
  }
  fcitx::dbus::Message call(const char* member) {
    return bus_->createMethodCall(kExtension, "/org/projectkey/Panel", kExtensionInterface, member);
  }
  const Geometry& geometry() const { return geometry_; }
  int nextId() { return ++windows_; }
  std::vector<GnomeWindow*> shown;  // for finding a click's window

 private:
  // The X screen's width, which Mutter makes the stage's times an integer (XWayland's scale),
  // such as 2 at 125%; 0 where X cannot be reached.
  double xRootWidth() {
    if (!x11_ || xcb_connection_has_error(x11_)) {
      xcb_disconnect(x11_);
      x11_ = xcb_connect(nullptr, nullptr);
    }
    if (xcb_connection_has_error(x11_)) return 0;
    xcb_window_t root = xcb_setup_roots_iterator(xcb_get_setup(x11_)).data->root;
    xcb_get_geometry_reply_t* reply = xcb_get_geometry_reply(x11_, xcb_get_geometry(x11_, root), nullptr);
    double width = reply ? reply->width : 0;
    free(reply);
    return width;
  }

  fcitx::dbus::Bus* bus_;
  PanelHost host_;
  fcitx::dbus::ServiceWatcher watcher_;
  std::unique_ptr<fcitx::dbus::ServiceWatcherEntry> entry_;
  Geometry geometry_;
  double xScale_ = 1;  // X pixels per stage unit, for the focused window
  xcb_connection_t* x11_ = nullptr;
  bool known_ = false;
  int windows_ = 0;
};

void PanelHost::geometry(int32_t originX, int32_t originY, int32_t areaX, int32_t areaY, int32_t areaWidth, int32_t areaHeight,
                         double scale, int32_t x11StageWidth) {
  screen_.setGeometry({originX, originY, areaX, areaY, areaWidth, areaHeight, scale, x11StageWidth});
}

class GnomeWindow final : public Window {
 public:
  explicit GnomeWindow(GnomeScreen& screen) : screen_(screen), id_("w" + std::to_string(screen.nextId())) {
    frame.width = frame.height = 1;
    std::string base = std::string(std::getenv("XDG_RUNTIME_DIR")) + "/project-key-" + std::to_string(getpid()) + "-" + id_;
    files_[0] = base + "-a.raw";
    files_[1] = base + "-b.raw";
    screen_.shown.push_back(this);
  }
  ~GnomeWindow() override {
    std::erase(screen_.shown, this);
    unmap();
    if (image_) cairo_surface_destroy(image_);
    for (const std::string& file : files_) unlink(file.c_str());
  }

  void place(const Frame& to) override { frame = {to.x, to.y, std::max(to.width, 1), std::max(to.height, 1)}; }
  void map(fcitx::InputContext&) override {  // the next frame shows it
    if (!mapped && clicked) {
      auto message = screen_.call("Reactive");
      message << id_;
      message.send();
    }
    mapped = true;
  }
  const std::string& id() const { return id_; }
  void unmap() override {
    if (!mapped) return;
    mapped = false;
    auto message = screen_.call("Hide");
    message << id_;
    message.send();
  }
  void setBlur(bool on) override {
    blurred = on;  // the next frame carries it
    // Without blur the alpha is painted, so a fade's last opacity must not stay on the actor.
    if (!on && opacity_ != 255) {
      opacity_ = 255;
      auto message = screen_.call("Opacity");
      message << id_ << opacity_;
      message.send();
    }
  }
  // The extension fades its actor, blur and all; a painted alpha would leave the blur at full strength.
  bool setOpacity(float alpha) override {
    if (!blurred) return false;
    int32_t opacity = static_cast<int32_t>(std::lround(alpha * 255));
    if (opacity == opacity_) return true;
    opacity_ = opacity;
    auto message = screen_.call("Opacity");
    message << id_ << opacity;
    message.send();
    return true;
  }

  cairo_t* beginPaint() override {
    if (!mapped) return nullptr;
    if (!image_ || cairo_image_surface_get_width(image_) != frame.width || cairo_image_surface_get_height(image_) != frame.height) {
      if (image_) cairo_surface_destroy(image_);
      image_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, frame.width, frame.height);
    }
    return cairo_create(image_);
  }

  void endPaint(cairo_t* cr) override {
    cairo_destroy(cr);
    cairo_surface_flush(image_);
    // Two files in turn, so the extension never reads one being written.
    const std::string& file = files_[next_];
    next_ ^= 1;
    if (!write(file)) return;
    const Geometry& g = screen_.geometry();
    auto message = screen_.call("Show");
    // The extension moves the place onto the monitor's pixel grid.
    message << id_ << frame.x / g.scale + g.areaX << frame.y / g.scale + g.areaY
            << static_cast<int32_t>(frame.width) << static_cast<int32_t>(frame.height) << file << blurred
            << static_cast<int32_t>(std::lround(kWindowRadius * g.scale));
    message.send();
  }

 private:
  // The image's rows, without the padding Cairo may put after each.
  bool write(const std::string& file) const {
    int fd = open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    const unsigned char* data = cairo_image_surface_get_data(image_);
    int stride = cairo_image_surface_get_stride(image_);
    size_t row = static_cast<size_t>(frame.width) * 4;
    bool ok = true;
    for (int y = 0; y < frame.height && ok; y++) ok = ::write(fd, data + static_cast<size_t>(y) * stride, row) == static_cast<ssize_t>(row);
    close(fd);
    return ok;
  }

  GnomeScreen& screen_;
  std::string id_;
  std::string files_[2];
  int next_ = 0;
  int32_t opacity_ = 255;  // what the extension was last told
  cairo_surface_t* image_ = nullptr;
};

void PanelHost::clicked(const std::string& id, int32_t x, int32_t y) {
  for (GnomeWindow* window : screen_.shown) {
    if (window->id() == id && window->mapped && window->clicked) window->clicked(x, y);
  }
}

void PanelHost::moved(const std::string& id, int32_t x, int32_t y) {
  for (GnomeWindow* window : screen_.shown) {
    if (window->id() == id && window->mapped && window->moved) window->moved(x, y);
  }
}

std::unique_ptr<Window> GnomeScreen::createWindow(std::function<void()>, std::function<void()>) {
  return std::make_unique<GnomeWindow>(*this);
}

}  // namespace

std::unique_ptr<Screen> OpenGnomeScreen(fcitx::Instance* instance) {
  fcitx::AddonInstance* dbus = instance->addonManager().addon("dbus");
  fcitx::dbus::Bus* bus = dbus ? dbus->call<fcitx::IDBusModule::bus>() : nullptr;
  // The images go to private files there; /tmp would let another user plant a link under their name.
  if (!std::getenv("XDG_RUNTIME_DIR")) return nullptr;
  return bus ? std::make_unique<GnomeScreen>(bus) : nullptr;
}

}  // namespace ProjectKey
