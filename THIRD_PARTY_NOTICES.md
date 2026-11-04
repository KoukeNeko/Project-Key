# Third-Party Notices

Project Key 以 MIT 授權發布（見 [LICENSE](LICENSE)）。本檔列出 repo 內含的第三方內容，以及工具執行時下載、但不隨 repo 散布的元件。

## repo 內含

### McBopomofo

來源：https://github.com/openvanilla/McBopomofo ，commit `257da536ddaf590aae0e74e6adb9b415988344f6`。

- `tests/corpus/corpus.tsv` 的 `zhuyin` 欄位由 McBopomofo 詞庫自動標註後再人工修正。
- macOS 輸入法（`platforms/macos`）在建置時下載 McBopomofo 的引擎（gramambular2、Mandarin、McBopomofoLM）並連結進執行檔，詞庫 `data.txt` 隨 App 發布。App 內附授權條文 `McBopomofo-LICENSE.txt`。

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

## 執行時下載，不隨 repo 散布

`tools/corpus-evaluator/setup.sh`、`annotate.py` 和 `tools/bigram-trainer/train.sh` 會把下列元件下載到各自的 `.cache/`（已列入 `.gitignore`），只用於本機評測。由維基百科與立法院公報統計出的 `bigram.bin` 若存在，`platforms/macos/build.sh` 會把它放進本機建置的 App；它能否隨產品散布尚未確認。

| 元件 | 授權 | 用途 |
|---|---|---|
| [librime](https://github.com/rime/librime) | BSD-3-Clause | 評測引擎 |
| rime-prelude、rime-essay、rime-bopomofo、rime-terra-pinyin | LGPL-3.0 | RIME 注音方案與詞庫 |
| [OpenCC](https://github.com/BYVoid/OpenCC) 資料（隨 Homebrew librime 安裝） | Apache-2.0 | RIME 臺灣字形轉換 |
| [libchewing](https://github.com/chewing/libchewing) | LGPL-2.1 | 評測引擎 |
| [McBopomofo](https://github.com/openvanilla/McBopomofo) 引擎與詞庫 | MIT | 評測引擎、注音標註來源 |
| [中文維基百科](https://zh.wikipedia.org)（Hugging Face `wikimedia/wikipedia` 20231101.zh 第一個分片） | CC BY-SA 4.0 | `tools/bigram-trainer` 的訓練文字 |
| [pyarrow](https://arrow.apache.org) | Apache-2.0 | 讀取維基百科分片 |
| [立法院公報](https://ppg.ly.gov.tw)（經 g0v [立法院 API](https://v2.ly.govapi.tw) 取得純文字） | 政府資料開放授權條款第 1 版（與 CC BY 4.0 相容），資料提供者：立法院 | `tools/bigram-trainer` 的訓練文字（`fetch_gazette.py`） |

日後產品若隨附其中任何元件，須把對應的授權條文移到「repo 內含」一節。LGPL 元件還必須保持可替換。
