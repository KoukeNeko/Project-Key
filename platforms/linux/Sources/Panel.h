// The Direction A candidate window and the 中／英 badge, drawn with Cairo and
// Pango: a port of the Windows CandidateWindow and ModeBadge
// (platforms/windows/Sources/TextService/CandidateWindow.cpp), with the same
// metrics and colors. They go on an X11 screen or, under Wayland, into input
// method popups (Screen.h); ok() is false when there is neither.
#pragma once

#include <cairo.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/rect.h>
#include <fcitx/inputcontext.h>
#include <pango/pango.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "Controller.h"
#include "Screen.h"

namespace ProjectKey {

class Panel {
 public:
  // settings gives the appearance: Setting::kAppearance 1 blurs what is behind the windows.
  Panel(fcitx::Instance* instance, const Settings& settings);
  ~Panel();
  Panel(const Panel&) = delete;
  Panel& operator=(const Panel&) = delete;

  bool ok() const { return screen_ != nullptr; }
  // Whether the windows can go at ic's caret; Fcitx5's panel stands in where not.
  bool shows(const fcitx::InputContext& ic) const { return screen_ && screen_->reaches(ic); }
  // Grid columns a candidate takes, measured as the window draws it.
  int span(const std::string& candidate) const;

  // anchor is the caret in ic's coordinates. ic is the context showing the panel,
  // so one context hiding does not hide another's.
  void show(fcitx::InputContext& ic, const PanelContent& content, const fcitx::Rect& anchor);
  void hide(const fcitx::InputContext& ic);
  // Moves what ic shows to a new caret, without restarting the badge's fade.
  void follow(fcitx::InputContext& ic, const fcitx::Rect& anchor);
  void showBadge(fcitx::InputContext& ic, const std::string& label, const fcitx::Rect& anchor);
  void hideBadge();
  // The Tab hint in a window of its own beside the candidate window, away from
  // the text (Environment::hintWindow); after show, which it is placed from.
  void showHint(fcitx::InputContext& ic, const PanelContent& hint);
  void hideHint();

 private:
  struct Rect {
    float left = 0, top = 0, right = 0, bottom = 0;
    bool empty() const { return right <= left || bottom <= top; }
    bool operator==(const Rect&) const = default;
  };

  // Whether the windows should blur what is behind them: the setting asks for
  // it and the compositor (KWin) offers it.
  bool blurs();
  void setFontOptions(bool blurred);
  void startTimer(std::unique_ptr<fcitx::EventSourceTime>& timer, bool (Panel::*tick)());

  // Candidate window.
  void paintPanel();
  bool tickPanel();
  float proofHeight() const;
  float rowHeight() const;
  float rowTop() const;
  float keyWidth(const PanelContent& content) const;
  float proofWidth(const PanelContent& content) const;
  float fittingWidth() const;
  Rect selectionRect() const;
  Rect shownSelectionRect() const;
  void drawProof(cairo_t* cr, const PanelContent& content, float top, float width);
  void drawCandidates(cairo_t* cr, float top, float width);

  // Tab hint.
  void placeHint();
  void paintHint();

  // Badge.
  void placeBadge(const fcitx::Rect& anchor);
  void avoidPanel();
  void paintBadge();
  bool tickBadge();

  float width(const std::string& text, PangoFontDescription* font) const;
  PangoLayout* layout(const std::string& text, PangoFontDescription* font) const;

  const Settings& settings_;
  fcitx::EventLoop& loop_;
  std::unique_ptr<Screen> screen_;
  float scale_ = 1;

  PangoContext* context_ = nullptr;
  PangoFontDescription *proofFont_ = nullptr, *candidateFont_ = nullptr, *typingFont_ = nullptr, *labelFont_ = nullptr, *pageFont_ = nullptr,
                       *keyFont_ = nullptr, *badgeFont_ = nullptr;
  float columnWidth_ = 0;

  std::unique_ptr<Window> panel_;
  fcitx::InputContext* owner_ = nullptr;
  fcitx::Rect anchor_;  // owner_'s, for placing the window again
  std::optional<PanelContent> content_;
  bool proofBelow_ = false;  // window above the text: proof row last, next to the text
  bool rounded_ = false;     // a compositor shows the corners through
  Rect pillFrom_, pillTo_;
  Frame frameFrom_, frameTo_;
  uint64_t slideStart_ = 0, resizeStart_ = 0;
  bool sliding_ = false, resizing_ = false;
  std::unique_ptr<fcitx::EventSourceTime> panelTimer_;

  std::unique_ptr<Window> hint_;
  std::optional<PanelContent> hintContent_;

  std::unique_ptr<Window> badge_;
  std::string badgeLabel_;
  fcitx::Rect badgeAnchor_;
  float badgeAlpha_ = 1;
  bool badgeFades_ = false;  // a compositor shows its alpha
  uint64_t badgeStart_ = 0;
  std::unique_ptr<fcitx::EventSourceTime> badgeTimer_;
};

}  // namespace ProjectKey
