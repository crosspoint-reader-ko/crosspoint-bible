#pragma once

class GfxRenderer;

// Boot and sleep screen: the black cover (src/images/BibleCover.h, drawn by
// tools/bible/make_cover.py) with "CrossPoint Bible" and an optional status line
// in white below the cross. Fills the whole screen; when the screen is shorter
// than the cover (landscape) the cover is left out and only the text is drawn.
void drawBibleCover(const GfxRenderer& renderer, const char* status);
