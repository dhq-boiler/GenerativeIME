# GenerativeIME

> **The idea was by 99% dhq-boiler, but implementation was 100% AI.**
>
> アイデアの 99% は dhq-boiler のもの。実装の 100% は AI (Claude Code) によるもの。

Windows 用の日本語 IME (TSF テキストサービス) です。SKK 辞書 + MeCab 形態素解析という古典的な変換エンジンを土台に、ローカル LLM (Ollama) を非同期で組み合わせた「生成 AI 支援」の日本語入力を実験しています。

## 特徴

- **辞書ベースの即時変換** — SKK-JISYO.L (約 30 万エントリ) による読み→語変換。送り仮名エントリからの活用形復元、現代語優先の候補順補正付き
- **文節変換 (Phase B)** — MeCab + UniDic-Lite で長いひらがな列を文節分割し、文節ごとに候補選択・伸縮できる UI
- **ローカル LLM 連携 (Ollama / gemma4:12b)** — 辞書・形態素解析が苦手な読み (外来語など) の変換補完と、文書文脈を使った候補リランキングを非同期実行。LLM 不在でも SKK + MeCab パスはそのまま動作
- **投機的変換** — 読みの途中から補完候補をポップアップ (こんに → 今日は)
- **ドキュメント文脈バイアス** — カーソル周辺のテキストを解析し、その文書に出てくる語を候補上位へ
- **学習** — アプリ別コンテキストつきの候補学習と、Shift+Delete によるオプトアウト (ブラックリスト)
- **絵文字変換** — かな読み→絵文字 (えがお → 😀、はーと → ❤️)。CLDR 由来の 2,496 エントリ、候補ウィンドウは Direct2D/DirectWrite でカラー絵文字を表示
- **記号・単位辞書** — へいほうめーとる → ㎡、やじるし → → などの即時変換
- **5 つの入力モード** — ひらがな / 全角カナ / 半角カナ / 全角英数 / 半角英数

## 変換パイプライン

```
ローマ字入力 → かな (romajitokana)
  → 学習 fav 高速パス (learningstore)
  → 記号・単位辞書 (symboldictionary)
  → SKK 辞書 + 現代語順補正 + 絵文字 (skkdictionary / modernranking)
  → MeCab 文節分割・活用形再構成 (bunsetsu / mecabanalyzer)
  → Ollama 非同期補完・リランキング (ollamaclient)
```

## 動作環境

- Windows 10/11 x64
- [Ollama](https://ollama.com/) + `gemma4:12b` (推奨。無くても LLM 連携が無効になるだけで変換は動作)

## インストール

`installer/build_msi.ps1` が WiX 5 で MSI を生成します。MSI は TSF テキストサービスの登録と辞書・MeCab ランタイム一式の配置を行います。

## ビルド・開発

Visual Studio 2026 (v18) + vcpkg (mecab) + UniDic-Lite が必要です。手順の詳細は [docs/SETUP-NEW-MACHINE.md](docs/SETUP-NEW-MACHINE.md) を参照してください。

```powershell
# コアロジックの unit test (TSF 統合なしで実行可能)
.\tests\build_tests.ps1
```

## サードパーティ

| コンポーネント | 用途 | ライセンス |
|---|---|---|
| [SKK-JISYO.L](https://github.com/skk-dev/dict) | かな漢字変換辞書 | GPL-2.0 |
| [SKK-JISYO.emoji](https://github.com/skk-dev/dict) (加工済) | 絵文字辞書 | Unicode License |
| [MeCab](https://taku910.github.io/mecab/) (vcpkg) | 形態素解析 | GPL / LGPL / BSD |
| [UniDic-Lite](https://github.com/polm/unidic-lite) | 解析用辞書 | BSD |

## 開発について

このプロジェクトは、dhq-boiler が出したアイデアと方針判断を、Claude Code (Anthropic) がほぼ全てのコード実装・テスト・デバッグ・E2E 検証まで担って作られています。コミット履歴がそのままペアプログラミング (人間がナビゲーター、AI がドライバー) の記録になっています。
