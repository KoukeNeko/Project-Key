# Linux 輸入法

以 Fcitx5 實作的注音輸入法，按鍵與 [macOS 版](../macos/README.md)、[Windows 版](../windows/README.md) 相同。已測試的環境：Ubuntu 24.04 的 KDE Plasma（X11 與 Wayland）與 GNOME（Xorg 與 Wayland）、Debian 13 的 GNOME（Wayland），以及 Pop!_OS 24.04 的 COSMIC。

## 架構

`projectkey.so` 為 Fcitx5 的 addon，載入於 Fcitx5 行程中：每個輸入欄位各有一個 `Controller`（`../../core/engine`，與其他平台共用），所有欄位共用詞庫、上下文模型、選字紀錄與中／英狀態。

| 引擎功能 | Fcitx5 上的實作 |
|---|---|
| 組字區 | preedit；可疑字以外的段落加底線，選字中的段落反白 |
| 候選字窗、中／英標記 | 由 Project Key 自行繪製的視窗（`Sources/Panel.cpp`，Cairo + Pango），尺寸、顏色與動畫與 Windows 版相同，候選字與第一列尾端的頁碼或箭頭支援滑鼠點選。X11 上為 X11 視窗（`Sources/X11Screen.cpp`），Wayland 上為輸入法的 popup（`Sources/WaylandScreen.cpp`），GNOME 上由 Project Key 的 GNOME Shell 擴充功能顯示（`Sources/GnomeScreen.cpp`） |
| 前文（Tab 英文轉注音、組字接續游標前的字選字） | surrounding text；App 未提供時 Tab 照常輸入，選字視為句首 |
| 英文判斷（Tab 注音轉英文） | Enchant 的 en_US 字典 |

「候選視窗外觀」分為深色與模糊（預設）兩種；模糊為模糊視窗後方的內容，再疊加一層深色。中／英標記採用相同外觀。有合成器時視窗為圓角，無合成器時為直角。各桌面的模糊方式如下：

| 桌面 | 模糊 |
|---|---|
| KDE Plasma（X11） | KWin 的 blur behind（`_KDE_NET_WM_BLUR_BEHIND_REGION`），須啟用 KWin 的模糊特效 |
| KDE Plasma（Wayland） | 候選字窗為 Fcitx5 的面板，採用 Project Key 主題的 `EnableBlur` |
| GNOME | 擴充功能中的 `Shell.BlurEffect` |
| niri 26.04 以上、KWin 6.7 以上（input-method-v2） | `ext-background-effect-v1` |
| Hyprland | 由合成器模糊：`decoration:blur:input_methods = true` 時 Project Key 改為繪製半透明，關閉時為深色 |
| sway、wayfire、COSMIC、其他 X11 視窗管理員 | 合成器未提供模糊，改為深色 |

Wayland 下，App 經由合成器的輸入法協定（text-input-v3 → input-method-v2，sway、Hyprland 等 wlroots 系列）連線至 Fcitx5 時，候選字窗與中／英標記為繪製於 Fcitx5 Wayland 連線上的 input popup，由合成器置於游標下方，下方空間不足時置於上方（COSMIC 將 popup 的左上角置於游標頂端，Project Key 自行向下讓出游標的高度）；模糊方式見上表。App 經由 Fcitx5 的 D-Bus（`GTK_IM_MODULE=fcitx`、`QT_IM_MODULE=fcitx`）或 KWin 的輸入法（input-method-v1）連線時，以及 X11 上 App 僅提供相對座標時，改用 Fcitx5 內建的候選面板，外觀依循 Fcitx5 主題。操作與其他平台相同：整句預覽與頁碼位於第一行，選字列逐格選取，按 ↓ 展開的格線以直排列出最多五列（GNOME 上面板由 App 繪製，每個候選僅能為一行純文字，因此選取的列整列反白，選取的字以［］標示）。

GNOME（Mutter）未提供讓輸入法將視窗置於游標處的協定，因此 GNOME 上的候選字窗、Tab 提示與中／英標記由 GNOME Shell 擴充功能「Project Key」（`gnome-shell/`，安裝於 `/usr/share/gnome-shell/extensions/project-key@koukeneko.github.io`）顯示：Project Key 照常自行繪製，將繪製完成的影像寫入 `XDG_RUNTIME_DIR` 中的檔案，經 D-Bus 交由擴充功能置於游標處；擴充功能則回報焦點視窗與工作區的位置，將 App 提供的相對座標換算為螢幕座標。經 GNOME Shell 轉送的 App（IBus）與經 Fcitx5 D-Bus 的 App 皆適用。安裝後須於「擴充功能」App 啟用（或執行 `gnome-extensions enable project-key@koukeneko.github.io`，須重新登入）；未啟用時改用 Fcitx5 的候選面板。動畫與其他桌面相同，每一格畫面皆經由擴充功能顯示；滑鼠點選由擴充功能回傳至 Project Key。更新後須重新登入，才會載入新版擴充功能。候選字窗開啟與收起時，整個視窗連同模糊一併淡入淡出（GNOME 由擴充功能處理，X11 透過視窗的 `_NET_WM_WINDOW_OPACITY`）；其他 Wayland 桌面無調整模糊強度的協定，模糊本身直接出現與消失。

