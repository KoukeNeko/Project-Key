# 字元語言模型訓練（原型）

訓練字元級 GPT-2，給 `tools/corpus-evaluator/rerank.py` 重排組字結果用，評估小模型對選字與鄰鍵修正有沒有幫助。

```bash
tools/bigram-trainer/train.sh    # 先產生 .cache/text-200000000.txt（維基百科＋立法院公報）
python3 -I tools/lm-trainer/prepare.py tools/lm-trainer/.cache < tools/bigram-trainer/.cache/text-200000000.txt
python tools/lm-trainer/train.py tools/lm-trainer/.cache <輸出目錄> --size small    # 需要 CUDA、torch、transformers
python3 tools/corpus-evaluator/rerank.py <輸出目錄> --slip row
```

- `prepare.py` 把文字切成不含標點的漢字句子（評測語料與組字區的樣子），以 `[CLS]…[SEP]` 包起來，轉成字元編號。出現少於 10 次的字算未知字。每 100 句留 1 句作驗證。
- `train.py` 的 `small` 約 14M 參數（384 維、6 層），`medium` 約 30M（512 維、8 層），上下文 64 字。存成 transformers 可直接載入的模型，tokenizer 為 BertTokenizerFast（漢字逐字切開）。
- 在 RTX 4070 Ti（12GB）上，batch 256 時 small 每 1000 步約 2 分鐘；batch 512 會超出顯示記憶體。

訓練文字的授權同 `tools/bigram-trainer`：維基百科為 CC BY-SA 4.0，立法院公報為政府資料開放授權條款第 1 版。模型目前只供本機評測。
