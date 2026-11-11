# Windows 輸入法

以 TSF（Text Services Framework）實作的注音輸入法，功能與按鍵與 [macOS 版](../macos/README.md) 相同。候選視窗採用 [A 整句校對條](../../docs/design/candidate-window/notes.md) 第 03 輪的 Windows 版樣式，設定視窗依據 [設定頁：Windows 版](../../docs/design/settings/windows.md)。

## 架構

依 [可行性評估](../../docs/feasibility.md) 第五節，分為兩層：

| 元件 | 位置 | 職責 |
|---|---|---|
| `ProjectKey.dll` | 載入至每個 App | TSF 文字服務：將按鍵送至引擎、依回覆改寫組字區、繪製候選視窗與中／英標記、提供工作列的中／英按鈕 |
| `ProjectKeyServer.exe` | 每位使用者一個行程 | 詞庫、上下文模型、選字紀錄與中／英狀態，由所有 App 共用；設定讀寫於登錄檔 |
| `Settings\ProjectKeySettings.exe` | 由輸入法開啟 | WinUI 3 設定視窗 |

兩者以 named pipe `\\.\pipe\ProjectKey-<使用者 SID>` 溝通，ACL 開放予 ALL APPLICATION PACKAGES 與低完整性層級，因此 Store App 與沙盒中的 App 亦可使用。每個按鍵往返一次；伺服器未回應時（最多等待 0.5 秒），按鍵照常交由 App 處理。

組字與按鍵邏輯與其他平台共用：`../../core/engine` 的 `Composer`、`Bigram`、`Controller` 直接編譯進伺服器，不依賴 Windows API。

## 建置與安裝

發行版：

```powershell
scoop bucket add koukeneko https://github.com/KoukeNeko/scoop-bucket
scoop install koukeneko/project-key
```

