// The candidate window's and 中／英 badge's metrics, colors and motion, the
// same on macOS, Windows and Linux (docs/design/candidate-window, round 03).
// Lengths are device-independent pixels. Each platform keeps its own fonts,
// font sizes and corner radii, which follow the system's look.
#pragma once

#include <cstdint>

namespace ProjectKey::Metrics {

constexpr float kGapToText = 8, kAnchorInset = 16;
// Proof row: the 20px line centered with 9px above and below; its marks (2px,
// 3px under the line, inset 1px per side) sit in the bottom pad. Proof text
// starts 19px in; candidate text, kRowLeft + kCellSide, 17px.
constexpr float kProofPad = 9, kProofLeft = 19, kProofRight = 19, kProofLine = 20;
constexpr float kMarkGap = 3, kMarkHeight = 2, kMarkInset = 1;
// Keycap after the proof text: 20px tall, 6px side padding, 10px from the text.
constexpr float kKeyGap = 10, kKeyHeight = 20, kKeyPadding = 6, kKeyRadius = 5, kKeyLip = 1.5;
// Candidate row: padding 6 on every side; cells padding 5 11 6, 25px line, 5px label gap, 2px apart.
constexpr float kRowTop = 6, kRowBottom = 6, kRowLeft = 6, kRowRight = 6;
constexpr float kCellTop = 5, kCellBottom = 6, kCellSide = 11, kCellLine = 25, kLabelGap = 5, kCellGap = 2;
constexpr float kPageLeft = 12, kPageRight = 0;  // the page label sits kRowRight from the edge
// The row shown while typing (selectedRow -1): no digits, cells as wide as their words,
// 9px a side, in the muted color at 0.85 of the candidate font size.
constexpr float kTypingCellSide = 9, kTypingTextScale = 0.85f;
// The Tab hint's own window (Environment::hintWindow), this far from the candidate
// window, on the side away from the text.
constexpr float kHintGap = 6;
// 中／英 badge.
constexpr float kBadgeSide = 30, kBadgePadding = 11;
// Motion: the selection slides, the grid grows and folds; the badge fades.
constexpr int kSlideMs = 120, kResizeMs = 160, kBadgeFadeInMs = 120, kBadgeHoldMs = 800, kBadgeFadeOutMs = 250;

// sRGB colors, 0xRRGGBB, from the prototype's dark theme. In their own
// namespace so macOS can name its NSColors after them.
namespace Colors {
constexpr uint32_t kBackground = 0x23282c, kLine = 0x343b40, kText = 0xe8ecee, kMuted = 0x8e9aa3;
constexpr uint32_t kAccent = 0x14806f, kAccentSoft = 0x3fb9a3;
constexpr float kDoubtMarkAlpha = 0.7f;
}  // namespace Colors

}  // namespace ProjectKey::Metrics
