// Project Key's windows on Wayland: input method popups (input-method-unstable-v2),
// which the compositor (sway, Hyprland and the other wlroots ones) places at the
// text's caret, below it or, where the screen ends, above. A popup belongs to the
// input method object Fcitx5 bound on its own Wayland connection, so the windows
// are made on that connection, as Fcitx5's own panel is. Under KWin's input method
// (input-method-unstable-v1, Plasma) they are overlay input panels instead, which
// KWin places the same way. Contexts from other frontends (Fcitx5's D-Bus one,
// which GTK_IM_MODULE=fcitx uses) get Fcitx5's panel.
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
#include "alpha-modifier-v1-client-protocol.h"
#include "ext-background-effect-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "input-method-unstable-v1-client-protocol.h"
#include "input-method-unstable-v2-client-protocol.h"
#include "viewporter-client-protocol.h"

namespace fcitx::wayland {
class ZwpInputMethodV2;
}

// From Fcitx5's frontend/waylandim/waylandim_public.h, which it does not install.
FCITX_ADDON_DECLARE_FUNCTION(WaylandIMModule, getInputMethodV2, fcitx::wayland::ZwpInputMethodV2*(fcitx::InputContext*));

namespace ProjectKey {

namespace {

class WaylandScreen;

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
  zwp_input_panel_v1* inputPanel = nullptr;  // KWin's, on the connection it started Fcitx5 with
  ext_background_effect_manager_v1* effects = nullptr;  // KWin 6.7, Mutter 51, niri 26.04
  bool blur = false;                                    // effects says it can blur
  wp_alpha_modifier_v1* alphaModifier = nullptr;        // fades a surface, the blur behind it included
  // Together, for drawing at a fractional scale (150%) rather than one the compositor resamples.
  wp_fractional_scale_manager_v1* fractionalScale = nullptr;
  wp_viewporter* viewporter = nullptr;
  WaylandScreen* screen = nullptr;
  wl_seat* seat = nullptr;  // shortcut: the first seat only; bind the others if a multi-seat desktop shows up
  wl_pointer* pointer = nullptr;
  wl_surface* pointerSurface = nullptr;  // the pointer is over this one of Fcitx5's connection's surfaces
  int pointerX = 0, pointerY = 0;        // on it
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
    const Globals* found = globals(ic);
    return found && (inputMethod(const_cast<fcitx::InputContext&>(ic)) || inputPanel(ic, *found));
  }
  std::unique_ptr<Window> createWindow(std::function<void()> exposed, std::function<void()> moved) override;
  // The compositor knows the caret; frames start from it. Smithay's compositors put a popup's
  // corner at the caret's top instead of below it, so frames start a caret's height down.
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
    if (!waylandim_ || std::string_view(ic.frontend()) != "wayland_v2") return nullptr;
    // A Fcitx5 whose waylandim does not register the call throws; Fcitx5's panel stands in there.
    try {
      return Proxy(waylandim_->call<fcitx::IWaylandIMModule::getInputMethodV2>(&ic));
    } catch (const std::exception&) {
      waylandim_ = nullptr;
      return nullptr;
    }
  }
  // KWin's input method (v1) gives no caret but places an overlay panel at it, below it or, where the
  // screen ends, above. shortcut: KWin does not say which, so a window above the caret is drawn as below
  // (its proof row on top); tell by the caret if that shows.
  static zwp_input_panel_v1* inputPanel(const fcitx::InputContext& ic, const Globals& globals) {
    return std::string_view(ic.frontend()) == "wayland" ? globals.inputPanel : nullptr;
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
  static const wl_seat_listener kSeatListener;
  static const wl_pointer_listener kPointerListener;

  void connect(wl_display* display, fcitx::FocusGroup* group);
  void disconnect(wl_display* display);

  mutable fcitx::AddonInstance* waylandim_;  // dropped once it turns out not to offer the call
  // Smithay sends the caret in the app's window, not in the popup as wlroots, Hyprland and KWin
  // do, and leaves the popup's corner at the caret's top-left (COSMIC, Nekoland), never above it.
  // niri, on smithay too, moves the popup below the caret itself.
  // shortcut: niri told by XDG_CURRENT_DESKTOP; add any other smithay compositor that moves its popups.
  bool placesBelow_ = false;
  // Smithay keeps one popup per input method, the newest: only it follows the caret and moves
  // to the window typed in. The window showing makes its popup the newest again (WaylandWindow::map).
  WaylandWindow* newestPopup_ = nullptr;
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
    surfaceData_.window = this;
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
  bool setOpacity(float alpha) override;
  cairo_t* beginPaint() override;
  void endPaint(cairo_t* cr) override;
  bool above() const override { return above_; }

  // Drops the Wayland objects, as their connection closes or the window goes.
  void release();
  wl_display* display() const { return globals_ ? globals_->display : nullptr; }
  wl_surface* surface() const { return surface_; }
  // A left click at (x, y) on the surface.
  void click(int x, int y) {
    if (mapped && clicked) clicked(x - (frame.x - std::min(0, frame.x)), y - (frame.y - std::min(0, frame.y)));
  }
  // The pointer at (x, y) on the surface; off it when leaving.
  void move(int x, int y, bool leaving = false) {
    if (!mapped || !moved) return;
    if (leaving) {
      moved(-1, -1);
    } else {
      moved(x - (frame.x - std::min(0, frame.x)), y - (frame.y - std::min(0, frame.y)));
    }
  }

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
  static const wp_fractional_scale_v1_listener kFractionalScaleListener;

  // The surface's user data. Fcitx5's own pointer listener shares the connection, and when the
  // pointer enters this surface takes that data for its fcitx::wayland::WlSurface, reads
  // userData() from it and leaves alone a surface where it is null: zeros, well past that field.
  struct SurfaceData {
    std::array<void*, 32> zeros{};
    WaylandWindow* window = nullptr;
  };

  void flush() { wl_display_flush(globals_->display); }
  void setBlurRegion(int x, int y);
  void setInputRegion(int x, int y);

  WaylandScreen& screen_;
  std::function<void()> exposed_, moved_;
  const Globals* globals_ = nullptr;
  wl_surface* surface_ = nullptr;
  SurfaceData surfaceData_;
  zwp_input_method_v2* im_ = nullptr;
  zwp_input_popup_surface_v2* popup_ = nullptr;
  zwp_input_panel_surface_v1* panelSurface_ = nullptr;  // in place of popup_ under KWin's input method (v1)
  ext_background_effect_surface_v1* effect_ = nullptr;
  wp_alpha_modifier_surface_v1* alpha_ = nullptr;
  wp_fractional_scale_v1* fractionalScale_ = nullptr;  // with viewport_, where the compositor has both
  wp_viewport* viewport_ = nullptr;
  std::array<int, 5> blurRegion_{};  // what effect_ was last given: on, x, y, width, height
  std::array<int, 4> inputRegion_{};  // what the surface takes clicks in: x, y, width, height
  std::array<Buffer, 2> buffers_;
  Buffer* painting_ = nullptr;
  double scale_ = 1;  // the surface's preferred scale: fractional with viewport_, else an integer
  int surfaceWidth_ = 0, surfaceHeight_ = 0;  // as last painted, in the surface's coordinates
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
            // Version 6 tells the surface its buffer scale; an older libwayland (1.20, Ubuntu 22.04) knows
            // only its own version's events.
            globals->compositorVersion = std::min({version, 6u, static_cast<uint32_t>(wl_compositor_interface.version)});
            globals->compositor =
                static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, globals->compositorVersion));
          } else if (std::strcmp(interface, wl_shm_interface.name) == 0 && !globals->shm) {
            globals->shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
          } else if (std::strcmp(interface, zwp_input_panel_v1_interface.name) == 0 && !globals->inputPanel) {
            globals->inputPanel = static_cast<zwp_input_panel_v1*>(wl_registry_bind(registry, name, &zwp_input_panel_v1_interface, 1));
          } else if (std::strcmp(interface, ext_background_effect_manager_v1_interface.name) == 0 && !globals->effects) {
            globals->effects = static_cast<ext_background_effect_manager_v1*>(
                wl_registry_bind(registry, name, &ext_background_effect_manager_v1_interface, 1));
            ext_background_effect_manager_v1_add_listener(globals->effects, &kEffectsListener, globals);
          } else if (std::strcmp(interface, wp_alpha_modifier_v1_interface.name) == 0 && !globals->alphaModifier) {
            globals->alphaModifier =
                static_cast<wp_alpha_modifier_v1*>(wl_registry_bind(registry, name, &wp_alpha_modifier_v1_interface, 1));
          } else if (std::strcmp(interface, wl_seat_interface.name) == 0 && !globals->seat) {
            // Version 1: a listener must have every event of the version it binds.
            globals->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
            wl_seat_add_listener(globals->seat, &kSeatListener, globals);
          } else if (std::strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0 && !globals->fractionalScale) {
            globals->fractionalScale = static_cast<wp_fractional_scale_manager_v1*>(
                wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1));
          } else if (std::strcmp(interface, wp_viewporter_interface.name) == 0 && !globals->viewporter) {
            globals->viewporter = static_cast<wp_viewporter*>(wl_registry_bind(registry, name, &wp_viewporter_interface, 1));
          }
        },
    .global_remove = [](void*, wl_registry*, uint32_t) {},
};