自行建置須具備 Visual Studio 2026（「使用 C++ 的桌面開發」，含 Windows SDK）、.NET 10 SDK 與 [uv](https://docs.astral.sh/uv/)（產生詞庫所用的 Python）。設定視窗使用系統的 Windows App Runtime 2（Windows 11 已內建）。

```powershell
platforms\windows\build.ps1      # 下載並編譯引擎、產生詞庫、跑測試，輸出到 build\out
platforms\windows\install.ps1    # 安裝到 C:\Program Files\Project Key 並註冊（會要求系統管理員權限）
```

安裝完成後開啟歡迎視窗，按「加入」即加入鍵盤清單；亦可於「設定 > 時間與語言 > 語言與地區 > 中文（台灣）> 語言選項 > 新增鍵盤」加入 Project Key。之後以 Win+Space 切換。重新安裝無須再次加入；正在使用舊版的 App 須重新開啟才會改用新版。

移除方式：執行 `platforms\windows\uninstall.ps1`（設定保留於 `HKCU\Software\ProjectKey`）。

啟用「智慧型應用程式控制」（Smart App Control）的電腦會依雲端信譽逐一判斷未簽章的程式，新建置的測試程式、伺服器或 DLL 可能遭到封鎖。`build.ps1` 遇到此情況時會提示，而不視為測試失敗；建置具確定性（`/Brepro`），原始碼不變時檔案位元組亦不變。正式散布須進行程式碼簽章。

## 按鍵

與 macOS 相同，按鍵名稱改為 PC 鍵盤：

| 狀態 | 按鍵 | 動作 |
|---|---|---|
| 組字中 | 注音鍵、聲調鍵 3 4 6 7 | 輸入注音；聲調鍵結束音節。輸入已有注音的位置時取代該注音（ㄅ 再按 ㄆ 為 ㄆ，ㄋㄧ 再按 ㄨ 為 ㄋㄨ）。聲調可省略：拼成完整音節後，位置較前或無法接續的注音開始下一個音節，並不分聲調比對（ㄖㄣㄨㄟ 為「認為」）。開啟「省略聲調」（預設關閉）時，輸入音節結尾位置的注音亦開始下一個音節（ㄕㄉㄜ 為「是的」）。未依順序輸入的注音於按下聲調鍵時合併為一個音節（ㄧㄚㄐ 加一聲為 ㄐㄧㄚ），依輸入順序已構成詞語者除外；開啟簡拼時不合併。尚未結束的音節顯示為注音，結束後轉為字。輸入時候選視窗僅顯示一列，列出剛輸入完成之詞（開啟簡拼時為尚未結束的音節）的其他候選，外觀與選字時相同，推測的詞加上底色；此時主鍵盤的 1–6 仍為注音，開啟「數字鍵盤選字」時，數字鍵盤的 1–6 選取該列的字。整句預覽依設定顯示。以方向鍵移動游標後收起。開啟簡拼（預設關閉）時，單一注音亦代表以其開頭的音節（ㄨㄇ 為「我們」），因此輸入已有注音的位置即開始下一個音節；尚未結束的音節亦先推測為字，列於候選列（組字區顯示「你ㄏㄠ」） |
| | Space | 結束一聲音節；無未加聲調的注音時開啟候選 |
| | Tab | 送出整句，尚未結束的音節採用推測的字（你ㄏㄠ 為「你好」）；整句預覽列最右側顯示 Tab 鍵帽。所輸入的按鍵可轉為英文時，Tab 改為轉換英文，送出改用 Shift+Tab |
| | ↓ | 開啟候選。候選亦列出注音相同、聲調不同的字詞，排在所輸入聲調的字詞之後，因此 ㄨㄟˇ ㄗㄠˋ 亦可找到「偽造」（ㄨㄟˋ ㄗㄠˋ）；整句仍依所輸入的聲調推測。選字位置為游標後（預設）時：游標位於句尾時自第一個可疑處開始（以方向鍵移至句尾時則為最後一個字），否則自游標後的字開始。選字位置為游標前時：列出游標前的詞，以及結尾於游標的較長詞語；較短的候選為詞尾的字（「打字」之後為 ㄗˋ 的「字」「自」等，選取後僅取代詞尾）；游標位於詞中時為游標前的部分（今｜天 為「今」） |
| | Enter | 送出整句；最後一個未加聲調的音節以注音符號送出（ㄋㄧㄏㄠ 為「你ㄏㄠ」） |
| | ←、→、Home、End | 於字與字之間移動游標 |
| | Shift+←、Shift+→ | 選取游標旁的音節，按 Enter 將所選的詞加入自訂詞（說明同 macOS 版） |
| | Backspace | 刪除一個注音符號或游標前的字 |
| | Delete | 刪除游標後的字 |
| | Esc | 清除未完成的注音；再按一次清除整句 |
| | Shift + 標點鍵 | 全形標點，例如 Shift+, 輸入「，」 |
| | ` 、 ~ | 開啟符號表、表情符號表，選取方式同 macOS 版（說明見 macOS README） |
| | 標點鍵 | `\` 或 `'` 輸入「、」，`[` `]` 輸入 `「` `」` |
| | 其他字元（如 Shift+字母） | 送出整句，再輸入該字元 |
| | Ctrl+Shift+P | 開關整句預覽 |
| 選字中 | 1–9 | 選取所在列的字，選取後移至下一個可疑處（可於設定關閉，關閉時選取後即結束選字） |
| | ←、→ | 移動選取 |
| | ↓ | 展開為多列候選，移至下一列的同一欄。尚有其他列時，候選列最右側顯示頁碼，收合時附 ⌄，展開後附 ⌃。每列 6 至 9 欄（依設定，預設 6），長詞佔多欄。注音本身（如「ㄏㄡˋ」）位於第一列之後，選取時連同整句送出 |
| | Space、Page Down | 下一列；展開時向下捲動 5 列 |
| | ↑ | 上一列；展開時於第一列收合，未展開時於第一列關閉候選 |
| | Page Up | 上一列，至第一列為止；展開時向上捲動 5 列 |
| | Enter | 選定目前的候選 |
| | Tab、Shift+Tab | 移至下一個／上一個可疑處 |
| | + | 將反白的候選加入自訂詞（說明同 macOS 版） |
| | Delete 或 Shift+Backspace | 排除反白的候選，須按兩次；第一次按下時反白轉為紅色並顯示確認（說明同 macOS 版） |
| | Esc | 展開時收合，否則返回組字 |
| | Backspace | 返回組字 |
| 任何時候 | Caps Lock | 於設定開啟 Caps Lock 切換中英（預設關閉）時，切換中／英；大寫鎖定亮起時字母仍輸入小寫，大寫須按 Shift |
| | 單按 Shift | 切換中／英，游標下方短暫顯示目前的語言與寬度，如「ㄅ半」「Ａ半」 |
| | Shift+Space | 切換全形／半形，游標下方短暫顯示目前的語言與寬度，如「ㄅ全」「Ａ半」（說明同 macOS 版） |
| | 游標進入輸入欄位 | 游標下方短暫顯示目前的語言與寬度，不切換 |
| | Ctrl、Alt、Win 組合鍵 | 先送出組字，再交由 App 處理 |
| 英文模式 | Tab | 剛輸入的注音鍵若為含聲調鍵的完整注音（如 ji3cl3），轉為組字中的「我好」並切回中文；輸入聲調鍵時預覽轉換結果與 Tab 鍵帽。無預覽時 Tab 照常輸入 |
| 組字中 | Tab | 組字的按鍵若拼成英文單字（如 hello，且非注音音節），轉回英文並切換至英文模式；此時游標下方預覽英文與 Tab 鍵帽，最後一個音節尚未結束時，預覽顯示於候選視窗旁的小視窗。拼字檢查採用 Windows 內建的英文拼字檢查 |

工作列通知區域顯示目前的語言與寬度（如 ㄅ半、Ａ全）：按左鍵切換中英，按右鍵開啟「設定…」。Windows「設定」中 Project Key 的「選項」亦開啟設定視窗。安裝程式另於開始選單加入「Project Key」，鍵盤清單或通知區域的圖示未顯示時，可由此開啟設定。設定視窗由引擎伺服器啟動，因為自沙盒中的 App 直接啟動的程式同樣受限於沙盒。

## 設定

設定視窗的內容與 macOS 相同：

- 選字位置（游標後或游標前）、每列欄數。
- 鍵盤排列：大千（預設）、倚天、許氏、倚天 26 鍵、IBM。
- 開關：選完跳到下一個可疑處、簡拼、近似音（預設關閉）、數字鍵盤選字（預設關閉）、記住選過的字（可清除紀錄；開啟時亦自送出的文字學習）、Caps Lock（預設關閉）、單按 Shift 切換中英與 Shift+Space 切換全形（預設開啟），以及 Tab 的英文轉注音與注音轉英文。
- 候選視窗外觀：深色、Acrylic（預設）、Mica、Mica Alt。
- 輸入時與選字時的整句預覽：永遠／有可疑字時／關閉，預設為有可疑字時。
- 恢復原廠設定：「恢復設定」將所有設定改回預設值，保留自訂詞、排除詞與選字紀錄；「一併刪除資料」另刪除這些資料（`phrases.txt`、`exclude-phrases.txt`、`personal.txt`）並清除引擎伺服器記憶體中的選字紀錄。兩者皆保留下載的選字模型。
- 「關於」：版本取自 `ProjectKey.dll`，並查詢 GitHub 最新發行版有無新版。有新版時按「更新到」，即下載發行版的 zip、核對 GitHub 列出的 SHA-256，再執行其中的 `install.ps1 -Update`，經一次系統管理員確認後安裝並重新開啟設定；已開啟的 App 須重新開啟才會改用新版的文字服務。「選字模型」顯示模型日期，有新模型時可直接下載，存放於 `%LOCALAPPDATA%\Project Key`，核對 SHA-256 後無須重新安裝即生效。

變更立即寫入 `HKCU\Software\ProjectKey`（DWORD，名稱與 macOS 的 NSUserDefaults 鍵相同），於下一次組字時生效。設計文件記載的是 `ApplicationData.LocalSettings`，但該 API 僅限封裝（MSIX）的 App 使用。

## 自訂詞

自訂詞位於 `%LOCALAPPDATA%\Project Key\phrases.txt`，由設定視窗的「自訂詞」「編輯…」開啟；不再出現的詞列於同一資料夾的 `exclude-phrases.txt`，由「排除詞」開啟；「資料夾」顯示該資料夾。格式與 macOS 相同，見 [自訂詞](../macos/README.md#自訂詞)。

## 測試

`build.ps1` 執行下列測試：

- `ComposerTest`：與其他平台共用的組字測試。
- `ControllerTest`：與其他平台共用的按鍵邏輯測試。
- `ProtocolTest`：pipe 訊息的讀寫，以及截斷訊息的拒絕。

安裝後另可執行：

- `Tests\ServerTest.ps1`：直接透過 pipe 對執行中的伺服器輸入一整句、選字並送出。
- `Tests\TypingTest.ps1`：開啟一個 WPF 文字框（`-Kind winforms` 改用 Win32 的 Edit 控制項），僅於該程序內啟用 Project Key，以模擬按鍵輸入並將截圖存至 `build\shots`，涵蓋組字、候選、展開、中／英標記、英文轉注音預覽與 Caps Lock。`-Flip` 將文字置於螢幕底部，檢查候選視窗翻至上方。須於已解鎖的桌面執行。
- `Tests\BrowserTest.ps1`：於 Edge 的 textarea 輸入並單按 Shift 切換中英。Chromium 會略過 TSF 的測試階段，直接呼叫 `OnKeyDown`／`OnKeyUp`。Edge 無法僅對單一程序啟用鍵盤，測試期間整個工作階段切換至 Project Key，結束後切回英文（美國）。
- `Tests\RestoreKeyboard.ps1`：將整個工作階段的鍵盤切回英文（美國）。

## 與 macOS 的差異

- 視窗外觀採用 Windows 11 的設計語彙：DWM 圓角 8px 與 1px 外框、選取膠囊圓角 4px、微軟正黑體 19px。macOS 的 Liquid Glass 於 Windows 改為三種系統材質：Acrylic（模糊背後的內容）、Mica 與 Mica Alt（採用桌布的色調）。Mica 僅繪製於作用中的視窗，而候選視窗從不取得焦點，因此由系統將其框線視為作用中。中／英標記與候選視窗採用相同外觀。視窗剛出現時 DWM 尚未完成材質繪製，因此兩者皆先隱形（cloak）備妥材質再顯示，並於使用之間保持隱形而不關閉。候選視窗與 Tab 提示開啟、收起時，整個視窗連同材質一併淡入淡出（分層視窗的 `SetLayeredWindowAttributes`）。Acrylic 會透出背後的視窗，因此兩者皆疊加一層深色。
- 轉換預覽的鍵帽標示為「Tab」。
- 組字中輸入非注音亦非標點的字元時，送出組字並由輸入法輸入該字元。macOS 將按鍵交還 App；TSF 不會再送回已判定交由 App 的按鍵，因此改由輸入法輸入。
- 透過 IMM32 相容層運作的傳統 Win32 文字框（例如 WinForms 的 TextBox）不向 TSF 提供文件內容：於這類 App 中，英文模式不顯示轉注音預覽，Tab 照常輸入。
- 候選視窗支援滑鼠操作：點選候選字即選取，點選第一列尾端的頁碼或箭頭即展開或收合候選格線。輸入時顯示的候選列亦可點選，點選後可繼續輸入。點選其他位置時，交由 App 處理。

## 尚未實作

- 候選清單的 UI Automation 支援（供朗讀程式讀出候選字）；目前僅送出 `EVENT_OBJECT_IME_SHOW／HIDE／CHANGE`。
- 避開觸控鍵盤與遊戲全螢幕所用的 UI Element（`ITfUIElement`）。
- 聯想詞。
- 自行建置的上下文模型：`tools/bigram-trainer` 的訓練腳本為 shell script，於 Windows 上須另行執行，或以 Git Bash 執行 `fetch-models.sh` 下載發行版所用的同一組檔案。存在 `bigram.bin`、`chars.bin` 時，`build.ps1` 一併安裝；存在補充詞彙後的 `data.txt` 時，以其取代 McBopomofo 的詞庫。缺少這些檔案時，選字結果與 McBopomofo 相同。發行版已附有這些檔案。
- 選過的字僅存於伺服器記憶體，登出或重新安裝後清空（同 macOS）；自送出文字學得的字串次數存於 `%LOCALAPPDATA%\Project Key\personal.txt`，設定中的「清除」一併清空。Store App、沙盒中與低完整性的 App 不依此紀錄排序，亦不寫入紀錄，以免經由候選順序得知於其他 App 輸入過的文字。
- 組字一律視為句首：文字服務僅於英文模式按 Tab 時將游標前的文字傳給伺服器；macOS 則接續游標前的漢字選字。
- 程式碼簽章與安裝程式：目前以 PowerShell 腳本安裝，僅支援 x64。
