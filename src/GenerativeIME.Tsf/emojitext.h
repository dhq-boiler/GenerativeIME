#pragma once

#include <string_view>

// Classifies whether a candidate string will render as (color) emoji.
// Used by the candidate window to tag emoji rows with an "(emoji)"
// annotation — a 16px glyph cell can be hard to identify at a glance
// (❕ vs ❗, 🕐 vs 🕑), and the annotation also flags that the committed
// character may render differently across apps/fonts.
namespace emojitext
{
    bool IsEmoji(std::wstring_view s);
}
