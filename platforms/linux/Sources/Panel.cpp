#include "Panel.h"

#include <fcitx/instance.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>

#include "PanelMetrics.h"

namespace ProjectKey {

namespace {

using namespace Metrics;
using namespace Metrics::Colors;

// Windows' corners, fonts and sizes (platforms/windows/Sources/Common/Layout.h).
constexpr float kPillRadius = 4;
constexpr float kProofSize = 14, kCandidateSize = 19, kLabelSize = 12, kPageSize = 11, kKeySize = 11, kBadgeSize = 15;
constexpr char kTextFont[] = "Noto Sans CJK TC";
constexpr char kMonoFont[] = "Noto Sans Mono, DejaVu Sans Mono, monospace";
constexpr char kKeyFont[] = "Noto Sans, sans-serif";
constexpr char kReplacedArrow[] = " → ";
// The dark background's opacity over a blur. Darker than over Acrylic (0.6):
// KWin's blur adds no tint of its own, so over a white page 0.6 reads as light gray.
constexpr float kAcrylicTint = 0.85f;
constexpr uint64_t kFrameUs = 8000;

uint64_t NowMs() {
  return fcitx::now(CLOCK_MONOTONIC) / 1000;
}

float Ease(float t) {
  return 1 - std::pow(1 - t, 3.f);
}

void SetColor(cairo_t* cr, uint32_t rgb, float alpha = 1) {
  cairo_set_source_rgba(cr, ((rgb >> 16) & 0xff) / 255.0, ((rgb >> 8) & 0xff) / 255.0, (rgb & 0xff) / 255.0, alpha);
}

void RoundedRect(cairo_t* cr, double left, double top, double right, double bottom, double radius) {
  radius = std::min({radius, (right - left) / 2, (bottom - top) / 2});
  cairo_new_sub_path(cr);
  cairo_arc(cr, right - radius, top + radius, radius, -M_PI / 2, 0);
  cairo_arc(cr, right - radius, bottom - radius, radius, 0, M_PI / 2);
  cairo_arc(cr, left + radius, bottom - radius, radius, M_PI / 2, M_PI);
  cairo_arc(cr, left + radius, top + radius, radius, M_PI, 3 * M_PI / 2);
  cairo_close_path(cr);
}

// A window's background: dark with a 1px edge, rounded where a compositor shows
// the corners; over a blur, a dark tint, as over Acrylic on Windows.
void DrawBackground(cairo_t* cr, float width, float height, bool rounded, bool blurred) {
  RoundedRect(cr, 0.5, 0.5, width - 0.5, height - 0.5, rounded ? kWindowRadius : 0);
  SetColor(cr, kBackground, blurred ? kAcrylicTint : 1);
  cairo_fill_preserve(cr);
  SetColor(cr, kLine, blurred ? 0.6f : 1);
  cairo_set_line_width(cr, 1);
  cairo_stroke(cr);
}

// One line of text laid out with a font, and where to draw it.
struct Line {
  PangoLayout* layout = nullptr;
  float width = 0, height = 0, baseline = 0;
  explicit Line(PangoLayout* l) : layout(l) {
    PangoRectangle logical;
    pango_layout_get_extents(layout, nullptr, &logical);
    width = std::ceil(static_cast<float>(logical.width) / PANGO_SCALE);
    height = static_cast<float>(logical.height) / PANGO_SCALE;
    baseline = static_cast<float>(pango_layout_get_baseline(layout)) / PANGO_SCALE;
  }
  ~Line() { g_object_unref(layout); }
  Line(const Line&) = delete;
  Line& operator=(const Line&) = delete;
  void draw(cairo_t* cr, float x, float y) const {
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
  }
  // Centered vertically in a line box starting at `top`.
  void draw(cairo_t* cr, float x, float top, float lineHeight) const { draw(cr, x, top + (lineHeight - height) / 2); }
};

PangoFontDescription* Font(const char* family, float size, PangoWeight weight) {
  PangoFontDescription* font = pango_font_description_from_string(family);
  pango_font_description_set_absolute_size(font, size * PANGO_SCALE);
  pango_font_description_set_weight(font, weight);
  return font;
}

}  // namespace

Panel::Panel(fcitx::Instance* instance, const Settings& settings) : settings_(settings), loop_(instance->eventLoop()) {
  // X windows cannot be placed at a Wayland app's caret; input method popups can, and on
  // GNOME, which has none, its shell can.
  const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
  bool gnome = desktop && std::string_view(desktop).find("GNOME") != std::string_view::npos;
  if (!std::getenv("WAYLAND_DISPLAY")) {
    screen_ = OpenX11Screen(loop_);
  } else {
    screen_ = gnome ? OpenGnomeScreen(instance) : OpenWaylandScreen(instance);
  }
  if (!screen_) return;
  scale_ = screen_->scale();

  context_ = pango_font_map_create_context(pango_cairo_font_map_get_default());
  proofFont_ = Font(kTextFont, kProofSize, PANGO_WEIGHT_NORMAL);
  candidateFont_ = Font(kTextFont, kCandidateSize, PANGO_WEIGHT_NORMAL);
  labelFont_ = Font(kMonoFont, kLabelSize, PANGO_WEIGHT_MEDIUM);
  pageFont_ = Font(kMonoFont, kPageSize, PANGO_WEIGHT_MEDIUM);
  keyFont_ = Font(kKeyFont, kKeySize, PANGO_WEIGHT_MEDIUM);
  badgeFont_ = Font(kTextFont, kBadgeSize, PANGO_WEIGHT_MEDIUM);
  // Every column fits a two-character word.
  columnWidth_ = kCellSide + width("6", labelFont_) + kLabelGap + width("國國", candidateFont_) + kCellSide;
}

Panel::~Panel() {
  panelTimer_.reset();
  badgeTimer_.reset();
  panel_.reset();
  hint_.reset();
  badge_.reset();
  for (PangoFontDescription* font : {proofFont_, candidateFont_, labelFont_, pageFont_, keyFont_, badgeFont_}) {
    if (font) pango_font_description_free(font);
  }
  if (context_) g_object_unref(context_);
}

PangoLayout* Panel::layout(const std::string& text, PangoFontDescription* font) const {
  PangoLayout* layout = pango_layout_new(context_);
  pango_layout_set_font_description(layout, font);
  pango_layout_set_text(layout, text.data(), static_cast<int>(text.size()));
  return layout;
}

float Panel::width(const std::string& text, PangoFontDescription* font) const {
  return text.empty() ? 0 : Line(layout(text, font)).width;
}

std::vector<std::string> Panel::problems() const {
  std::vector<std::string> problems;
  if (!screen_) {
    problems.push_back("連不上 X11 或 Wayland 顯示，候選字窗改用 Fcitx5 的面板");
  } else if (std::string problem = screen_->problem(); !problem.empty()) {
    problems.push_back(problem);
  }
  // The other fonts fall back to a generic family; this one has no stand-in.
  PangoFontFamily** families = nullptr;
  int count = 0;
  pango_font_map_list_families(pango_cairo_font_map_get_default(), &families, &count);
  bool found = std::any_of(families, families + count,
                           [](PangoFontFamily* family) { return g_ascii_strcasecmp(pango_font_family_get_name(family), kTextFont) == 0; });
  g_free(families);
  if (!found) problems.push_back(std::string("缺少 ") + kTextFont + " 字型（Debian／Ubuntu 的 fonts-noto-cjk），候選字會用別的字型");
  return problems;
}

int Panel::span(const std::string& candidate) const {
  float cell = kCellSide + width("6", labelFont_) + kLabelGap + width(candidate, candidateFont_) + kCellSide;
  int span = static_cast<int>(std::ceil((cell + kCellGap) / (columnWidth_ + kCellGap)));
  return std::clamp(span, 1, kGridColumns);
}

// MARK: Windows

bool Panel::blurs() {
  return settings_.get(Setting::kAppearance) != 0 && screen_->blurs();
}

// Over a blur the text sits on a translucent window, where subpixel
// antialiasing would fringe; Windows draws it grayscale there too.
void Panel::setFontOptions(bool blurred) {
  cairo_font_options_t* options = cairo_font_options_create();
  if (blurred) cairo_font_options_set_antialias(options, CAIRO_ANTIALIAS_GRAY);
  pango_cairo_context_set_font_options(context_, options);
  cairo_font_options_destroy(options);
}

// A repeating frame timer. It is never destroyed while it may be running its
// callback: a finished animation just lets the one-shot lapse.
void Panel::startTimer(std::unique_ptr<fcitx::EventSourceTime>& timer, bool (Panel::*tick)()) {
  uint64_t next = fcitx::now(CLOCK_MONOTONIC) + kFrameUs;
  if (timer) {
    timer->setTime(next);
    timer->setOneShot();
    return;
  }
  timer = loop_.addTimeEvent(CLOCK_MONOTONIC, next, 0, [this, tick](fcitx::EventSourceTime* source, uint64_t) {
    if ((this->*tick)()) {
      source->setNextInterval(kFrameUs);
      source->setOneShot();
    }
    return true;
  });
}

// MARK: Candidate window

float Panel::proofHeight() const {
  return content_ && content_->showsProof ? kProofPad + kProofLine + kProofPad : 0;  // marks sit in the bottom pad
}

float Panel::rowHeight() const {
  size_t rows = content_ ? content_->rows.size() : 0;
  if (!rows) return 0;
  return kRowTop + rows * (kCellTop + kCellLine + kCellBottom) + (rows - 1) * kCellGap + kRowBottom;
}

float Panel::rowTop() const {
  bool both = content_->showsProof && !content_->rows.empty();
  return proofBelow_ ? 0 : proofHeight() + (both ? 1 : 0);
}

float Panel::keyWidth(const PanelContent& c) const {
  return std::max(kKeyHeight, width(c.keyHint, keyFont_) + 2 * kKeyPadding);
}

float Panel::proofWidth(const PanelContent& c) const {
  std::string sentence;
  for (const std::string& segment : c.segments) sentence += segment;
  sentence += c.pendingReading;
  float key = c.keyHint.empty() ? 0 : kKeyGap + keyWidth(c);
  if (!c.replacedText.empty()) key += width(c.replacedText + kReplacedArrow, proofFont_);
  return kProofLeft + width(sentence, proofFont_) + key + kProofRight;
}

float Panel::fittingWidth() const {
  float result = content_->showsProof ? proofWidth(*content_) : 0;
  if (!content_->rows.empty()) {
    float row = kRowLeft + kGridColumns * (columnWidth_ + kCellGap);
    result = std::max(result, row + tailWidth());
  }
  return result;
}

// The first row's end: a down chevron while folded with more rows; unfolded, the page label
// and an up chevron. Every row keeps room for them and for the widest label (999/999, or
// wider for more rows), so the window keeps its width from typing to choosing, folded or not.
float Panel::tailWidth() const {
  std::string total = content_->pageLabel.substr(content_->pageLabel.find('/') + 1);
  if (total.size() < 3) total = "999";
  return kPageLeft + width(total + "/" + total, pageFont_) + kChevronGap + kChevronWidth + kChevronInset;
}

Panel::Rect Panel::selectionRect() const {
  if (!content_) return {};
  const auto& rows = content_->rows;
  // While typing (selectedRow -1), the one row, drawn as when choosing though its digits type
  // Zhuyin, so choosing from it changes nothing on screen.
  int labeledRow = std::max(0, content_->selectedRow);
  if (labeledRow >= static_cast<int>(rows.size())) return {};
  const auto& row = rows[labeledRow];
  if (content_->selection < 0 || content_->selection >= static_cast<int>(row.size())) return {};
  float cellHeight = kCellTop + kCellLine + kCellBottom;
  int column = 0;
  for (int i = 0; i < content_->selection; i++) column += span(row[i]);
  int cells = span(row[content_->selection]);
  float left = kRowLeft + column * (columnWidth_ + kCellGap);
  float top = rowTop() + kRowTop + labeledRow * (cellHeight + kCellGap);
  return {left, top, left + cells * columnWidth_ + (cells - 1) * kCellGap, top + cellHeight};
}

Panel::Rect Panel::shownSelectionRect() const {
  if (!sliding_) return pillTo_;
  float t = Ease(std::min(1.f, static_cast<float>(NowMs() - slideStart_) / kSlideMs));
  auto mix = [t](float a, float b) { return a + (b - a) * t; };
  return {mix(pillFrom_.left, pillTo_.left), mix(pillFrom_.top, pillTo_.top), mix(pillFrom_.right, pillTo_.right),
          mix(pillFrom_.bottom, pillTo_.bottom)};
}

void Panel::show(fcitx::InputContext& ic, const PanelContent& content, const fcitx::Rect& anchor) {
  if (!ok()) return;
  scale_ = screen_->scale();  // GNOME's extension reports it after Project Key loads
  if (!panel_) {
    // On Wayland the compositor can move the window above the text once it sees its size.
    panel_ = screen_->createWindow([this] { paintPanel(); },
                                   [this] {
                                     if (owner_ && content_ && panel_->mapped) show(*owner_, PanelContent(*content_), anchor_);
                                   });
  }
  bool wasVisible = panel_->mapped;
  // Changes within one list move: the highlight slides and the window resizes.
  bool animate = screen_->animates() && wasVisible && content_ && !content_->rows.empty() && !content.rows.empty();
  Rect from = shownSelectionRect();
  owner_ = &ic;
  anchor_ = anchor;
  content_ = content;
  if (!wasVisible) {
    rounded_ = screen_->composited();
    panel_->setBlur(blurs());
  }

  float separator = content_->showsProof && !content_->rows.empty() ? 1.f : 0.f;
  int width = static_cast<int>(std::ceil(fittingWidth() * scale_));
  int height = static_cast<int>(std::ceil((proofHeight() + separator + rowHeight()) * scale_));
  int gap = static_cast<int>(std::lround(kGapToText * scale_));
  fcitx::Rect caret = screen_->caret(ic, anchor);
  int left = caret.left() - static_cast<int>(std::lround(kAnchorInset * scale_));
  int top = caret.bottom() + gap;
  // A Tab hint goes under the window too, so it needs the room as well.
  int hintRoom = hintContent_ ? hintHeight() + static_cast<int>(std::lround(kHintGap * scale_)) : 0;
  proofBelow_ = panel_->above() || top + height + hintRoom > screen_->height();
  if (proofBelow_) top = caret.top() - gap - height;
  left = std::max(0, std::min(left, screen_->width() - width));

  Rect to = selectionRect();
  sliding_ = animate && !from.empty() && !to.empty() && !(from == to);
  if (sliding_) {
    pillFrom_ = from;
    slideStart_ = NowMs();
  }
  pillTo_ = to;

  resizing_ = animate && (panel_->frame.width != width || panel_->frame.height != height);
  if (resizing_) {
    frameFrom_ = panel_->frame;
    frameTo_ = {left, top, width, height};
    resizeStart_ = NowMs();
  } else {
    panel_->place({left, top, width, height});
  }
  panel_->map(ic);
  paintPanel();
  if (badge_ && badge_->mapped) {  // the badge and the panel both sit under the caret
    avoidPanel();
    paintBadge();
  }
  if (hint_ && hint_->mapped) {  // beside the panel, which may have moved or grown
    placeHint();
    paintHint();
  }
  if (sliding_ || resizing_) startTimer(panelTimer_, &Panel::tickPanel);
  screen_->sync();
}

bool Panel::tickPanel() {
  if (!panel_->mapped) return false;
  uint64_t now = NowMs();
  if (sliding_ && now - slideStart_ >= static_cast<uint64_t>(kSlideMs)) sliding_ = false;
  if (resizing_) {
    float t = std::min(1.f, static_cast<float>(now - resizeStart_) / kResizeMs);
    float e = Ease(t);
    auto mix = [e](int a, int b) { return static_cast<int>(std::lround(a + (b - a) * e)); };
    int left = mix(frameFrom_.x, frameTo_.x), top = mix(frameFrom_.y, frameTo_.y);
    int right = mix(frameFrom_.x + frameFrom_.width, frameTo_.x + frameTo_.width);
    int bottom = mix(frameFrom_.y + frameFrom_.height, frameTo_.y + frameTo_.height);
    panel_->place({left, top, right - left, bottom - top});
    if (t >= 1) resizing_ = false;
  }
  paintPanel();
  return sliding_ || resizing_;
}

void Panel::hide(const fcitx::InputContext& ic) {
  if (!panel_ || &ic != owner_) return;
  if (panelTimer_) panelTimer_->setEnabled(false);
  sliding_ = resizing_ = false;
  panel_->unmap();
  content_.reset();
  owner_ = nullptr;
  hideHint();
}

// MARK: Tab hint

void Panel::showHint(fcitx::InputContext& ic, const PanelContent& hint) {
  if (!ok() || !panel_ || !panel_->mapped || &ic != owner_) return;
  if (!hint_) hint_ = screen_->createWindow([this] { paintHint(); }, [this] { paintHint(); });
  if (!hint_->mapped) hint_->setBlur(blurs());
  bool hadHint = hintContent_.has_value();
  hintContent_ = hint;
  // Placed without room for a hint under it, the window may have to go above the text now.
  if (!hadHint && !proofBelow_) show(ic, PanelContent(*content_), anchor_);
  placeHint();
  hint_->map(ic);
  paintHint();
  screen_->sync();
}

void Panel::hideHint() {
  if (!hint_) return;
  hint_->unmap();
  hintContent_.reset();
}

int Panel::hintHeight() const {
  return static_cast<int>(std::ceil((kProofPad + kProofLine + kProofPad) * scale_));
}

// Its left edge on the panel's, kept on screen, on the panel's side away from the
// text: below it under the text, above it over the text (show() leaves the room).
// shortcut: on Wayland the compositor turns each popup by its own height, so a hint the panel's
// side cannot fit can open on the other; place both from one popup if that shows.
void Panel::placeHint() {
  if (!hintContent_) return;
  const Frame& next = resizing_ ? frameTo_ : panel_->frame;
  int width = static_cast<int>(std::ceil(proofWidth(*hintContent_) * scale_)), height = hintHeight();
  int gap = static_cast<int>(std::lround(kHintGap * scale_));
  int left = std::max(0, std::min(next.x, screen_->width() - width));
  int top = proofBelow_ ? next.y - gap - height : next.y + next.height + gap;
  hint_->place({left, top, width, height});
}

void Panel::paintHint() {
  if (!hintContent_ || !hint_->mapped) return;
  cairo_t* cr = hint_->beginPaint();
  if (!cr) return;
  bool blurred = hint_->blurred;
  cairo_push_group(cr);
  setFontOptions(blurred);
  pango_cairo_update_context(cr, context_);
  cairo_scale(cr, scale_, scale_);
  float width = hint_->frame.width / scale_, height = hint_->frame.height / scale_;
  DrawBackground(cr, width, height, rounded_, blurred);
  drawProof(cr, *hintContent_, 0, width);
  cairo_pop_group_to_source(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint(cr);
  hint_->endPaint(cr);
}

void Panel::paintPanel() {
  if (!content_ || !panel_->mapped) return;
  cairo_t* cr = panel_->beginPaint();
  if (!cr) return;
  bool blurred = panel_->blurred;
  cairo_push_group(cr);
  setFontOptions(blurred);
  pango_cairo_update_context(cr, context_);
  cairo_scale(cr, scale_, scale_);
  float width = panel_->frame.width / scale_, height = panel_->frame.height / scale_;

  DrawBackground(cr, width, height, rounded_, blurred);
  float lineAlpha = blurred ? 0.6f : 1;

  bool both = content_->showsProof && !content_->rows.empty();
  // Above the text the proof row hugs the bottom edge, so it stays put while the window resizes.
  float proofTop = proofBelow_ ? height - proofHeight() : 0;
  if (both) {
    float y = proofBelow_ ? proofTop - 1 : proofHeight();
    SetColor(cr, kLine, lineAlpha);
    cairo_rectangle(cr, 0, y, width, 1);
    cairo_fill(cr);
  }
  if (content_->showsProof) drawProof(cr, *content_, proofTop, width);
  if (!content_->rows.empty()) {
    // While the window grows above the text, rows laid out for the final size
    // would run over the pinned proof row; keep them above it.
    bool clip = proofBelow_ && content_->showsProof;
    if (clip) {
      cairo_save(cr);
      cairo_rectangle(cr, 0, 0, width, proofTop - 1);
      cairo_clip(cr);
    }
    drawCandidates(cr, rowTop(), width);
    if (clip) cairo_restore(cr);
  }

  cairo_pop_group_to_source(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint(cr);
  panel_->endPaint(cr);
}

void Panel::drawProof(cairo_t* cr, const PanelContent& c, float top, float windowWidth) {
  float x = kProofLeft;
  // The line stays centered; marks sit in the space below it, so they come and
  // go without moving the text.
  float lineTop = top + kProofPad;
  float markTop = lineTop + kProofLine + kMarkGap;
  if (!c.keyHint.empty()) {
    float w = keyWidth(c);
    float right = windowWidth - kProofRight;
    float capTop = lineTop + (kProofLine - kKeyHeight) / 2, capBottom = capTop + kKeyHeight;
    // A key: a face with a hairline edge, raised on a darker lip along its bottom.
    cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
    cairo_set_line_width(cr, kKeyLip);
    cairo_move_to(cr, right - w + kKeyRadius, capBottom + kKeyLip / 2);
    cairo_line_to(cr, right - kKeyRadius, capBottom + kKeyLip / 2);
    cairo_stroke(cr);
    RoundedRect(cr, right - w + 0.25, capTop + 0.25, right - 0.25, capBottom - 0.25, kKeyRadius);
    SetColor(cr, kMuted, 0.16f);
    cairo_fill_preserve(cr);
    SetColor(cr, kMuted, 0.35f);
    cairo_set_line_width(cr, 0.5);
    cairo_stroke(cr);
    Line key(layout(c.keyHint, keyFont_));
    SetColor(cr, kText);
    key.draw(cr, right - w + (w - key.width) / 2, capTop, kKeyHeight);
  }
  if (!c.replacedText.empty()) {
    Line replaced(layout(c.replacedText, proofFont_));
    PangoAttrList* attributes = pango_attr_list_new();
    pango_attr_list_insert(attributes, pango_attr_strikethrough_new(TRUE));
    pango_layout_set_attributes(replaced.layout, attributes);
    pango_attr_list_unref(attributes);
    SetColor(cr, kMuted);
    replaced.draw(cr, x, lineTop, kProofLine);
    x += replaced.width;
    Line arrow(layout(kReplacedArrow, proofFont_));
    arrow.draw(cr, x, lineTop, kProofLine);
    x += arrow.width;
  }
  uint32_t length = 0;
  for (const std::string& segment : c.segments) length += Utf16Length(segment);
  const bool hint = !c.replacedText.empty();  // a conversion preview, not the proof row's Tab
  // The pending reading, or a caret line when there is none, sits at the cursor.
  auto drawCursor = [&](float& at) {
    if (!c.pendingReading.empty()) {
      Line line(layout(c.pendingReading, proofFont_));
      SetColor(cr, kMuted);
      line.draw(cr, at, lineTop, kProofLine);
      at += line.width;
    } else if (c.caret < length && c.activeSegment < 0) {
      SetColor(cr, kAccentSoft);
      cairo_rectangle(cr, at, lineTop + 1, 1, kProofLine - 2);
      cairo_fill(cr);
    }
  };
  if (c.caret == 0) drawCursor(x);
  uint32_t start = 0;
  for (size_t i = 0; i < c.segments.size(); i++) {
    const std::string& segment = c.segments[i];
    uint32_t segmentLength = Utf16Length(segment);
    bool active = static_cast<int>(i) == c.activeSegment;
    // A conversion preview shows its result in full text color.
    uint32_t color = active ? kAccentSoft : hint ? kText : kMuted;
    float left = x;
    uint32_t split = c.caret > start && c.caret < start + segmentLength ? c.caret - start : 0;
    auto drawText = [&](const std::string& text) {
      Line line(layout(text, proofFont_));
      SetColor(cr, color);
      line.draw(cr, x, lineTop, kProofLine);
      x += line.width;
    };
    if (split) {
      size_t bytes = Utf8Bytes(segment, split);
      drawText(segment.substr(0, bytes));
      drawCursor(x);
      drawText(segment.substr(bytes));
    } else {
      drawText(segment);
    }
    bool doubtful = i < c.doubtful.size() && c.doubtful[i];
    if (active || doubtful) {
      SetColor(cr, kAccentSoft, active ? 1 : kDoubtMarkAlpha);
      RoundedRect(cr, left + kMarkInset, markTop, x - kMarkInset, markTop + kMarkHeight, 1);
      cairo_fill(cr);
    }
    start += segmentLength;
    if (c.caret == start && start > 0) drawCursor(x);
  }
}

void Panel::drawCandidates(cairo_t* cr, float top, float windowWidth) {
  float cellHeight = kCellTop + kCellLine + kCellBottom;
  Rect pill = shownSelectionRect();
  if (!pill.empty()) {
    if (content_->selectedRow >= 0) {
      SetColor(cr, kAccent);
    } else {
      SetColor(cr, kText, kGuessAlpha);
    }
    RoundedRect(cr, pill.left, pill.top, pill.right, pill.bottom, kPillRadius);
    cairo_fill(cr);
  }
  float labelWidth = width("6", labelFont_);
  for (size_t r = 0; r < content_->rows.size(); r++) {
    float cellTop = top + kRowTop + r * (cellHeight + kCellGap);
    bool labeled = static_cast<int>(r) == std::max(0, content_->selectedRow);  // as selectionRect says
    int column = 0;
    const auto& row = content_->rows[r];
    for (size_t i = 0; i < row.size(); i++) {
      float x = kRowLeft + column * (columnWidth_ + kCellGap);
      int cells = span(row[i]);
      column += cells;
      bool on = labeled && content_->selectedRow >= 0 && static_cast<int>(i) == content_->selection;  // on the accent
      Line word(layout(row[i], candidateFont_));
      float wordTop = cellTop + kCellTop + (kCellLine - word.height) / 2;
      if (labeled) {
        // The label shares the candidate's baseline.
        Line label(layout(std::to_string(i + 1), labelFont_));
        if (on) {
          cairo_set_source_rgba(cr, 1, 1, 1, 0.72);
        } else {
          SetColor(cr, kMuted);
        }
        label.draw(cr, x + kCellSide, wordTop + word.baseline - label.baseline);
      }
      if (on) {
        cairo_set_source_rgb(cr, 1, 1, 1);
      } else {
        SetColor(cr, kText);
      }
      word.draw(cr, x + kCellSide + labelWidth + kLabelGap, wordTop);
    }
  }
  if (!content_->pageLabel.empty()) {  // the chevron and page label sit on the first row
    float x = windowWidth - kChevronInset - kChevronWidth, middle = top + kChevronMiddle;
    float tip = content_->expanded ? -kChevronHeight / 2 : kChevronHeight / 2;  // up or down
    SetColor(cr, kMuted);
    cairo_save(cr);
    cairo_move_to(cr, x, middle - tip);
    cairo_line_to(cr, x + kChevronWidth / 2, middle + tip);
    cairo_line_to(cr, x + kChevronWidth, middle - tip);
    cairo_set_line_width(cr, kChevronLine);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_stroke(cr);
    cairo_restore(cr);
    if (content_->expanded) {
      Line page(layout(content_->pageLabel, pageFont_));
      page.draw(cr, x - kChevronGap - page.width, top + kRowTop + kCellTop, kCellLine);
    }
  }
}

void Panel::follow(fcitx::InputContext& ic, const fcitx::Rect& anchor) {
  if (!ok()) return;
  if (badge_ && badge_->mapped) {
    placeBadge(screen_->caret(ic, anchor));
    paintBadge();
  }
  if (&ic == owner_ && content_ && panel_->mapped) show(ic, PanelContent(*content_), anchor);
}

// MARK: Badge

void Panel::showBadge(fcitx::InputContext& ic, const std::string& label, const fcitx::Rect& anchor) {
  if (!ok()) return;
  scale_ = screen_->scale();
  if (!badge_) {
    badge_ = screen_->createWindow([this] { paintBadge(); },
                                   [this] {
                                     if (!badge_->mapped) return;
                                     placeBadge(badgeCaret_);
                                     paintBadge();
                                   });
  }
  badgeLabel_ = label;
  placeBadge(screen_->caret(ic, anchor));
  badgeStart_ = NowMs();
  // Without a compositor a window cannot fade or round its corners: it shows square at full strength.
  badgeFades_ = screen_->composited();
  badgeAlpha_ = badgeFades_ ? 0 : 1;
  badge_->setBlur(blurs());  // the same appearance as the panel
  badge_->map(ic);
  paintBadge();
  startTimer(badgeTimer_, &Panel::tickBadge);
  screen_->sync();
}

// Under the caret, above it at the screen's end, beside the panel when they overlap.
void Panel::placeBadge(const fcitx::Rect& caret) {
  badgeCaret_ = caret;
  int width = static_cast<int>(std::ceil(std::max(kBadgeSide, this->width(badgeLabel_, badgeFont_) + 2 * kBadgePadding) * scale_));
  int height = static_cast<int>(std::ceil(kBadgeSide * scale_));
  int gap = static_cast<int>(std::lround(kGapToText * scale_));
  int x = caret.left(), y = caret.bottom() + gap;
  if (badge_->above() || y + height > screen_->height()) y = caret.top() - gap - height;
  badge_->place({x, y, width, height});
  avoidPanel();
}

// Both hang from the caret, so their tops already line up: the badge moves to
// the panel's right, or its left when the screen ends.
void Panel::avoidPanel() {
  if (!badge_ || !panel_ || !panel_->mapped) return;
  const Frame& p = resizing_ ? frameTo_ : panel_->frame;
  const Frame& b = badge_->frame;
  bool overlap = b.x < p.x + p.width && p.x < b.x + b.width && b.y < p.y + p.height && p.y < b.y + b.height;
  if (!overlap) return;
  int gap = static_cast<int>(std::lround(kGapToText * scale_));
  int x = p.x + p.width + gap;
  if (x + b.width > screen_->width()) x = p.x - gap - b.width;
  badge_->place({x, b.y, b.width, b.height});
}

void Panel::hideBadge() {
  if (!badge_) return;
  if (badgeTimer_) badgeTimer_->setEnabled(false);
  badge_->unmap();
}

// 120ms in, 800ms held, 250ms out.
bool Panel::tickBadge() {
  if (!badge_->mapped) return false;
  float elapsed = static_cast<float>(NowMs() - badgeStart_);
  if (elapsed >= kBadgeFadeInMs + kBadgeHoldMs + kBadgeFadeOutMs) {
    hideBadge();
    return false;
  }
  if (!badgeFades_) return true;
  badgeAlpha_ = elapsed < kBadgeFadeInMs                 ? elapsed / kBadgeFadeInMs
                : elapsed < kBadgeFadeInMs + kBadgeHoldMs ? 1
                                                          : 1 - (elapsed - kBadgeFadeInMs - kBadgeHoldMs) / kBadgeFadeOutMs;
  paintBadge();
  return true;
}

void Panel::paintBadge() {
  if (!badge_ || !badge_->mapped) return;
  cairo_t* cr = badge_->beginPaint();
  if (!cr) return;
  cairo_push_group(cr);
  setFontOptions(badge_->blurred);
  pango_cairo_update_context(cr, context_);
  cairo_scale(cr, scale_, scale_);
  float width = badge_->frame.width / scale_;
  RoundedRect(cr, 0.25, 0.25, width - 0.25, kBadgeSide - 0.25, badgeFades_ ? kWindowRadius : 0);
  SetColor(cr, kBackground, badge_->blurred ? kAcrylicTint : 1);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.5);
  cairo_set_line_width(cr, 0.5);
  cairo_stroke(cr);
  Line line(layout(badgeLabel_, badgeFont_));
  SetColor(cr, kText);
  line.draw(cr, (width - line.width) / 2, 0, kBadgeSide);
  cairo_pop_group_to_source(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint_with_alpha(cr, badgeAlpha_);
  badge_->endPaint(cr);
}

}  // namespace ProjectKey
