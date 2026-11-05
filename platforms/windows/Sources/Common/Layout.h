// Candidate window metrics and text measurement, shared by the text service,
// which draws the window, and the engine server, which packs candidates into
// grid rows. Values follow docs/design/candidate-window (round 03, Windows
// variant: Microsoft JhengHei, 19px candidates, 8px window and 4px selection
// corners). Lengths are device-independent pixels.
#pragma once

#include <dwrite.h>
#include <wrl/client.h>

#include <string>

namespace ProjectKey::Layout {

constexpr float kWindowRadius = 8, kPillRadius = 4, kGapToText = 8, kAnchorInset = 16;
// Proof row: the 20px line centered with 9px above and below; its marks (2px,
// 3px under the line, inset 1px per side) sit in the bottom pad. Text starts
// 19px in on both rows: kRowLeft + kCellSide = kProofLeft.
constexpr float kProofPad = 9, kProofLeft = 19, kProofRight = 19, kProofLine = 20;
constexpr float kMarkGap = 3, kMarkHeight = 2, kMarkInset = 1;
// Keycap after the proof text: 20px tall, 6px side padding, 10px from the text.
constexpr float kKeyGap = 10, kKeyHeight = 20, kKeyPadding = 6;
// Candidate row: padding 6 6 6 8; cells padding 5 11 6, 25px line, 5px label gap, 2px apart.
constexpr float kRowTop = 6, kRowBottom = 6, kRowLeft = 8, kRowRight = 6;
constexpr float kCellTop = 5, kCellBottom = 6, kCellSide = 11, kCellLine = 25, kLabelGap = 5, kCellGap = 2;
constexpr float kPageLeft = 12, kPageRight = 13;  // kRowRight + kPageRight = kProofRight
// 中／英 badge.
constexpr float kBadgeSide = 30, kBadgePadding = 11;
// Motion: the selection slides, the grid grows and folds; the badge fades.
constexpr int kSlideMs = 120, kResizeMs = 160, kBadgeFadeInMs = 120, kBadgeHoldMs = 800, kBadgeFadeOutMs = 250;

constexpr float kProofSize = 14, kCandidateSize = 19, kLabelSize = 12, kPageSize = 11, kKeySize = 11, kBadgeSize = 15;
constexpr const wchar_t* kTextFont = L"Microsoft JhengHei UI";
constexpr const wchar_t* kMonoFont = L"Cascadia Mono";  // falls back to Consolas
constexpr const wchar_t* kKeyFont = L"Segoe UI";

constexpr const wchar_t* kReplacedArrow = L" → ";

// sRGB colors, 0xRRGGBB, from the prototype's dark theme.
constexpr uint32_t kBackground = 0x23282c, kLine = 0x343b40, kText = 0xe8ecee, kMuted = 0x8e9aa3;
constexpr uint32_t kAccent = 0x14806f, kAccentSoft = 0x3fb9a3;
constexpr float kDoubtMarkAlpha = 0.7f;

// Text formats and measurement. Not thread-safe; each thread makes its own.
class Fonts {
 public:
  Fonts();
  bool ok() const { return proof_ && candidate_ && label_ && page_ && key_ && badge_; }
  IDWriteFactory* factory() const { return factory_.Get(); }
  IDWriteTextFormat* proof() const { return proof_.Get(); }
  IDWriteTextFormat* candidate() const { return candidate_.Get(); }
  IDWriteTextFormat* label() const { return label_.Get(); }
  IDWriteTextFormat* page() const { return page_.Get(); }
  IDWriteTextFormat* key() const { return key_.Get(); }
  IDWriteTextFormat* badge() const { return badge_.Get(); }

  // Advance width of one line of text, rounded up to whole pixels.
  float widthOf(const std::wstring& text, IDWriteTextFormat* format) const;
  // A cell's natural width: side padding, label, gap, text, side padding.
  float cellWidth(const std::wstring& candidate) const;
  // Every column fits a two-character word; longer words span more columns.
  float columnWidth() const { return columnWidth_; }
  // Grid columns a candidate takes, 1 to 6.
  int span(const std::wstring& candidate) const;

 private:
  Microsoft::WRL::ComPtr<IDWriteTextFormat> makeFormat(const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight) const;
  Microsoft::WRL::ComPtr<IDWriteFactory> factory_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> proof_, candidate_, label_, page_, key_, badge_;
  float columnWidth_ = 0;
};

}  // namespace ProjectKey::Layout
