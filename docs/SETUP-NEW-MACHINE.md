# New Machine Setup

GenerativeIME を別マシンで開発再開するための手順。

## 1. 前提ツール

- **Windows 10/11 64bit**
- **Visual Studio 2026 Community v18** (C++ workload + Windows SDK 10.0.28000 以降)
  - 標準パス: `C:\Program Files\Microsoft Visual Studio\18\Community\`
  - vcxproj の `<PlatformToolset>` は **v145** (cl 14.50.x)。v143 (VS 2022) や v180 ではビルド不可。
- **Git for Windows** (もしくは PowerShell 経由で `git`)
- **GitHub CLI (`gh`)** — `gh auth login` 済み (private repo を pull するため)
- **Python 3.10+** + `pip` (UniDic-Lite 辞書取得用、PATH に通っていること)
- **Ollama** (`gemma4:12b` モデル pull 済み) — **必須**
  - https://ollama.com/download/windows から OllamaSetup.exe
  - `ollama pull gemma4:12b`
  - 推奨: `keep_alive='30m'` で warmup しておく
  - 未起動時の挙動: 文脈考慮 reorder と #13 fallback が無効化されるだけで、SKK+MeCab パスは動く（候補が変わらないだけ）
- **Claude Code CLI** (`claude` コマンド) — セッション復元に使う

## 2. リポジトリの clone

```powershell
cd C:\Git
gh repo clone dhq-boiler/GenerativeIME  # or git clone <SSH URL>
cd GenerativeIME
```

clone 直後の構成:
- `src/` — C++ + C# のソース
- `third_party/skk/SKK-JISYO.L.utf8` — SKK 辞書 (6 MB、commit 済)
- `third_party/mecab/unidic-lite/` — **存在しない** (gitignore、次の手順で取得)
- `docs/SESSION-STATE.md` — Claude セッション状態スナップショット

## 3. vcpkg + MeCab セットアップ

```powershell
# vcpkg を C:\vcpkg に置く前提 (vcxproj の VcpkgRoot プロパティのデフォルト)
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
& 'C:\vcpkg\bootstrap-vcpkg.bat' -disableMetrics
& 'C:\vcpkg\vcpkg.exe' install mecab:x64-windows
```

完了すると:
- `C:\vcpkg\installed\x64-windows\include\mecab\mecab.h`
- `C:\vcpkg\installed\x64-windows\bin\mecab.dll` (Release)
- `C:\vcpkg\installed\x64-windows\debug\bin\mecab.dll` (Debug、TSF Debug ビルドで使う)
- `C:\vcpkg\installed\x64-windows\lib\mecab.lib` / `debug\lib\mecab.lib`

別パスに置く場合は `vcxproj` の `<VcpkgRoot>` を直接編集するか、環境変数 `VcpkgRoot` をセット。

## 4. UniDic-Lite 辞書取得

```powershell
python -m pip install unidic-lite ipadic mecab-python3
.\scripts\setup-mecab-dict.ps1
```

スクリプトが `unidic_lite.DICDIR` を見つけて `third_party/mecab/unidic-lite/` にコピーしてくれる (約 248 MB)。

### Python が入っていないマシン向けの代替手順

```powershell
$url = (Invoke-RestMethod -Uri 'https://pypi.org/pypi/unidic-lite/json').urls |
        Where-Object { $_.packagetype -eq 'sdist' } | Select-Object -First 1 -ExpandProperty url
$tgz = 'C:\Git\GenerativeIME\third_party\mecab\unidic-lite.tar.gz'
Invoke-WebRequest -Uri $url -OutFile $tgz -UseBasicParsing
$tmp = "$env:TEMP\unidic-extract-$(Get-Random)"
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
tar -xzf $tgz -C $tmp
$src = Get-ChildItem $tmp -Recurse -Directory -Filter dicdir | Select-Object -First 1
Move-Item -Path $src.FullName -Destination 'C:\Git\GenerativeIME\third_party\mecab\unidic-lite' -Force
Remove-Item -Recurse -Force $tmp; Remove-Item -Force $tgz
```

PyPI から sdist (tarball) を直接落として `unidic_lite/dicdir/` を `third_party/mecab/unidic-lite/` に移動する。Python / pip 不要。

## 5. Ollama warmup

```powershell
Invoke-RestMethod -Uri http://127.0.0.1:11434/api/generate `
  -Method Post -ContentType 'application/json' `
  -Body (@{model='gemma4:12b';prompt='warmup';stream=$false;keep_alive='30m';think=$false} | ConvertTo-Json)
```

## 6. ビルド

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe' `
  'C:\Git\GenerativeIME\src\GenerativeIME.Tsf\GenerativeIME.Tsf.vcxproj' `
  /p:Configuration=Debug /p:Platform=x64
```

成功すると `src\GenerativeIME.Tsf\build\x64\Debug\` に:
- `GenerativeIME.Tsf.dll`
- `SKK-JISYO.L.utf8`
- `mecab.dll` (Debug 版)
- `unidic-lite\` 配下に辞書一式

post-build event が自動でステージしている。

## 7. IME 登録

```powershell
regsvr32 'C:\Git\GenerativeIME\src\GenerativeIME.Tsf\build\x64\Debug\GenerativeIME.Tsf.dll'
```

その後 **再ログイン** で TSF カテゴリ反映 → 言語バーに切替可能。

## 8. Claude Code でセッション復元

```powershell
cd C:\Git\GenerativeIME
claude   # Claude Code CLI 起動
```

中で:

```
/load-session
```

`~/.claude/saved-sessions/` を探しに行くが、ここに無いので「直接 `docs/SESSION-STATE.md` を読んでくれ」と指示する形になる。例えば:

```
docs/SESSION-STATE.md を読んで、そこに書かれている状態から作業を再開して
```

Claude が SESSION-STATE.md を読み、タスクリスト・進捗・次のステップを復元する。

## 9. 開発時の小ネタ

- **DLL ロックエラー**:
  ```powershell
  $dll = 'C:\Git\GenerativeIME\src\GenerativeIME.Tsf\build\x64\Debug\GenerativeIME.Tsf.dll'
  Move-Item -Path $dll -Destination "$dll.locked.$(Get-Random)" -Force
  ```
- **DebugView** (Sysinternals) で `[GenerativeIME]` で grep して動作確認

## 10. アンインストール

```powershell
regsvr32 /u 'C:\Git\GenerativeIME\src\GenerativeIME.Tsf\build\x64\Debug\GenerativeIME.Tsf.dll'
```
