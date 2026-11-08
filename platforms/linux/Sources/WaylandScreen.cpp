// Project Key's windows on Wayland: input method popups (input-method-unstable-v2),
// which the compositor (sway, Hyprland and the other wlroots ones) places at the
// text's caret, below it or, where the screen ends, above. A popup belongs to the
// input method object Fcitx5 bound on its own Wayland connection, so the windows
// are made on that connection, as Fcitx5's own panel is. Contexts from other
// frontends (Fcitx5's D-Bus one, which GTK_IM_MODULE=fcitx uses) and KWin's
// input method (v1) get Fcitx5's panel instead.
#include <fcitx-utils/handlertable.h>
#include <fcitx-utils/signals.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland_public.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "Screen.h"
#include "ext-background-effect-v1-client-protocol.h"
#include "input-method-unstable-v2-client-protocol.h"

namespace fcitx::wayland {
class ZwpInputMethodV2;
}

// From Fcitx5's frontend/waylandim/waylandim_public.h, which it does not install.
FCITX_ADDON_DECLARE_FUNCTION(WaylandIMModule, getInputMethodV2, fcitx::wayland::ZwpInputMethodV2*(fcitx::InputContext*));

namespace ProjectKey {

namespace {

// fcitx::wayland::ZwpInputMethodV2 as Fcitx5's fcitx-wayland library lays it
// out (src/lib/fcitx-wayland/input-method-v2/zwp_input_method_v2.h, generated
// and unchanged through 5.1). Fcitx5 installs neither that header nor the
// library, so the proxy is read from where the class keeps it, then checked
// against what libwayland knows of it.
struct InputMethodV2Layout {
  fcitx::Signal<void()> activate, deactivate;
  fcitx::Signal<void(const char*, uint32_t, uint32_t)> surroundingText;
  fcitx::Signal<void(uint32_t)> textChangeCause;
  fcitx::Signal<void(uint32_t, uint32_t)> contentType;
  fcitx::Signal<void()> done, unavailable;
  uint32_t version;
  void* userData;
  zwp_input_method_v2* proxy;
};

zwp_input_method_v2* Proxy(fcitx::wayland::ZwpInputMethodV2* im) {
  if (!im) return nullptr;
  const auto* layout = reinterpret_cast<const InputMethodV2Layout*>(im);
  if (layout->version != 1 || !layout->proxy) return nullptr;  // Fcitx5 binds version 1
  auto* proxy = reinterpret_cast<wl_proxy*>(layout->proxy);
  if (wl_proxy_get_user_data(proxy) != im || std::strcmp(wl_proxy_get_class(proxy), zwp_input_method_v2_interface.name) != 0) {
    return nullptr;
  }
  return layout->proxy;
}

// What Project Key binds on one of Fcitx5's connections. The registry stays on
// the default queue, which Fcitx5 dispatches, so the globals arrive with its events.
struct Globals {
  wl_display* display = nullptr;
  fcitx::FocusGroup* group = nullptr;  // the input contexts on this connection
  wl_registry* registry = nullptr;
  wl_compositor* compositor = nullptr;
  uint32_t compositorVersion = 0;
  wl_shm* shm = nullptr;
  ext_background_effect_manager_v1* effects = nullptr;  // KWin 6.7, Mutter 51, niri 26.04
  bool blur = false;                                    // effects says it can blur
};

// Hyprland blurs input method popups itself while its decoration:blur:input_methods is on;
// it has no protocol for asking, so its socket (hyprctl's) is asked whether it is on.
bool HyprlandBlursInputMethods() {
  const char* signature = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
  const char* runtime = std::getenv("XDG_RUNTIME_DIR");
  if (!signature || !runtime) return false;
  std::string path = std::string(runtime) + "/hypr/" + signature + "/.socket.sock";
  sockaddr_un address{};
  if (path.size() >= sizeof(address.sun_path)) return false;
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  auto on = [&](const std::string& option) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    std::string request = "j/getoption " + option, reply;
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
        write(fd, request.data(), request.size()) == static_cast<ssize_t>(request.size())) {
      char buffer[256];
      for (ssize_t n; (n = read(fd, buffer, sizeof(buffer))) > 0;) reply.append(buffer, n);
    }
    close(fd);
    // {"option": "...", "int": 1, "set": true}
    size_t value = reply.find("\"int\":");
    return value != std::string::npos && reply.find_first_not_of(' ', value + 6) == reply.find('1', value + 6);
  };
  return on("decoration:blur:enabled") && on("decoration:blur:input_methods");
}