const wl_seat_listener WaylandScreen::kSeatListener = {
    .capabilities =
        [](void* data, wl_seat* seat, uint32_t capabilities) {
          auto* globals = static_cast<Globals*>(data);
          bool has = capabilities & WL_SEAT_CAPABILITY_POINTER;
          if (has && !globals->pointer) {
            globals->pointer = wl_seat_get_pointer(seat);
            wl_pointer_add_listener(globals->pointer, &kPointerListener, globals);
          } else if (!has && globals->pointer) {
            wl_pointer_destroy(globals->pointer);
            globals->pointer = nullptr;
            globals->pointerSurface = nullptr;
          }
        },
    .name = [](void*, wl_seat*, const char*) {},
};

const wl_pointer_listener WaylandScreen::kPointerListener = {
    .enter =
        [](void* data, wl_pointer*, uint32_t, wl_surface* surface, wl_fixed_t x, wl_fixed_t y) {
          auto* globals = static_cast<Globals*>(data);
          globals->pointerSurface = surface;
          globals->pointerX = wl_fixed_to_int(x);
          globals->pointerY = wl_fixed_to_int(y);
          for (WaylandWindow* window : globals->screen->windows_) {
            if (window->surface() == surface) window->move(globals->pointerX, globals->pointerY);
          }
        },
    .leave =
        [](void* data, wl_pointer*, uint32_t, wl_surface* surface) {
          auto* globals = static_cast<Globals*>(data);
          globals->pointerSurface = nullptr;
          for (WaylandWindow* window : globals->screen->windows_) {
            if (window->surface() == surface) window->move(0, 0, /*leaving=*/true);
          }
        },
    .motion =
        [](void* data, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) {
          auto* globals = static_cast<Globals*>(data);
          globals->pointerX = wl_fixed_to_int(x);
          globals->pointerY = wl_fixed_to_int(y);
          for (WaylandWindow* window : globals->screen->windows_) {
            if (window->surface() == globals->pointerSurface) window->move(globals->pointerX, globals->pointerY);
          }
        },
    .button =
        [](void* data, wl_pointer*, uint32_t, uint32_t, uint32_t button, uint32_t state) {
          auto* globals = static_cast<Globals*>(data);
          constexpr uint32_t kLeftButton = 0x110;  // BTN_LEFT
          if (button != kLeftButton || state != WL_POINTER_BUTTON_STATE_RELEASED || !globals->pointerSurface) return;
          for (WaylandWindow* window : globals->screen->windows_) {
            if (window->surface() == globals->pointerSurface) window->click(globals->pointerX, globals->pointerY);
          }
        },
    .axis = [](void*, wl_pointer*, uint32_t, uint32_t, wl_fixed_t) {},
};

