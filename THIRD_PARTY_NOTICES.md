# Third-Party Notices

Project Key 以 MIT 授權發布（見 [LICENSE](LICENSE)）。本檔列出 repo 內含的第三方內容，以及工具執行時下載、但不隨 repo 散布的元件。

## repo 內含

### McBopomofo 詞庫衍生的注音

`tests/corpus/corpus.tsv` 的 `zhuyin` 欄位由 McBopomofo 詞庫自動標註後再人工修正。詞庫版本為 commit `257da536ddaf590aae0e74e6adb9b415988344f6`，來源：https://github.com/openvanilla/McBopomofo

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

`tools/corpus-evaluator/setup.sh` 和 `annotate.py` 會把下列元件下載到 `.cache/`（已列入 `.gitignore`），只用於本機評測。

| 元件 | 授權 | 用途 |
|---|---|---|
| [librime](https://github.com/rime/librime) | BSD-3-Clause | 評測引擎 |
| rime-prelude、rime-essay、rime-bopomofo、rime-terra-pinyin | LGPL-3.0 | RIME 注音方案與詞庫 |
| [OpenCC](https://github.com/BYVoid/OpenCC) 資料（隨 Homebrew librime 安裝） | Apache-2.0 | RIME 臺灣字形轉換 |
| [libchewing](https://github.com/chewing/libchewing) | LGPL-2.1 | 評測引擎 |
| [McBopomofo](https://github.com/openvanilla/McBopomofo) 引擎與詞庫 | MIT | 評測引擎、注音標註來源 |

日後產品若隨附其中任何元件，須把對應的授權條文移到「repo 內含」一節。LGPL 元件還必須保持可替換。