class WaylandWindow;

class WaylandScreen final : public Screen {
 public:
  WaylandScreen(fcitx::AddonInstance* wayland, fcitx::AddonInstance* waylandim);
  ~WaylandScreen() override;

  bool reaches(const fcitx::InputContext& ic) const override {
    zwp_input_method_v2* im = inputMethod(const_cast<fcitx::InputContext&>(ic));
    return im && globals(ic);
  }
  std::unique_ptr<Window> createWindow(std::function<void()> exposed, std::function<void()> moved) override;
  // The compositor knows the caret; frames start from it. COSMIC puts a popup's corner at the
  // caret's top instead of below it, so frames start a caret's height down.
  fcitx::Rect caret(const fcitx::InputContext&, const fcitx::Rect&) const override { return fcitx::Rect(0, 0, 0, caretHeight_); }
  // And keeps the popups on screen.
  int width() const override { return INT_MAX / 2; }
  int height() const override { return INT_MAX / 2; }
  // Frames are in surface coordinates; each window scales its buffers.
  float scale() const override { return 1; }
  bool composited() override { return true; }
  bool blurs() override {
    return std::any_of(connections_.begin(), connections_.end(), [](const auto& connection) { return connection.second.blur; }) ||
           HyprlandBlursInputMethods();
  }

  zwp_input_method_v2* inputMethod(fcitx::InputContext& ic) const {
    if (!waylandim_ || ic.frontendName() != "wayland_v2") return nullptr;
    // A Fcitx5 whose waylandim does not register the call throws; Fcitx5's panel stands in there.
    try {
      return Proxy(waylandim_->call<fcitx::IWaylandIMModule::getInputMethodV2>(&ic));
    } catch (const std::exception&) {
      waylandim_ = nullptr;
      return nullptr;
    }
  }
  // The globals on ic's connection, once both have arrived. Fcitx5 gives each
  // connection its own focus group, so the group finds it (libwayland only tells
  // a proxy's display from 1.23, newer than Ubuntu 24.04's).
  const Globals* globals(const fcitx::InputContext& ic) const {
    for (const auto& [display, globals] : connections_) {
      if (globals.group == ic.focusGroup()) return globals.compositor && globals.shm ? &globals : nullptr;
    }
    return nullptr;
  }

 private:
  friend class WaylandWindow;
  static const wl_registry_listener kRegistryListener;
  static const ext_background_effect_manager_v1_listener kEffectsListener;

  void connect(wl_display* display, fcitx::FocusGroup* group);
  void disconnect(wl_display* display);

  mutable fcitx::AddonInstance* waylandim_;  // dropped once it turns out not to offer the call
  // COSMIC (smithay) places a popup with its corner at the caret's top-left and sends the
  // caret in the app's window, not in the popup, as wlroots does; it never flips one above.
  // shortcut: told by XDG_CURRENT_DESKTOP; tell by the rectangle if another smithay compositor does this.
  bool cornerAtCaret_ = false;
  int caretHeight_ = 0;
  std::unordered_map<wl_display*, Globals> connections_;
  std::vector<WaylandWindow*> windows_;
  std::unique_ptr<fcitx::HandlerTableEntry<fcitx::WaylandConnectionCreated>> created_;
  std::unique_ptr<fcitx::HandlerTableEntry<fcitx::WaylandConnectionClosed>> closed_;
};

class WaylandWindow final : public Window {
 public:
  WaylandWindow(WaylandScreen& screen, std::function<void()> exposed, std::function<void()> moved)
      : screen_(screen), exposed_(std::move(exposed)), moved_(std::move(moved)) {
    screen_.windows_.push_back(this);
  }
  ~WaylandWindow() override {
    release();
    std::erase(screen_.windows_, this);
  }

  void place(const Frame& to) override { frame = to; }
  void map(fcitx::InputContext& ic) override;
  void unmap() override;
  // The blur itself goes with each frame (endPaint), as the frame moves in the surface.
  void setBlur(bool on) override { blurred = on; }
  cairo_t* beginPaint() override;
  void endPaint(cairo_t* cr) override;
  bool above() const override { return above_; }

  // Drops the Wayland objects, as their connection closes or the window goes.
  void release();
  wl_display* display() const { return globals_ ? globals_->display : nullptr; }

