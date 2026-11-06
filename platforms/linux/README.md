# Linux 輸入法

Fcitx5 注音輸入法（大千鍵盤），按鍵與 [macOS 版](../macos/README.md)、[Windows 版](../windows/README.md) 相同。目前在 Ubuntu 24.04 上測過：KDE Plasma（X11），以及 GNOME（Xorg 與 Wayland）。

## 架構

`projectkey.so` 是 Fcitx5 的 addon，載入到 Fcitx5 行程裡：每個輸入欄位一個 `Controller`（`../../core/engine`，與其他平台共用），所有欄位共用詞庫、上下文模型、選字紀錄與中／英狀態。

| 引擎要做的事 | Fcitx5 上的做法 |
|---|---|
| 組字區 | preedit，可疑字以外的段落加底線，選字中的段落反白 |
| 候選字窗、中／英標記 | Project Key 自己畫的視窗（`Sources/Panel.cpp`，Cairo + Pango），尺寸、顏色、動畫與 Windows 版相同：X11 上是 X11 視窗（`Sources/X11Screen.cpp`），Wayland 上是輸入法的 popup（`Sources/WaylandScreen.cpp`），GNOME 上由 Project Key 的 GNOME Shell 擴充功能顯示（`Sources/GnomeScreen.cpp`） |
| 前文（Tab 英文轉注音、組字接著游標前的字選字） | surrounding text；App 不提供時 Tab 照常輸入，選字當成句首 |
| 英文判斷（Tab 注音轉英文） | Enchant 的 en_US 字典 |

「候選視窗外觀」有深色與模糊（預設）兩種。模糊用 KWin 的 blur behind（`_KDE_NET_WM_BLUR_BEHIND_REGION`），GNOME 上用 GNOME Shell 的模糊（擴充功能裡的 `Shell.BlurEffect`），模糊視窗後方的內容再疊一層深色；KWin 的模糊特效關著或用別的視窗管理員時，模糊會改成深色。中／英標記跟著用同一種外觀。有合成器時視窗是圓角，沒有時是直角。

Wayland 下，App 經由合成器的輸入法協定（text-input-v3 → input-method-v2，sway、Hyprland 等 wlroots 系列）連到 Fcitx5 時，候選字窗與中／英標記是畫在 Fcitx5 的 Wayland 連線上的 input popup，由合成器放在游標下方，下方放不下時放在上方；Wayland 下沒有模糊，「模糊」外觀會改成深色。App 經由 Fcitx5 的 D-Bus（`GTK_IM_MODULE=fcitx`、`QT_IM_MODULE=fcitx`）或 KWin 的輸入法（input-method-v1）連線時，以及 X11 上 App 只給相對座標時，改用 Fcitx5 自己的候選面板，外觀跟隨 Fcitx5 主題；操作與其他平台相同：整句預覽和頁碼在第一行，選字列逐格選取，按 ↓ 展開的格線以直排列出最多五列（GNOME 上面板由 App 畫，每個候選只能是一行純文字，所以選中的那列整列反白、選中的字以［］標出）。

GNOME（Mutter）沒有讓輸入法把視窗放在游標處的協定，所以 GNOME 上候選字窗、Tab 提示與中／英標記由 GNOME Shell 擴充功能「Project Key」（`gnome-shell/`，裝在 `/usr/share/gnome-shell/extensions/project-key@koukeneko.github.io`）顯示：Project Key 照常自己畫，把畫好的影像寫到 `XDG_RUNTIME_DIR` 的檔案，經 D-Bus 交給擴充功能放到游標處；擴充功能則回報焦點視窗與工作區的位置，把 App 給的相對座標換成螢幕座標。GNOME Shell 轉送的 App（IBus）與經 Fcitx5 D-Bus 的 App 都適用。安裝後在「擴充功能」App 開啟它（或 `gnome-extensions enable project-key@koukeneko.github.io`，需重新登入）；沒開啟時改用 Fcitx5 的候選面板。GNOME 上視窗沒有滑動動畫。

安裝時附上 Fcitx5 主題「Project Key」（`theme/`，裝在 `/usr/share/fcitx5/themes/projectkey`）：深色、圓角、選取的字用強調色，邊距與候選字窗相同。在 Fcitx5 設定的「附加元件 → 經典使用者介面 → 主題」選它，用到 Fcitx5 候選面板時外觀就和其他平台一致（GNOME 上這個面板由 App 自己畫，同樣讀 Fcitx5 主題）；它也會套用到其他 Fcitx5 輸入法。主題的圖片由 `tools/icon/make-icons.ps1` 產生。

