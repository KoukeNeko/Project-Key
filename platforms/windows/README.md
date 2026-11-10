# Windows 輸入法

TSF（Text Services Framework）注音輸入法（大千鍵盤），功能與按鍵和 [macOS 版](../macos/README.md) 相同。候選視窗採用 [A 整句校對條](../../docs/design/candidate-window/notes.md) 第 03 輪的 Windows 版樣式，設定視窗依照 [設定頁：Windows 版](../../docs/design/settings/windows.md)。

## 架構

依 [可行性評估](../../docs/feasibility.md) 第五節，分成兩層：

| 元件 | 位置 | 職責 |
|---|---|---|
| `ProjectKey.dll` | 載入每個 App | TSF 文字服務：把按鍵送給引擎、依回覆改寫組字區、繪製候選視窗與中／英標記、工作列的中／英按鈕 |
| `ProjectKeyServer.exe` | 每位使用者一個行程 | 詞庫、上下文模型、選字紀錄與中／英狀態，所有 App 共用；設定讀寫在登錄檔 |
| `Settings\ProjectKeySettings.exe` | 由輸入法開啟 | WinUI 3 設定視窗 |

兩者以 named pipe `\\.\pipe\ProjectKey-<使用者 SID>` 溝通，ACL 開放給 ALL APPLICATION PACKAGES 與低完整性層級，所以 Store App 與沙盒中的 App 也能使用。每個按鍵一來一回，伺服器沒有回應時（最多等 0.5 秒），按鍵照常交給 App。

組字與按鍵邏輯與 macOS 共用：`../../core/engine` 的 `Composer`、`Bigram`、`Controller` 直接編進伺服器，不依賴 Windows API。

## 建置與安裝

發行版：

```powershell
scoop bucket add koukeneko https://github.com/KoukeNeko/scoop-bucket
scoop install koukeneko/project-key
```

