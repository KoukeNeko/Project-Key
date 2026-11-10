# Linux 輸入法

Fcitx5 注音輸入法（大千鍵盤），按鍵與 [macOS 版](../macos/README.md)、[Windows 版](../windows/README.md) 相同。目前測過：Ubuntu 24.04 的 KDE Plasma（X11 與 Wayland）與 GNOME（Xorg 與 Wayland）、Debian 13 的 GNOME（Wayland），以及 Pop!_OS 24.04 的 COSMIC。

## 架構

`projectkey.so` 是 Fcitx5 的 addon，載入到 Fcitx5 行程裡：每個輸入欄位一個 `Controller`（`../../core/engine`，與其他平台共用），所有欄位共用詞庫、上下文模型、選字紀錄與中／英狀態。

| 引擎要做的事 | Fcitx5 上的做法 |
|---|---|
| 組字區 | preedit，可疑字以外的段落加底線，選字中的段落反白 |
| 候選字窗、中／英標記 | Project Key 自己畫的視窗（`Sources/Panel.cpp`，Cairo + Pango），尺寸、顏色、動畫與 Windows 版相同，候選字與第一列尾端的頁碼或箭頭可用滑鼠點選：X11 上是 X11 視窗（`Sources/X11Screen.cpp`），Wayland 上是輸入法的 popup（`Sources/WaylandScreen.cpp`），GNOME 上由 Project Key 的 GNOME Shell 擴充功能顯示（`Sources/GnomeScreen.cpp`） |
| 前文（Tab 英文轉注音、組字接著游標前的字選字） | surrounding text；App 不提供時 Tab 照常輸入，選字當成句首 |
| 英文判斷（Tab 注音轉英文） | Enchant 的 en_US 字典 |

「候選視窗外觀」有深色與模糊（預設）兩種，模糊是模糊視窗後方的內容再疊一層深色。中／英標記跟著用同一種外觀。有合成器時視窗是圓角，沒有時是直角。各桌面的模糊：

| 桌面 | 模糊 |
|---|---|
| KDE Plasma（X11） | KWin 的 blur behind（`_KDE_NET_WM_BLUR_BEHIND_REGION`），KWin 的模糊特效要開著 |
| KDE Plasma（Wayland） | 候選字窗是 Fcitx5 的面板，用 Project Key 主題的 `EnableBlur` |
| GNOME | 擴充功能裡的 `Shell.BlurEffect` |
| niri 26.04 以上、KWin 6.7 以上（input-method-v2） | `ext-background-effect-v1` |
| Hyprland | 合成器自己模糊：`decoration:blur:input_methods = true` 時 Project Key 改畫半透明，關著時是深色 |
| sway、wayfire、COSMIC、其他 X11 視窗管理員 | 合成器沒有提供模糊，改成深色 |

Wayland 下，App 經由合成器的輸入法協定（text-input-v3 → input-method-v2，sway、Hyprland 等 wlroots 系列）連到 Fcitx5 時，候選字窗與中／英標記是畫在 Fcitx5 的 Wayland 連線上的 input popup，由合成器放在游標下方，下方放不下時放在上方（COSMIC 把 popup 的左上角放在游標頂端，Project Key 自己往下讓出游標的高度）；模糊見上表。App 經由 Fcitx5 的 D-Bus（`GTK_IM_MODULE=fcitx`、`QT_IM_MODULE=fcitx`）或 KWin 的輸入法（input-method-v1）連線時，以及 X11 上 App 只給相對座標時，改用 Fcitx5 自己的候選面板，外觀跟隨 Fcitx5 主題；操作與其他平台相同：整句預覽和頁碼在第一行，選字列逐格選取，按 ↓ 展開的格線以直排列出最多五列（GNOME 上面板由 App 畫，每個候選只能是一行純文字，所以選中的那列整列反白、選中的字以［］標出）。

