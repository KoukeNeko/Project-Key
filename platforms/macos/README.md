# macOS 輸入法

InputMethodKit 注音輸入法，大千鍵盤，候選視窗採用 [A 整句校對條](../../docs/design/candidate-window/notes.md) 第 02 輪的設計。選字引擎與詞庫目前使用 McBopomofo（MIT，固定 commit），組字邏輯在 `Sources/Composer.*`，不依賴 InputMethodKit。

## 建置與安裝

需要 Xcode（或 Command Line Tools）與 CMake，macOS 13 以上。

```bash
platforms/macos/build.sh     # 下載並編譯引擎、產生詞庫、跑組字測試、輸出 .build/ProjectKey.app
platforms/macos/install.sh   # 安裝到 ~/Library/Input Methods 並向系統註冊
```

安裝後到「系統設定 > 鍵盤 > 輸入方式」按「+」，在繁體中文下加入 Project Key。之後重新安裝不需要再加一次。

移除：刪掉 `~/Library/Input Methods/ProjectKey.app`，再從輸入方式清單移除。

## 按鍵

| 狀態 | 按鍵 | 動作 |
|---|---|---|
| 組字中 | 注音鍵、聲調鍵 3 4 6 7 | 輸入注音；聲調鍵結束音節 |
| | Space | 結束一聲音節；沒有未完成的注音時，開啟候選 |
| | ↓、Tab | 開啟候選：游標在句尾時從第一個可疑處開始，否則從游標後的詞開始 |
| | Enter | 送出整句 |
| | ←、→、Home、End | 在字與字之間移動游標，新輸入的字插在游標處 |
| | Delete | 刪除一個注音符號或游標前的字 |
| | ⌦ | 刪除游標後的字 |
| | Esc | 清除未完成的注音；再按一次清除整句 |
| | ⇧ + 標點鍵 | 全形標點，例如 ⇧, 輸入「，」 |
| 選字中 | 1–7 | 選字，選完跳到下一個可疑處 |
| | ←、→ | 移動選取 |
| | Space、↓、PageDown | 下一頁 |
| | ↑ | 上一頁；在第一頁時關閉候選，回到組字 |
| | PageUp | 上一頁，停在第一頁 |
| | Enter | 選定目前候選 |
| | Tab、⇧Tab | 跳到下一個／上一個可疑處 |
| | Esc、Delete | 回到組字 |
| 任何時候 | 單按 ⇧ | 切換中／英，游標下方短暫顯示「ㄅ」或「A」 |
| 組字中 | ⌃⇧P | 開關整句預覽 |

整句預覽有三種模式（永遠、有可疑字時、關閉），可在輸入法選單或「設定…」視窗中切換，設定會保存。選單列圖示是系統繪製的「ㄅ」標籤，會跟著淺色與深色選單列變色。

## 可疑處的判斷

引擎走出的每個詞，若與同讀音的下一個候選分數差距小於 0.5（log10），就標為可疑。使用者選過的詞不再標示。這只能抓到同音詞的猶豫，抓不到斷詞錯誤，例如「重構成」被斷成「重／構成」。

## 上下文排序

有 `tools/bigram-trainer/.cache/bigram.bin` 時，`build.sh` 會把它放進 App，組字器改用詞 bigram 依前後文選字；沒有時與 McBopomofo 的選字結果相同。評測結果見 `tools/corpus-evaluator/results/2026-11-05-bigram.md`。

## 尚未實作

- 聯想詞：McBopomofo 的聯想詞資料需要另外產生。
- 候選展開成格狀、滑鼠點選、出現與切換的動畫。
- 淺色候選視窗（目前固定深色）。
- 整句預覽列不顯示游標位置，未完成的注音一律畫在句尾。
- 個人化學習與自訂詞：每次組字都從詞庫預設值開始。
- 候選清單的排序仍是詞庫分數，未依前後文調整。
- Universal binary：只編譯本機架構。程式碼簽章為 ad-hoc，尚未公證，無法散布給其他電腦。

`ProjectKey --preview <資料夾>` 會把候選視窗的幾個狀態畫成 PNG，供設計檢查。
