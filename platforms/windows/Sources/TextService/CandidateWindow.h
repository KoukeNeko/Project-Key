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
#include "Controller.h"

namespace ProjectKey {

class ModeBadge;

class CandidateWindow {
 public:
  explicit CandidateWindow(ModeBadge& badge) : badge_(badge) {}
  ~CandidateWindow();
  // anchor is the composition's first line in screen coordinates.
  void show(const PanelContent& content, const RECT& anchor, HWND owner, int appearance);
  // The Tab hint's own window: next to `panel`, on the side away from the text.
  void showBeside(const PanelContent& content, const CandidateWindow& panel, HWND owner, int appearance);
  void hide();
  bool visible() const { return hwnd_ && IsWindowVisible(hwnd_) && !hiding_; }
  RECT frame() const;
  const std::optional<PanelContent>& content() const { return content_; }

 private:
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
  bool ensureWindow();
  void applyAppearance(int appearance);  // Setting::kAppearance
  void paint();
  void tick();
  void setLayered(bool layered);
  void fadeIn();
  void startFade();
  void reveal();
  void finishHiding();
  D2D1_SIZE_F fittingSize() const;
  float proofHeight() const;
  float rowHeight() const;
  float tailWidth() const;
  float rowTop() const;
  D2D1_RECT_F selectionRect() const;  // relative to the rows
  D2D1_RECT_F shownSelectionRect() const;
  float shownRowTop() const;  // where the rows start, as drawn now
  bool proofMoves() const;
  void drawProofRow(ID2D1RenderTarget* target, const PanelContent& content, bool below, float alpha, float width, float height);
  void drawProof(ID2D1RenderTarget* target, const PanelContent& content, float top);
  void drawCandidates(ID2D1RenderTarget* target, float top, float width);
  float keyWidth(const PanelContent& content) const;

  ModeBadge& badge_;
  HWND hwnd_ = nullptr;
  Layout::Fonts fonts_;
  Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
  Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
  std::optional<PanelContent> content_;
  bool proofBelow_ = false;  // window above the text: proof row last, next to the text
  RECT placed_{};            // where the last show put the window, before any resize animation ends
  int appearance_ = 0;
  bool material() const { return appearance_ != 0; }  // a system material shows through
  float scale_ = 1;
  // The selection highlight slides from one rect to the other, relative to the rows.
  D2D1_RECT_F pillFrom_{}, pillTo_{};
  // resizing_: the window changing shape, moving and resizing from frameFrom_ to frameTo_ while
  // progress_ runs from 0 to 1: the rows move from oldRowTop_ and a proof row that comes, goes
  // or changes sides fades from its place in oldContent_ to its new one.
  RECT frameFrom_{}, frameTo_{};
  ULONGLONG slideStart_ = 0, resizeStart_ = 0;
  bool sliding_ = false, resizing_ = false;
  std::optional<PanelContent> oldContent_;
  bool oldProofBelow_ = false;
  float oldRowTop_ = 0, progress_ = 1;
  // Fading in as the window opens and, hiding_, out as it closes, layered only meanwhile. A
  // window with a material cannot be layered: its content fades and it grows and shrinks (reveal).
  bool fading_ = false, hiding_ = false;
  float alpha_ = 1, fadeFrom_ = 1;
  ULONGLONG fadeStart_ = 0;
};

class ModeBadge {
 public:
  ~ModeBadge();
  // appearance is the candidate window's (Setting::kAppearance), so the two match.
  void show(const std::wstring& label, const RECT& anchor, HWND owner, const RECT* avoid, int appearance);
  // Moves a showing badge out of `rect`, the candidate window's frame.
  void avoid(const RECT& rect);
  // Moves a showing badge to a new anchor without restarting its fade.
  void follow(const RECT& anchor, const RECT* avoid);
  void hide();
  bool visible() const { return hwnd_ && IsWindowVisible(hwnd_); }

 private:
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
  bool ensureWindow(int appearance);
  void place(const RECT& anchor, const RECT* avoid);
  void render();
  void present(BYTE alpha);
  void paint();
  void tick();

  // Dark: a layered window that fades. With a material: a plain window, as
  // layered windows take no system backdrop.
  HWND hwnd_ = nullptr;
  int appearance_ = 0;
  Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
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
