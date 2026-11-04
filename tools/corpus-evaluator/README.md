# 選字評測工具

以同一份臺灣正體語料比較各注音引擎「連續輸入整句、不手動選字」時的結果與每鍵延遲。

```bash
tools/corpus-evaluator/setup.sh                  # 安裝 librime、libchewing，編譯轉接程式，準備 RIME 資料
python3 tools/corpus-evaluator/evaluate.py       # 評測全部引擎，結果輸出到 stdout
python3 tools/corpus-evaluator/evaluate.py --engine rime --out report.md
```

需要 macOS 與 Homebrew。下載的資料與編譯產物都放在 `.cache/`。

## 語料

| 檔案 | 內容 |
|---|---|
| `tests/corpus/sentences.tsv` | `領域<Tab>句子`，人工撰寫，不含標點與英文 |
| `tests/corpus/corpus.tsv` | 加上注音與複核狀態，由 `annotate.py` 產生 |

新增句子的流程：

1. 把句子加到 `sentences.tsv`。
2. 執行 `python3 tools/corpus-evaluator/annotate.py`。
3. 檢查新列的注音，修正後把 `status` 改成 `checked`。

已存在的列不會被覆寫，所以手動修正會保留。

注音以 McBopomofo 的 MIT 詞庫（固定 commit）自動標註，並依實際打字習慣調整：

- 「一」「不」用本調，不標變調。
- 「個」標 ㄍㄜˋ、「們」標 ㄇㄣ˙ 等，見 `annotate.py` 的 `TYPED_FORM`。

`status` 欄位：

| 值 | 意義 |
|---|---|
| `review` | 讀音無法自動判定 |
| `auto` | 自動標註，未經人工複核 |
| `checked` | 已人工複核 |

## 指標

| 指標 | 計算方式 |
|---|---|
| 整句正確 | 引擎輸出與預期句子完全相同 |
| 字正確率 | 1 − 錯字數 ÷ 總字數，以 difflib 對齊 |
| 需修正處 | 連續錯誤區段的數量，約等於使用者要回頭選字的次數 |
| 每鍵延遲 | 處理按鍵加上取得預編輯區的時間，不含每句第一鍵（第一鍵包含延遲載入） |

異體字（帳／賬、布／佈）目前算錯。

## 引擎轉接程式

`adapters/*.c` 從 stdin 每行讀入一句的大千鍵序（一聲為空白鍵），每行輸出 `每鍵毫秒,逗號分隔<Tab>輸出文字`。轉接程式只清除組字、不送出文字，句子之間不會互相學習。

新增引擎的步驟：

1. 寫一個符合上述協定的程式。
2. 在 `setup.sh` 加上編譯步驟。
3. 在 `evaluate.py` 的 `engines()` 註冊。

| 引擎 | 版本與資料 |
|---|---|
| `rime` | librime + `bopomofo_tw` 方案（terra_pinyin 詞庫） |
| `chewing` | libchewing（Homebrew 版本內建詞庫），關閉自動學習 |
| `mcbopomofo` | McBopomofo 引擎與 `data.txt`，與 `annotate.py` 使用同一個 commit |

注意事項：

- 語料的注音取自 McBopomofo 詞庫，讀音必定在它的詞庫裡，比較時對它略為有利。
- McBopomofo 只在聲調鍵結束音節時重算整句，其他按鍵幾乎不花時間。RIME 和新酷音每一鍵都會更新組字，所以三者的 P50 不能直接比較，比較時應看 P95 和最大值。
- macOS 內建注音無法以程式庫呼叫，尚未納入。
