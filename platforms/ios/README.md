# iOS 鍵盤

自訂鍵盤擴充（Keyboard Extension），大千注音。按鍵外觀沿用系統鍵盤的底色、鍵色與圓角，深色與淺色跟著系統切換；排列採 Gboard 注音的五排配置：數字列併入注音（ㄅ1 ㄉ2 ˇ3 ˋ4 …），最下排為 ?123、「，」、地球鍵（系統要求時才顯示）、空白鍵、ㄦ、刪除、換行。組字與選字邏輯在 `../../core/engine`，與 macOS、Windows 共用；`Keyboard/KeyboardViewController.mm` 只負責按鍵畫面與 `UITextDocumentProxy`。

## 建置與安裝

需要 Xcode 與 XcodeGen（`brew install xcodegen`），iOS 17 以上。

```bash
platforms/ios/build.sh   # 下載 McBopomofo、產生詞庫、產生 Xcode 專案並編譯模擬器版本
```

輸出 `platforms/ios/.build/derived/Build/Products/Release-iphonesimulator/ProjectKey.app`。安裝到已開機的模擬器：

```bash
xcrun simctl install booted platforms/ios/.build/derived/Build/Products/Release-iphonesimulator/ProjectKey.app
```

安裝到實機：先跑過 `build.sh`，再以自己的開發團隊 ID 簽署建置（Xcode 會自動註冊 App ID 與描述檔），裝到用 USB 或同一網路連線的裝置：

```bash
cd platforms/ios
xcodebuild -project ProjectKey.xcodeproj -scheme ProjectKey -destination 'id=<裝置 UDID>' -configuration Release \
  -derivedDataPath .build/derived DEVELOPMENT_TEAM=<團隊 ID> -allowProvisioningUpdates -quiet build
xcrun devicectl device install app --device <裝置 UDID> .build/derived/Build/Products/Release-iphoneos/ProjectKey.app
```

裝置 UDID 用 `xcrun devicectl list devices` 查。

啟用：設定 > 一般 > 鍵盤 > 鍵盤 > 新增鍵盤 > Project Key。主程式只有啟用說明與試打欄位。

## 按鍵

| 狀態 | 按鍵 | 動作 |
|---|---|---|
| 組字中 | 注音鍵、聲調鍵 | 輸入注音；聲調鍵結束音節。聲調可省略：拼成的音節不分聲調比對並直接顯示成字，接不上的注音開始下一個音節（ㄋㄧㄏㄠ 為「你好」）。單獨一個注音代表以它開頭的音節（ㄨㄇ 為「我們」） |
| | 候選列 | 音節打完就列出剛打的字的所有候選，可左右滑動，灰底是目前的字，注音本身（如「ㄏㄡˋ」）排在第六個；點字選取，選完跳到下一個可疑處，選注音則連同整句送出。有未完成的注音時改為顯示整句，點一下送出 |
| | 空白鍵 | 有還沒打聲調的注音時結束一聲音節，否則往後跳六個候選 |
| | ⌄ | 候選字攤開蓋住按鍵，可上下捲動；⌃ 收回 |
| | 換行 | 送出整句，不換行；還沒打聲調的最後一個音節送出注音符號 |
| | 刪除 | 刪除一個注音符號或游標前的字；按住連續刪除 |
| 任何時候 | 長按注音鍵 | 輸入右上角的字元：數字、英文字母，ㄤ ㄝ ㄡ ㄥ 為「：」「…」「！」「？」 |
| | 「，」 | 輸入「，」；長按輸入「。」 |
| | ?123 | 切換到全形符號頁，再按一次（ㄅㄆㄇ）切回 |

## 與桌面版的差異

- 上下文排序與鄰鍵修正需要 `tools/bigram-trainer` 訓練出的 `bigram.bin` 與 `chars.bin`；有時 `build.sh` 一併放入，以記憶體映射讀取，不占鍵盤擴充的記憶體上限。沒有時只用詞庫（`data.txt`，沒有時用 McBopomofo 的），鄰鍵修正也不啟用。
- 沒有英文模式與 Tab 轉換，切換英文用系統的地球鍵。
- 沒有設定畫面；選過的字只記在記憶體，鍵盤程式被系統結束後清空。

## 已知問題

- 剛安裝或更新後第一次叫出鍵盤時，模擬器常改用系統注音鍵盤，用地球鍵切回即可。
- 未在實機量過記憶體與冷啟動時間。
