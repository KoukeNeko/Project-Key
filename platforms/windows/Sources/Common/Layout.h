// Candidate window fonts and text measurement, shared by the text service,
// which draws the window, and the engine server, which packs candidates into
// grid rows. The metrics shared with macOS and Linux are in PanelMetrics.h;
// the Windows variant adds Microsoft JhengHei, 19px candidates, and 8px window
// and 4px selection corners. Lengths are device-independent pixels.
#pragma once

#include <dwrite.h>
#include <wrl/client.h>

#include <string>

#include "PanelMetrics.h"

namespace ProjectKey::Layout {

using namespace Metrics;
using namespace Metrics::Colors;

constexpr float kWindowRadius = 8, kPillRadius = 4;

constexpr float kProofSize = 14, kCandidateSize = 19, kLabelSize = 12, kPageSize = 11, kKeySize = 11, kBadgeSize = 15;
constexpr const wchar_t* kTextFont = L"Microsoft JhengHei UI";
constexpr const wchar_t* kMonoFont = L"Cascadia Mono";  // falls back to Consolas
constexpr const wchar_t* kKeyFont = L"Segoe UI";

constexpr const wchar_t* kReplacedArrow = L" → ";

constexpr float kAcrylicTint = 0.6f;  // the dark background's opacity over Acrylic

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