自行建置需要 Visual Studio 2026（「使用 C++ 的桌面開發」，含 Windows SDK）、.NET 10 SDK 與 [uv](https://docs.astral.sh/uv/)（產生詞庫用的 Python）。設定視窗使用系統的 Windows App Runtime 2（Windows 11 已內建）。

```powershell
platforms\windows\build.ps1      # 下載並編譯引擎、產生詞庫、跑測試，輸出到 build\out
platforms\windows\install.ps1    # 安裝到 C:\Program Files\Project Key 並註冊（會要求系統管理員權限）
```

安裝後會開啟歡迎視窗，按「加入」就會加進鍵盤清單；也可以到「設定 > 時間與語言 > 語言與地區 > 中文（台灣）> 語言選項 > 新增鍵盤」加入 Project Key。之後用 Win+Space 切換。重新安裝不需要再加一次；正在使用舊版的 App 要重新開啟才會換成新版。

移除：`platforms\windows\uninstall.ps1`（設定留在 `HKCU\Software\ProjectKey`）。

開著「智慧型應用程式控制」（Smart App Control）的電腦會依雲端信譽逐一判斷沒有簽章的程式，新建置的測試程式、伺服器或 DLL 可能被擋下。`build.ps1` 遇到時會提示而不是當成測試失敗；建置是確定性的（`/Brepro`），原始碼不變時檔案位元組也不變。正式散布需要程式碼簽章。

## 按鍵

與 macOS 相同，按鍵名稱換成 PC 鍵盤：

| 狀態 | 按鍵 | 動作 |
|---|---|---|
| 組字中 | 注音鍵、聲調鍵 3 4 6 7 | 輸入注音；聲調鍵結束音節。按到已有注音的位置就換掉它（ㄅ 再按 ㄆ 為 ㄆ，ㄋㄧ 再按 ㄨ 為 ㄋㄨ）。聲調可省略：拼成完整音節後，位置更前面或接不上的注音開始下一個音節，不分聲調比對（ㄖㄣㄨㄟ 為「認為」）。開啟「省略聲調」（預設關）時，按到音節結尾的位置也開始下一個音節（ㄕㄉㄜ 為「是的」）。還沒結束的音節顯示注音，結束後才變成字。打字時候選視窗只有一列，列出剛打完的詞（開啟簡拼時是還沒結束的音節）的其他候選，樣子與選字時相同，猜的詞有底色；這時主鍵盤的 1–6 仍是注音，開啟「數字鍵盤選字」時數字鍵盤的 1–6 選取這一列的字。整句預覽照設定出現。方向鍵移動游標後收起。開啟簡拼（預設關）時，單獨一個注音也代表以它開頭的音節（ㄨㄇ 為「我們」），所以按到已有注音的位置就開始下一個音節；還沒結束的音節也先猜成字，列在候選列（組字區顯示「你ㄏㄠ」） |
| | Space | 結束一聲音節；沒有還沒打聲調的注音時，開啟候選 |
| | Tab | 送出整句，還沒結束的音節用猜的字（你ㄏㄠ 為「你好」）；整句預覽列最右邊有 Tab 鍵帽。打的鍵可以轉成英文時 Tab 轉英文，改用 Shift+Tab 送出 |
| | ↓ | 開啟候選。候選也列出同樣注音、不同聲調的字詞，排在打的聲調之後，所以 ㄨㄟˇ ㄗㄠˋ 也找得到「偽造」（ㄨㄟˋ ㄗㄠˋ）；整句仍照打的聲調猜。選字位置為游標後（預設）：游標在句尾時從第一個可疑處開始（用方向鍵移到句尾時則是最後一個字），否則從游標後的字開始。選字位置為游標前：游標前的詞，也列出結尾在游標、更長的詞；較短的候選是詞尾的字（「打字」之後是 ㄗˋ 的「字」「自」…，選了只換掉詞尾）；游標在詞中間時是游標前那一段（今｜天 為「今」） |
| | Enter | 送出整句；還沒打聲調的最後一個音節送出注音符號（ㄋㄧㄏㄠ 為「你ㄏㄠ」） |
| | ←、→、Home、End | 在字與字之間移動游標 |
| | Backspace | 刪除一個注音符號或游標前的字 |
| | Delete | 刪除游標後的字 |
| | Esc | 清除未完成的注音；再按一次清除整句 |
| | Shift + 標點鍵 | 全形標點，例如 Shift+, 輸入「，」 |
| | 標點鍵 | `\` 或 `'` 輸入「、」，`[` `]` 輸入 `「` `」` |
| | 其他字元（如 Shift+字母） | 送出整句，接著輸入該字元 |
| | Ctrl+Shift+P | 開關整句預覽 |
| 選字中 | 1–9 | 選取所在那一列的字，選完跳到下一個可疑處（可在設定關閉，關閉時選完即結束選字） |
| | ←、→ | 移動選取 |
| | ↓ | 展開成多列候選，移到下一列的同一欄。還有其他列時，候選列最右邊有頁碼，收合時接 ⌄，展開後接 ⌃。每列 6 至 9 欄（設定，預設 6），長詞佔多欄。注音本身（如「ㄏㄡˋ」）接在第一列之後，選它會連同整句送出 |
| | Space、Page Down | 下一列；展開時往下捲 5 列 |
| | ↑ | 上一列；展開時在第一列收合，未展開時在第一列關閉候選 |
| | Page Up | 上一列，停在第一列；展開時往上捲 5 列 |
| | Enter | 選定目前候選 |
| | Tab、Shift+Tab | 跳到下一個／上一個可疑處 |
| | Esc | 展開時收合，否則回到組字 |
| | Backspace | 回到組字 |
| 任何時候 | Caps Lock | 在設定開啟 Caps Lock 切換中英時（預設關），切換中／英；大寫鎖定亮著時字母仍打小寫，大寫用 Shift |
| | 單按 Shift | 切換中／英，游標下方短暫顯示「ㄅ」或「A」 |
| | 游標進入輸入欄位 | 游標下方短暫顯示目前的模式「ㄅ」或「A」，不切換 |
| | Ctrl、Alt、Win 組合鍵 | 先送出組字，再交給 App |
| 英文模式 | Tab | 剛才打的大千鍵若是含聲調鍵的完整注音（如 ji3cl3），改成組字中的「我好」並切回中文；打到聲調鍵時會預覽轉換結果與 Tab 鍵帽。沒有預覽時 Tab 照常輸入 |
| 組字中 | Tab | 組字的按鍵若拼成英文單字（如 hello，且不是注音音節），改回英文並切到英文模式，此時游標下方會預覽英文與 Tab 鍵帽，最後一個音節還沒結束時，預覽是候選視窗旁的小視窗。拼字檢查用 Windows 內建的英文拼字檢查 |

工作列通知區域會顯示目前的模式（ㄅ／A）：左鍵切換中英，右鍵開啟「設定…」。Windows「設定」裡 Project Key 的「選項」也會開啟設定視窗。安裝時也在開始選單加入「Project Key」，鍵盤清單或通知區域的圖示出不來時，可以從這裡開啟設定。設定視窗由引擎伺服器啟動：從沙盒中的 App 直接啟動的程式也會被關進沙盒。

## 設定

設定視窗的內容與 macOS 相同：選字位置（游標後或游標前）、每列欄數、選完跳到下一個可疑處、簡拼、數字鍵盤選字（預設關）、記住選過的字（可清除紀錄；開著時也從送出的文字學習）、候選視窗外觀（深色、Acrylic（預設）、Mica、Mica Alt）、輸入時與選字時的整句預覽（永遠／有可疑字時／關閉，預設有可疑字時）、Caps Lock（預設關）與單按 Shift 切換中英、Tab 的英文轉注音與注音轉英文，以及「關於」（版本取自 `ProjectKey.dll`，並查 GitHub 最新發行版有無新版：有新版時按「更新到」會下載發行版的 zip、核對 GitHub 列出的 SHA-256，再執行其中的 `install.ps1 -Update`，經一次系統管理員確認後安裝，並重新開啟設定；開著的 App 重新開啟後才換成新版的文字服務；「選字模型」顯示模型日期，有新模型時可以直接下載，存在 `%LOCALAPPDATA%\Project Key`，核對 SHA-256 後不必重新安裝就生效）。改了立即寫入 `HKCU\Software\ProjectKey`（DWORD，名稱同 macOS 的 NSUserDefaults 鍵），下一次組字時生效。設計文件寫的是 `ApplicationData.LocalSettings`，但那只有封裝（MSIX）的 App 能用。

## 測試
## 自訂詞

自訂詞在 `%LOCALAPPDATA%\Project Key\phrases.txt`，設定視窗的「編輯…」開啟；不要出現的詞放在同一個資料夾的 `exclude-phrases.txt`。格式與 macOS 相同，見 [自訂詞](../macos/README.md#自訂詞)。


`build.ps1` 會執行：

- `ComposerTest`：與 macOS 共用的組字測試。
- `ControllerTest`：與 macOS 共用的按鍵邏輯測試。
- `ProtocolTest`：pipe 訊息的讀寫，以及被截斷的訊息會被拒絕。

安裝後可以再跑：

- `Tests\ServerTest.ps1`：直接透過 pipe 對執行中的伺服器打一整句、選字、送出。
- `Tests\TypingTest.ps1`：開一個 WPF 文字框（`-Kind winforms` 改用 Win32 的 Edit 控制項），只在該程序內啟用 Project Key，用模擬按鍵打字並截圖到 `build\shots`：組字、候選、展開、中／英標記、英文轉注音預覽、Caps Lock。`-Flip` 把文字放在螢幕底部，檢查候選視窗翻到上方。需要已解鎖的桌面。
- `Tests\BrowserTest.ps1`：在 Edge 的 textarea 打字並單按 Shift 切換中英。Chromium 會跳過 TSF 的測試階段，直接呼叫 `OnKeyDown`／`OnKeyUp`。Edge 無法只對單一程序啟用鍵盤，測試期間會把整個工作階段切到 Project Key，結束後切回英文（美國）。
- `Tests\RestoreKeyboard.ps1`：把整個工作階段的鍵盤切回英文（美國）。

## 與 macOS 的差異

- 視窗外觀用 Windows 11 的語彙：DWM 圓角 8px 與 1px 外框、選中膠囊圓角 4px、微軟正黑體 19px；macOS 的「Liquid Glass」在 Windows 換成三種系統材質：Acrylic（模糊背後的內容）、Mica 與 Mica Alt（取桌布的色調）。Mica 只畫在作用中的視窗上，候選視窗從不取得焦點，所以讓系統把它的框線當成作用中。中／英標記跟著候選視窗用同一種外觀。視窗剛出現時 DWM 還沒畫好材質，所以兩者都先隱形（cloak）備妥材質再顯示，並在使用之間保持隱形而不關閉。候選視窗與 Tab 提示開啟、收起時，整個視窗連同材質一起淡入淡出（分層視窗的 `SetLayeredWindowAttributes`）。Acrylic 會透出背後的視窗，兩者都加一層深色。
- 轉換預覽的鍵帽寫「Tab」。
- 組字中打出不是注音也不是標點的字元時，會送出組字並由輸入法輸入該字元。macOS 把按鍵交還給 App；TSF 不會再把已經判定交給 App 的按鍵送回來，所以改由輸入法輸入。
- 透過 IMM32 相容層運作的傳統 Win32 文字框（例如 WinForms 的 TextBox）不提供文件內容給 TSF：在這些 App 裡，英文模式不顯示轉注音預覽，Tab 照常輸入。
- 候選視窗可用滑鼠點選：點候選字即選取，點第一列尾端的頁碼或箭頭展開或收合候選格線。打字時顯示的候選列也可點選，點選後可繼續打字。點在其他位置時，點擊會交給 App。

## 尚未實作

- 候選清單的 UI Automation 支援（讓朗讀程式讀出候選字）；目前只送出 `EVENT_OBJECT_IME_SHOW／HIDE／CHANGE`。
- 避開觸控鍵盤、遊戲全螢幕用的 UI Element（`ITfUIElement`）。
- 聯想詞。
- 自行建置的上下文模型：`tools/bigram-trainer` 的訓練腳本是 shell script，在 Windows 上需要另外執行，或用 Git Bash 執行 `fetch-models.sh` 下載發行版那一組；有 `bigram.bin`、`chars.bin` 時 `build.ps1` 會一併安裝；有補詞後的 `data.txt` 時，以它取代 McBopomofo 的詞庫。沒有它時選字與 McBopomofo 相同。發行版已附上。
- 選過的字只存在伺服器記憶體，登出或重新安裝後清空（同 macOS）；從送出的文字學到的字串次數存在 `%LOCALAPPDATA%\Project Key\personal.txt`，設定的「清除」一併清空。Store App、沙盒中與低完整性的 App 不從這份紀錄排序，也不會加入紀錄，以免從候選順序讀出在其他 App 打過的字。
- 組字一律當成句首：文字服務只在英文模式按 Tab 時把游標前的文字傳給伺服器，macOS 則接著游標前的漢字選字。
- 程式碼簽章與安裝程式；目前用 PowerShell 腳本安裝，只支援 x64。
