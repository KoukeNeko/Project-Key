# Linux 輸入法

Fcitx5 注音輸入法，大千鍵盤，按鍵與 [macOS 版](../macos/README.md)、[Windows 版](../windows/README.md) 相同。目前在 Ubuntu 24.04 的 KDE Plasma（X11）上測過。

## 架構

`projectkey.so` 是 Fcitx5 的 addon，載入到 Fcitx5 行程裡：每個輸入欄位一個 `Controller`（`../../core/engine`，與其他平台共用），所有欄位共用詞庫、上下文模型、選字紀錄與中／英狀態。

| 引擎要做的事 | Fcitx5 上的做法 |
|---|---|
| 組字區 | preedit，可疑字以外的段落加底線，選字中的段落反白 |
| 候選字窗、中／英標記 | Project Key 自己畫的 X11 視窗（`Sources/Panel.cpp`，Cairo + Pango），尺寸、顏色、動畫與 Windows 版相同 |
| 前文（Tab 英文轉注音） | surrounding text；App 不提供時 Tab 照常輸入 |
| 英文判斷（Tab 注音轉英文） | Enchant 的 en_US 字典 |

「候選視窗外觀」有深色與模糊（預設）兩種。模糊用 KWin 的 blur behind（`_KDE_NET_WM_BLUR_BEHIND_REGION`），模糊視窗後方的內容再疊一層深色；KWin 的模糊特效關著或用別的視窗管理員時，模糊會改成深色。中／英標記跟著用同一種外觀。有合成器時視窗是圓角，沒有時是直角。Wayland 下或 App 只給相對座標時，改用 Fcitx5 自己的候選面板，外觀跟隨 Fcitx5 主題。

Fcitx5 預設用左 Shift 暫時切換輸入法；Project Key 啟用中、且「單按 Shift 切換中英」開著時，Shift 由 Project Key 處理，切的是中／英而不是輸入法。關掉這項設定，Shift 就回到 Fcitx5。

## 建置與安裝

需要 CMake、C++20 編譯器、Python 3（產生詞庫），以及 Fcitx5、Enchant、Cairo、Pango 與 Qt 6 的開發檔。Debian／Ubuntu：

```sh
sudo apt install cmake g++ python3 git libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev fcitx5-modules-dev libenchant-2-dev libxcb1-dev libcairo2-dev libpango1.0-dev qt6-base-dev hunspell-en-us fonts-noto-cjk
platforms/linux/build.sh                       # 下載並編譯引擎、產生詞庫、跑測試，輸出到 .build
sudo cmake --install platforms/linux/.build    # 安裝到 /usr
```

安裝後重新啟動 Fcitx5（`fcitx5 -r`），在 Fcitx5 設定的「輸入法」加入 Project Key（取消「只顯示目前語言」才看得到）。之後用 Ctrl+Space 切換。

設定視窗 `projectkey-settings`（`Settings/main.cpp`，Qt 6）的分組與 macOS、Windows 相同，改了立即生效。從 Fcitx5 選單的「Project Key 設定…」或應用程式選單打開。它透過 Fcitx5 的 D-Bus 介面讀寫設定，所以 Fcitx5 設定裡 Project Key 的「設定」頁也能改同一份，存在 `~/.config/fcitx5/conf/projectkey.conf`。「清除選字紀錄」呼叫 addon 在 Fcitx5 D-Bus 上的 `/projectkey`：選字紀錄只存在記憶體裡，和其他平台一樣。

移除：`sudo xargs rm < platforms/linux/.build/install_manifest.txt`。
