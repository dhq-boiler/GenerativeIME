# GenerativeIME E2E テストマトリクス v2 — 漢字変換 + カタカナ語変換 (2026-07-02)

前回 (`e2e-conversion-test-matrix-2026-07-02.md` + `e2e-conversion-test-report-2026-07-02.md`) の網羅結果を踏まえ、**漢字変換** と **カタカナ語変換** に絞って深掘りする。MSI v0.1.6 (Phase B fix 済) を使用。

## 環境と制約

- **MSI**: v0.1.6 (installerId `8ff21210-62e5-4f94-b5bb-3676750adf61`, sha256 `c59453b028c481cfc2892c741f2da93d2c6ebdcf38a7cbf6c8b56178b4799115`, 40214528B)
- **前回セッションの scratchpad**: `C:\Users\dhq_b\AppData\Local\Temp\claude\C--Git-GenerativeIME\4fda55e4-e021-49df-8105-c4b1b42f3bb1\scratchpad\pkg\GenerativeIME.msi`
- **Ollama 不到達**: Trigger A/B/C/E 発火時のフォールバック候補は seed のみ観測
- **DBWIN 捕捉不可**: `list_process_modules` で DLL ロードだけ確認
- **WDAC send_key 制約**: VK_CONVERT / VK_NONCONVERT / VK_KANJI / VK_DBE_HIRAGANA は名前で送れない。F6-F10 と Space / Enter / ↓ / ↑ / Tab / ESC は OK

## 実行フロー (各ケース共通)

```
1. notepad Ctrl+A → Delete でクリア
2. ime_type(<romaji>)
3. send_key("SPACE")   # 変換
4. (必要なら) send_key("↓") で候補選択
5. send_key("ENTER")   # 確定
6. get_ui_tree で Edit の valueCurrent を assert
7. ESC で残り composition クリア
```

判定:
- **PASS**: 期待値と実測が完全一致
- **KNOWN_FAIL**: Ollama 依存で改善不能な系統
- **BUG**: それ以外の期待外れ → 再現手順を残す

---

## Cat K1: 単漢字 (漢字 1 文字)

| Input | 期待 (top) | 備考 |
|---|---|---|
| `ame` | 雨 | 同音: 飴/天 |
| `hi` | 火 | 同音: 日/氷/樋 |
| `mizu` | 水 | |
| `hon` | 本 | (v0.1.6 fix で「其れ」ではない) |
| `kawa` | 川 | 同音: 河/皮 |
| `ki` | 木 | 同音: 気/機/器 |
| `sora` | 空 | |
| `hoshi` | 星 | ★ が symbol dict にあり混在の可能性 |
| `hana` | 花 | 同音: 鼻/華 |
| `kaze` | 風 | 同音: 風邪 |
| `hito` | 人 | |
| `neko` | 猫 | |
| `inu` | 犬 | |
| `tori` | 鳥 | |
| `sakana` | 魚 | 同音: 肴 |

---

## Cat K2: 二字熟語

| Input | 期待 |
|---|---|
| `tokyo` | 東京 (raw ASCII の場合 fail) |
| `toukyou` | 東京 |
| `nihon` | 日本 |
| `kaigi` | 会議 |
| `denwa` | 電話 |
| `denki` | 電気 |
| `gakkou` | 学校 |
| `senpai` | 先輩 |
| `kouhai` | 後輩 |
| `taisetsu` | 大切 |
| `benkyou` | 勉強 |
| `shigoto` | 仕事 |
| `jikan` | 時間 |
| `shinbun` | 新聞 |
| `otouto` | 弟 |
| `imouto` | 妹 |
| `kazoku` | 家族 |
| `heiwa` | 平和 |
| `kaisha` | 会社 |
| `ryokou` | 旅行 |

---

## Cat K3: 三字/四字熟語

| Input | 期待 |
|---|---|
| `chuugakusei` | 中学生 |
| `koukousei` | 高校生 |
| `daigakusei` | 大学生 |
| `shinbunshi` | 新聞紙 |
| `takusan` | 沢山 |
| `jidousha` | 自動車 |
| `denshirenji` | 電子レンジ (カタカナ混在) |
| `taiikukan` | 体育館 |
| `ichigoichie` | 一期一会 |
| `sokosokuhoukoku` | 逐次報告 (誤変換余地大) |
| `nyuugakushiken` | 入学試験 |

---

## Cat K4: 動詞活用形