WaylandScreen::WaylandScreen(fcitx::AddonInstance* wayland, fcitx::AddonInstance* waylandim) : waylandim_(waylandim) {
  const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
  placesBelow_ = desktop && std::strstr(desktop, "niri");
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
  globals.screen = this;
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
  if (globals.pointer) wl_pointer_destroy(globals.pointer);
  if (globals.seat) wl_seat_destroy(globals.seat);
  if (globals.effects) ext_background_effect_manager_v1_destroy(globals.effects);
  if (globals.alphaModifier) wp_alpha_modifier_v1_destroy(globals.alphaModifier);
  if (globals.fractionalScale) wp_fractional_scale_manager_v1_destroy(globals.fractionalScale);
  if (globals.viewporter) wp_viewporter_destroy(globals.viewporter);
  if (globals.inputPanel) zwp_input_panel_v1_destroy(globals.inputPanel);
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
#ifdef WL_SURFACE_PREFERRED_BUFFER_SCALE_SINCE_VERSION  // libwayland 1.22
    .preferred_buffer_scale =
        [](void* data, wl_surface*, int32_t factor) {
          WaylandWindow* window = static_cast<SurfaceData*>(data)->window;
          if (window->viewport_ || factor < 1 || factor == window->scale_) return;
          window->scale_ = factor;
          if (window->mapped) window->exposed_();
        },
    .preferred_buffer_transform = [](void*, wl_surface*, uint32_t) {},
#endif
};

