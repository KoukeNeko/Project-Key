#include "TextService/CandidateWindow.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>

#include "Common/Win.h"
#include "TextService/Globals.h"

using Microsoft::WRL::ComPtr;

namespace ProjectKey {

using namespace Layout;

namespace {

constexpr wchar_t kCandidateClass[] = L"IME_Candidate_Window";  // the class name the IME requirements ask for
constexpr wchar_t kBadgeClass[] = L"ProjectKeyModeBadge";
constexpr UINT_PTR kAnimationTimer = 1;
constexpr UINT kFrameMs = 8;

D2D1_COLOR_F Color(uint32_t rgb, float alpha = 1) {
  return D2D1::ColorF(((rgb >> 16) & 0xff) / 255.f, ((rgb >> 8) & 0xff) / 255.f, (rgb & 0xff) / 255.f, alpha);
}

bool ReducesMotion() {
  BOOL animate = TRUE;
  SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0);
  return !animate;
}

// Ease-out cubic.
float Ease(float t) {
  return 1 - std::pow(1 - t, 3.f);
}

RECT WorkArea(const RECT& anchor) {
  MONITORINFO info{sizeof(info)};
  GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &info);
  return info.rcWork;
}

UINT DpiOf(HWND owner) {
  UINT dpi = owner ? GetDpiForWindow(owner) : 0;
  return dpi ? dpi : GetDpiForSystem();
}

bool IsEmpty(const D2D1_RECT_F& r) {
  return r.right <= r.left || r.bottom <= r.top;
}

bool SameRect(const D2D1_RECT_F& a, const D2D1_RECT_F& b) {
  return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

// One line of text laid out with a format, and where to draw it.
struct Line {
  ComPtr<IDWriteTextLayout> layout;
  float width = 0, height = 0, baseline = 0;
};

Line Measure(const Fonts& fonts, const std::wstring& text, IDWriteTextFormat* format) {
  Line line;
  fonts.factory()->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format, 10000, 1000, &line.layout);
  if (!line.layout) return line;
  DWRITE_LINE_METRICS metrics{};
  UINT32 count = 0;
  line.layout->GetLineMetrics(&metrics, 1, &count);
  line.height = metrics.height;
  line.baseline = metrics.baseline;
  line.width = fonts.widthOf(text, format);
  return line;
}

// Draws one line centered vertically in a line box starting at `top`.
void DrawLine(ID2D1RenderTarget* target, const Line& line, float x, float top, float lineHeight, ID2D1Brush* brush) {
  if (line.layout) target->DrawTextLayout(D2D1::Point2F(x, top + (lineHeight - line.height) / 2), line.layout.Get(), brush);
}

ComPtr<ID2D1SolidColorBrush> Brush(ID2D1RenderTarget* target, D2D1_COLOR_F color) {
  ComPtr<ID2D1SolidColorBrush> brush;
  target->CreateSolidColorBrush(color, &brush);
  return brush;
}

int ClampAppearance(int appearance) {
  return appearance >= 0 && appearance < 4 ? appearance : 0;
}

// Windows 11 draws the rounded corners, the 1px border and the shadow; the
// materials (Acrylic, Mica, Mica Alt) show behind transparent content.
void ApplyAppearance(HWND hwnd, int appearance) {
  static constexpr DWM_SYSTEMBACKDROP_TYPE kBackdrops[] = {DWMSBT_NONE, DWMSBT_TRANSIENTWINDOW, DWMSBT_MAINWINDOW, DWMSBT_TABBEDWINDOW};
  bool material = appearance != 0;
  DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
  DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
  BOOL dark = TRUE;
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
  COLORREF border = material ? DWMWA_COLOR_DEFAULT : RGB((kLine >> 16) & 0xff, (kLine >> 8) & 0xff, kLine & 0xff);
  DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
  DWM_SYSTEMBACKDROP_TYPE backdrop = kBackdrops[appearance];
  DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
  MARGINS margins = material ? MARGINS{-1, -1, -1, -1} : MARGINS{0, 0, 0, 0};
  DwmExtendFrameIntoClientArea(hwnd, &margins);
}

// What a window fills its client area with: the dark background, a dark tint
// over Acrylic, which shows the window behind as is, or nothing over Mica.
D2D1_COLOR_F Fill(int appearance) {
  return appearance == 0 ? Color(kBackground) : appearance == 1 ? Color(kBackground, kAcrylicTint) : D2D1::ColorF(0, 0, 0, 0);
}

