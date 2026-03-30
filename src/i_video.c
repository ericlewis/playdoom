//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 3
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	DOOM graphics stuff for SDL.
//

#include "my_stdio.h"

#include "doomtype.h"
#include "i_input.h"
#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"
#include "m_argv.h"
#include "m_config.h"
#include "m_misc.h"
#include "tables.h"
#include "r_main.h"
#include "r_state.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

#include <stdlib.h>

#include "playdate.h"

// The screen buffer; this is modified to draw things to the screen
pixel_t *I_VideoBuffer = NULL;

// Pre-composed gray colormaps: gray_colormaps[colormap_offset + palette_index] = gray shade
uint8_t *gray_colormaps = NULL;

// Loaded font.
static LCDFont *font = NULL;

//
// I_StartFrame
//
void I_StartFrame (void)
{
}

//
// I_StartTic
//
void I_StartTic (void)
{
    I_ReadButtons();
    I_ReadMouse();
}


//
// I_UpdateNoBlit
//
void I_UpdateNoBlit (void)
{
    // what is this?
}

//
// I_FinishUpdate
//
void I_FinishUpdate (void)
{
    playdate->graphics->markUpdatedRows(0, LCD_ROWS - 1);
}


//
// I_ReadScreen
//
void I_ReadScreen (pixel_t* scr)
{
    memcpy(scr, I_VideoBuffer, SCREENSTRIDE*SCREENHEIGHT*sizeof(*scr));
}

// which of the 17 shades of gray for each color?
uint8_t graymap[256];

// 16x16 Blue Noise threshold matrix (values 0-255).
// Blue noise distributes thresholds more naturally than Bayer,
// eliminating visible crosshatch patterns on 1-bit displays.
// 4×4 Bayer threshold matrix (0-15). The correct match for DOOM's
// pre-quantized palette — 16 thresholds for 17 apparent levels.
// Smaller pattern = crisper crosshatch on the Playdate's sharp LCD.
static const uint8_t bayer4x4[4][4] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5},
};

// 17 shade levels × 4 rows = 68 bytes. Tiny, fits in one cache line pair.
#define NUM_SHADE_LEVELS 17
uint8_t shades[NUM_SHADE_LEVELS][4];

static void I_InitDitherTables(void) {
    for (int gray = 0; gray < NUM_SHADE_LEVELS; gray++) {
        for (int row = 0; row < 4; row++) {
            uint8_t pattern = 0;
            for (int col = 0; col < 4; col++) {
                // gray 0-16 compared to threshold 0-15.
                // gray 0 = all black, gray 16 = all white.
                if (gray > bayer4x4[row][col])
                    pattern |= (0x88 >> col);  // repeat 4-bit pattern across byte
            }
            shades[gray][row] = pattern;
        }
    }
}

// Low-quality shades: 17 levels with blue noise dithering (16 rows).
// (shades definition is above, near the top of file)


//
// I_SetPalette
//
void I_SetPalette (byte *doompalette)
{
    int i;
    byte *pal = doompalette;

    for (i=0; i<256; ++i)
    {
        uint8_t red = *pal++;
        uint8_t green = *pal++;
        uint8_t blue = *pal++;
        // Perceptual luminance (0.0 - 1.0)
        float lum = (red * 299 + green * 587 + blue * 114) / 255000.0f;

        // Single nonlinear remap: compresses midtones, expands
        // dark/bright separation. Then gain boost (DOOM is dark).
        int l = (int)(lum * 255.0f);
        l = (l * (l + 96)) >> 8;   // gentler nonlinear
        l = (l * 3) >> 1;          // 1.5x gain — DOOM is very dark
        if (l > 255) l = 255;
        if (l < 16) l = 0;         // soft floor: kill shadow speckle

        graymap[i] = (uint8_t)(l >> 4);  // 0-255 → 0-15
        if (graymap[i] > 16) graymap[i] = 16;
    }

    I_InitDitherTables();
    I_RebuildGrayColormaps();
}

void I_RebuildGrayColormaps(void)
{
    if (colormaps == NULL)
        return;

    // COLORMAP lump: 34 maps of 256 bytes each
    int num_maps = 34;
    int total = num_maps * 256;

    if (gray_colormaps == NULL)
        gray_colormaps = Z_Malloc(total, PU_STATIC, NULL);

    for (int i = 0; i < total; i++) {
        gray_colormaps[i] = graymap[colormaps[i]];
    }
}

void I_InitGraphics(void)
{
    byte *doompal;
    char *env;

    // Set the palette

    doompal = W_CacheLumpName("PLAYPAL", PU_CACHE);
    I_SetPalette(doompal);

    I_VideoBuffer = playdate->graphics->getFrame();
    V_RestoreBuffer();

    // Clear the screen to black.

    memset(I_VideoBuffer, 0, SCREENSTRIDE * SCREENHEIGHT);
}

// Load Playdate font.
void I_LoadFont(const char *path) {
    if (font != NULL) {
        playdate->system->realloc(font, 0);
    }
    font = playdate->graphics->loadFont(path, NULL);
    playdate->graphics->setFont(font);
}

// Draw centered text.
void I_DrawText(const char *text, int x, int y, int w) {
    int length = strlen(text);
    int width = playdate->graphics->getTextWidth(font, text, length, kUTF8Encoding, 0);
    playdate->graphics->drawText(text, length, kUTF8Encoding, x + (w - width) / 2, y);
}

// Draw centered line.
void I_DrawLine(const char *text, int y) {
    I_DrawText(text, 0, y, LCD_COLUMNS);
}