| Input | 期待 |
|---|---|
| `taberu` | 食べる |
| `tabeta` | 食べた |
| `tabete` | 食べて |
| `tabenai` | 食べない |
| `tabemasu` | 食べます |
| `hashiru` | 走る |
| `hashitta` | 走った |
| `iku` | 行く |
| `itta` | 行った |
| `mita` | 見た |
| `okuru` | 送る |
| `okutta` | 送った |
| `kau` | 買う |
| `katta` | 買った |
| `nomu` | 飲む |
| `nonda` | 飲んだ |
| `yomu` | 読む |
| `yonda` | 読んだ |

---

## Cat K5: 形容詞

| Input | 期待 |
|---|---|
| `atarashii` | 新しい |
| `furui` | 古い |
| `takai` | 高い |
| `yasui` | 安い |
| `ookii` | 大きい |
| `chiisai` | 小さい |
| `hayai` | 早い (同音: 速い) |
| `osoi` | 遅い |
| `omoshiroi` | 面白い |
| `utsukushii` | 美しい |

---

## Cat K6: 送り仮名注意 (Phase B / SKK フィルタリング関連)

| Input | 期待 | 備考 |
|---|---|---|
| `akarui` | 明るい | v0.1.6 で「明い」除外 |
| `atatakai` | 暖かい | |
| `yasashii` | 優しい | |
| `kanashii` | 悲しい | |
| `kirei` | 綺麗 | |
| `shizuka` | 静か | |

---

## Cat K7: 同音異義語 (候補窓ナビ含む)

各 `ime_type → SPACE → ↓×N → ENTER` で下位候補まで到達可能か確認。

| Input | 期待順 (top→) |
|---|---|
| `ame` | 雨 / 飴 / 天 |
| `hashi` | 橋 / 箸 / 端 |
| `kami` | 神 / 紙 / 髪 |
| `kumo` | 雲 / 蜘蛛 |
| `sake` | 酒 / 鮭 |
| `tsutsu` | 筒 / 続 |
| `koutou` | 高等 / 口頭 / 喉頭 |
| `kikou` | 気候 / 機構 / 気功 |

---

## Cat K8: 固有名詞 (地名/人名)

| Input | 期待 | 備考 |
|---|---|---|
| `toukyou` | 東京 | |
| `oosaka` | 大阪 | |
| `kyouto` | 京都 | |
| `fukuoka` | 福岡 | |
| `sapporo` | 札幌 | |
| `yamada` | 山田 | 人名 top 想定 |
| `suzuki` | 鈴木 | |
| `satou` | 佐藤 | |
| `tanaka` | 田中 | |
| `nihonbashi` | 日本橋 | 複合地名 |

---

## Cat K9: 助詞絡み文 (Phase B 分節)

複数文節。Phase B 分節 UI に入る。top 候補コミット後の全文確認。

| Input | 期待 (全文節 top コミット) |
|---|---|
| `watashihagakusei` | 私は学生 (v0.1.6 fix) |
| `kyouhakaigi` | 今日は会議 |
| `atarashiihon` | 新しい本 (v0.1.6 fix) |
| `haruhaakaruikisetsudesu` | 春は明るい季節です (v0.1.6 fix) |
| `sorehatanoshii` | それは楽しい |
| `karehaisha` | 彼は医者 |
| `kanojohagakusei` | 彼女は学生 |
| `kononihonhaii` | この本は良い |
| `asagohantabetakute` | 朝ご飯食べたくて |
| `konyounahibaka` | この様な日ばかり (誤変換確率高) |

---

## Cat K10: 難読/やや珍しい漢字

| Input | 期待 |
|---|---|
| `arigatou` | 有難う / 有り難う |
| `ojigi` | お辞儀 |
| `shitsurei` | 失礼 |
| `sumimasen` | 済みません |
| `konnichiwa` | 今日は |
| `sayounara` | 左様なら |

---

## Cat Kat1: 基本外来語

Space 一発で **カタカナ top** に来るか (SKK に該当エントリがある場合)。もし来ない場合は F7 押下でカタカナ強制変換を試す。

| Input | 期待 top |
|---|---|
| `tesuto` | テスト |
| `deeta` / `de-ta` | データ |
| `sofuto` | ソフト |
| `saabisu` / `sa-bisu` | サービス |
| `bideo` | ビデオ |
| `hoteru` | ホテル |
| `terebi` | テレビ |
| `radio` | ラジオ |
| `gemu` / `ge-mu` | ゲーム |
| `bagu` | バグ |
| `apuri` | アプリ |
| `resutoran` | レストラン |
| `pasokon` | パソコン |
| `sumaho` | スマホ |
| `bijinesu` | ビジネス |

---

## Cat Kat2: 長音を含む外来語 (Trigger C 発火)

長音記号 `ー` が 2 つ以上入る場合 → Trigger C → seed[1] にカタカナ probe を提示。

