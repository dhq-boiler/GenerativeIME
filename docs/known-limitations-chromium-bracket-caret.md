# Known Limitation: Chromium contenteditable + bracket-pair caret

**Status**: unresolved. TSF-internal fix not feasible.
**Affected hosts**: `<div contenteditable>` in Chromium-family browsers
(Edge, Chrome, Opera, etc.). `<input type="text">` and `<textarea>` are NOT
affected — DoEnd's double-SetSelection lands the caret correctly there.

---

## Symptom

Type `[` + SPACE + ENTER (commits `「」`), then `a` + ENTER: the `あ` lands
AFTER the closing bracket instead of between the pair.

Expected: `「あ」`
Actual on ce-div: `「」あ`

Confirmed via WDAC E2E (`caret-test.html#ce-div`) and local Edge + notepad
E2E on 2026-07-12.

## Root cause

Chromium's `<div contenteditable>` runs a separate `dom.selection` state
inside Blink's Editing pipeline. After our TSF `EndComposition` fires,
Blink's `compositionend` cleanup runs on the JS event-loop tick and resets
`dom.selection` to the END of the just-committed range (past `」`).

Our TSF `SetSelection` updates TSF's view of the caret position, but Blink
does NOT sync TSF-side selection back into `dom.selection` after this reset.
The next composition uses `dom.selection`, so it starts past `」` regardless
of where TSF thinks the caret is.

## What we tried (2026-07-12 session)

**Option A** — deferred TSF SetSelection via `SetTimer(N ms)` past the JS tick.
Reproduces the fire-order pattern below:

1. `EndComposition` fires
2. TSF `SetSelection` sets caret to pos 1 (correct)
3. Blink's JS `compositionend` handler runs
4. Blink resets `dom.selection` to pos 2 (past `」`)
5. Our SetTimer fires N ms later, runs `CSetCaretSession`
6. `CSetCaretSession` reads current TSF selection → still shows pos 1 (`「`
   at cursor-1)
7. Guard concludes "already correct", no-ops

Verified via `caret-debug.log` on Chromium Edge, delay = 200 ms:

```
CSetCaretSession::DoEditSession enter
CSetCaretSession: needFix=0 offset=1 expectedTail=U+300D
```

**needFix=0** means the guard's probe read `「` at cursor-1, not `」`. That
confirms TSF's internal caret IS at pos 1 (correct), but Blink's `dom.selection`
is at pos 2 anyway, and the next composition inserts there.

No delay length fixes this — the issue is a data-flow gap, not a timing gap.
Committed as da7be9b then reverted as 8e7378f after this investigation.

## What would fix this (all out of scope for a TSF DLL)

- **Browser extension** injecting `window.getSelection().collapse(node, 1)`
  after commit. Requires a new companion extension.
- **CDP** (Chrome DevTools Protocol) via WebSocket. Requires Chromium to be
  launched with `--remote-debugging-port` and mutual TCP trust — not
  suitable for shipping.
- **Blink patch** exposing a `TF_ATTR_*` that Chromium honors for cursor
  hints. Would need upstream cooperation.
- **Composition-anchored insertion** — instead of committing `「」` as
  concrete text, keep the composition open with `「」` displayed and
  cursor between the pair. Next keystroke extends the composition rather
  than starting a new one. Big architectural change; also breaks common
  IME UX expectations (users expect ENTER to commit).

## Impact estimate

Users typing `「」` pairs in Chromium contenteditable fields (X.com,
Slack web, some CMS editors) will see the caret land outside the pair.
Workaround: type `「` `」` explicitly, then click between them, OR use the
IME's F4 repeat-paste after the first correct bracketed entry, OR type in
`<input>`/`<textarea>` hosts where the fix works.

Not a data-loss bug; purely a caret positioning inconvenience.

## Related

- `docs/e2e/caret-test.html` — WDAC / local E2E harness
- Commits: 3b9a416 (initial fix, `<input>` fixed), cfca8d0 (partial revert,
  ce-div deferred), da7be9b (Option A attempt), 8e7378f (Option A revert)
- Saved sessions:
  `~/.claude/saved-sessions/C--Git-GenerativeIME__2026-07-12-141551__*.md`
