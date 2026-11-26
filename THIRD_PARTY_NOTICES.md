# Third-Party Notices

Project Key 以 MIT 授權發布（見 [LICENSE](LICENSE)）。本檔列出 repo 內含的第三方內容，以及工具執行時下載、但不隨 repo 散布的元件。

## repo 內含

### McBopomofo

來源：https://github.com/openvanilla/McBopomofo ，commit `257da536ddaf590aae0e74e6adb9b415988344f6`。

- `tests/corpus/corpus.tsv` 的 `zhuyin` 欄位由 McBopomofo 詞庫自動標註後再人工修正。
- macOS 輸入法（`platforms/macos`）在建置時下載 McBopomofo 的引擎（gramambular2、Mandarin、McBopomofoLM）並連結進執行檔，詞庫 `data.txt` 與由其產生的聯想詞表 `associated-phrases-v2.txt` 隨 App 發布。App 內附授權條文 `McBopomofo-LICENSE.txt`。
- Windows 輸入法（`platforms/windows`）同樣在建置時下載並連結這些引擎；建置腳本為了讓 MSVC 編譯，會改寫兩個檔案中 `string_view` 迭代器的宣告（見 `build.ps1`）。詞庫 `data.txt`、聯想詞表 `associated-phrases-v2.txt` 與 `McBopomofo-LICENSE.txt` 安裝在同一個資料夾。

