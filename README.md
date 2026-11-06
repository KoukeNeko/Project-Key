# Project Key

[![CI](https://github.com/KoukeNeko/Project-Key/actions/workflows/ci.yml/badge.svg)](https://github.com/KoukeNeko/Project-Key/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/KoukeNeko/Project-Key)](https://github.com/KoukeNeko/Project-Key/releases/latest)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

大千注音輸入法，也可改用漢語拼音；macOS、Windows、Linux（Fcitx5）與 iOS 共用同一個組字引擎。連續打完整句再一起選字，引擎依上下文排序，並標出可能選錯的地方。

![候選視窗：上方整句預覽標出可疑的「程式」，下方為候選列](.github/assets/candidate-window.png)

## 功能

- **整句組字**：整句打完一起選字，同音詞分數接近的地方在整句預覽中標為可疑，Tab 在可疑處之間跳。
- **聲調可省略**：ㄋㄧㄏㄠ 為「你好」；還沒結束的音節在組字區顯示注音，猜的詞標在整句預覽與候選列。
- **簡拼**：單獨一個注音代表以它開頭的音節，ㄨㄇ 為「我們」、ㄎㄧ 為「可以」。
- **鄰鍵修正**：按錯一個相鄰鍵（同列左右或上下列）時，依上下文改讀，ㄍㄛ˙ 讀成 ㄍㄜ˙。
- **中英互轉**：在中文模式打成英文（hello）或在英文模式打成注音（ji3cl3），按 Tab 轉回來。
- **接著前文選字**：組字接著游標前的漢字算，不當成句首（macOS、Linux、iOS）。
- **記住選過的字**：同樣的上下文下次直接選它。
- **漢語拼音**：設定的「輸入法」可改為漢語拼音（macOS、Windows、Linux）。聲調鍵為 2–5，空白鍵為一聲，可省略；音節之間可用 ' 分開（fang'an）；ü 打成 v。拼音不提供鄰鍵修正與 Tab 中英互轉。

## 安裝

| 平台 | 安裝方式 | 說明 |
|---|---|---|
| macOS（Apple 晶片） | `brew install --cask koukeneko/tap/project-key` | [platforms/macos](platforms/macos/README.md) |
| Windows（x64） | `scoop bucket add koukeneko https://github.com/KoukeNeko/scoop-bucket`<br>`scoop install koukeneko/project-key` | [platforms/windows](platforms/windows/README.md) |
| Linux（Fcitx5） | `brew install koukeneko/tap/fcitx5-projectkey`<br>`projectkey-link` | 需先裝相依套件，見 [platforms/linux](platforms/linux/README.md) |
| iOS 17 以上 | 從原始碼建置 | [platforms/ios](platforms/ios/README.md) |

安裝後到系統的輸入法設定加入 Project Key：macOS 為「系統設定 > 鍵盤 > 輸入方式」，Windows 為「設定 > 時間與語言 > 語言與地區 > 中文（台灣）> 語言選項 > 新增鍵盤」，Linux 為 Fcitx5 設定的「輸入法」。

發行版附上[上下文模型](#上下文模型)與補詞後的詞庫，選字依上下文排序，鄰鍵修正也會啟用。這些資料檔另有授權（CC BY-SA 4.0 與 LGPL-2.1-or-later），見 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 常用按鍵

| 狀態 | 按鍵 | 動作 |
|---|---|---|
| 組字中 | Space | 結束一聲音節；沒有未打完的注音時開啟候選 |
| | ↓、Tab | 開啟候選 |
| | Enter | 送出整句 |
| 選字中 | 1–6 | 選取這一列的字 |
| | Tab | 跳到下一個可疑處 |
| 任何時候 | 單按 Shift | 切換中／英 |

完整按鍵表在各平台的 README。

## 從原始碼建置

| 平台 | 指令 | 需要 |
|---|---|---|
| macOS | `platforms/macos/build.sh`，再 `platforms/macos/install.sh` | Xcode 或 Command Line Tools、CMake，macOS 13 以上 |
| Windows | `platforms\windows\build.ps1`，再 `platforms\windows\install.ps1` | Visual Studio 2026（C++ 桌面開發）、.NET 10 SDK、uv |
| Linux | `platforms/linux/build.sh`，再 `sudo cmake --install platforms/linux/build` | CMake、Ninja、Fcitx5 與 Qt 等開發檔 |
| iOS | `platforms/ios/build.sh` | Xcode、XcodeGen |

建置腳本會下載固定版本的 McBopomofo（commit 記在 [MCBOPOMOFO_COMMIT](MCBOPOMOFO_COMMIT)）、產生詞庫、編譯並執行引擎測試，輸出在各平台的 `build/`。

## 專案結構

```
core/engine/        組字、選字與按鍵邏輯，各平台共用；測試在 Tests/
platforms/macos/    InputMethodKit 輸入法
platforms/windows/  TSF 文字服務（ProjectKey.dll）與引擎伺服器（ProjectKeyServer.exe）
platforms/linux/    Fcitx5 附加元件、候選視窗、設定視窗與 GNOME Shell 擴充功能
platforms/ios/      鍵盤擴充
tools/              上下文模型訓練、選字評測與圖示產生
tests/corpus/       評測語料
distribution/       發行說明，由發行流程讀取
```

## 上下文模型

`tools/bigram-trainer` 以維基百科與立法院公報訓練兩個模型，輸入法建置時若存在就一併放入，發行版也附上這兩個模型：

- `bigram.bin`：詞 bigram 與 trigram，依前兩個詞選字（約 190MB）。
- `chars.bin`：字 trigram，補上詞模型沒見過的詞對（約 126MB）。

```bash
platforms/macos/build.sh            # 先建置一次，取得 McBopomofo 的詞庫
tools/bigram-trainer/train.sh       # 下載訓練文字並訓練，約 10 分鐘
platforms/macos/build.sh            # 再建置一次，把模型放進 App
```

不想自行訓練時，`tools/bigram-trainer/fetch-models.sh` 下載發行版使用的同一組檔案。

選字的評測工具在 [tools/corpus-evaluator](tools/corpus-evaluator/)。

## 授權

Project Key 以 [MIT](LICENSE) 授權發布。詞庫與選字引擎來自 [McBopomofo](https://github.com/openvanilla/McBopomofo)（MIT）。發行版附上的上下文模型以 CC BY-SA 4.0 授權，補詞後的詞庫以 LGPL-2.1-or-later 授權；這些資料檔與其他第三方內容、訓練文字的授權見 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