GNOME（Mutter）沒有讓輸入法把視窗放在游標處的協定，所以 GNOME 上候選字窗、Tab 提示與中／英標記由 GNOME Shell 擴充功能「Project Key」（`gnome-shell/`，裝在 `/usr/share/gnome-shell/extensions/project-key@koukeneko.github.io`）顯示：Project Key 照常自己畫，把畫好的影像寫到 `XDG_RUNTIME_DIR` 的檔案，經 D-Bus 交給擴充功能放到游標處；擴充功能則回報焦點視窗與工作區的位置，把 App 給的相對座標換成螢幕座標。GNOME Shell 轉送的 App（IBus）與經 Fcitx5 D-Bus 的 App 都適用。安裝後在「擴充功能」App 開啟它（或 `gnome-extensions enable project-key@koukeneko.github.io`，需重新登入）；沒開啟時改用 Fcitx5 的候選面板。動畫與其他桌面相同，每一格畫面都經由擴充功能顯示；滑鼠點選由擴充功能回傳給 Project Key，更新後需重新登入才會載入新版擴充功能。候選字窗開啟與收起時，整個視窗連同模糊一起淡入淡出（GNOME 由擴充功能、X11 由視窗的 `_NET_WM_WINDOW_OPACITY`）；其他 Wayland 桌面沒有調整模糊強度的協定，模糊本身會直接出現與消失。

KDE Plasma 的 Wayland 上，KWin 用自己的輸入法協定（input-method-v1）啟動 Fcitx5，候選字預設由 Plasma 的輸入法面板（kimpanel）顯示，外觀跟隨 Plasma 主題；以 `fcitx5 --disable=kimpanel` 啟動時改用 Fcitx5 的面板與下面的主題。

COSMIC（Pop!_OS）上，支援 text-input-v3 的 App（例如 Firefox）經合成器連到 Fcitx5；COSMIC 自己的 App（例如 COSMIC Edit）不支援輸入法協定，無法輸入中文。COSMIC 的系統匣會把圖示染成文字顏色，所以那裡改用單色圖示 `fcitx-projectkey-symbolic.svg`（ㄅ 挖空）。

安裝時附上 Fcitx5 主題「Project Key」（`theme/`，裝在 `/usr/share/fcitx5/themes/projectkey`）：半透明深色、圓角、選取的字用強調色，邊距與候選字窗相同；KWin 等合成器支援時模糊背後的內容（`EnableBlur`）。在 Fcitx5 設定的「附加元件 → 經典使用者介面 → 主題」選它，用到 Fcitx5 候選面板時外觀就和其他平台一致（GNOME 上這個面板由 App 自己畫，同樣讀 Fcitx5 主題）；它也會套用到其他 Fcitx5 輸入法。主題的圖片由 `tools/icon/make-icons.ps1` 產生。

Fcitx5 預設用左 Shift 暫時切換輸入法；Project Key 啟用中、且「單按 Shift 切換中英」開著時，Shift 由 Project Key 處理，切的是中／英而不是輸入法。關掉這項設定，Shift 就回到 Fcitx5。