void RegisterClassOnce(const wchar_t* name, WNDPROC proc, UINT style) {
  WNDCLASSEXW existing{sizeof(existing)};
  if (GetClassInfoExW(gInstance, name, &existing)) return;
  WNDCLASSEXW wc{sizeof(wc)};
  wc.style = style;
  wc.lpfnWndProc = proc;
  wc.hInstance = gInstance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = name;
  RegisterClassExW(&wc);
}

}  // namespace

// MARK: Candidate window

CandidateWindow::~CandidateWindow() {
  target_.Reset();
  if (hwnd_) DestroyWindow(hwnd_);
}

LRESULT CALLBACK CandidateWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_NCCREATE) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
  }
  auto* self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (message) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      if (self) self->paint();
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_TIMER:
      if (self && wParam == kAnimationTimer) self->tick();
      return 0;
    case WM_NCHITTEST:
      return HTTRANSPARENT;  // clicks go to the app, as on macOS
    case WM_NCACTIVATE:
      // Mica draws only on an active frame, and this window never takes focus: keep its frame active.
      return DefWindowProcW(hwnd, message, TRUE, lParam);
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
  }
  return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool CandidateWindow::ensureWindow() {
  if (hwnd_) return true;
  if (!fonts_.ok()) return false;
  if (!factory_ && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()))) return false;
  RegisterClassOnce(kCandidateClass, WindowProc, CS_DROPSHADOW);
  hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, kCandidateClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr,
                          nullptr, gInstance, this);
  if (!hwnd_) return false;
  applyAppearance(0);
  return true;
}

void CandidateWindow::applyAppearance(int appearance) {
  appearance_ = ClampAppearance(appearance);
  ApplyAppearance(hwnd_, appearance_);
  target_.Reset();
}

float CandidateWindow::proofHeight() const {
  return content_ && content_->showsProof ? kProofPad + kProofLine + kProofPad : 0;  // marks sit in the bottom pad
}

float CandidateWindow::rowHeight() const {
  size_t rows = content_ ? content_->rows.size() : 0;
  if (!rows) return 0;
  float cell = kCellTop + kCellLine + kCellBottom;
  return kRowTop + rows * cell + (rows - 1) * kCellGap + kRowBottom;
}

float CandidateWindow::keyWidth() const {
  return std::max(kKeyHeight, fonts_.widthOf(Widen(content_->keyHint), fonts_.key()) + 2 * kKeyPadding);
}

// The first row's end: a down chevron while folded with more rows; unfolded, the page label
// and an up chevron. Every row keeps room for them and for the widest label (999/999, or
// wider for more rows), so the window keeps its width from typing to choosing, folded or not.
float CandidateWindow::tailWidth() const {
  std::string total = content_->pageLabel.substr(content_->pageLabel.find('/') + 1);
  if (total.size() < 3) total = "999";
  float label = fonts_.widthOf(Widen(total + "/" + total), fonts_.page());
  return kPageLeft + label + kChevronGap + kChevronWidth + kChevronInset;
}

D2D1_SIZE_F CandidateWindow::fittingSize() const {
  float width = 0;
  if (content_->showsProof) {
    std::string sentence;
    for (const std::string& segment : content_->segments) sentence += segment;
    sentence += content_->pendingReading;
    float key = content_->keyHint.empty() ? 0 : kKeyGap + keyWidth();
    if (!content_->replacedText.empty()) key += fonts_.widthOf(Widen(content_->replacedText) + kReplacedArrow, fonts_.proof());
    width = std::max(width, kProofLeft + fonts_.widthOf(Widen(sentence), fonts_.proof()) + key + kProofRight);
  }
  if (!content_->rows.empty()) {
    float row = kRowLeft + kGridColumns * (fonts_.columnWidth() + kCellGap);
    width = std::max(width, row + tailWidth());
  }
  float separator = content_->showsProof && !content_->rows.empty() ? 1.f : 0.f;
  return D2D1::SizeF(width, proofHeight() + separator + rowHeight());
}

float CandidateWindow::rowTop() const {
  bool both = content_->showsProof && !content_->rows.empty();
  return proofBelow_ ? 0 : proofHeight() + (both ? 1 : 0);
}

