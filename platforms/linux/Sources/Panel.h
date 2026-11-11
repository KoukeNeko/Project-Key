// The Direction A candidate window and the 中／英 badge, drawn with Cairo and
// Pango: a port of the Windows CandidateWindow and ModeBadge
// (platforms/windows/Sources/TextService/CandidateWindow.cpp), with the same
// metrics and colors. They go on an X11 screen or, under Wayland, into input
// method popups (Screen.h); ok() is false when there is neither.
#pragma once

#include <cairo.h>
#include <fcitx-utils/dbus/bus.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/rect.h>
#include <fcitx/inputcontext.h>
#include <pango/pango.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Controller.h"
#include "PanelMetrics.h"
#include "Screen.h"

namespace ProjectKey {

class Panel {
 public:
  // settings gives the appearance, Setting::kAppearance: 0 深色, 1 模糊 (blurs what is behind
  // the windows), 2 淺色, 3 自動; 模糊 and 自動 are light or dark as the desktop is.
  Panel(fcitx::Instance* instance, const Settings& settings);
  ~Panel();
  Panel(const Panel&) = delete;
  Panel& operator=(const Panel&) = delete;

  bool ok() const { return screen_ != nullptr; }
  // Whether the windows can go at ic's caret; Fcitx5's panel stands in where not.
  bool shows(const fcitx::InputContext& ic) const { return screen_ && screen_->reaches(ic); }
  // What keeps the windows from showing as they should, for the install check.
  std::vector<std::string> problems() const;
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
  // A click on a candidate or the chevron of the window ic shows.
  std::function<void(fcitx::InputContext& ic, const Metrics::PanelHit& hit)> clicked;

 private:
  struct Rect {
    float left = 0, top = 0, right = 0, bottom = 0;
    bool empty() const { return right <= left || bottom <= top; }
    bool operator==(const Rect&) const = default;
  };

  // Whether the windows should blur what is behind them: the setting asks for
  // it and the compositor (KWin) offers it.
  bool blurs();
  // Whether the windows are drawn light: 淺色, or 自動 and 模糊 while the desktop is light.
  bool light() const;
  void watchColorScheme(fcitx::Instance* instance);
  void setFontOptions(bool blurred);
  void startTimer(std::unique_ptr<fcitx::EventSourceTime>& timer, bool (Panel::*tick)());
  // A window fading in or out, between two opacities; start is 0 when it is not.
  struct Fade {
    uint64_t start = 0;
    float from = 0, to = 1;
    bool hiding() const { return start && to == 0; }
  };
  bool startFade(Fade& fade, float to);
  float fadeAlpha(const Fade& fade) const;
  static bool endFade(Fade& fade, uint64_t now);
  void finishHide();
  void finishHideHint();

  // Candidate window.
  void paintPanel();
  bool tickPanel();
  void clickPanel(int x, int y);
  void hoverPanel(int x, int y);
  Metrics::PanelHit hitPanel(int x, int y) const;
  float proofHeight() const;
  float rowHeight() const;
  float rowTop() const;
  float keyWidth(const PanelContent& content) const;
  float proofWidth(const PanelContent& content) const;
  float fittingWidth() const;
  float tailWidth() const;
  Rect selectionRect() const;  // relative to the rows
  Rect shownSelectionRect() const;
  float shownRowTop() const;  // where the rows start, as drawn now
  bool proofMoves() const;
  void drawProofRow(cairo_t* cr, const PanelContent& content, bool below, float alpha, float width, float height);
  void drawProof(cairo_t* cr, const PanelContent& content, float top, float width);
  void drawCandidates(cairo_t* cr, float top, float width);

  // Tab hint.
  int hintHeight() const;
  void placeHint();
  void paintHint();

  // Badge.
  void placeBadge(const fcitx::Rect& caret);  // as Screen::caret gives it
  void avoidPanel();
  void paintBadge();
  bool tickBadge();

  float width(const std::string& text, PangoFontDescription* font) const;
  PangoLayout* layout(const std::string& text, PangoFontDescription* font) const;

  const Settings& settings_;
  fcitx::EventLoop& loop_;
  std::unique_ptr<Screen> screen_;
  float scale_ = 1;
  bool light_ = false;  // light() as the windows last showed
  // The desktop portal's org.freedesktop.appearance color-scheme (1 dark, 2 light, 0 no
  // preference), kept current by its SettingChanged signal; unset until it answers.
  std::optional<uint32_t> colorScheme_;
  std::unique_ptr<fcitx::dbus::Slot> colorSchemeRead_, colorSchemeChanged_;

  PangoContext* context_ = nullptr;
  PangoFontDescription *proofFont_ = nullptr, *candidateFont_ = nullptr, *labelFont_ = nullptr, *pageFont_ = nullptr,
                       *keyFont_ = nullptr, *badgeFont_ = nullptr;
  float columnWidth_ = 0;

  std::unique_ptr<Window> panel_;
  fcitx::InputContext* owner_ = nullptr;
  fcitx::Rect anchor_;  // owner_'s, for placing the window again
  std::optional<PanelContent> content_;
  bool proofBelow_ = false;  // window above the text: proof row last, next to the text
  bool rounded_ = false;     // a compositor shows the corners through
  Rect pillFrom_, pillTo_;  // the highlight slides from one to the other, relative to the rows
  Frame frameFrom_, frameTo_;
  uint64_t slideStart_ = 0, resizeStart_ = 0;
  // resizing_: the window changing shape, moving and resizing from frameFrom_ to frameTo_ while
  // progress_ runs from 0 to 1: the rows move from oldRowTop_ and a proof row that comes, goes
  // or changes sides fades from its place in oldContent_ to its new one.
  bool sliding_ = false, resizing_ = false;
  Metrics::PanelHit hover_;  // the candidate under the pointer
  std::vector<std::vector<int>> spans_;  // grid columns of content_'s candidates, for hitPanel
  std::optional<PanelContent> oldContent_;
  bool oldProofBelow_ = false;
  float oldRowTop_ = 0, progress_ = 1;
  Fade panelFade_;
  std::unique_ptr<fcitx::EventSourceTime> panelTimer_;

  std::unique_ptr<Window> hint_;
  std::optional<PanelContent> hintContent_;
  std::optional<PanelContent> fadingHint_;  // what the hint shows as it fades out
  Fade hintFade_;

  std::unique_ptr<Window> badge_;
  std::string badgeLabel_;
  fcitx::Rect badgeCaret_;
  float badgeAlpha_ = 1;
  bool badgeFades_ = false;  // a compositor shows its alpha
  uint64_t badgeStart_ = 0;
  std::unique_ptr<fcitx::EventSourceTime> badgeTimer_;
};

}  // namespace ProjectKey
