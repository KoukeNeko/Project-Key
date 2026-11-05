// The Direction A candidate window and the 中／英 badge, ported from the macOS
// PKCandidatePanel and PKModeIndicator (platforms/macos/Sources/CandidatePanel.mm).
// Both live on the app's UI thread, owned by its window, and never take focus.
#pragma once

#include <d2d1.h>
#include <windows.h>
#include <wrl/client.h>

#include <optional>
#include <string>

#include "Common/Layout.h"
#include "Engine/Controller.h"

namespace ProjectKey {

class ModeBadge;

class CandidateWindow {
 public:
  explicit CandidateWindow(ModeBadge& badge) : badge_(badge) {}
  ~CandidateWindow();
  // anchor is the composition's first line in screen coordinates.
  void show(const PanelContent& content, const RECT& anchor, HWND owner, int appearance);
  void hide();
  bool visible() const { return hwnd_ && IsWindowVisible(hwnd_); }
  RECT frame() const;
  const std::optional<PanelContent>& content() const { return content_; }

 private:
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
  bool ensureWindow();
  void applyAppearance(int appearance);  // Setting::kAppearance
  void paint();
  void tick();
  D2D1_SIZE_F fittingSize() const;
  float proofHeight() const;
  float rowHeight() const;
  float rowTop() const;
  D2D1_RECT_F selectionRect() const;
  D2D1_RECT_F shownSelectionRect() const;
  void drawProof(ID2D1RenderTarget* target, float top);
  void drawCandidates(ID2D1RenderTarget* target, float top, float width);
  float keyWidth() const;

  ModeBadge& badge_;
  HWND hwnd_ = nullptr;
  Layout::Fonts fonts_;
  Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
  Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
  std::optional<PanelContent> content_;
  bool proofBelow_ = false;  // window above the text: proof row last, next to the text
  int appearance_ = 0;
  bool material() const { return appearance_ != 0; }  // a system material shows through
  float scale_ = 1;
  // The selection highlight slides from one rect to the other; the window resizes from one frame to the other.
  D2D1_RECT_F pillFrom_{}, pillTo_{};
  RECT frameFrom_{}, frameTo_{};
  ULONGLONG slideStart_ = 0, resizeStart_ = 0;
  bool sliding_ = false, resizing_ = false;
};

class ModeBadge {
 public:
  ~ModeBadge();
  void show(const std::wstring& label, const RECT& anchor, HWND owner, const RECT* avoid);
  // Moves a showing badge out of `rect`, the candidate window's frame.
  void avoid(const RECT& rect);
  // Moves a showing badge to a new anchor without restarting its fade.
  void follow(const RECT& anchor, const RECT* avoid);
  void hide();
  bool visible() const { return hwnd_ && IsWindowVisible(hwnd_); }

 private:
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
  void place(const RECT& anchor, const RECT* avoid);
  void render();
  void present(BYTE alpha);
  void tick();

  HWND hwnd_ = nullptr;
  Layout::Fonts fonts_;
  Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
  HDC dc_ = nullptr;
  HBITMAP bitmap_ = nullptr;
  HGDIOBJ oldBitmap_ = nullptr;
  POINT position_{};
  float scale_ = 1;
  SIZE size_{};
  std::wstring label_;
  ULONGLONG start_ = 0;
};

}  // namespace ProjectKey