D2D1_RECT_F CandidateWindow::selectionRect() const {
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
  for (int i = 0; i < content_->selection; i++) column += fonts_.span(Widen(row[i]));
  int span = fonts_.span(Widen(row[content_->selection]));
  float left = kRowLeft + column * (fonts_.columnWidth() + kCellGap);
  float top = rowTop() + kRowTop + labeledRow * (cellHeight + kCellGap);
  return D2D1::RectF(left, top, left + span * fonts_.columnWidth() + (span - 1) * kCellGap, top + cellHeight);
}

D2D1_RECT_F CandidateWindow::shownSelectionRect() const {
  if (!sliding_) return pillTo_;
  float t = Ease(std::min(1.f, static_cast<float>(GetTickCount64() - slideStart_) / kSlideMs));
  auto mix = [t](float a, float b) { return a + (b - a) * t; };
  return D2D1::RectF(mix(pillFrom_.left, pillTo_.left), mix(pillFrom_.top, pillTo_.top), mix(pillFrom_.right, pillTo_.right),
                     mix(pillFrom_.bottom, pillTo_.bottom));
}

void CandidateWindow::show(const PanelContent& content, const RECT& anchor, HWND owner, int appearance) {
  if (!ensureWindow()) return;
  if (appearance != appearance_) applyAppearance(appearance);
  bool wasVisible = visible();
  // Changes within one list move: the highlight slides and the window resizes.
  bool animate = wasVisible && content_ && !content_->rows.empty() && !content.rows.empty() && !ReducesMotion();
  D2D1_RECT_F from = shownSelectionRect();
  content_ = content;

  D2D1_SIZE_F size = fittingSize();
  UINT dpi = DpiOf(owner);
  scale_ = dpi / 96.f;
  int width = static_cast<int>(std::ceil(size.width * scale_)), height = static_cast<int>(std::ceil(size.height * scale_));
  RECT work = WorkArea(anchor);
  int gap = static_cast<int>(std::lround(kGapToText * scale_));
  RECT frame;
  frame.left = anchor.left - static_cast<int>(std::lround(kAnchorInset * scale_));
  frame.top = anchor.bottom + gap;
  proofBelow_ = frame.top + height > work.bottom;
  if (proofBelow_) frame.top = anchor.top - gap - height;
  frame.left = std::max(work.left, std::min(frame.left, work.right - width));
  frame.right = frame.left + width;
  frame.bottom = frame.top + height;
  placed_ = frame;
  badge_.avoid(frame);  // both sit under the caret

  D2D1_RECT_F to = selectionRect();
  sliding_ = animate && !IsEmpty(from) && !IsEmpty(to) && !SameRect(from, to);
  if (sliding_) {
    pillFrom_ = from;
    slideStart_ = GetTickCount64();
  }
  pillTo_ = to;

  SetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
  RECT current = this->frame();
  resizing_ = animate && (current.right - current.left != width || current.bottom - current.top != height);
  if (resizing_) {
    frameFrom_ = current;
    frameTo_ = frame;
    resizeStart_ = GetTickCount64();
  } else {
    SetWindowPos(hwnd_, HWND_TOPMOST, frame.left, frame.top, width, height, SWP_NOACTIVATE);
  }
  if (!wasVisible) {
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    if (material()) SendMessageW(hwnd_, WM_NCACTIVATE, TRUE, 0);  // see WM_NCACTIVATE in WindowProc
  }
  NotifyWinEvent(wasVisible ? EVENT_OBJECT_IME_CHANGE : EVENT_OBJECT_IME_SHOW, hwnd_, OBJID_CLIENT, CHILDID_SELF);
  InvalidateRect(hwnd_, nullptr, FALSE);
  UpdateWindow(hwnd_);
  if (sliding_ || resizing_) SetTimer(hwnd_, kAnimationTimer, kFrameMs, nullptr);
}