| Input | 期待 seed[1] (katakana probe) | 前回結果 |
|---|---|---|
| `ko-hi-` | コーヒー | 前回 top: 琥珀 ⚠️ |
| `su-pa-` | スーパー | |
| `me-ru` | メール | |
| `pu-ru` | プール | |
| `su-tsu` | スーツ | (1 個の ー なので Trigger C 未発火可能) |
| `konpyu-ta-` | コンピューター | 前回: 此れぴゅうたー ⚠️ |
| `su-pa-ma-ketto` | スーパーマーケット | 前回: 崩れ ⚠️ |
| `ekusukuramesyonmaaku` | エクスクラメーションマーク | ー なし → Trigger C 発火せず |
| `nyuusuke-sutaa` | ニュースキャスター | |
| `intaanetto` | インターネット | |
| `hairai-tou` | ハイライト | |
| `bideoge-mu` | ビデオゲーム | |

---

## Cat Kat3: 拗音/特殊音を含む外来語

| Input | 期待 |
|---|---|
| `chansu` | チャンス |
| `janpu` | ジャンプ |
| `shanpu-` | シャンプー |
| `chikin` | チキン |
| `fairu` | ファイル |
| `fesutibaru` | フェスティバル |
| `wain` | ワイン |
| `webu` | ウェブ |
| `weekuendo` / `wi-kuendo` | ウィークエンド |
| `vaiorin` | ヴァイオリン (vi は「ヴィ」相当) |
| `tii` | ティー |
| `paatii` | パーティー |
| `tsua-` | ツアー |
| `disuku` | ディスク |
| `deyeto` / `daieto` | ダイエット |

---

## Cat Kat4: 促音を含む外来語

| Input | 期待 |
|---|---|
| `pokketto` | ポケット |
| `chiketto` | チケット |
| `raketto` | ラケット |
| `kukkii` | クッキー |
| `sukurappu` | スクラップ |
| `sanguraasu` | サングラス |
| `wakingu` | ウォーキング |
| `randoseru` | ランドセル |

---

## Cat Kat5: F7 で全角カタカナに強制変換

Space を押さず、composition 中に **F7** で全角カタカナ commit。

| Input (composition) | F7 後 |
|---|---|
| `arigatou` | アリガトウ |
| `konnichiwa` | コンニチハ |
| `watashi` | ワタシ |
| `tesuto` | テスト |
| `namae` | ナマエ |
| `nihon` | ニホン |

---

## Cat Kat6: F8 で半角カタカナに強制変換

| Input (composition) | F8 後 |
|---|---|
| `arigatou` | ｱﾘｶﾞﾄｳ |
| `konnichiwa` | ｺﾝﾆﾁﾊ |
| `namae` | ﾅﾏｴ |

---

## Cat Kat7: 複合カタカナ + 漢字混在

Phase B 分節で「漢字 + カタカナ + 漢字」の並びが正しくコミットされるか。

| Input | 期待 |
|---|---|
| `denshirenji` | 電子レンジ |
| `konpyu-ta-shitsu` | コンピューター室 |
| `karaokebako` | カラオケ箱 |
| `terebibangumi` | テレビ番組 |
| `sumahoo` | スマホ (「smaho」変換余地) |

---

## 実行順

1. **Phase A (5 min)**: セッション起動 + v0.1.6 install + activate
2. **Phase B (30 min)**: Cat K1 (単漢字) → Cat K2 (二字) → Cat K3 (三字/四字)
3. **Phase C (30 min)**: Cat K4 (動詞) → Cat K5 (形容詞) → Cat K6 (送り仮名)
4. **Phase D (20 min)**: Cat K7 (同音) → Cat K8 (固有名詞)
5. **Phase E (30 min)**: Cat K9 (Phase B 助詞絡み文) → Cat K10
6. **Phase F (30 min)**: Cat Kat1 (基本外来語) → Cat Kat2 (長音 Trigger C)
7. **Phase G (20 min)**: Cat Kat3 (拗音) → Cat Kat4 (促音)
8. **Phase H (10 min)**: Cat Kat5-6 (F7/F8) → Cat Kat7 (混在)
9. **Phase I**: terminate_session + `docs/e2e-kanji-katakana-report-2026-07-02.md` 集約

## 発見時の記録テンプレ

BUG 発見時:
```
### BUG-<n>: <input> → <実測> (期待: <expected>)
- Cat: <K3 etc.>
- 再現: ime_type("...") → SPACE → ENTER
- 推定原因: <MeCab lemma 誤/SKK okuri-ari 合成/カタカナ probe 不足...>
- 優先度: HIGH/MEDIUM/LOW
```