Fcitx5 預設用左 Shift 暫時切換輸入法；Project Key 啟用中、且「單按 Shift 切換中英」開著時，Shift 由 Project Key 處理，切的是中／英而不是輸入法。關掉這項設定，Shift 就回到 Fcitx5。

## 建置與安裝

用 Homebrew（Linux）安裝發行版：先裝下面 `apt install` 那一行的套件，再

```sh
brew install koukeneko/tap/fcitx5-projectkey
projectkey-link    # 把 Fcitx5 與 GNOME 讀的檔案連結到 ~/.local/share
```

它從原始碼建置，用的是系統的編譯器、Fcitx5 與 Qt（載入它的是系統的 Fcitx5），裝在 Homebrew 的目錄。Fcitx5 只在自己的目錄與 `~/.local/share/fcitx5` 找附加元件，所以附加元件的設定檔以完整路徑指向程式庫，`projectkey-link` 再把設定檔、主題、GNOME 擴充功能、設定視窗的捷徑與圖示連結過去（連到 `opt`，升級後仍有效）。之後同下面「安裝後」的步驟。

自行建置需要 CMake、Ninja、C++20 編譯器、Python 3（產生詞庫），以及 Fcitx5、Enchant、Cairo、Pango、Wayland（含 wayland-scanner）與 Qt 的開發檔；GNOME 版設定視窗另需 libadwaita、libsoup 3 與 json-glib。KDE 版設定視窗用 KDE 外觀所裝的那一版 Qt：Plasma 6 是 Qt 6，Plasma 5（Ubuntu 24.04）是 Qt 5（`qtbase5-dev`），否則會是 Qt 的預設外觀；也可用 `-DPROJECTKEY_QT=5` 或 `6` 指定。Debian／Ubuntu（Plasma 5 另裝 `qtbase5-dev`）：

```sh
sudo apt install cmake ninja-build g++ python3 git libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev fcitx5-modules-dev libenchant-2-dev libxcb1-dev libcairo2-dev libpango1.0-dev libwayland-dev qt6-base-dev libadwaita-1-dev libsoup-3.0-dev libjson-glib-dev hunspell-en-us fonts-noto-cjk
platforms/linux/build.sh                       # 下載並編譯引擎、產生詞庫、跑測試，輸出到 build
sudo cmake --install platforms/linux/build    # 安裝到 /usr
```

安裝後重新啟動 Fcitx5（`fcitx5 -r`），在 Fcitx5 設定的「輸入法」加入 Project Key（取消「只顯示目前語言」才看得到）。之後用 Ctrl+Space 切換。

Fcitx5 選單的「檢查 Project Key 安裝」以通知列出還缺什麼：詞庫、英文字典（Tab 提示用）、候選字窗的字型、設定程式，以及 Project Key 自己的視窗能不能出現（GNOME 上要開啟擴充功能）。從用 Fcitx5 候選面板的 App 打開時，也檢查 Fcitx5 的主題是不是 Project Key。

設定視窗的分組與 macOS、Windows 相同，改了立即生效。`projectkey-settings` 依桌面開啟對應的版本：KDE 上是 Qt（`Settings/Qt.cpp`，排版與 KDE 系統設定相同），GNOME 等 GTK 桌面上是 libadwaita（`Settings/Gtk.cpp`，沒有建置時改開 Qt 版）。環境裡沒有 `XDG_CURRENT_DESKTOP` 時（例如手動重新啟動的 Fcitx5），改讀 systemd 使用者環境裡的桌面。兩者的列定義在 `Settings/Rows.h`。從 Fcitx5 選單的「Project Key 設定…」或應用程式選單打開。它透過 Fcitx5 的 D-Bus 介面讀寫設定，所以 Fcitx5 設定裡 Project Key 的「設定」頁也能改同一份，存在 `~/.config/fcitx5/conf/projectkey.conf`。「清除選字紀錄」呼叫 addon 在 Fcitx5 D-Bus 上的 `/projectkey`。選過的字只存在記憶體裡；從送出的文字學到的字串次數存在 `$XDG_DATA_HOME/projectkey/personal.txt`（預設 `~/.local/share/projectkey/personal.txt`），「清除選字紀錄」一併清空，關掉「記住選過的字」時不學。

移除：`sudo xargs rm < platforms/linux/build/install_manifest.txt`。