void CandidateWindow::showBeside(const PanelContent& content, const CandidateWindow& panel, HWND owner, int appearance) {
  if (!ensureWindow()) return;
  if (appearance != appearance_) applyAppearance(appearance);
  bool wasVisible = visible();
  content_ = content;
  proofBelow_ = false;
  pillTo_ = {};
  D2D1_SIZE_F size = fittingSize();
  scale_ = DpiOf(owner) / 96.f;
  int width = static_cast<int>(std::ceil(size.width * scale_)), height = static_cast<int>(std::ceil(size.height * scale_));
  int gap = static_cast<int>(std::lround(kHintGap * scale_));
  const RECT& next = panel.placed_;
  placed_.left = next.left;
  placed_.top = panel.proofBelow_ ? next.top - gap - height : next.bottom + gap;
  placed_.right = placed_.left + width;
  placed_.bottom = placed_.top + height;
  SetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
  SetWindowPos(hwnd_, HWND_TOPMOST, placed_.left, placed_.top, width, height, SWP_NOACTIVATE);
  if (!wasVisible) {
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    if (material()) SendMessageW(hwnd_, WM_NCACTIVATE, TRUE, 0);  // see WM_NCACTIVATE in WindowProc
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
  UpdateWindow(hwnd_);
}

void CandidateWindow::tick() {
  ULONGLONG now = GetTickCount64();
  if (sliding_ && now - slideStart_ >= static_cast<ULONGLONG>(kSlideMs)) sliding_ = false;
  if (resizing_) {
    float t = std::min(1.f, static_cast<float>(now - resizeStart_) / kResizeMs);
    float e = Ease(t);
    auto mix = [e](LONG a, LONG b) { return static_cast<int>(std::lround(a + (b - a) * e)); };
    int left = mix(frameFrom_.left, frameTo_.left), top = mix(frameFrom_.top, frameTo_.top);
    int right = mix(frameFrom_.right, frameTo_.right), bottom = mix(frameFrom_.bottom, frameTo_.bottom);
    SetWindowPos(hwnd_, HWND_TOPMOST, left, top, right - left, bottom - top, SWP_NOACTIVATE);
    if (t >= 1) resizing_ = false;
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
  UpdateWindow(hwnd_);
  if (!sliding_ && !resizing_) KillTimer(hwnd_, kAnimationTimer);
}

void CandidateWindow::hide() {
  if (!visible()) return;
  KillTimer(hwnd_, kAnimationTimer);
  sliding_ = resizing_ = false;
  ShowWindow(hwnd_, SW_HIDE);
  NotifyWinEvent(EVENT_OBJECT_IME_HIDE, hwnd_, OBJID_CLIENT, CHILDID_SELF);
  content_.reset();
}

RECT CandidateWindow::frame() const {
  RECT r{};
  if (hwnd_) GetWindowRect(hwnd_, &r);
  return r;
}

void CandidateWindow::paint() {
  if (!content_) return;
  RECT client;
  GetClientRect(hwnd_, &client);
  D2D1_SIZE_U pixels = D2D1::SizeU(client.right, client.bottom);
  if (!target_) {
    D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(factory_->CreateHwndRenderTarget(properties, D2D1::HwndRenderTargetProperties(hwnd_, pixels), &target_))) return;
  } else {
    target_->Resize(pixels);
  }
  target_->SetDpi(96 * scale_, 96 * scale_);
  target_->SetTextAntialiasMode(material() ? D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE : D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
  float width = client.right / scale_, height = client.bottom / scale_;

  target_->BeginDraw();
  target_->Clear(Fill(appearance_));
  bool both = content_->showsProof && !content_->rows.empty();
  // Above the text the proof row hugs the bottom edge, so it stays put while the window resizes.
  float proofTop = proofBelow_ ? height - proofHeight() : 0;
  if (both) {
    auto line = Brush(target_.Get(), Color(kLine, material() ? 0.6f : 1));
    float y = proofBelow_ ? proofTop - 1 : proofHeight();
    target_->FillRectangle(D2D1::RectF(0, y, width, y + 1), line.Get());
  }
  if (content_->showsProof) drawProof(target_.Get(), proofTop);
  if (!content_->rows.empty()) {
    // While the window grows above the text, rows laid out for the final size
    // would run over the pinned proof row; keep them above it.
    bool clip = proofBelow_ && content_->showsProof;
    if (clip) target_->PushAxisAlignedClip(D2D1::RectF(0, 0, width, proofTop - 1), D2D1_ANTIALIAS_MODE_ALIASED);
    drawCandidates(target_.Get(), rowTop(), width);
    if (clip) target_->PopAxisAlignedClip();
  }
  if (target_->EndDraw() == D2DERR_RECREATE_TARGET) target_.Reset();
}

void CandidateWindow::drawProof(ID2D1RenderTarget* target, float top) {
  float x = kProofLeft;
  // The line stays centered; marks sit in the space below it, so they come and
  // go without moving the text.
  float lineTop = top + kProofPad;
  float markTop = lineTop + kProofLine + kMarkGap;
  auto muted = Brush(target, Color(kMuted));
  auto accentSoft = Brush(target, Color(kAccentSoft));
  auto doubtMark = Brush(target, Color(kAccentSoft, kDoubtMarkAlpha));
  if (!content_->keyHint.empty()) {
    float w = keyWidth();
    float right = static_cast<float>(target->GetSize().width) - kProofRight;
    D2D1_RECT_F cap = D2D1::RectF(right - w, lineTop + (kProofLine - kKeyHeight) / 2, right, lineTop + (kProofLine + kKeyHeight) / 2);
    // A key: a face with a hairline edge, raised on a darker lip along its bottom.
    D2D1_ROUNDED_RECT face = D2D1::RoundedRect(D2D1::RectF(cap.left + 0.25f, cap.top + 0.25f, cap.right - 0.25f, cap.bottom - 0.25f), kKeyRadius, kKeyRadius);
    auto lip = Brush(target, D2D1::ColorF(0, 0, 0, 0.35f));
    target->DrawLine(D2D1::Point2F(cap.left + kKeyRadius, cap.bottom + kKeyLip / 2), D2D1::Point2F(cap.right - kKeyRadius, cap.bottom + kKeyLip / 2), lip.Get(), kKeyLip);
    auto faceFill = Brush(target, Color(kMuted, 0.16f));
    target->FillRoundedRectangle(face, faceFill.Get());
    auto edge = Brush(target, Color(kMuted, 0.35f));
    target->DrawRoundedRectangle(face, edge.Get(), 0.5f);
    Line key = Measure(fonts_, Widen(content_->keyHint), fonts_.key());
    auto text = Brush(target, Color(kText));
    DrawLine(target, key, (cap.left + cap.right - key.width) / 2, cap.top, kKeyHeight, text.Get());
  }
  if (!content_->replacedText.empty()) {
    Line replaced = Measure(fonts_, Widen(content_->replacedText), fonts_.proof());
    if (replaced.layout) replaced.layout->SetStrikethrough(TRUE, {0, static_cast<UINT32>(Widen(content_->replacedText).size())});
    DrawLine(target, replaced, x, lineTop, kProofLine, muted.Get());
    x += replaced.width;
    Line arrow = Measure(fonts_, kReplacedArrow, fonts_.proof());
    DrawLine(target, arrow, x, lineTop, kProofLine, muted.Get());
    x += arrow.width;
  }
  std::wstring joined;
  for (const std::string& segment : content_->segments) joined += Widen(segment);
  const uint32_t length = static_cast<uint32_t>(joined.size());
  const bool hint = !content_->replacedText.empty();  // a conversion preview, not the proof row's Tab
  // The pending reading, or a caret line when there is none, sits at the cursor.
  auto drawCursor = [&](float& at) {
    std::wstring reading = Widen(content_->pendingReading);
    if (!reading.empty()) {
      Line line = Measure(fonts_, reading, fonts_.proof());
      DrawLine(target, line, at, lineTop, kProofLine, muted.Get());
      at += line.width;
    } else if (content_->caret < length && content_->activeSegment < 0) {
      target->FillRectangle(D2D1::RectF(at, lineTop + 1, at + 1, lineTop + kProofLine - 1), accentSoft.Get());
    }
  };
  if (content_->caret == 0) drawCursor(x);
  uint32_t start = 0;
  for (size_t i = 0; i < content_->segments.size(); i++) {
    std::wstring segment = Widen(content_->segments[i]);
    uint32_t segmentLength = static_cast<uint32_t>(segment.size());
    bool active = static_cast<int>(i) == content_->activeSegment;
    // A conversion preview shows its result in full text color.
    auto brush = active ? accentSoft : hint ? Brush(target, Color(kText)) : muted;
    float left = x;
    uint32_t split = content_->caret > start && content_->caret < start + segmentLength ? content_->caret - start : 0;
    if (split) {
      Line head = Measure(fonts_, segment.substr(0, split), fonts_.proof());
      DrawLine(target, head, x, lineTop, kProofLine, brush.Get());
      x += head.width;
      drawCursor(x);
      Line tail = Measure(fonts_, segment.substr(split), fonts_.proof());
      DrawLine(target, tail, x, lineTop, kProofLine, brush.Get());
      x += tail.width;
    } else {
      Line line = Measure(fonts_, segment, fonts_.proof());
      DrawLine(target, line, x, lineTop, kProofLine, brush.Get());
      x += line.width;
    }
    ID2D1Brush* mark = active ? accentSoft.Get() : i < content_->doubtful.size() && content_->doubtful[i] ? doubtMark.Get() : nullptr;
    if (mark) {
      D2D1_RECT_F line = D2D1::RectF(left + kMarkInset, markTop, x - kMarkInset, markTop + kMarkHeight);
      target->FillRoundedRectangle(D2D1::RoundedRect(line, 1, 1), mark);
    }
    start += segmentLength;
    if (content_->caret == start && start > 0) drawCursor(x);
  }
}

void CandidateWindow::drawCandidates(ID2D1RenderTarget* target, float top, float width) {
  float cellHeight = kCellTop + kCellLine + kCellBottom;
  D2D1_RECT_F pill = shownSelectionRect();
  if (!IsEmpty(pill)) {
    auto fill = Brush(target, content_->selectedRow >= 0 ? Color(kAccent) : Color(kText, kGuessAlpha));
    target->FillRoundedRectangle(D2D1::RoundedRect(pill, kPillRadius, kPillRadius), fill.Get());
  }
  auto text = Brush(target, Color(kText));
  auto white = Brush(target, D2D1::ColorF(1, 1, 1, 1));
  auto onLabel = Brush(target, D2D1::ColorF(1, 1, 1, 0.72f));
  auto muted = Brush(target, Color(kMuted));
  float labelWidth = fonts_.widthOf(L"6", fonts_.label());
  for (size_t r = 0; r < content_->rows.size(); r++) {
    float cellTop = top + kRowTop + r * (cellHeight + kCellGap);
    bool labeled = static_cast<int>(r) == std::max(0, content_->selectedRow);  // as selectionRect says
    int column = 0;
    const auto& row = content_->rows[r];
    for (size_t i = 0; i < row.size(); i++) {
      std::wstring candidate = Widen(row[i]);
      int span = fonts_.span(candidate);
      float x = kRowLeft + column * (fonts_.columnWidth() + kCellGap);
      column += span;
      bool on = labeled && content_->selectedRow >= 0 && static_cast<int>(i) == content_->selection;  // on the accent
      Line word = Measure(fonts_, candidate, fonts_.candidate());
      float wordTop = cellTop + kCellTop + (kCellLine - word.height) / 2;
      if (labeled) {
        // The label shares the candidate's baseline.
        Line label = Measure(fonts_, std::to_wstring(i + 1), fonts_.label());
        if (label.layout) {
          target->DrawTextLayout(D2D1::Point2F(x + kCellSide, wordTop + word.baseline - label.baseline), label.layout.Get(),
                                 on ? onLabel.Get() : muted.Get());
        }
      }
      float wordLeft = x + kCellSide + labelWidth + kLabelGap;
      if (word.layout) {
        target->DrawTextLayout(D2D1::Point2F(wordLeft, wordTop), word.layout.Get(), on ? white.Get() : text.Get());
      }
    }
  }
  if (!content_->pageLabel.empty()) {  // the chevron and page label sit on the first row
    float x = width - kChevronInset - kChevronWidth, middle = top + kChevronMiddle;
    float tip = content_->expanded ? -kChevronHeight / 2 : kChevronHeight / 2;  // up or down
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> roundCaps;
    factory_->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                            D2D1_LINE_JOIN_ROUND),
                                nullptr, 0, &roundCaps);
    D2D1_POINT_2F apex = D2D1::Point2F(x + kChevronWidth / 2, middle + tip);
    target->DrawLine(D2D1::Point2F(x, middle - tip), apex, muted.Get(), kChevronLine, roundCaps.Get());
    target->DrawLine(apex, D2D1::Point2F(x + kChevronWidth, middle - tip), muted.Get(), kChevronLine, roundCaps.Get());
    if (content_->expanded) {
      Line page = Measure(fonts_, Widen(content_->pageLabel), fonts_.page());
      DrawLine(target, page, x - kChevronGap - page.width, top + kRowTop + kCellTop, kCellLine, muted.Get());
    }
  }
}