自訂詞在 `$XDG_DATA_HOME/projectkey/phrases.txt`（預設 `~/.local/share/projectkey/phrases.txt`），設定視窗的「自訂詞」「編輯…」開啟；不要出現的詞放在同一個資料夾的 `exclude-phrases.txt`，由「排除詞」開啟，「資料夾」顯示這個資料夾。格式與 macOS 相同，見 [自訂詞](../macos/README.md#自訂詞)。

## 建置與安裝

用套件庫安裝（Debian 13、Ubuntu 24.04、Fedora 43，x86_64）。
```sh
# Debian / Ubuntu（suite：Debian 13 用 trixie，Ubuntu 24.04 用 noble）
sudo mkdir -p /etc/apt/keyrings
curl -fsSL https://koukeneko.github.io/Project-Key/apt/key.asc | sudo tee /etc/apt/keyrings/projectkey.asc >/dev/null
echo "deb [signed-by=/etc/apt/keyrings/projectkey.asc] https://koukeneko.github.io/Project-Key/apt trixie main" | sudo tee /etc/apt/sources.list.d/projectkey.list
sudo apt update && sudo apt install fcitx5-projectkey

# Fedora
sudo tee /etc/yum.repos.d/projectkey.repo <<'EOF'
[projectkey]
name=Project Key
baseurl=https://koukeneko.github.io/Project-Key/rpm
enabled=1
gpgcheck=0
repo_gpgcheck=1
gpgkey=https://koukeneko.github.io/Project-Key/rpm/key.asc
EOF
sudo dnf install fcitx5-projectkey
```
字典與選字模型是獨立的 `fcitx5-projectkey-data` 套件（約 100 MB，只在模型更新時換版），程式套件只有幾百 KB。套件庫由 `release.yml` 在每次發行時更新：`package-deb.sh`、`package-rpm.sh` 在各發行版的容器裡建置，`publish-repos.sh` 簽章後推到 gh-pages 的 `apt/` 與 `rpm/`；簽章金鑰是 secret `REPO_GPG_PRIVATE_KEY`（無密碼的 ASCII 私鑰）。Arch 用 `platforms/linux/arch/PKGBUILD`（`makepkg -si`）。

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

安裝後重新啟動 Fcitx5（`fcitx5 -r`），執行 `projectkey-settings --welcome` 按「加入」；也可以在 Fcitx5 設定的「輸入法」加入 Project Key（取消「只顯示目前語言」才看得到）。之後用 Ctrl+Space 切換。

Fcitx5 選單的「檢查 Project Key 安裝」以通知列出還缺什麼：詞庫、英文字典（Tab 提示用）、候選字窗的字型、設定程式，以及 Project Key 自己的視窗能不能出現（GNOME 上要開啟擴充功能）。從用 Fcitx5 候選面板的 App 打開時，也檢查 Fcitx5 的主題是不是 Project Key。

設定視窗的分組與 macOS、Windows 相同，改了立即生效。`projectkey-settings` 依桌面開啟對應的版本：KDE 上是 Qt（`Settings/Qt.cpp`，排版與 KDE 系統設定相同），GNOME 等 GTK 桌面上是 libadwaita（`Settings/Gtk.cpp`，沒有建置時改開 Qt 版）。環境裡沒有 `XDG_CURRENT_DESKTOP` 時（例如手動重新啟動的 Fcitx5），改讀 systemd 使用者環境裡的桌面。兩者的列定義在 `Settings/Rows.h`。從 Fcitx5 選單的「Project Key 設定…」或應用程式選單打開。它透過 Fcitx5 的 D-Bus 介面讀寫設定，所以 Fcitx5 設定裡 Project Key 的「設定」頁也能改同一份，存在 `~/.config/fcitx5/conf/projectkey.conf`。「清除選字紀錄」呼叫 addon 在 Fcitx5 D-Bus 上的 `/projectkey`。選過的字只存在記憶體裡；從送出的文字學到的字串次數存在 `$XDG_DATA_HOME/projectkey/personal.txt`（預設 `~/.local/share/projectkey/personal.txt`），「清除選字紀錄」一併清空，關掉「記住選過的字」時不學。「關於」顯示選字模型的日期，有新模型時可以直接下載到同一個資料夾，核對 SHA-256 後不必重新安裝就生效。用 Homebrew 安裝時，有新版會顯示「更新到」：執行 `brew upgrade fcitx5-projectkey`（從原始碼建置，約需幾分鐘）與 `projectkey-link`，再重新啟動 Fcitx5 並開啟新版的設定視窗，過程記錄在 `~/.cache/projectkey-update.log`；自行建置安裝的則連到發行版頁面。

移除：`sudo xargs rm < platforms/linux/build/install_manifest.txt`。
