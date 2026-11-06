#include "Panel.h"

#include <cairo-xcb.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace ProjectKey {

namespace {

// Metrics and colors from platforms/windows/Sources/Common/Layout.h, in
// device-independent pixels (docs/design/candidate-window, round 03).
constexpr float kWindowRadius = 8, kPillRadius = 4, kGapToText = 8, kAnchorInset = 16;
constexpr float kProofPad = 9, kProofLeft = 19, kProofRight = 19, kProofLine = 20;
constexpr float kMarkGap = 3, kMarkHeight = 2, kMarkInset = 1;
constexpr float kKeyGap = 10, kKeyHeight = 20, kKeyPadding = 6, kKeyRadius = 5, kKeyLip = 1.5;
constexpr float kRowTop = 6, kRowBottom = 6, kRowLeft = 8, kRowRight = 6;
constexpr float kCellTop = 5, kCellBottom = 6, kCellSide = 11, kCellLine = 25, kLabelGap = 5, kCellGap = 2;
constexpr float kPageLeft = 12, kPageRight = 13;
constexpr float kBadgeSide = 30, kBadgePadding = 11;
constexpr int kSlideMs = 120, kResizeMs = 160, kBadgeFadeInMs = 120, kBadgeHoldMs = 800, kBadgeFadeOutMs = 250;
constexpr float kProofSize = 14, kCandidateSize = 19, kLabelSize = 12, kPageSize = 11, kKeySize = 11, kBadgeSize = 15;
constexpr char kTextFont[] = "Noto Sans CJK TC";
constexpr char kMonoFont[] = "Noto Sans Mono, DejaVu Sans Mono, monospace";
constexpr char kKeyFont[] = "Noto Sans, sans-serif";
constexpr char kReplacedArrow[] = " → ";
constexpr uint32_t kBackground = 0x23282c, kLine = 0x343b40, kText = 0xe8ecee, kMuted = 0x8e9aa3;
constexpr uint32_t kAccent = 0x14806f, kAccentSoft = 0x3fb9a3;
constexpr float kDoubtMarkAlpha = 0.7f;
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

}  // namespace

size_t BytesFor(const std::string& text, uint32_t units) {
  size_t i = 0;
  for (uint32_t n = 0; i < text.size() && n < units;) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    size_t length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xe ? 3 : 4;
    n += length == 4 ? 2 : 1;
    i += length;
  }
  return std::min(i, text.size());
}

Panel::Panel(fcitx::EventLoop& loop, const Settings& settings) : settings_(settings), loop_(loop) {
  if (std::getenv("WAYLAND_DISPLAY")) return;  // X windows cannot be placed at a Wayland app's caret
  int screenNumber = 0;
  connection_ = xcb_connect(nullptr, &screenNumber);
  if (xcb_connection_has_error(connection_)) {
    xcb_disconnect(connection_);
    connection_ = nullptr;
    return;
  }
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
  scale_ = DpiScale(connection_, screen_);

  context_ = pango_font_map_create_context(pango_cairo_font_map_get_default());
  proofFont_ = Font(kTextFont, kProofSize, PANGO_WEIGHT_NORMAL);
  candidateFont_ = Font(kTextFont, kCandidateSize, PANGO_WEIGHT_NORMAL);
  labelFont_ = Font(kMonoFont, kLabelSize, PANGO_WEIGHT_MEDIUM);
  pageFont_ = Font(kMonoFont, kPageSize, PANGO_WEIGHT_MEDIUM);
  keyFont_ = Font(kKeyFont, kKeySize, PANGO_WEIGHT_MEDIUM);
  badgeFont_ = Font(kTextFont, kBadgeSize, PANGO_WEIGHT_MEDIUM);
  // Every column fits a two-character word.
  columnWidth_ = kCellSide + width("6", labelFont_) + kLabelGap + width("國國", candidateFont_) + kCellSide;

  io_ = loop_.addIOEvent(xcb_get_file_descriptor(connection_), fcitx::IOEventFlag::In, [this](fcitx::EventSourceIO*, int, fcitx::IOEventFlags) {
    dispatch();
    return true;
  });
  xcb_flush(connection_);
}

