// Trivial stand-ins for i_video.c/i_input.c's own exported symbol surface, for the OxideBSD
// backend (see doomgeneric_oxidebsd.c). Not compiled in place of those two files by choice --
// real i_video.c does its own direct Linux fbdev ioctls against <linux/fb.h>, a header this
// project's musl fork doesn't vendor (same "real Linux kernel uapi headers we don't have" gap
// CLAUDE.md documents for a chunk of the BusyBox roster), so it can't even compile against this
// target's sysroot. Real video/input already happens in doomgeneric_oxidebsd.c's own DG_* + main()
// -- everything here is either a real no-op (nothing on OxideBSD needs it) or plain storage for a
// config variable m_config.c still binds, matching this project's own "stub what has no backing
// hardware/concept" precedent (getrusage, chrt, DG_SetWindowTitle's own doc comment).
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "doomgeneric.h"
#include "doomtype.h"
#include "i_video.h"
#include "tables.h" // gammatable[][], real upstream's own I_SetPalette gamma lookup.

// Real palette, gamma-corrected RGBA -- populated by I_SetPalette exactly like real upstream
// i_video.c's own `colors[256]` does (not compiled here -- see this file's own module doc
// comment), consumed by I_FinishUpdate below to do the one real conversion step doomgeneric's own
// architecture actually needs: real upstream's classic 8-bit-paletted I_VideoBuffer (320x200, the
// entire engine's real software-rendered output) into DG_ScreenBuffer's own 32-bit RGBA space
// (640x400 by default -- exactly 2x each dimension), which doomgeneric_oxidebsd.c's own
// DG_DrawFrame then blits again, up to whatever real resolution this boot's framebuffer reports.
// Skipping this step (an earlier version of this file did, before this bug was found and fixed)
// left DG_ScreenBuffer permanently all-zero -- Doom's own real per-tic rendering never had
// anywhere to land.
static uint32_t colors[256];

char *video_driver = "";
boolean screenvisible = true;
int usemouse = 0;
float mouse_acceleration = 2.0f;
int mouse_threshold = 10;
int vanilla_keyboard_mapping = 1;
boolean screensaver_mode = false;
int usegamma = 0;
// Real vanilla Doom's own classic 8-bit indexed-color software-render target -- v_video.c's own
// drawing primitives (patch/column/etc. blitting) write directly into this before doomgeneric's
// own separate DG_ScreenBuffer conversion step; a real, zeroed allocation (not NULL) since it's a
// genuine write target, not just a declared-but-unused extern.
byte *I_VideoBuffer = NULL;

int screen_width = SCREENWIDTH;
int screen_height = SCREENHEIGHT;
int screen_bpp = 32;
int fullscreen = 1;
int aspect_ratio_correct = 1;

int show_diskicon = 0;
int diskicon_readbytes = 0;

void I_InitGraphics(void)
{
	I_VideoBuffer = calloc(1, (size_t)SCREENWIDTH * SCREENHEIGHT);
}

void I_GraphicsCheckCommandLine(void) {}

void I_ShutdownGraphics(void) {}

void I_SetPalette(byte *palette)
{
	// Matches real upstream i_video.c's own I_SetPalette exactly (gamma-corrected r/g/b, alpha
	// left 0) -- see this file's own module doc comment.
	for (int i = 0; i < 256; i++) {
		uint8_t r = gammatable[usegamma][*palette++];
		uint8_t g = gammatable[usegamma][*palette++];
		uint8_t b = gammatable[usegamma][*palette++];
		colors[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
	}
}

int I_GetPaletteIndex(int r, int g, int b)
{
	// Real vanilla Doom looks this up against the actual loaded PLAYPAL lump; doomgeneric's own
	// separate DG_ScreenBuffer path (this port's real rendering output) never consults this --
	// only a handful of UI-border/background color lookups in v_video.c do, and those only
	// affect content this port's own DG_DrawFrame blit already overwrites in full every frame.
	// A cheap, deterministic packing (not a real palette match) is a harmless placeholder.
	return ((r & 0xff) << 16) | ((g & 0xff) << 8) | (b & 0xff);
}

void I_UpdateNoBlit(void) {}

void I_FinishUpdate(void)
{
	// Real 2x pixel-doubling conversion from I_VideoBuffer (320x200, 1 byte/pixel palette index)
	// into DG_ScreenBuffer (640x400 by default, 4 bytes/pixel RGBA) -- see this file's own module
	// doc comment for why this exists at all.
	for (int y = 0; y < SCREENHEIGHT; y++) {
		const byte *line_in = I_VideoBuffer + (size_t)y * SCREENWIDTH;
		uint32_t *out0 = DG_ScreenBuffer + (size_t)(y * 2) * DOOMGENERIC_RESX;
		uint32_t *out1 = DG_ScreenBuffer + (size_t)(y * 2 + 1) * DOOMGENERIC_RESX;
		for (int x = 0; x < SCREENWIDTH; x++) {
			uint32_t pixel = colors[line_in[x]];
			out0[x * 2] = pixel;
			out0[x * 2 + 1] = pixel;
			out1[x * 2] = pixel;
			out1[x * 2 + 1] = pixel;
		}
	}
	DG_DrawFrame();
}

void I_ReadScreen(byte *scr)
{
	if (I_VideoBuffer) {
		memcpy(scr, I_VideoBuffer, (size_t)SCREENWIDTH * SCREENHEIGHT);
	}
}

void I_BeginRead(void) {}

void I_EndRead(void) {}

void I_SetWindowTitle(char *title)
{
	DG_SetWindowTitle(title);
}

void I_CheckIsScreensaver(void) {}

void I_SetGrabMouseCallback(grabmouse_callback_t func)
{
	(void)func;
}

void I_DisplayFPSDots(boolean dots_on)
{
	(void)dots_on;
}

void I_BindVideoVariables(void) {}

void I_InitWindowTitle(void) {}

void I_InitWindowIcon(void) {}

void I_StartFrame(void) {}

void I_StartTic(void) {}

void I_EnableLoadingDisk(void) {}
