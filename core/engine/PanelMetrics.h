// The candidate window's and 中／英 badge's metrics, colors and motion, the
// same on macOS, Windows and Linux (docs/design/candidate-window, round 03).
// Lengths are device-independent pixels. Each platform keeps its own fonts,
// font sizes and corner radii, which follow the system's look.
#pragma once

#include <cstdint>
#include <vector>

namespace ProjectKey::Metrics {

constexpr float kGapToText = 8, kAnchorInset = 16;
// Proof row: the 20px line centered with 9px above and below; its marks (2px,
// 3px under the line, inset 1px per side) sit in the bottom pad. Proof text
// starts 19px in; candidate text, kRowLeft + kCellSide, 17px.
constexpr float kProofPad = 9, kProofLeft = 19, kProofRight = 19, kProofLine = 20;
constexpr float kMarkGap = 3, kMarkHeight = 2, kMarkInset = 1;
// Keycap after the proof text: 20px tall, 6px side padding, 10px from the text.
constexpr float kKeyGap = 10, kKeyHeight = 20, kKeyPadding = 6, kKeyRadius = 5, kKeyLip = 1.5;
// Candidate row: padding 6 on the left, top and bottom; cells padding 5 11 6, 25px line, 5px label gap, 2px apart.
constexpr float kRowTop = 6, kRowBottom = 6, kRowLeft = 6;
constexpr float kCellTop = 5, kCellBottom = 6, kCellSide = 11, kCellLine = 25, kLabelGap = 5, kCellGap = 2;
constexpr float kPageLeft = 12;  // after the grid's last column
// A chevron at the first row's end: down while folded with more rows (instead of the page
// label), up after the page label while unfolded. 10 x 5, 1.5px strokes, 8px after the label,
// centered in the row and as far from the window's right edge as from the row's top and bottom.
constexpr float kChevronWidth = 10, kChevronHeight = 5, kChevronLine = 1.5f, kChevronGap = 8;
constexpr float kChevronMiddle = kRowTop + (kCellTop + kCellLine + kCellBottom) / 2;  // below the row's top
constexpr float kChevronInset = kChevronMiddle - kChevronHeight / 2;
// The row shown while typing (selectedRow -1) looks as when choosing from it: digits, and the
// guess (selection) highlighted.
// The Tab hint's own window (Environment::hintWindow), this far from the candidate
// window, on the side away from the text.
constexpr float kHintGap = 6;
// 中／英 badge.
constexpr float kBadgeSide = 30, kBadgePadding = 11;
// Motion: the selection slides, the grid grows and folds; the badge fades, and so do the
// candidate and hint windows as they open and close.
constexpr int kSlideMs = 120, kResizeMs = 160, kBadgeFadeInMs = 120, kBadgeHoldMs = 800, kBadgeFadeOutMs = 250;
constexpr int kWindowFadeMs = 120;

// What a click on the candidate rows lands on.
struct PanelHit {
  enum Kind { kNone, kCandidate, kChevron } kind = kNone;
  int row = 0, index = 0;  // kCandidate: in PanelContent::rows
};

// spans: the grid columns each shown candidate takes. x is from the window's left, y from the
// top of the rows. The chevron's target is all of the first row past the grid, page label included.
inline PanelHit HitCandidates(const std::vector<std::vector<int>>& spans, bool hasChevron, float x, float y, float columnWidth,
                              int gridColumns) {
  float cell = kCellTop + kCellLine + kCellBottom;
  for (int r = 0; r < static_cast<int>(spans.size()); r++) {
    float top = kRowTop + r * (cell + kCellGap);
    if (y < top || y >= top + cell) continue;
    if (r == 0 && hasChevron && x >= kRowLeft + gridColumns * (columnWidth + kCellGap)) return {PanelHit::kChevron, 0, 0};
    int column = 0;
    for (int i = 0; i < static_cast<int>(spans[r].size()); i++) {
      float left = kRowLeft + column * (columnWidth + kCellGap);
      if (x >= left && x < left + spans[r][i] * columnWidth + (spans[r][i] - 1) * kCellGap) return {PanelHit::kCandidate, r, i};
      column += spans[r][i];
    }
  }
  return {};
}

// sRGB colors, 0xRRGGBB, from the prototype's dark theme. In their own
// namespace so macOS can name its NSColors after them.
namespace Colors {
constexpr uint32_t kBackground = 0x23282c, kLine = 0x343b40, kText = 0xe8ecee, kMuted = 0x8e9aa3;
constexpr uint32_t kAccent = 0x14806f, kAccentSoft = 0x3fb9a3;
// The selection while the Delete that excludes a word waits for a second press.
constexpr uint32_t kDanger = 0xb3382c;
constexpr float kDoubtMarkAlpha = 0.7f;
// While typing, the guess sits on a highlight of kText at this alpha, not the accent of a choice.
constexpr float kGuessAlpha = 0.12f;
// A candidate under the pointer, not selected, sits on kText at this alpha.
constexpr float kHoverAlpha = 0.07f;
// The light theme (macOS 淺色): the same accent and danger; the soft accent darker, to read on white.
namespace Light {
constexpr uint32_t kBackground = 0xf7f8f9, kLine = 0xdde2e6, kText = 0x1d2226, kMuted = 0x66717a, kAccentSoft = 0x14806f;
}  // namespace Light
}  // namespace Colors

}  // namespace ProjectKey::Metrics