Panel::~Panel() {
  panelTimer_.reset();
  badgeTimer_.reset();
  io_.reset();
  for (Window* window : {&panel_, &badge_}) {
    if (window->surface) cairo_surface_destroy(window->surface);
  }
  for (PangoFontDescription* font : {proofFont_, candidateFont_, labelFont_, pageFont_, keyFont_, badgeFont_}) {
    if (font) pango_font_description_free(font);
  }
  if (context_) g_object_unref(context_);
  if (connection_) xcb_disconnect(connection_);
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

int Panel::span(const std::string& candidate) const {
  float cell = kCellSide + width("6", labelFont_) + kLabelGap + width(candidate, candidateFont_) + kCellSide;
  int span = static_cast<int>(std::ceil((cell + kCellGap) / (columnWidth_ + kCellGap)));
  return std::clamp(span, 1, kGridColumns);
}

// MARK: Windows

void Panel::createWindow(Window& window) {
  window.id = xcb_generate_id(connection_);
  uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP;
  uint32_t values[] = {0, 0, 1, XCB_EVENT_MASK_EXPOSURE, colormap_};
  xcb_create_window(connection_, depth_, window.id, screen_->root, 0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, visual_->visual_id, mask,
                    values);
  // Compositors treat it as a popup: no decoration, focus or taskbar entry.
  xcb_atom_t type = Atom(connection_, "_NET_WM_WINDOW_TYPE"), popup = Atom(connection_, "_NET_WM_WINDOW_TYPE_POPUP_MENU");
  xcb_change_property(connection_, XCB_PROP_MODE_REPLACE, window.id, type, XCB_ATOM_ATOM, 32, 1, &popup);
  window.surface = cairo_xcb_surface_create(connection_, window.id, visual_, 1, 1);
  window.width = window.height = 1;
}

void Panel::place(Window& window, int x, int y, int width, int height) {
  width = std::max(width, 1);
  height = std::max(height, 1);
  uint32_t values[] = {static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                       XCB_STACK_MODE_ABOVE};
  xcb_configure_window(connection_, window.id,
                       XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
                           XCB_CONFIG_WINDOW_STACK_MODE,
                       values);
  bool resized = width != window.width || height != window.height;
  if (resized) cairo_xcb_surface_set_size(window.surface, width, height);
  window.x = x;
  window.y = y;
  window.width = width;
  window.height = height;
  if (resized && window.blurred) setBlur(window, true);
}

void Panel::map(Window& window) {
  if (window.mapped) return;
  xcb_map_window(connection_, window.id);
  window.mapped = true;
}

void Panel::unmap(Window& window) {
  if (!window.mapped) return;
  xcb_unmap_window(connection_, window.id);
  window.mapped = false;
  xcb_flush(connection_);
}

void Panel::dispatch() {
  while (xcb_generic_event_t* event = xcb_poll_for_event(connection_)) {
    if ((event->response_type & ~0x80) == XCB_EXPOSE) {
      xcb_window_t window = reinterpret_cast<xcb_expose_event_t*>(event)->window;
      if (window == panel_.id) paintPanel();
      if (window == badge_.id) paintBadge();
    }
    free(event);
  }
}

bool Panel::composited() {
  if (!argb_) return false;
  xcb_get_selection_owner_reply_t* reply =
      xcb_get_selection_owner_reply(connection_, xcb_get_selection_owner(connection_, compositorAtom_), nullptr);
  bool owned = reply && reply->owner != XCB_NONE;
  free(reply);
  return owned;
}

bool Panel::blurs() {
  if (settings_.get(Setting::kAppearance) == 0 || !composited()) return false;
  // KWin's blur effect, while it is on, sets this property on the root window.
  xcb_get_property_reply_t* reply =
      xcb_get_property_reply(connection_, xcb_get_property(connection_, 0, screen_->root, blurAtom_, XCB_GET_PROPERTY_TYPE_ANY, 0, 0), nullptr);
  bool offered = reply && reply->type != XCB_NONE;
  free(reply);
  return offered;
}

// The region KWin blurs: the window's rounded rectangle, one row of pixels at
// a time through the corners, so no blur shows outside them.
void Panel::setBlur(Window& window, bool on) {
  window.blurred = on;
  if (!on) {
    xcb_delete_property(connection_, window.id, blurAtom_);
    return;
  }
  int radius = static_cast<int>(std::lround(kWindowRadius * scale_));
  radius = std::min({radius, window.width / 2, window.height / 2});
  std::vector<uint32_t> rects;
  auto add = [&](int x, int y, int w, int h) {
    rects.insert(rects.end(), {static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(w), static_cast<uint32_t>(h)});
  };
  for (int y = 0; y < radius; y++) {
    double dy = radius - y - 0.5;
    int inset = static_cast<int>(std::lround(radius - std::sqrt(radius * radius - dy * dy)));
    add(inset, y, window.width - 2 * inset, 1);
    add(inset, window.height - 1 - y, window.width - 2 * inset, 1);
  }
  add(0, radius, window.width, window.height - 2 * radius);
  xcb_change_property(connection_, XCB_PROP_MODE_REPLACE, window.id, blurAtom_, XCB_ATOM_CARDINAL, 32, rects.size(), rects.data());
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

float Panel::keyWidth() const {
  return std::max(kKeyHeight, width(content_->keyHint, keyFont_) + 2 * kKeyPadding);
}

float Panel::fittingWidth() const {
  float result = 0;
  if (content_->showsProof) {
    std::string sentence;
    for (const std::string& segment : content_->segments) sentence += segment;
    sentence += content_->pendingReading;
    float key = content_->keyHint.empty() ? 0 : kKeyGap + keyWidth();
    if (!content_->replacedText.empty()) key += width(content_->replacedText + kReplacedArrow, proofFont_);
    result = kProofLeft + width(sentence, proofFont_) + key + kProofRight;
  }
  if (!content_->rows.empty()) {
    // As wide as the fullest visible row, so a short single row stays compact.
    int columns = 0;
    for (const auto& row : content_->rows) {
      int used = 0;
      for (const std::string& candidate : row) used += span(candidate);
      columns = std::max(columns, used);
    }
    float row = kRowLeft + columns * (columnWidth_ + kCellGap) + kRowRight;
    if (!content_->pageLabel.empty()) row += kPageLeft + width(content_->pageLabel, pageFont_) + kPageRight;
    result = std::max(result, row);
  }
  return result;
}

Panel::Rect Panel::selectionRect() const {
  if (!content_) return {};
  const auto& rows = content_->rows;
  if (content_->selectedRow < 0 || content_->selectedRow >= static_cast<int>(rows.size())) return {};
  const auto& row = rows[content_->selectedRow];
  if (content_->selection < 0 || content_->selection >= static_cast<int>(row.size())) return {};
  float cellHeight = kCellTop + kCellLine + kCellBottom;
  int column = 0;
  for (int i = 0; i < content_->selection; i++) column += span(row[i]);
  int cells = span(row[content_->selection]);
  float left = kRowLeft + column * (columnWidth_ + kCellGap);
  float top = rowTop() + kRowTop + content_->selectedRow * (cellHeight + kCellGap);
  return {left, top, left + cells * columnWidth_ + (cells - 1) * kCellGap, top + cellHeight};
}

Panel::Rect Panel::shownSelectionRect() const {
  if (!sliding_) return pillTo_;
  float t = Ease(std::min(1.f, static_cast<float>(NowMs() - slideStart_) / kSlideMs));
  auto mix = [t](float a, float b) { return a + (b - a) * t; };
  return {mix(pillFrom_.left, pillTo_.left), mix(pillFrom_.top, pillTo_.top), mix(pillFrom_.right, pillTo_.right),
          mix(pillFrom_.bottom, pillTo_.bottom)};
}

void Panel::show(const void* owner, const PanelContent& content, const fcitx::Rect& anchor) {
  if (!ok()) return;
  if (!panel_.id) createWindow(panel_);
  bool wasVisible = panel_.mapped;
  // Changes within one list move: the highlight slides and the window resizes.
  bool animate = wasVisible && content_ && !content_->rows.empty() && !content.rows.empty();
  Rect from = shownSelectionRect();
  owner_ = owner;
  content_ = content;
  if (!wasVisible) {
    rounded_ = composited();
    setBlur(panel_, blurs());
  }

  float separator = content_->showsProof && !content_->rows.empty() ? 1.f : 0.f;
  int width = static_cast<int>(std::ceil(fittingWidth() * scale_));
  int height = static_cast<int>(std::ceil((proofHeight() + separator + rowHeight()) * scale_));
  int gap = static_cast<int>(std::lround(kGapToText * scale_));
  // shortcut: the whole X screen is the work area, so a panel can overlap a dock or straddle monitors; use RandR and _NET_WORKAREA if that shows.
  int screenWidth = screen_->width_in_pixels, screenHeight = screen_->height_in_pixels;
  int left = anchor.left() - static_cast<int>(std::lround(kAnchorInset * scale_));
  int top = anchor.bottom() + gap;
  proofBelow_ = top + height > screenHeight;
  if (proofBelow_) top = anchor.top() - gap - height;
  left = std::max(0, std::min(left, screenWidth - width));

  Rect to = selectionRect();
  sliding_ = animate && !from.empty() && !to.empty() && !(from == to);
  if (sliding_) {
    pillFrom_ = from;
    slideStart_ = NowMs();
  }
  pillTo_ = to;

  resizing_ = animate && (panel_.width != width || panel_.height != height);
  if (resizing_) {
    frameFrom_ = panel_;
    frameTo_.x = left;
    frameTo_.y = top;
    frameTo_.width = width;
    frameTo_.height = height;
    resizeStart_ = NowMs();
  } else {
    place(panel_, left, top, width, height);
  }
  map(panel_);
  paintPanel();
  if (badge_.mapped) avoidPanel();  // the badge and the panel both sit under the caret
  if (sliding_ || resizing_) startTimer(panelTimer_, &Panel::tickPanel);
  dispatch();
}

bool Panel::tickPanel() {
  if (!panel_.mapped) return false;
  uint64_t now = NowMs();
  if (sliding_ && now - slideStart_ >= static_cast<uint64_t>(kSlideMs)) sliding_ = false;
  if (resizing_) {
    float t = std::min(1.f, static_cast<float>(now - resizeStart_) / kResizeMs);
    float e = Ease(t);
    auto mix = [e](int a, int b) { return static_cast<int>(std::lround(a + (b - a) * e)); };
    int left = mix(frameFrom_.x, frameTo_.x), top = mix(frameFrom_.y, frameTo_.y);
    int right = mix(frameFrom_.x + frameFrom_.width, frameTo_.x + frameTo_.width);
    int bottom = mix(frameFrom_.y + frameFrom_.height, frameTo_.y + frameTo_.height);
    place(panel_, left, top, right - left, bottom - top);
    if (t >= 1) resizing_ = false;
  }
  paintPanel();
  return sliding_ || resizing_;
}

void Panel::hide(const void* owner) {
  if (!ok() || owner != owner_) return;
  if (panelTimer_) panelTimer_->setEnabled(false);
  sliding_ = resizing_ = false;
  unmap(panel_);
  content_.reset();
  owner_ = nullptr;
}

void Panel::paintPanel() {
  if (!content_ || !panel_.mapped) return;
  cairo_t* cr = cairo_create(panel_.surface);
  cairo_push_group(cr);
  setFontOptions(panel_.blurred);
  pango_cairo_update_context(cr, context_);
  cairo_scale(cr, scale_, scale_);
  float width = panel_.width / scale_, height = panel_.height / scale_;

  // The window: the dark background with a 1px edge, rounded where a compositor
  // shows the corners; over a blur, a dark tint, as over Acrylic on Windows.
  float radius = rounded_ ? kWindowRadius : 0;
  float lineAlpha = panel_.blurred ? 0.6f : 1;
  RoundedRect(cr, 0.5, 0.5, width - 0.5, height - 0.5, radius);
  SetColor(cr, kBackground, panel_.blurred ? kAcrylicTint : 1);
  cairo_fill_preserve(cr);
  SetColor(cr, kLine, lineAlpha);
  cairo_set_line_width(cr, 1);
  cairo_stroke(cr);

  bool both = content_->showsProof && !content_->rows.empty();
  // Above the text the proof row hugs the bottom edge, so it stays put while the window resizes.
  float proofTop = proofBelow_ ? height - proofHeight() : 0;
  if (both) {
    float y = proofBelow_ ? proofTop - 1 : proofHeight();
    SetColor(cr, kLine, lineAlpha);
    cairo_rectangle(cr, 0, y, width, 1);
    cairo_fill(cr);
  }
  if (content_->showsProof) drawProof(cr, proofTop, width);
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
  cairo_destroy(cr);
  cairo_surface_flush(panel_.surface);
  xcb_flush(connection_);
}

void Panel::drawProof(cairo_t* cr, float top, float windowWidth) {
  float x = kProofLeft;
  // The line stays centered; marks sit in the space below it, so they come and
  // go without moving the text.
  float lineTop = top + kProofPad;
  float markTop = lineTop + kProofLine + kMarkGap;
  if (!content_->keyHint.empty()) {
    float w = keyWidth();
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
    Line key(layout(content_->keyHint, keyFont_));
    SetColor(cr, kText);
    key.draw(cr, right - w + (w - key.width) / 2, capTop, kKeyHeight);
  }
  if (!content_->replacedText.empty()) {
    Line replaced(layout(content_->replacedText, proofFont_));
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
  for (const std::string& segment : content_->segments) length += Utf16Length(segment);
  const bool hint = !content_->keyHint.empty();
  // The pending reading, or a caret line when there is none, sits at the cursor.
  auto drawCursor = [&](float& at) {
    if (!content_->pendingReading.empty()) {
      Line line(layout(content_->pendingReading, proofFont_));
      SetColor(cr, kMuted);
      line.draw(cr, at, lineTop, kProofLine);
      at += line.width;
    } else if (content_->caret < length && content_->activeSegment < 0) {
      SetColor(cr, kAccentSoft);
      cairo_rectangle(cr, at, lineTop + 1, 1, kProofLine - 2);
      cairo_fill(cr);
    }
  };
  if (content_->caret == 0) drawCursor(x);
  uint32_t start = 0;
  for (size_t i = 0; i < content_->segments.size(); i++) {
    const std::string& segment = content_->segments[i];
    uint32_t segmentLength = Utf16Length(segment);
    bool active = static_cast<int>(i) == content_->activeSegment;
    // A conversion preview shows its result in full text color.
    uint32_t color = active ? kAccentSoft : hint ? kText : kMuted;
    float left = x;
    uint32_t split = content_->caret > start && content_->caret < start + segmentLength ? content_->caret - start : 0;
    auto drawText = [&](const std::string& text) {
      Line line(layout(text, proofFont_));
      SetColor(cr, color);
      line.draw(cr, x, lineTop, kProofLine);
      x += line.width;
    };
    if (split) {
      size_t bytes = BytesFor(segment, split);
      drawText(segment.substr(0, bytes));
      drawCursor(x);
      drawText(segment.substr(bytes));
    } else {
      drawText(segment);
    }
    bool doubtful = i < content_->doubtful.size() && content_->doubtful[i];
    if (active || doubtful) {
      SetColor(cr, kAccentSoft, active ? 1 : kDoubtMarkAlpha);
      RoundedRect(cr, left + kMarkInset, markTop, x - kMarkInset, markTop + kMarkHeight, 1);
      cairo_fill(cr);
    }
    start += segmentLength;
    if (content_->caret == start && start > 0) drawCursor(x);
  }
}

void Panel::drawCandidates(cairo_t* cr, float top, float windowWidth) {
  float cellHeight = kCellTop + kCellLine + kCellBottom;
  Rect pill = shownSelectionRect();
  if (!pill.empty()) {
    SetColor(cr, kAccent);
    RoundedRect(cr, pill.left, pill.top, pill.right, pill.bottom, kPillRadius);
    cairo_fill(cr);
  }
  float labelWidth = width("6", labelFont_);
  for (size_t r = 0; r < content_->rows.size(); r++) {
    float cellTop = top + kRowTop + r * (cellHeight + kCellGap);
    bool labeled = static_cast<int>(r) == content_->selectedRow;
    int column = 0;
    const auto& row = content_->rows[r];
    for (size_t i = 0; i < row.size(); i++) {
      float x = kRowLeft + column * (columnWidth_ + kCellGap);
      column += span(row[i]);
      bool on = labeled && static_cast<int>(i) == content_->selection;
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
  if (!content_->pageLabel.empty()) {  // the page label sits on the first row
    Line page(layout(content_->pageLabel, pageFont_));
    SetColor(cr, kMuted);
    page.draw(cr, windowWidth - kRowRight - kPageRight - page.width, top + kRowTop + kCellTop, kCellLine);
  }
}

void Panel::follow(const void* owner, const fcitx::Rect& anchor) {
  if (!ok()) return;
  if (badge_.mapped) placeBadge(anchor);
  if (owner == owner_ && content_ && panel_.mapped) show(owner, PanelContent(*content_), anchor);
}

// MARK: Badge

void Panel::showBadge(const std::string& label, const fcitx::Rect& anchor) {
  if (!ok()) return;
  if (!badge_.id) createWindow(badge_);
  badgeLabel_ = label;
  placeBadge(anchor);
  badgeStart_ = NowMs();
  // Without a compositor a window cannot fade or round its corners: it shows square at full strength.
  badgeFades_ = composited();
  badgeAlpha_ = badgeFades_ ? 0 : 1;
  setBlur(badge_, blurs());  // the same appearance as the panel
  map(badge_);
  paintBadge();
  startTimer(badgeTimer_, &Panel::tickBadge);
  dispatch();
}

// Under the caret, above it at the screen's end, beside the panel when they overlap.
void Panel::placeBadge(const fcitx::Rect& anchor) {
  int width = static_cast<int>(std::ceil(std::max(kBadgeSide, this->width(badgeLabel_, badgeFont_) + 2 * kBadgePadding) * scale_));
  int height = static_cast<int>(std::ceil(kBadgeSide * scale_));
  int gap = static_cast<int>(std::lround(kGapToText * scale_));
  int x = anchor.left(), y = anchor.bottom() + gap;
  if (y + height > screen_->height_in_pixels) y = anchor.top() - gap - height;
  place(badge_, x, y, width, height);
  avoidPanel();
}

// Both hang from the caret, so their tops already line up: the badge moves to
// the panel's right, or its left when the screen ends.
void Panel::avoidPanel() {
  if (!badge_.id || !panel_.mapped) return;
  const Window& p = resizing_ ? frameTo_ : panel_;
  bool overlap = badge_.x < p.x + p.width && p.x < badge_.x + badge_.width && badge_.y < p.y + p.height && p.y < badge_.y + badge_.height;
  if (!overlap) return;
  int gap = static_cast<int>(std::lround(kGapToText * scale_));
  int x = p.x + p.width + gap;
  if (x + badge_.width > screen_->width_in_pixels) x = p.x - gap - badge_.width;
  place(badge_, x, badge_.y, badge_.width, badge_.height);
}

void Panel::hideBadge() {
  if (!ok()) return;
  if (badgeTimer_) badgeTimer_->setEnabled(false);
  unmap(badge_);
}

// 120ms in, 800ms held, 250ms out.
bool Panel::tickBadge() {
  if (!badge_.mapped) return false;
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
  if (!badge_.mapped) return;
  cairo_t* cr = cairo_create(badge_.surface);
  cairo_push_group(cr);
  setFontOptions(badge_.blurred);
  pango_cairo_update_context(cr, context_);
  cairo_scale(cr, scale_, scale_);
  float width = badge_.width / scale_;
  RoundedRect(cr, 0.25, 0.25, width - 0.25, kBadgeSide - 0.25, badgeFades_ ? kWindowRadius : 0);
  SetColor(cr, kBackground, badge_.blurred ? kAcrylicTint : 1);
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
  cairo_destroy(cr);
  cairo_surface_flush(badge_.surface);
  xcb_flush(connection_);
}

}  // namespace ProjectKey