 private:
  // A shared-memory buffer, busy while the compositor reads it.
  struct Buffer {
    WaylandWindow* window = nullptr;
    wl_buffer* buffer = nullptr;
    cairo_surface_t* surface = nullptr;
    void* data = MAP_FAILED;
    size_t size = 0;
    int width = 0, height = 0;
    bool busy = false;
    bool make(wl_shm* shm, int w, int h);
    void clear();
  };
  static const wl_buffer_listener kBufferListener;
  static const wl_surface_listener kSurfaceListener;
  static const zwp_input_popup_surface_v2_listener kPopupListener;

  void flush() { wl_display_flush(globals_->display); }
  void setBlurRegion(int x, int y);

  WaylandScreen& screen_;
  std::function<void()> exposed_, moved_;
  const Globals* globals_ = nullptr;
  wl_surface* surface_ = nullptr;
  zwp_input_method_v2* im_ = nullptr;
  zwp_input_popup_surface_v2* popup_ = nullptr;
  ext_background_effect_surface_v1* effect_ = nullptr;
  std::array<int, 5> blurRegion_{};  // what effect_ was last given: on, x, y, width, height
  std::array<Buffer, 2> buffers_;
  Buffer* painting_ = nullptr;
  int scale_ = 1;       // the surface's preferred buffer scale
  bool above_ = false;  // the compositor put the popup above the caret
  bool waiting_ = false;  // a paint found both buffers busy
};

// MARK: Screen

const ext_background_effect_manager_v1_listener WaylandScreen::kEffectsListener = {
    .capabilities = [](void* data, ext_background_effect_manager_v1*, uint32_t flags) {
      static_cast<Globals*>(data)->blur = flags & EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR;
    },
};

const wl_registry_listener WaylandScreen::kRegistryListener = {
    .global =
        [](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
          auto* globals = static_cast<Globals*>(data);
          if (std::strcmp(interface, wl_compositor_interface.name) == 0 && !globals->compositor) {
            // Version 6 tells the surface its buffer scale.
            globals->compositorVersion = std::min(version, 6u);
            globals->compositor =
                static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, globals->compositorVersion));
          } else if (std::strcmp(interface, wl_shm_interface.name) == 0 && !globals->shm) {
            globals->shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
          } else if (std::strcmp(interface, ext_background_effect_manager_v1_interface.name) == 0 && !globals->effects) {
            globals->effects = static_cast<ext_background_effect_manager_v1*>(
                wl_registry_bind(registry, name, &ext_background_effect_manager_v1_interface, 1));
            ext_background_effect_manager_v1_add_listener(globals->effects, &kEffectsListener, globals);
          }
        },
    .global_remove = [](void*, wl_registry*, uint32_t) {},
};

WaylandScreen::WaylandScreen(fcitx::AddonInstance* wayland, fcitx::AddonInstance* waylandim) : waylandim_(waylandim) {
  const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
  cornerAtCaret_ = desktop && std::strstr(desktop, "COSMIC");
  // Called at once for the connections Fcitx5 already has.
  created_ = wayland->call<fcitx::IWaylandModule::addConnectionCreatedCallback>(
      [this](const std::string&, wl_display* display, fcitx::FocusGroup* group) { connect(display, group); });
  closed_ = wayland->call<fcitx::IWaylandModule::addConnectionClosedCallback>(
      [this](const std::string&, wl_display* display) { disconnect(display); });
}

WaylandScreen::~WaylandScreen() {
  created_.reset();
  closed_.reset();
  while (!connections_.empty()) disconnect(connections_.begin()->first);
}

std::unique_ptr<Window> WaylandScreen::createWindow(std::function<void()> exposed, std::function<void()> moved) {
  return std::make_unique<WaylandWindow>(*this, std::move(exposed), std::move(moved));
}

void WaylandScreen::connect(wl_display* display, fcitx::FocusGroup* group) {
  if (connections_.contains(display)) return;
  Globals& globals = connections_[display];  // a node: its address stays for the listener
  globals.display = display;
  globals.group = group;
  globals.registry = wl_display_get_registry(display);
  wl_registry_add_listener(globals.registry, &kRegistryListener, &globals);
  wl_display_flush(display);
}

// Fcitx5 calls this before it closes the connection, so the objects can still be destroyed.
void WaylandScreen::disconnect(wl_display* display) {
  auto found = connections_.find(display);
  if (found == connections_.end()) return;
  for (WaylandWindow* window : windows_) {
    if (window->display() == display) window->release();
  }
  Globals& globals = found->second;
  if (globals.effects) ext_background_effect_manager_v1_destroy(globals.effects);
  if (globals.shm) wl_shm_destroy(globals.shm);
  if (globals.compositor) wl_compositor_destroy(globals.compositor);
  wl_registry_destroy(globals.registry);
  wl_display_flush(display);
  connections_.erase(found);
}

