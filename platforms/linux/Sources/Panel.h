// The Direction A candidate window and the 中／英 badge on X11, drawn with
// Cairo and Pango: a port of the Windows CandidateWindow and ModeBadge
// (platforms/windows/Sources/TextService/CandidateWindow.cpp), with the same
// metrics and colors. Both are override-redirect windows that never take focus.
// Under Wayland there is no X display to place them on, and ok() is false.
#pragma once

#include <cairo.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/rect.h>
#include <pango/pango.h>
#include <xcb/xcb.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "Controller.h"

namespace ProjectKey {

class Panel {
 public:
  // settings gives the appearance: Setting::kAppearance 1 blurs what is behind the windows.
  Panel(fcitx::EventLoop& loop, const Settings& settings);
  ~Panel();
  Panel(const Panel&) = delete;
  Panel& operator=(const Panel&) = delete;

  bool ok() const { return connection_ != nullptr; }
  // Grid columns a candidate takes, measured as the window draws it.
  int span(const std::string& candidate) const;

  // anchor is the caret in screen pixels. owner is the session showing it, so
  // one session hiding does not hide another's panel.
  void show(const void* owner, const PanelContent& content, const fcitx::Rect& anchor);
  void hide(const void* owner);
  // Moves what owner shows to a new caret, without restarting the badge's fade.
  void follow(const void* owner, const fcitx::Rect& anchor);
  void showBadge(const std::string& label, const fcitx::Rect& anchor);
  void hideBadge();

 private:
  struct Window {
    xcb_window_t id = 0;
    cairo_surface_t* surface = nullptr;
    int x = 0, y = 0, width = 0, height = 0;
    bool mapped = false;
    bool blurred = false;
  };
  struct Rect {
    float left = 0, top = 0, right = 0, bottom = 0;
    bool empty() const { return right <= left || bottom <= top; }
    bool operator==(const Rect&) const = default;
  };

  void createWindow(Window& window);
  void place(Window& window, int x, int y, int width, int height);
  void map(Window& window);
  void unmap(Window& window);
  void dispatch();
  bool composited();
  // Whether the windows should blur what is behind them: the setting asks for
  // it and the compositor (KWin) offers it.
  bool blurs();
  void setBlur(Window& window, bool on);
  void setFontOptions(bool blurred);
  void startTimer(std::unique_ptr<fcitx::EventSourceTime>& timer, bool (Panel::*tick)());

  // Candidate window.
  void paintPanel();
  bool tickPanel();
  float proofHeight() const;
  float rowHeight() const;
  float rowTop() const;
  float keyWidth() const;
  float fittingWidth() const;
  Rect selectionRect() const;
  Rect shownSelectionRect() const;
  void drawProof(cairo_t* cr, float top, float width);
  void drawCandidates(cairo_t* cr, float top, float width);

  // Badge.
  void placeBadge(const fcitx::Rect& anchor);
  void avoidPanel();
  void paintBadge();
  bool tickBadge();

  float width(const std::string& text, PangoFontDescription* font) const;
  PangoLayout* layout(const std::string& text, PangoFontDescription* font) const;

  xcb_connection_t* connection_ = nullptr;
  xcb_screen_t* screen_ = nullptr;
  xcb_visualtype_t* visual_ = nullptr;
  uint8_t depth_ = 0;
  xcb_colormap_t colormap_ = 0;
  xcb_atom_t compositorAtom_ = 0, blurAtom_ = 0;
  const Settings& settings_;
  bool argb_ = false;
  float scale_ = 1;
  fcitx::EventLoop& loop_;
  std::unique_ptr<fcitx::EventSourceIO> io_;

  PangoContext* context_ = nullptr;
  PangoFontDescription *proofFont_ = nullptr, *candidateFont_ = nullptr, *labelFont_ = nullptr, *pageFont_ = nullptr,
                       *keyFont_ = nullptr, *badgeFont_ = nullptr;
  float columnWidth_ = 0;

  Window panel_;
  const void* owner_ = nullptr;
  std::optional<PanelContent> content_;
  bool proofBelow_ = false;  // window above the text: proof row last, next to the text
  bool rounded_ = false;     // a compositor shows the corners through
  Rect pillFrom_, pillTo_;
  Window frameFrom_, frameTo_;  // geometry only
  uint64_t slideStart_ = 0, resizeStart_ = 0;
  bool sliding_ = false, resizing_ = false;
  std::unique_ptr<fcitx::EventSourceTime> panelTimer_;

  Window badge_;
  std::string badgeLabel_;
  float badgeAlpha_ = 1;
  bool badgeFades_ = false;  // a compositor shows its alpha
  uint64_t badgeStart_ = 0;
  std::unique_ptr<fcitx::EventSourceTime> badgeTimer_;
};

}  // namespace ProjectKey