// In 120ths: 180 at 150%.
const wp_fractional_scale_v1_listener WaylandWindow::kFractionalScaleListener = {
    .preferred_scale =
        [](void* data, wp_fractional_scale_v1*, uint32_t scale) {
          auto* window = static_cast<WaylandWindow*>(data);
          double factor = scale / 120.0;
          if (scale == 0 || factor == window->scale_) return;
          window->scale_ = factor;
          if (window->mapped) window->exposed_();
        },
};

// The caret in the popup's coordinates touches the popup: it ends at the popup's top when the
// compositor put the popup below it, starts at its bottom when above. Anywhere else it is in
// the app's window, as smithay sends it (WaylandScreen::placesBelow_).
const zwp_input_popup_surface_v2_listener WaylandWindow::kPopupListener = {
    .text_input_rectangle =
        [](void* data, zwp_input_popup_surface_v2*, int32_t, int32_t y, int32_t, int32_t height) {
          auto* window = static_cast<WaylandWindow*>(data);
          bool inPopup = y == -height || (y > 0 && std::abs(y - window->surfaceHeight_) <= 1);
          bool above = inPopup && y > 0;
          int caretHeight = inPopup || window->screen_.placesBelow_ ? 0 : height;
          if (above == window->above_ && caretHeight == window->screen_.caretHeight_) return;
          window->above_ = above;
          window->screen_.caretHeight_ = caretHeight;
          if (window->mapped) window->moved_();
        },
};

