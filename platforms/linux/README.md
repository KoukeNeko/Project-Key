# Linux 輸入法

Fcitx5 注音輸入法，大千鍵盤，按鍵與 [macOS 版](../macos/README.md)、[Windows 版](../windows/README.md) 相同。目前在 Ubuntu 24.04 的 KDE Plasma（X11）上測過。

## 架構

`projectkey.so` 是 Fcitx5 的 addon，載入到 Fcitx5 行程裡：每個輸入欄位一個 `Controller`（`../../core/engine`，與其他平台共用），所有欄位共用詞庫、上下文模型、選字紀錄與中／英狀態。

| 引擎要做的事 | Fcitx5 上的做法 |
|---|---|
| 組字區 | preedit，可疑字以外的段落加底線，選字中的段落反白 |
| 候選字 | Fcitx5 的候選面板：整句預覽在上，目前那一列候選字，頁碼在下 |
| 中／英標記 | Fcitx5 的輸入法提示（A／ㄅ） |
| 前文（Tab 英文轉注音） | surrounding text；App 不提供時 Tab 照常輸入 |
| 英文判斷（Tab 注音轉英文） | Enchant 的 en_US 字典 |

候選面板由 Fcitx5 繪製，外觀跟隨 Fcitx5 的主題，所以沒有「外觀」這項設定。

Fcitx5 預設用左 Shift 暫時切換輸入法；Project Key 啟用中、且「單按 Shift 切換中英」開著時，Shift 由 Project Key 處理，切的是中／英而不是輸入法。關掉這項設定，Shift 就回到 Fcitx5。

## 建置與安裝

需要 CMake、C++20 編譯器、Python 3（產生詞庫）以及 Fcitx5 與 Enchant 的開發檔。Debian／Ubuntu：

```sh
sudo apt install cmake g++ python3 git libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev fcitx5-modules-dev libenchant-2-dev hunspell-en-us
platforms/linux/build.sh                       # 下載並編譯引擎、產生詞庫、跑測試，輸出到 .build
sudo cmake --install platforms/linux/.build    # 安裝到 /usr
```

安裝後重新啟動 Fcitx5（`fcitx5 -r`），在 Fcitx5 設定的「輸入法」加入 Project Key（取消「只顯示目前語言」才看得到）。之後用 Ctrl+Space 切換。

設定在 Fcitx5 設定裡 Project Key 的「設定」，存在 `~/.config/fcitx5/conf/projectkey.conf`。

移除：`sudo xargs rm < platforms/linux/.build/install_manifest.txt`。