// MARK: Window

const wl_buffer_listener WaylandWindow::kBufferListener = {
    .release =
        [](void* data, wl_buffer*) {
          auto* buffer = static_cast<Buffer*>(data);
          buffer->busy = false;
          WaylandWindow* window = buffer->window;
          if (window->waiting_) {
            window->waiting_ = false;
            window->exposed_();
          }
        },
};

const wl_surface_listener WaylandWindow::kSurfaceListener = {
    .enter = [](void*, wl_surface*, wl_output*) {},
    .leave = [](void*, wl_surface*, wl_output*) {},
    .preferred_buffer_scale =
        [](void* data, wl_surface*, int32_t factor) {
          auto* window = static_cast<WaylandWindow*>(data);
          if (factor < 1 || factor == window->scale_) return;
          window->scale_ = factor;
          if (window->mapped) window->exposed_();
        },
    .preferred_buffer_transform = [](void*, wl_surface*, uint32_t) {},
};

// The caret in the popup's coordinates: above the popup's top when the
// compositor put it below the caret, at its bottom when above.
const zwp_input_popup_surface_v2_listener WaylandWindow::kPopupListener = {
    .text_input_rectangle =
        [](void* data, zwp_input_popup_surface_v2*, int32_t, int32_t y, int32_t, int32_t height) {
          auto* window = static_cast<WaylandWindow*>(data);
          if (window->screen_.cornerAtCaret_) {
            if (height == window->screen_.caretHeight_) return;
            window->screen_.caretHeight_ = height;
            if (window->mapped) window->moved_();
            return;
          }
          bool above = y > 0;
          if (above == window->above_) return;
          window->above_ = above;
          if (window->mapped) window->moved_();
        },
};

void WaylandWindow::map(fcitx::InputContext& ic) {
  zwp_input_method_v2* im = screen_.inputMethod(ic);
  const Globals* globals = im ? screen_.globals(ic) : nullptr;
  if (!globals) return;
  if (globals != globals_) {
    release();
    globals_ = globals;
    surface_ = wl_compositor_create_surface(globals_->compositor);
    wl_surface_add_listener(surface_, &kSurfaceListener, this);
    // Clicks go through to the app, as on X11, where the window takes no input either.
    wl_region* none = wl_compositor_create_region(globals_->compositor);
    wl_surface_set_input_region(surface_, none);
    wl_region_destroy(none);
    for (Buffer& buffer : buffers_) buffer.window = this;
  }
  // Fcitx5 has one input method object for each seat; the popup goes with it.
  if (im != im_) {
    if (popup_) zwp_input_popup_surface_v2_destroy(popup_);
    popup_ = zwp_input_method_v2_get_input_popup_surface(im, surface_);
    zwp_input_popup_surface_v2_add_listener(popup_, &kPopupListener, this);
    im_ = im;
  }
  mapped = true;
}

void WaylandWindow::unmap() {
  if (!mapped) return;
  mapped = false;
  waiting_ = false;
  if (!surface_) return;
  wl_surface_attach(surface_, nullptr, 0, 0);
  wl_surface_commit(surface_);
  flush();
}

void WaylandWindow::release() {
  for (Buffer& buffer : buffers_) buffer.clear();
  if (effect_) ext_background_effect_surface_v1_destroy(effect_);
  effect_ = nullptr;
  blurRegion_ = {};
  if (popup_) zwp_input_popup_surface_v2_destroy(popup_);
  if (surface_) wl_surface_destroy(surface_);
  if (globals_) flush();
  popup_ = nullptr;
  surface_ = nullptr;
  im_ = nullptr;
  globals_ = nullptr;
  painting_ = nullptr;
  mapped = waiting_ = false;
}