// MARK: Mode badge

ModeBadge::~ModeBadge() {
  if (dc_) {
    SelectObject(dc_, oldBitmap_);
    DeleteDC(dc_);
  }
  if (bitmap_) DeleteObject(bitmap_);
  if (hwnd_) DestroyWindow(hwnd_);
}

LRESULT CALLBACK ModeBadge::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_NCCREATE) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
  }
  auto* self = reinterpret_cast<ModeBadge*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (message) {
    case WM_TIMER:
      if (self && wParam == kAnimationTimer) self->tick();
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      if (self) self->paint();
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_NCACTIVATE:
      return DefWindowProcW(hwnd, message, TRUE, lParam);  // Mica draws only on an active frame
    case WM_NCHITTEST:
      return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
  }
  return DefWindowProcW(hwnd, message, wParam, lParam);
}

namespace {

// The badge's frame moved beside `rect` when it overlaps it: right of it, or
// left when the screen ends. Both hang from the text, so their tops already line up.
POINT Beside(POINT position, SIZE size, const RECT& rect, int gap) {
  RECT badge{position.x, position.y, position.x + size.cx, position.y + size.cy};
  RECT overlap;
  if (!IntersectRect(&overlap, &badge, &rect)) return position;
  RECT work = WorkArea(rect);
  position.x = rect.right + gap;
  if (position.x + size.cx > work.right) position.x = rect.left - gap - size.cx;
  return position;
}

}  // namespace

