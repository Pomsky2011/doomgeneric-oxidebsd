// doomgeneric backend for OxideBSD (https://github.com/Pomsky2011/OxideBSD).
//
// Real video: opens /dev/fb0, queries geometry via the FBIOGET_OXIDEBSD ioctl, and mmap()s the
// real console framebuffer directly (process::mm::do_mmap_fb, kernel tree) -- writes through the
// mapped pointer land straight on real physical VRAM, no intermediate blit step on the kernel
// side. DG_DrawFrame does its own nearest-neighbor integer-scale blit from doomgeneric's internal
// DOOMGENERIC_RESX x DOOMGENERIC_RESY RGBA8888 buffer up to whatever real resolution Limine
// negotiated this boot (never assumed fixed).
//
// Real input: OxideBSD's own SYS_GET_KEYEVENT (558) -- a non-blocking raw (keycode, pressed) pair
// in this kernel's own small stable keycode space (see src/console/keyevents.rs in the kernel
// tree), translated below to doomkeys.h's own space. Deliberately not evdev/Linux input-event-
// codes -- OxideBSD has no such subsystem, and doesn't need one for this.
//
// Modeled on doomgeneric_linuxvt.c's own shape (a real main() driving doomgeneric_Create() then
// looping doomgeneric_Tick() forever), but talks to OxideBSD's own syscalls throughout, not real
// Linux fbdev/evdev.
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "doomgeneric.h"
#include "doomkeys.h"
#include "m_argv.h"

// Must match src/syscall/ffi.rs's own FBIOGET_OXIDEBSD/RawFbInfo, and src/console/keyevents.rs's
// own RawKeyEvent -- no shared header across this ABI boundary, same convention every other
// userland/kernel wire-struct pair in this project uses.
#define FBIOGET_OXIDEBSD 0x4600
#define SYS_GET_KEYEVENT 558

struct fb_info {
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t bpp;
};

struct raw_key_event {
	unsigned char keycode;
	unsigned char pressed;
};

static int fb_fd = -1;
static struct fb_info fb_info;
static uint32_t *fb_pixels = NULL;

void DG_Init(void)
{
	fb_fd = open("/dev/fb0", O_RDWR);
	if (fb_fd < 0) {
		fprintf(stderr, "doomgeneric_oxidebsd: open(/dev/fb0) failed\n");
		exit(1);
	}
	if (ioctl(fb_fd, FBIOGET_OXIDEBSD, &fb_info) != 0) {
		fprintf(stderr, "doomgeneric_oxidebsd: FBIOGET_OXIDEBSD ioctl failed\n");
		exit(1);
	}
	if (fb_info.bpp != 32) {
		fprintf(stderr, "doomgeneric_oxidebsd: only 32bpp framebuffers are supported\n");
		exit(1);
	}
	size_t len = (size_t)fb_info.pitch * (size_t)fb_info.height;
	void *mapped = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
	if (mapped == MAP_FAILED) {
		fprintf(stderr, "doomgeneric_oxidebsd: mmap(/dev/fb0) failed\n");
		exit(1);
	}
	fb_pixels = (uint32_t *)mapped;
}

// Nearest-neighbor integer-scale blit from doomgeneric's internal DOOMGENERIC_RESX x
// DOOMGENERIC_RESY RGBA8888 buffer up to the real, runtime-queried framebuffer resolution.
// Assumes a real XRGB8888/ARGB8888-shaped 32bpp layout (matches DG_ScreenBuffer's own byte order
// directly with no repacking) -- a reasonable simplification for a first port (real QEMU/OVMF
// stdvga reports exactly this); a future pass could consult the ioctl's own mask/shift fields the
// way the kernel's own console::framebuffer::pack_rgb does, if a real target ever needs it.
void DG_DrawFrame(void)
{
	uint32_t out_w = fb_info.width;
	uint32_t out_h = fb_info.height;
	uint32_t pitch_pixels = fb_info.pitch / 4;

	for (uint32_t y = 0; y < out_h; y++) {
		uint32_t src_y = (y * DOOMGENERIC_RESY) / out_h;
		uint32_t *row = fb_pixels + (size_t)y * pitch_pixels;
		uint32_t *src_row = DG_ScreenBuffer + (size_t)src_y * DOOMGENERIC_RESX;
		for (uint32_t x = 0; x < out_w; x++) {
			uint32_t src_x = (x * DOOMGENERIC_RESX) / out_w;
			row[x] = src_row[src_x];
		}
	}
}

void DG_SleepMs(uint32_t ms)
{
	usleep((useconds_t)ms * 1000);
}

uint32_t DG_GetTicksMs(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// This kernel's own stable keycode space (src/console/keyevents.rs, kernel tree) -- must match
// exactly.
#define OX_KEY_UP        0x80
#define OX_KEY_DOWN      0x81
#define OX_KEY_LEFT      0x82
#define OX_KEY_RIGHT     0x83
#define OX_KEY_ENTER     0x84
#define OX_KEY_ESCAPE    0x85
#define OX_KEY_TAB       0x86
#define OX_KEY_BACKSPACE 0x87
#define OX_KEY_LCTRL     0x88
#define OX_KEY_RCTRL     0x89
#define OX_KEY_LALT      0x8a
#define OX_KEY_RALT      0x8b
#define OX_KEY_LSHIFT    0x8c
#define OX_KEY_RSHIFT    0x8d

// Translates this kernel's own keycode space to doomkeys.h's -- ASCII letters/digits/space need
// no translation (both spaces use plain ASCII for those). Ctrl -> fire, Alt -> the (default)
// strafe-modifier key, Shift -> run, matching vanilla Doom's own default keyboard bindings.
static unsigned char translate_key(unsigned char keycode)
{
	switch (keycode) {
	case OX_KEY_UP: return KEY_UPARROW;
	case OX_KEY_DOWN: return KEY_DOWNARROW;
	case OX_KEY_LEFT: return KEY_LEFTARROW;
	case OX_KEY_RIGHT: return KEY_RIGHTARROW;
	case OX_KEY_ENTER: return KEY_ENTER;
	case OX_KEY_ESCAPE: return KEY_ESCAPE;
	case OX_KEY_TAB: return KEY_TAB;
	case OX_KEY_BACKSPACE: return KEY_BACKSPACE;
	case OX_KEY_LCTRL: return KEY_FIRE;
	case OX_KEY_RCTRL: return KEY_FIRE;
	case OX_KEY_LALT: return KEY_RALT;
	case OX_KEY_RALT: return KEY_RALT;
	case OX_KEY_LSHIFT: return KEY_RSHIFT;
	case OX_KEY_RSHIFT: return KEY_RSHIFT;
	default: return keycode; // ASCII passthrough for letters/digits/space/punctuation.
	}
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
	struct raw_key_event ev;
	long n = syscall(SYS_GET_KEYEVENT, &ev);
	if (n <= 0) {
		return 0;
	}
	*pressed = ev.pressed;
	*doomKey = translate_key(ev.keycode);
	return 1;
}

void DG_SetWindowTitle(const char *title)
{
	// No window/title-bar concept exists on OxideBSD's console -- no-op, matching this
	// project's own "stub what has no backing concept" precedent (e.g. getrusage, chrt).
	(void)title;
}

int main(int argc, char **argv)
{
	doomgeneric_Create(argc, argv);

	for (;;) {
		doomgeneric_Tick();
	}

	return 0;
}