void WaylandWindow::map(fcitx::InputContext& ic) {
  zwp_input_method_v2* im = screen_.inputMethod(ic);
  const Globals* globals = screen_.globals(ic);
  zwp_input_panel_v1* panel = globals && !im ? WaylandScreen::inputPanel(ic, *globals) : nullptr;
  if (!globals || (!im && !panel)) return;
  if (globals != globals_) {
    release();
    globals_ = globals;
    surface_ = wl_compositor_create_surface(globals_->compositor);
    wl_surface_add_listener(surface_, &kSurfaceListener, &surfaceData_);
    // Clicks go through to the app, unless a window that takes them sets its frame (setInputRegion).
    wl_region* none = wl_compositor_create_region(globals_->compositor);
    wl_surface_set_input_region(surface_, none);
    wl_region_destroy(none);
    if (globals_->fractionalScale && globals_->viewporter) {
      viewport_ = wp_viewporter_get_viewport(globals_->viewporter, surface_);
      fractionalScale_ = wp_fractional_scale_manager_v1_get_fractional_scale(globals_->fractionalScale, surface_);
      wp_fractional_scale_v1_add_listener(fractionalScale_, &kFractionalScaleListener, this);
    }
    for (Buffer& buffer : buffers_) buffer.window = this;
  }
  if (panel) {
    if (!panelSurface_) {
      panelSurface_ = zwp_input_panel_v1_get_input_panel_surface(panel, surface_);
      zwp_input_panel_surface_v1_set_overlay_panel(panelSurface_);
    }
  } else if (im != im_ || screen_.newestPopup_ != this) {
    // Fcitx5 has one input method object for each seat; the popup goes with it.
    if (popup_) zwp_input_popup_surface_v2_destroy(popup_);
    popup_ = zwp_input_method_v2_get_input_popup_surface(im, surface_);
    zwp_input_popup_surface_v2_add_listener(popup_, &kPopupListener, this);
    im_ = im;
    screen_.newestPopup_ = this;
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
  if (alpha_) wp_alpha_modifier_surface_v1_destroy(alpha_);
  alpha_ = nullptr;
  blurRegion_ = {};
  inputRegion_ = {};
  if (popup_) zwp_input_popup_surface_v2_destroy(popup_);
  if (panelSurface_) zwp_input_panel_surface_v1_destroy(panelSurface_);
  panelSurface_ = nullptr;
  if (screen_.newestPopup_ == this) screen_.newestPopup_ = nullptr;
  if (fractionalScale_) wp_fractional_scale_v1_destroy(fractionalScale_);
  if (viewport_) wp_viewport_destroy(viewport_);
  if (surface_) wl_surface_destroy(surface_);
  if (globals_) flush();
  popup_ = nullptr;
  fractionalScale_ = nullptr;
  viewport_ = nullptr;
  surface_ = nullptr;
  scale_ = 1;  // a new surface is told its own
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
  surfaceWidth_ = width;
  surfaceHeight_ = height;
  auto free = std::find_if(buffers_.begin(), buffers_.end(), [](const Buffer& buffer) { return !buffer.busy; });
  if (free == buffers_.end()) {
    waiting_ = true;
    return nullptr;
  }
  if (!free->make(globals_->shm, static_cast<int>(std::lround(width * scale_)), static_cast<int>(std::lround(height * scale_)))) {
    return nullptr;
  }
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
  if (viewport_) {
    wp_viewport_set_destination(viewport_, surfaceWidth_, surfaceHeight_);
  } else if (globals_->compositorVersion >= 3) {
    wl_surface_set_buffer_scale(surface_, static_cast<int>(scale_));
  }
  setBlurRegion(frame.x - std::min(0, frame.x), frame.y - std::min(0, frame.y));  // where beginPaint put the frame
  setInputRegion(frame.x - std::min(0, frame.x), frame.y - std::min(0, frame.y));
  wl_surface_attach(surface_, painting_->buffer, 0, 0);
  wl_surface_damage(surface_, 0, 0, INT32_MAX, INT32_MAX);
  wl_surface_commit(surface_);
  painting_->busy = true;
  painting_ = nullptr;
  flush();
}

// A blurred window fades as a whole, as the blur does not take the alpha it is painted with;
// the multiplier goes with the next commit, endPaint's.
bool WaylandWindow::setOpacity(float alpha) {
  if (!surface_ || !globals_->alphaModifier) return false;
  bool whole = blurred && globals_->blur;
  if (!whole && !alpha_) return false;
  if (!alpha_) alpha_ = wp_alpha_modifier_v1_get_surface(globals_->alphaModifier, surface_);
  wp_alpha_modifier_surface_v1_set_multiplier(alpha_, static_cast<uint32_t>(std::lround((whole ? alpha : 1.0) * UINT32_MAX)));
  return whole;
}

// The frame only: the gap between it and the text, part of the surface, stays the app's.
void WaylandWindow::setInputRegion(int x, int y) {
  std::array<int, 4> wanted{x, y, frame.width, frame.height};
  if (!clicked || wanted == inputRegion_) return;
  inputRegion_ = wanted;
  wl_region* region = wl_compositor_create_region(globals_->compositor);
  wl_region_add(region, x, y, frame.width, frame.height);
  wl_surface_set_input_region(surface_, region);
  wl_region_destroy(region);
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
