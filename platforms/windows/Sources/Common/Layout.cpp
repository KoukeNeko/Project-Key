#include "Common/Layout.h"

#include <algorithm>
#include <cmath>

#include "Engine/Controller.h"

using Microsoft::WRL::ComPtr;

namespace ProjectKey::Layout {

Fonts::Fonts() {
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(factory_.GetAddressOf())))) {
    return;
  }
  proof_ = makeFormat(kTextFont, kProofSize, DWRITE_FONT_WEIGHT_NORMAL);
  candidate_ = makeFormat(kTextFont, kCandidateSize, DWRITE_FONT_WEIGHT_NORMAL);
  label_ = makeFormat(kMonoFont, kLabelSize, DWRITE_FONT_WEIGHT_MEDIUM);
  page_ = makeFormat(kMonoFont, kPageSize, DWRITE_FONT_WEIGHT_MEDIUM);
  key_ = makeFormat(kKeyFont, kKeySize, DWRITE_FONT_WEIGHT_MEDIUM);
  badge_ = makeFormat(kTextFont, kBadgeSize, DWRITE_FONT_WEIGHT_MEDIUM);
  if (ok()) columnWidth_ = cellWidth(L"國國");
}

ComPtr<IDWriteTextFormat> Fonts::makeFormat(const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight) const {
  // Cascadia Mono ships with Windows 11; older systems have Consolas.
  if (family == kMonoFont) {
    ComPtr<IDWriteFontCollection> fonts;
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (SUCCEEDED(factory_->GetSystemFontCollection(&fonts)) && SUCCEEDED(fonts->FindFamilyName(family, &index, &exists)) && !exists) {
      family = L"Consolas";
    }
  }
  ComPtr<IDWriteTextFormat> format;
  if (FAILED(factory_->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                                        L"zh-TW", &format))) {
    return nullptr;
  }
  format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
  return format;
}

float Fonts::widthOf(const std::wstring& text, IDWriteTextFormat* format) const {
  if (text.empty()) return 0;
  ComPtr<IDWriteTextLayout> layout;
  if (FAILED(factory_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format, 10000, 1000, &layout))) return 0;
  DWRITE_TEXT_METRICS metrics{};
  layout->GetMetrics(&metrics);
  return std::ceil(metrics.widthIncludingTrailingWhitespace);
}

float Fonts::cellWidth(const std::wstring& candidate) const {
  return kCellSide + widthOf(L"6", label()) + kLabelGap + widthOf(candidate, this->candidate()) + kCellSide;
}

int Fonts::span(const std::wstring& candidate) const {
  int span = static_cast<int>(std::ceil((cellWidth(candidate) + kCellGap) / (columnWidth_ + kCellGap)));
  return std::max(1, std::min(kGridColumns, span));
}

}  // namespace ProjectKey::Layout
