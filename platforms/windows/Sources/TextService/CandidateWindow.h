// The Direction A candidate window and the 中／英 badge, ported from the macOS
// PKCandidatePanel and PKModeIndicator (platforms/macos/Sources/CandidatePanel.mm).
// Both live on the app's UI thread, owned by its window, and never take focus.
#pragma once

#include <d2d1.h>
#include <windows.h>
#include <wrl/client.h>

#include <functional>
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
  bool visible() const { return hwnd_ && IsWindowVisible(hwnd_) && !cloaked_ && !hiding_; }
  RECT frame() const;
  const std::optional<PanelContent>& content() const { return content_; }
  // A left click on a candidate or the chevron; unset, clicks go to the app.
  std::function<void(const Metrics::PanelHit&)> clicked;

 private:
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
  Metrics::PanelHit hitAt(POINT client) const;
  void hoverAt(POINT client);  // the candidate under the pointer, drawn on a faint highlight
  bool ensureWindow();
  void applyAppearance(int appearance);  // Setting::kAppearance
  void prepareToAppear();                // before the first paint of a showing
  void appear();                         // after it
  void finishHide();                     // once it has faded out
  float fadeAlpha() const;
  void applyFade();                      // sets the window's opacity to fadeAlpha
  void paint();
  void tick();
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
  bool cloaked_ = false;   // mapped but out of sight, between showings
  bool settling_ = false;  // its material was just set, and is not up yet
  bool fading_ = false, hiding_ = false;  // hiding_: fading out, already gone to the text service
  float fadeFrom_ = 0, fadeTo_ = 1;
  ULONGLONG fadeStart_ = 0;
  Metrics::PanelHit pressed_;  // what the left button went down on
  Metrics::PanelHit hover_;
  std::vector<std::vector<int>> spans_;  // grid columns of content_'s candidates, for hitAt
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
  bool visible() const { return hwnd_ && IsWindowVisible(hwnd_) && !cloaked_; }

 private:
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
  bool ensureWindow(int appearance);
  void place(const RECT& anchor, const RECT* avoid);
  void render();
  void present(BYTE alpha);
  void paint();
  void tick();

  // A layered window that fades, material and all.
  HWND hwnd_ = nullptr;
  int appearance_ = 0;
  bool cloaked_ = false;   // with a material: mapped but out of sight, between showings
  bool settling_ = false;  // its material was just set, and is not up yet
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
