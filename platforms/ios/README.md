# iOS 鍵盤

以自訂鍵盤擴充（Keyboard Extension）實作的大千注音鍵盤。按鍵外觀沿用系統鍵盤的底色、鍵色與圓角，深色與淺色隨系統切換；排列採用 Gboard 注音的五排配置：數字列併入注音（ㄅ1 ㄉ2 ˇ3 ˋ4 …），最下排為 ?123、「，」、地球鍵（僅於系統要求時顯示）、空白鍵、ㄦ、刪除、換行。組字與選字邏輯位於 `../../core/engine`，與其他平台共用；`Keyboard/KeyboardViewController.mm` 僅負責按鍵畫面與 `UITextDocumentProxy`。

## 建置與安裝

須具備 Xcode 與 XcodeGen（`brew install xcodegen`），系統須為 iOS 17 以上。

```bash
platforms/ios/build.sh   # 下載 McBopomofo、產生詞庫、產生 Xcode 專案並編譯模擬器版本
```

產出位於 `platforms/ios/build/derived/Build/Products/Release-iphonesimulator/ProjectKey.app`。安裝至已開機的模擬器：

```bash
xcrun simctl install booted platforms/ios/build/derived/Build/Products/Release-iphonesimulator/ProjectKey.app
```

安裝至實機：先執行 `build.sh`，再以開發團隊 ID 簽署建置（Xcode 自動註冊 App ID 與描述檔），安裝至以 USB 或同一網路連線的裝置：

```bash
cd platforms/ios
xcodebuild -project ProjectKey.xcodeproj -scheme ProjectKey -destination 'id=<裝置 UDID>' -configuration Release \
  -derivedDataPath build/derived DEVELOPMENT_TEAM=<團隊 ID> -allowProvisioningUpdates -quiet build
xcrun devicectl device install app --device <裝置 UDID> build/derived/Build/Products/Release-iphoneos/ProjectKey.app
```

裝置 UDID 可以 `xcrun devicectl list devices` 查詢。

啟用方式：設定 > 一般 > 鍵盤 > 鍵盤 > 新增鍵盤 > Project Key。主程式僅包含啟用說明與試打欄位。

## 按鍵

| 狀態 | 按鍵 | 動作 |
|---|---|---|
| 組字中 | 注音鍵、聲調鍵 | 輸入注音；聲調鍵結束音節。輸入已有注音的位置時取代該注音（ㄅ 再按 ㄆ 為 ㄆ，ㄋㄧ 再按 ㄨ 為 ㄋㄨ）。聲調可省略：拼成完整音節後，位置較前或無法接續的注音開始下一個音節（ㄖㄣㄨㄟ 為「認為」）。未依順序輸入的注音於按下聲調鍵時合併為一個音節（ㄧㄚㄐ 加一聲為 ㄐㄧㄚ），依輸入順序已構成詞語者除外。尚未結束的音節顯示為注音，結束後轉為字。不支援簡拼：單一注音不代表音節 |
| | 候選列 | 音節輸入完成後即列出該字的所有候選，可左右滑動；灰底為目前的字，注音本身（如「ㄏㄡˋ」）排在第六個。點選字即選取，選取後移至下一個可疑處；選取注音則連同整句送出。有未完成的注音時改為顯示整句，點選即送出 |
| | 空白鍵 | 有未加聲調的注音時結束一聲音節，否則向後移六個候選 |
| | ⌄ | 展開候選字並覆蓋按鍵區，可上下捲動；按 ⌃ 收回 |
| | 換行 | 送出整句，不換行；最後一個未加聲調的音節以注音符號送出 |
| | 刪除 | 刪除一個注音符號或游標前的字；按住可連續刪除 |
| 任何時候 | 長按注音鍵 | 輸入右上角的字元：數字、英文字母；ㄤ ㄝ ㄡ ㄥ 分別為「：」「…」「！」「？」 |
| | 「，」 | 輸入「，」；長按輸入「。」 |
| | ?123 | 切換至全形符號頁，再按一次（ㄅㄆㄇ）切回 |

## 與桌面版的差異

- 上下文排序須使用 `tools/bigram-trainer` 訓練的 `bigram.bin` 與 `chars.bin`；檔案存在時由 `build.sh` 一併放入，以記憶體映射讀取，不佔用鍵盤擴充的記憶體上限。檔案不存在時僅使用詞庫（`data.txt`，不存在時使用 McBopomofo 的詞庫）。
- 無英文模式與 Tab 轉換，切換英文須使用系統的地球鍵。
- 無設定畫面。選過的字僅記錄於記憶體，鍵盤程式遭系統結束後清空。自送出文字學得的字串次數存於鍵盤擴充的容器（`Library/Application Support/personal.txt`），每次送出後寫入；未提供清除功能，刪除 App 時一併刪除。

## 已知問題

- 剛安裝或更新後首次叫出鍵盤時，模擬器常改用系統注音鍵盤，以地球鍵切回即可。
- 尚未於實機量測記憶體用量與冷啟動時間。