```
MIT License

Copyright (c) 2011-2026 Mengjuei Hsieh et al.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### 發行版隨附的詞庫與上下文模型

發行版（macOS、Windows 與 Homebrew 的 Linux formula）隨附 `tools/bigram-trainer` 與 `tools/lm-trainer` 訓練出的四個資料檔，建置時由 `tools/bigram-trainer/fetch-models.sh` 從 GitHub 發行檔下載並以 `models.sha256` 核對。它們與程式分開授權，安裝在同一個資料夾，旁邊附上來源與授權說明 [`MODELS-NOTICE.txt`](tools/bigram-trainer/MODELS-NOTICE.txt) 與 LGPL 條文 [`LICENSE-LGPL-2.1.txt`](tools/bigram-trainer/LICENSE-LGPL-2.1.txt)。

| 檔案 | 來源 | 授權 |
|---|---|---|
| `bigram.bin`、`chars.bin`、`sentence.bin` | 以臺灣繁體中文語料（約 1.37 億字）訓練的詞與字 n-gram（`bigram.bin`、`chars.bin`）與字元級 GPT-2 整句模型的權重（`sentence.bin`），不含原文。語料來自 [agentlans/fineweb2-chinese](https://huggingface.co/datasets/agentlans/fineweb2-chinese) 的臺灣網頁文字（衍生自 HuggingFaceFW/fineweb-2，ODC-BY）、[twinkle-ai/tw-reasoning-instruct-50k](https://huggingface.co/datasets/twinkle-ai/tw-reasoning-instruct-50k) 的問答（MIT）、[立法院公報](https://ppg.ly.gov.tw)（資料提供者：立法院，政府資料開放授權條款第 1 版；部分段落由 [Breeze-7B-Instruct-v1_0](https://huggingface.co/MediaTek-Research/Breeze-7B-Instruct-v1_0)（聯發創新基地，Apache License 2.0）改寫為口語）、[erhwenkuo/wikinews-zhtw](https://huggingface.co/datasets/erhwenkuo/wikinews-zhtw) 的中文維基新聞（維基新聞貢獻者，CC BY-SA 3.0）、[OpenFormosa/common_voice_25_zh-TW](https://huggingface.co/datasets/OpenFormosa/common_voice_25_zh-TW) 的 Mozilla Common Voice 25 句子（CC0 1.0），以及經過濾、以 OpenCC 轉為臺灣正體的[中文維基百科](https://zh.wikipedia.org)（維基百科貢獻者，CC BY-SA 4.0） | CC BY-SA 4.0 |
| `data.txt` | McBopomofo 詞庫（MIT），加上 [libchewing-data](https://github.com/chewing/libchewing-data) `tsi.csv`（commit `c44e81aef24b06f1509f19e1be54c99812d0c43f`，LGPL-2.1-or-later）的詞、[全國路名資料](https://data.gov.tw/dataset/35321)（115 年版，資料提供者：內政部戶政司，政府資料開放授權條款第 1 版）的路名與鄉鎮市區名，以及從上述訓練文字找出的詞 | LGPL-2.1-or-later |

程式在執行時讀取這些檔案，可以直接替換或移除；沒有它們時改用 McBopomofo 的詞庫與排序。

`sentence.bin` 為選用；未提供時，引擎不重新排序整句候選。

### llama.cpp

[llama.cpp](https://github.com/ggml-org/llama.cpp)（MIT）執行下方的語音模型：發行版與 Linux 套件以 [LLAMA_RELEASE](LLAMA_RELEASE) 指定的版本建置並連結進去；自行建置時以 `PROJECTKEY_LLAMA`（Windows 亦可用 `build.ps1 -Llama`）指向其原始碼。

### 語音模型與 miniaudio

設定視窗可另外下載語音模型（`speech-YYYY-MM-DD` release 的 `speech.gguf` 與 `speech-encoder.gguf`）：[Qwen3-ASR 0.6B](https://huggingface.co/Qwen/Qwen3-ASR-0.6B)（阿里巴巴通義千問團隊，Apache License 2.0）由 llama.cpp 轉換並量化為 8 位元（Q8_0）的 GGUF（[ggml-org/Qwen3-ASR-0.6B-GGUF](https://huggingface.co/ggml-org/Qwen3-ASR-0.6B-GGUF)），release 附 Apache License 2.0 條文。語音輸入時，引擎以 llama.cpp 的 mtmd 執行它，將錄音轉為文字。錄音經 [miniaudio](https://github.com/mackron/miniaudio)（David Reid，public domain 或 MIT No Attribution）取得，原始碼隨 llama.cpp 一同建置。

### 簡體中文轉換表

`core/data/TSCharacters.txt` 為 [OpenCC](https://github.com/BYVoid/OpenCC)（ver.1.4.2）的繁體字轉簡體字表，以 Apache License 2.0 授權；各平台與它一同安裝其條文 [`OPENCC-LICENSE.txt`](core/data/OPENCC-LICENSE.txt)。開啟「簡體中文」時，引擎以它將送出的文字逐字轉為簡體。

### 表情符號

`core/data/emoji.txt` 由 `tools/emoji/build-emoji.py` 從 [Unicode CLDR](https://cldr.unicode.org) 的繁體中文表情符號註解（`common/annotations/zh_Hant.xml`）與 Unicode 的 `emoji-test.txt` 產生，以 Unicode License v3（Unicode-3.0）授權。各平台與 `emoji.txt` 一同安裝其條文 [`EMOJI-NOTICE.txt`](core/data/EMOJI-NOTICE.txt)。

### Wayland 協定定義

`platforms/linux/protocols/` 內含四份 Wayland 協定的 XML，建置時由 `wayland-scanner` 產生程式碼，連結進 Linux 的 addon。四者都以 MIT 授權發布，條文在各檔案開頭的 `<copyright>`：

| 檔案 | 來源 |
|---|---|
| `input-method-unstable-v2.xml` | wlroots 的 input-method-unstable-v2 協定 |
| `ext-background-effect-v1.xml` | [wayland-protocols](https://gitlab.freedesktop.org/wayland/wayland-protocols) 的 `staging/ext-background-effect` |
| `fractional-scale-v1.xml` | wayland-protocols 的 `staging/fractional-scale` |
| `viewporter.xml` | wayland-protocols 的 `stable/viewporter` |

### Windows 設定視窗的 NuGet 套件

`platforms/windows/Settings` 建置時由 NuGet 取得，連同設定程式一起安裝：

| 元件 | 授權 |
|---|---|
| [Windows App SDK](https://github.com/microsoft/WindowsAppSDK)（WinUI 3） | MIT；執行環境 Windows App Runtime 由系統提供 |
| [Windows Community Toolkit](https://github.com/CommunityToolkit/Windows)（`SettingsControls`） | MIT |
| Microsoft.Windows.SDK.BuildTools | Microsoft 軟體授權條款（建置工具，不隨程式散布） |

## 執行時下載，不隨 repo 散布

`tools/corpus-evaluator/setup.sh`、`annotate.py` 和 `tools/bigram-trainer/train.sh` 會把下列元件下載到各自的 `.cache/`（已列入 `.gitignore`），只用於本機評測與訓練。訓練出的檔案隨發行版散布的方式見上方「發行版隨附的詞庫與上下文模型」。

| 元件 | 授權 | 用途 |
|---|---|---|
| [librime](https://github.com/rime/librime) | BSD-3-Clause | 評測引擎 |
| rime-prelude、rime-essay、rime-bopomofo、rime-terra-pinyin | LGPL-3.0 | RIME 注音方案與詞庫 |
| [OpenCC](https://github.com/BYVoid/OpenCC) 資料（隨 Homebrew librime 安裝） | Apache-2.0 | RIME 臺灣字形轉換 |
| [libchewing](https://github.com/chewing/libchewing) | LGPL-2.1 | 評測引擎 |
| [libchewing-data](https://github.com/chewing/libchewing-data) 的 `tsi.csv`（固定 commit） | LGPL-2.1-or-later | `tools/bigram-trainer` 補進詞庫的詞 |
| [McBopomofo](https://github.com/openvanilla/McBopomofo) 引擎與詞庫 | MIT | 評測引擎、注音標註來源 |
| [中文維基百科](https://zh.wikipedia.org)（Hugging Face `wikimedia/wikipedia` 20231101.zh 第一個分片） | CC BY-SA 4.0 | `tools/bigram-trainer` 的訓練文字 |
| [pyarrow](https://arrow.apache.org) | Apache-2.0 | 讀取維基百科分片 |
| [立法院公報](https://ppg.ly.gov.tw)（經 g0v [立法院 API](https://v2.ly.govapi.tw) 取得純文字） | 政府資料開放授權條款第 1 版（與 CC BY 4.0 相容），資料提供者：立法院 | `tools/bigram-trainer` 的訓練文字（`fetch_gazette.py`） |
| [全國路名資料](https://data.gov.tw/dataset/35321)（115 年版） | 政府資料開放授權條款第 1 版，資料提供者：內政部戶政司 | `tools/bigram-trainer` 補進詞庫的路名與鄉鎮市區名（`supplement.py`） |

日後產品若隨附其中其他元件，須把對應的授權條文移到「repo 內含」一節。LGPL 元件還必須保持可替換。