KDE Plasma 的 Wayland 上，KWin 以其輸入法協定（input-method-v1）啟動 Fcitx5，候選字預設由 Plasma 的輸入法面板（kimpanel）顯示，外觀依循 Plasma 主題；以 `fcitx5 --disable=kimpanel` 啟動時，改用 Fcitx5 的面板與下述主題。

COSMIC（Pop!_OS）上，支援 text-input-v3 的 App（例如 Firefox）經由合成器連線至 Fcitx5；COSMIC 內建的 App（例如 COSMIC Edit）不支援輸入法協定，無法輸入中文。COSMIC 的系統匣會將圖示染為文字顏色，因此於該處改用單色圖示 `fcitx-projectkey-symbolic.svg`（ㄅ 挖空）。

安裝內容包含 Fcitx5 主題「Project Key」（`theme/`，安裝於 `/usr/share/fcitx5/themes/projectkey`）：半透明深色、圓角，選取的字採用強調色，邊距與候選字窗相同；KWin 等合成器支援時模糊背後的內容（`EnableBlur`）。於 Fcitx5 設定的「附加元件 → 經典使用者介面 → 主題」選取此主題後，使用 Fcitx5 候選面板時的外觀即與其他平台一致（GNOME 上此面板由 App 繪製，同樣讀取 Fcitx5 主題）；此主題亦套用於其他 Fcitx5 輸入法。主題圖片由 `tools/icon/make-icons.ps1` 產生。

Fcitx5 預設以左 Shift 暫時切換輸入法。Project Key 使用中且「單按 Shift 切換中英」開啟時，Shift 由 Project Key 處理，切換的是中／英而非輸入法；關閉此設定後，Shift 交回 Fcitx5 處理。