// A layered window for the dark badge, a plain one for a material; switching
// between the two makes a new window.
bool ModeBadge::ensureWindow(int appearance) {
  if (hwnd_ && (appearance != 0) != (appearance_ != 0)) {
    target_.Reset();
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  if (!hwnd_) {
    RegisterClassOnce(kBadgeClass, WindowProc, 0);
    DWORD layered = appearance == 0 ? WS_EX_LAYERED : 0;
    hwnd_ = CreateWindowExW(layered | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TRANSPARENT, kBadgeClass, L"", WS_POPUP,
                            0, 0, 1, 1, nullptr, nullptr, gInstance, this);
    if (!hwnd_) return false;
    appearance_ = -1;  // apply below
  }
  if (appearance != appearance_ && appearance != 0) {
    ApplyAppearance(hwnd_, appearance);
    target_.Reset();
  }
  appearance_ = appearance;
  return true;
}

void ModeBadge::show(const std::wstring& label, const RECT& anchor, HWND owner, const RECT* avoid, int appearance) {
  if (!fonts_.ok()) return;
  if (!factory_ && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()))) return;
  if (!ensureWindow(ClampAppearance(appearance))) return;
  scale_ = DpiOf(owner) / 96.f;
  label_ = label;
  float width = std::max(kBadgeSide, fonts_.widthOf(label, fonts_.badge()) + 2 * kBadgePadding);
  size_ = {static_cast<LONG>(std::ceil(width * scale_)), static_cast<LONG>(std::ceil(kBadgeSide * scale_))};
  place(anchor, avoid);
  SetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
  start_ = GetTickCount64();
  if (appearance_ == 0) {
    render();
    present(0);
  }
  SetWindowPos(hwnd_, HWND_TOPMOST, position_.x, position_.y, size_.cx, size_.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  if (appearance_ != 0) {
    SendMessageW(hwnd_, WM_NCACTIVATE, TRUE, 0);
    InvalidateRect(hwnd_, nullptr, FALSE);
    UpdateWindow(hwnd_);
  }
  SetTimer(hwnd_, kAnimationTimer, kFrameMs * 2, nullptr);
}

