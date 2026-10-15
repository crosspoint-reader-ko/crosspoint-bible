#pragma once

// Korean build ships only KoPub Batang (reader) + Pretendard (UI) to stay within the
// 6.25 MB OTA size budget. Upstream's NotoSerif/NotoSans/Ubuntu built-ins are intentionally
// NOT included here; src/main.cpp loads only the fonts below. The NOTOSERIF_*/NOTOSANS_*
// font IDs in fontIds.h remain defined so upstream code compiles, but their font data is absent.

// UI fonts (Pretendard 10pt) - Regular only, synthetic bold used when needed
#include <builtinFonts/pretendard_10_regular.h>

// CrossPoint Bible reader fonts (KoPub Batang Medium, KS X 1001 Hangul, compressed).
// lib/EpdFont/scripts/convert-bible-fonts.sh
#include <builtinFonts/bible_14_regular.h>
#include <builtinFonts/bible_16_regular.h>
#include <builtinFonts/bible_18_regular.h>
#include <builtinFonts/bible_22_regular.h>
#include <builtinFonts/bible_28_regular.h>