// The surface reaches from the caret to the frame's far side, so the frame's
// gap to the text stays clear: below the caret it starts at the caret's
// bottom, above it it ends at the caret's top.
cairo_t* WaylandWindow::beginPaint() {
  if (!mapped || !surface_) return nullptr;
  int left = std::min(0, frame.x), top = std::min(0, frame.y);
  int width = std::max(1, std::max(0, frame.x + frame.width) - left);
  int height = std::max(1, std::max(0, frame.y + frame.height) - top);
  auto free = std::find_if(buffers_.begin(), buffers_.end(), [](const Buffer& buffer) { return !buffer.busy; });
  if (free == buffers_.end()) {
    waiting_ = true;
    return nullptr;
  }
  if (!free->make(globals_->shm, width * scale_, height * scale_)) return nullptr;
  painting_ = &*free;
  cairo_surface_set_device_scale(painting_->surface, scale_, scale_);
  cairo_t* cr = cairo_create(painting_->surface);
  cairo_translate(cr, frame.x - left, frame.y - top);
  return cr;
}

void WaylandWindow::endPaint(cairo_t* cr) {
  cairo_destroy(cr);
  if (!painting_) return;
  cairo_surface_flush(painting_->surface);
  if (globals_->compositorVersion >= 3) wl_surface_set_buffer_scale(surface_, scale_);
  setBlurRegion(frame.x - std::min(0, frame.x), frame.y - std::min(0, frame.y));  // where beginPaint put the frame
  wl_surface_attach(surface_, painting_->buffer, 0, 0);
  wl_surface_damage(surface_, 0, 0, INT32_MAX, INT32_MAX);
  wl_surface_commit(surface_);
  painting_->busy = true;
  painting_ = nullptr;
  flush();
}

// The frame's rounded rectangle at (x, y) in the surface, one row at a time through the
// corners, so no blur shows outside them; as X11Window::setBlur gives KWin.
void WaylandWindow::setBlurRegion(int x, int y) {
  if (!globals_->effects) return;
  std::array<int, 5> wanted{blurred && globals_->blur, x, y, frame.width, frame.height};
  if (wanted == blurRegion_) return;
  blurRegion_ = wanted;
  if (!effect_) effect_ = ext_background_effect_manager_v1_get_background_effect(globals_->effects, surface_);
  if (!wanted[0]) {
    ext_background_effect_surface_v1_set_blur_region(effect_, nullptr);
    return;
  }
  wl_region* region = wl_compositor_create_region(globals_->compositor);
  int radius = std::min({static_cast<int>(kWindowRadius), frame.width / 2, frame.height / 2});
  for (int row = 0; row < radius; row++) {
    double dy = radius - row - 0.5;
    int inset = static_cast<int>(std::lround(radius - std::sqrt(radius * radius - dy * dy)));
    wl_region_add(region, x + inset, y + row, frame.width - 2 * inset, 1);
    wl_region_add(region, x + inset, y + frame.height - 1 - row, frame.width - 2 * inset, 1);
  }
  wl_region_add(region, x, y + radius, frame.width, frame.height - 2 * radius);
  ext_background_effect_surface_v1_set_blur_region(effect_, region);
  wl_region_destroy(region);
}

bool WaylandWindow::Buffer::make(wl_shm* shm, int w, int h) {
  if (buffer && width == w && height == h) return true;
  clear();
  int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w);
  size_t bytes = static_cast<size_t>(stride) * h;
  int fd = memfd_create("projectkey-panel", MFD_CLOEXEC);
  if (fd < 0) return false;
  if (ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
    close(fd);
    return false;
  }
  data = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED) {
    close(fd);
    return false;
  }
  size = bytes;
  wl_shm_pool* pool = wl_shm_create_pool(shm, fd, static_cast<int32_t>(bytes));
  // Cairo's ARGB32 is premultiplied, in native byte order, as ARGB8888 is.
  buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);
  close(fd);
  wl_buffer_add_listener(buffer, &kBufferListener, this);
  surface = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, w, h, stride);
  width = w;
  height = h;
  return true;
}

void WaylandWindow::Buffer::clear() {
  if (surface) cairo_surface_destroy(surface);
  if (buffer) wl_buffer_destroy(buffer);
  if (data != MAP_FAILED) munmap(data, size);
  surface = nullptr;
  buffer = nullptr;
  data = MAP_FAILED;
  size = 0;
  width = height = 0;
  busy = false;
}

}  // namespace

std::unique_ptr<Screen> OpenWaylandScreen(fcitx::Instance* instance) {
  fcitx::AddonManager& addons = instance->addonManager();
  fcitx::AddonInstance* wayland = addons.addon("wayland", true);
  fcitx::AddonInstance* waylandim = addons.addon("waylandim", true);
  if (!wayland || !waylandim) return nullptr;
  return std::make_unique<WaylandScreen>(wayland, waylandim);
}

}  // namespace ProjectKey