void ModeBadge::paint() {
  if (appearance_ == 0) return;  // the layered window was drawn by render()
  RECT client;
  GetClientRect(hwnd_, &client);
  D2D1_SIZE_U pixels = D2D1::SizeU(client.right, client.bottom);
  if (!target_) {
    D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(factory_->CreateHwndRenderTarget(properties, D2D1::HwndRenderTargetProperties(hwnd_, pixels), &target_))) return;
  } else {
    target_->Resize(pixels);
  }
  target_->SetDpi(96 * scale_, 96 * scale_);
  target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  target_->BeginDraw();
  target_->Clear(Fill(appearance_));
  Line line = Measure(fonts_, label_, fonts_.badge());
  auto text = Brush(target_.Get(), Color(kText));
  DrawLine(target_.Get(), line, (client.right / scale_ - line.width) / 2, 0, kBadgeSide, text.Get());
  if (target_->EndDraw() == D2DERR_RECREATE_TARGET) target_.Reset();
}

// Under the anchor, above it at the screen's end, beside `avoid` when they overlap.
void ModeBadge::place(const RECT& anchor, const RECT* avoid) {
  int gap = static_cast<int>(std::lround(kGapToText * scale_));
  position_ = {anchor.left, anchor.bottom + gap};
  if (position_.y + size_.cy > WorkArea(anchor).bottom) position_.y = anchor.top - gap - size_.cy;
  if (avoid) position_ = Beside(position_, size_, *avoid, gap);
}

