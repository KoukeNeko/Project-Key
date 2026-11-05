# 上下文 bigram／trigram 訓練

產生 macOS 輸入法用的詞 bigram 與 trigram 模型 `.cache/bigram.bin`（約 120MB）。組字器據此在相同讀音的詞之間，依前兩個詞選字，例如「第一」後面的 ㄌㄧㄝˋ 選「列」。

```bash
platforms/macos/build.sh             # 先建置，取得 McBopomofo 的 data.txt
tools/bigram-trainer/train.sh        # 下載維基百科分片（約 590MB）並訓練，約 3 分鐘
platforms/macos/build.sh             # 再建置一次，把模型放進 App
```

`train.sh [字數上限] [最少出現次數]`，預設為 2 億字、3 次。

## 做法

1. `extract.py` 從中文維基百科（20231101 版）第一個 parquet 分片取出段落。若有 `.cache/gazette/`，再加上立法院公報全文（質詢、委員會發言等口語）。公報需先另行下載：`python3 -I fetch_gazette.py .cache/gazette 11`（第 11 屆，約 3,300 份）。
2. OpenCC `s2twp` 轉成臺灣正體，再把 OpenCC 產生的佈、臺、賬改回布、台、帳。
3. `train.cpp` 把每段連續漢字依 McBopomofo 詞庫分數斷詞，計算相鄰詞對的條件機率 P(後詞｜前詞)，保留出現次數達門檻的詞對。每段的開頭與結尾也算成詞對（空字串代表段落邊界），讓組字器知道哪些詞常出現在片語開頭或結尾，例如「什」幾乎不會結尾。

模型格式定義在 `core/engine/Bigram.h`。組字器的計分方式為 log10(0.3 × 上下文 + 0.7 × 10^詞庫分數)，上下文為 P(詞｜前詞)，有三詞資料時與 P(詞｜前兩詞) 各半混合；沒有詞對資料時退回詞庫分數。

## 限制

- 訓練文字只有百科：口語句尾的「吧」「啦」等詞對幾乎沒有，「了吧」會被選成「了巴」。
- 模型約 52MB，App 啟動時整份載入記憶體。
- 立法院公報為政府資料開放授權條款第 1 版，散布衍生物時須標示資料提供者「立法院」。
- 維基百科文字為 CC BY-SA 4.0。由它統計出的模型能否隨產品散布尚未確認，目前只供本機評測與試用。
