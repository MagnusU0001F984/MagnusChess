# MNUE-X1 設計規格

## 目標

MNUE-X1 不是把現有 P2 的 `1024` 隱層盲目放大，而是增加目前網絡完全看不到的戰術資訊，同時保留可增量更新的棋子位置分支。

第一版的成功條件：

1. 相同訓練資料下，300M positions 的 validation loss 明顯優於 P2。
2. Trainer 與 engine 對每一個 active feature 產生完全相同的 index。
3. 完整網絡可在 RTX 4050 Laptop 6 GiB 上訓練。
4. 接入搜索後，相對 P2 的 NPS 損失可量化，並以 Elo/NPS 決定是否進入長訓練。

## 網絡結構

```text
59,392 sparse inputs
        |
        v
shared affine 59,392 -> 768, CReLU, pairwise multiply
        |
        +-- STM 384
        +-- NTM 384
                |
                v
             concat 768
                |
       material-bucket affine 768 -> 16, SCReLU
                |
       material-bucket affine 16 -> 32, SCReLU
                |
       material-bucket affine 32 -> 1
```

這是一個共同訓練、共同輸出的單一網絡。輸入在語義上分成棋子位置與戰術狀態兩組，但不是兩張獨立網絡。

固定尺寸：

| 項目 | 數值 |
|---|---:|
| Input king buckets | 16 |
| Piece inputs | 10,240 |
| Tactical inputs | 49,152 |
| Total inputs | 59,392 |
| Transformer width | 768 |
| Output buckets | 8 |
| Backend | 768 -> 16 -> 32 -> 1 |
| Parameters | 45,715,712 |
| Quantised payload | 91,433,760 bytes |
| File size with header | 91,433,824 bytes |

量化後約 87.20 MiB。最大 accumulator active features 是 62：30 個非王棋子位置，加上最多 32 個棋子的戰術狀態。

## 棋子位置特徵

沿用 P2，避免同時改變所有變數：

```text
index =
    (((king_bucket16 * 2 + relative_colour)
       * 5 + non_king_piece_type)
      * 64 + relative_square)
```

範圍是 `[0, 10240)`。王由 king bucket 表達，不在這一組 active features 中。

## 戰術狀態特徵

每一顆棋子（包含王）恰好啟用一個戰術 feature：

```text
relative_piece_class = relative_colour * 6 + piece_type

index =
    10240
    + ((relative_piece_class * 64 + relative_square) * 64)
    + tactical_status
```

`tactical_status` 是 6-bit mask：

| Bit | 意義 |
|---:|---|
| 0 | 被敵方兵攻擊 |
| 1 | 被敵方馬攻擊 |
| 2 | 被敵方象或后沿斜線攻擊 |
| 3 | 被敵方車或后沿直線攻擊 |
| 4 | 被敵方王攻擊 |
| 5 | 至少被一顆己方棋子保護 |

攻擊採用 pseudo-attack：被釘住的棋子仍算攻擊。滑子只攻擊到第一個阻擋格，與引擎現有 attack tables 一致。

這一組範圍是 `[10240, 59392)`。

## Output buckets

按盤面總棋子數分成八組。這比 P2 的「王區乘二階段」更平衡；王的位置已經由 input bucket 表達，不需要在輸出層重複 32 份。

| Bucket | 總棋子數 |
|---:|---|
| 0 | 2–5 |
| 1 | 6–8 |
| 2 | 9–11 |
| 3 | 12–14 |
| 4 | 15–17 |
| 5 | 18–20 |
| 6 | 21–24 |
| 7 | 25–32 |

## 量化與檔案格式

MNUE-X1 使用 MNUE file version 2、architecture id 5。Header 固定 64 bytes，後面按以下順序存放：

| Tensor | Shape | Type | Quantisation |
|---|---|---|---:|
| `l0w` | 59,392 x 768 | i16 | QA=255 |
| `l0b` | 768 | i16 | QA=255 |
| `l1w` | 8 x 16 x 768 | i16 | QB=64 |
| `l1b` | 8 x 16 | i16 | QA=255 |
| `l2w` | 8 x 32 x 16 | i16 | QB=64 |
| `l2b` | 8 x 32 | i16 | QA=255 |
| `l3w` | 8 x 1 x 32 | i16 | QB=64 |
| `l3b` | 8 | i32 | QA*QB=16,320 |

Header 的 16 個 32-bit little-endian 欄位：

```text
magic, version, arch, header_bytes,
input_size, hidden_size, input_buckets, output_buckets,
l1_size, l2_size, scale, qa,
qb, feature_version, flags, reserved
```

## 記憶體與速度

- 網絡權重約 87.20 MiB，所有 search threads 共用。
- 768-wide、雙 perspective、132-ply accumulator stack 約 396 KiB/thread，低於現有 P2 的約 528 KiB/thread。
- 真正的速度風險不是網絡檔案大小，而是戰術狀態在走子後可能影響多顆棋子。

第一個 engine reference implementation 可以全量重建戰術 features，但不能把它當最終 NPS。正式增量版只更新以下候選棋子：

1. 移動、被吃、升變與王車易位涉及的棋子。
2. 移動棋子在舊、新位置攻擊到的棋子。
3. 每個 occupancy 變更格八個方向上的第一顆棋子。
4. 上述候選棋子的舊、新 tactical status 不同時，才更新 accumulator row。

## 執行順序

1. 編譯 trainer 與 C++ feature index module。
2. 用固定 FEN 測試 STM/NTM feature index 對稱性。
3. 訓練 300M smoke network，不直接啟動 40B。
4. 寫 version-2 loader 與 scalar/reference forward，逐 tensor 驗證。
5. 接入 lazy full tactical rebuild，量測 eval/s、search NPS 與短 Elo。
6. 完成 tactical delta incremental update，再重新量測。
7. 只有在 300M 網絡已證明 Elo/成本方向正確後，才跑 3B、10B，最後決定是否 40B replay。

## X2：參考 Reckless 原則的優化方向

X1 smoke run 證明新特徵方向有效，但其 49,152 個 tactical rows 全部使用
i16，且後端三層全部整數量化。X2 保留 Magnus 自己的 feature definition，
採用現代雙 accumulator NNUE 的幾個有效原則：

- piece transformer 與 attack-edge transformer 分開儲存，activation 前求和；
- piece rows 使用 i16，attack-edge rows 使用 i8；
- 棋子特徵包含王，並依己王所在半邊做水平鏡像；
- tactical feature 改成實際存在的「攻擊者 -> 被攻擊棋子」edge；
- pairwise CReLU 直接產生 u8 activation；
- 768 -> 16 使用 i8，並可跳過全零的 4-byte activation chunks；
- 16 -> 32 -> 1 保留 float，減少低材料 bucket 的連續量化誤差。

X2 尺寸：

| 項目 | 數值 |
|---|---:|
| Piece inputs | 7,680 |
| Attack-edge inputs | 90,048 |
| Transformer width | 768 |
| Quantised transformer | piece i16 + edge i8 |
| Approximate network size | 77.3 MiB |

Attack-edge index 由 Magnus 自行定義：相對攻擊方顏色、攻擊棋種、來源格、
空盤合法攻擊目標 slot、相對受攻擊棋子 class。沒有使用 Reckless 的
piece-interaction exclusion table 或 feature index 程式碼。