void ModeBadge::follow(const RECT& anchor, const RECT* avoid) {
  if (!visible()) return;
  place(anchor, avoid);
  SetWindowPos(hwnd_, HWND_TOPMOST, position_.x, position_.y, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE);
}

void ModeBadge::avoid(const RECT& rect) {
  if (!hwnd_ || !IsWindowVisible(hwnd_)) return;
  POINT moved = Beside(position_, size_, rect, static_cast<int>(std::lround(kGapToText * DpiOf(GetWindow(hwnd_, GW_OWNER)) / 96.f)));
  if (moved.x == position_.x && moved.y == position_.y) return;
  position_ = moved;
  SetWindowPos(hwnd_, HWND_TOPMOST, position_.x, position_.y, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE);
}

void ModeBadge::hide() {
  if (!hwnd_) return;
  KillTimer(hwnd_, kAnimationTimer);
  ShowWindow(hwnd_, SW_HIDE);
}

void ModeBadge::tick() {
  // 120ms in, 800ms held, 250ms out.
  float elapsed = static_cast<float>(GetTickCount64() - start_);
  float alpha;
  if (elapsed < kBadgeFadeInMs) {
    alpha = elapsed / kBadgeFadeInMs;
  } else if (elapsed < kBadgeFadeInMs + kBadgeHoldMs) {
    alpha = 1;
  } else if (elapsed < kBadgeFadeInMs + kBadgeHoldMs + kBadgeFadeOutMs) {
    alpha = 1 - (elapsed - kBadgeFadeInMs - kBadgeHoldMs) / kBadgeFadeOutMs;
  } else {
    hide();
    return;
  }
  // shortcut: a window with a material cannot be layered, so it appears and
  // disappears without fading; fade it through DirectComposition if that matters.
  if (appearance_ == 0) present(static_cast<BYTE>(std::lround(alpha * 255)));
}

void ModeBadge::render() {
  if (dc_) {
    SelectObject(dc_, oldBitmap_);
    DeleteDC(dc_);
    dc_ = nullptr;
  }
  if (bitmap_) DeleteObject(bitmap_);
  BITMAPINFO info{};
  info.bmiHeader = {sizeof(BITMAPINFOHEADER), size_.cx, -size_.cy, 1, 32, BI_RGB};
  void* bits = nullptr;
  bitmap_ = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  dc_ = CreateCompatibleDC(nullptr);
  oldBitmap_ = SelectObject(dc_, bitmap_);
  ComPtr<ID2D1DCRenderTarget> target;
  D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  if (FAILED(factory_->CreateDCRenderTarget(&properties, &target))) return;
  RECT bounds{0, 0, size_.cx, size_.cy};
  target->BindDC(dc_, &bounds);
  float scale = static_cast<float>(size_.cy) / kBadgeSide;
  target->SetDpi(96 * scale, 96 * scale);
  target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  float width = size_.cx / scale;
  target->BeginDraw();
  target->Clear(D2D1::ColorF(0, 0, 0, 0));
  D2D1_ROUNDED_RECT shape = D2D1::RoundedRect(D2D1::RectF(0.25f, 0.25f, width - 0.25f, kBadgeSide - 0.25f), 8, 8);
  auto fill = Brush(target.Get(), Color(kBackground));
  target->FillRoundedRectangle(shape, fill.Get());
  auto edge = Brush(target.Get(), D2D1::ColorF(0, 0, 0, 0.5f));
  target->DrawRoundedRectangle(shape, edge.Get(), 0.5f);
  Line line = Measure(fonts_, label_, fonts_.badge());
  auto text = Brush(target.Get(), Color(kText));
  DrawLine(target.Get(), line, (width - line.width) / 2, 0, kBadgeSide, text.Get());
  target->EndDraw();
}

void ModeBadge::present(BYTE alpha) {
  if (!dc_) return;
  POINT origin{0, 0};
  BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
  UpdateLayeredWindow(hwnd_, nullptr, &position_, &size_, dc_, &origin, 0, &blend, ULW_ALPHA);
}

}  // namespace ProjectKey