自訂詞位於 `$XDG_DATA_HOME/projectkey/phrases.txt`（預設 `~/.local/share/projectkey/phrases.txt`），由設定視窗的「自訂詞」「編輯…」開啟；不再出現的詞列於同一資料夾的 `exclude-phrases.txt`，由「排除詞」開啟；「資料夾」顯示該資料夾。格式與 macOS 相同，見 [自訂詞](../macos/README.md#自訂詞)。

## 建置與安裝

以套件庫安裝（Debian 13、Ubuntu 24.04、Fedora 43，x86_64）：
```sh
# Debian / Ubuntu（suite：Debian 13 用 trixie，Ubuntu 24.04 用 noble）
sudo mkdir -p /etc/apt/keyrings
curl -fsSL https://koukeneko.github.io/Project-Key/apt/key.asc | sudo tee /etc/apt/keyrings/projectkey.asc >/dev/null
echo "deb [signed-by=/etc/apt/keyrings/projectkey.asc] https://koukeneko.github.io/Project-Key/apt trixie main" | sudo tee /etc/apt/sources.list.d/projectkey.list
sudo apt update
url=$(curl -fsSL https://api.github.com/repos/KoukeNeko/Project-Key/releases/latest | grep -o 'https://[^"]*projectkey-data[^"]*\.deb"' | tr -d '"')
curl -fsSLO "$url" -O "$url.asc"
export GNUPGHOME=$(mktemp -d) && gpg --import /etc/apt/keyrings/projectkey.asc &&
  gpg --verify fcitx5-projectkey-data_*.deb.asc fcitx5-projectkey-data_*.deb &&
  sudo apt install ./fcitx5-projectkey-data_*.deb fcitx5-projectkey

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
url=$(curl -fsSL https://api.github.com/repos/KoukeNeko/Project-Key/releases/latest | grep -o 'https://[^"]*projectkey-data[^"]*\.rpm"' | tr -d '"')
curl -fsSLO "$url" -O "$url.asc"
export GNUPGHOME=$(mktemp -d) && curl -fsSL https://koukeneko.github.io/Project-Key/rpm/key.asc | gpg --import &&
  gpg --verify fcitx5-projectkey-data-*.rpm.asc fcitx5-projectkey-data-*.rpm &&
  sudo dnf install ./fcitx5-projectkey-data-*.rpm fcitx5-projectkey
```
字典與選字模型為獨立的 `fcitx5-projectkey-data` 套件（超過 100 MB，僅於模型更新時改版），程式套件僅數百 KB。資料套件超過 GitHub 對分支內單一檔案的大小上限，因此不收錄於套件庫，而是附於每個發行版本，並附有以套件庫同一把金鑰簽署的 `.asc`。上述指令自最新發行版本取得資料套件，簽章驗證通過後才安裝；模型更新後須再次執行，才會改用新版。套件庫由 `release.yml` 於每次發行時更新：`package-deb.sh`、`package-rpm.sh` 於各發行版的容器中建置，`publish-repos.sh` 簽署後推送至 gh-pages 的 `apt/` 與 `rpm/`；簽章金鑰為 secret `REPO_GPG_PRIVATE_KEY`（無密碼的 ASCII 私鑰）。Arch 使用 `platforms/linux/arch/PKGBUILD`（`makepkg -si`）。

以 Homebrew（Linux）安裝發行版：先安裝下方 `apt install` 指令列出的套件，再執行：

```sh
brew install koukeneko/tap/fcitx5-projectkey
projectkey-link    # 把 Fcitx5 與 GNOME 讀的檔案連結到 ~/.local/share
```

此 formula 自原始碼建置，使用系統的編譯器、Fcitx5 與 Qt（由系統的 Fcitx5 載入），安裝於 Homebrew 的目錄。Fcitx5 僅於其自身目錄與 `~/.local/share/fcitx5` 尋找附加元件，因此附加元件的設定檔以完整路徑指向程式庫，再由 `projectkey-link` 將設定檔、主題、GNOME 擴充功能、設定視窗的捷徑與圖示建立連結（連結至 `opt`，升級後仍有效）。其後步驟同下方「安裝後」的說明。

自行建置須具備 CMake、Ninja、C++20 編譯器、Python 3（產生詞庫），以及 Fcitx5、Enchant、Cairo、Pango、Wayland（含 wayland-scanner）與 Qt 的開發檔；GNOME 版設定視窗另須 libadwaita、libsoup 3 與 json-glib。KDE 版設定視窗須使用 KDE 外觀所對應的 Qt 版本：Plasma 6 為 Qt 6，Plasma 5（Ubuntu 24.04）為 Qt 5（`qtbase5-dev`），否則將呈現 Qt 的預設外觀；亦可以 `-DPROJECTKEY_QT=5` 或 `6` 指定。Debian／Ubuntu（Plasma 5 另須安裝 `qtbase5-dev`）：

```sh
sudo apt install cmake ninja-build g++ python3 git libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev fcitx5-modules-dev libenchant-2-dev libxcb1-dev libcairo2-dev libpango1.0-dev libwayland-dev qt6-base-dev libadwaita-1-dev libsoup-3.0-dev libjson-glib-dev hunspell-en-us fonts-noto-cjk
platforms/linux/build.sh                       # 下載並編譯引擎、產生詞庫、跑測試，輸出到 build
sudo cmake --install platforms/linux/build    # 安裝到 /usr
```

安裝後重新啟動 Fcitx5（`fcitx5 -r`），執行 `projectkey-settings --welcome` 並按「加入」；亦可於 Fcitx5 設定的「輸入法」加入 Project Key（須取消勾選「只顯示目前語言」才會列出）。之後以 Ctrl+Space 切換。

Fcitx5 選單的「檢查 Project Key 安裝」以通知列出缺少的項目：詞庫、英文字典（Tab 提示用）、候選字窗的字型、設定程式，以及 Project Key 自行繪製的視窗能否顯示（GNOME 上須啟用擴充功能）。自使用 Fcitx5 候選面板的 App 開啟時，另檢查 Fcitx5 主題是否為 Project Key。

設定視窗的分組與 macOS、Windows 相同，變更立即生效。`projectkey-settings` 依桌面開啟對應版本：KDE 上為 Qt 版（`Settings/Qt.cpp`，排版與 KDE 系統設定相同），GNOME 等 GTK 桌面上為 libadwaita 版（`Settings/Gtk.cpp`，未建置時改開 Qt 版）。環境中無 `XDG_CURRENT_DESKTOP` 時（例如手動重新啟動的 Fcitx5），改讀取 systemd 使用者環境中的桌面。兩者的設定列定義於 `Settings/Rows.h`。可由 Fcitx5 選單的「Project Key 設定…」或應用程式選單開啟。設定視窗透過 Fcitx5 的 D-Bus 介面讀寫設定，因此 Fcitx5 設定中 Project Key 的「設定」頁修改的是同一份設定，存放於 `~/.config/fcitx5/conf/projectkey.conf`。「清除選字紀錄」呼叫 addon 於 Fcitx5 D-Bus 上的 `/projectkey`。選過的字僅存於記憶體；自送出文字學得的字串次數存於 `$XDG_DATA_HOME/projectkey/personal.txt`（預設 `~/.local/share/projectkey/personal.txt`），「清除選字紀錄」一併清空，關閉「記住選過的字」時不學習。「恢復原廠設定」的「恢復設定」呼叫 addon 的 `ResetSettings`，將所有設定改回預設值；「一併刪除資料」另刪除 `phrases.txt`、`exclude-phrases.txt` 與 `personal.txt` 並呼叫 `ClearChoices`；兩者皆保留下載的選字模型。「關於」顯示選字模型的日期，有新模型時可直接下載至同一資料夾，核對 SHA-256 後無須重新安裝即生效。以 Homebrew 安裝時，有新版即顯示「更新到」：執行 `brew upgrade fcitx5-projectkey`（自原始碼建置，約需數分鐘）與 `projectkey-link`，再重新啟動 Fcitx5 並開啟新版的設定視窗，過程記錄於 `~/.cache/projectkey-update.log`；自行建置安裝者則連結至發行版頁面。

移除方式：`sudo xargs rm < platforms/linux/build/install_manifest.txt`。
