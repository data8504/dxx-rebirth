/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 *
 * SDL video functions.
 *
 */

#include <algorithm>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <SDL3/SDL.h>
#include "gr.h"
#include "grdef.h"
#include "palette.h"
#include "game.h"
#include "console.h"
#include "u_mem.h"
#include "dxxerror.h"
#include "vers_id.h"
#include "gamefont.h"
#include "args.h"
#include "config.h"
#include "palette.h"

#include "compiler-range_for.h"
#include "d_range.h"
#include <memory>

using std::min;

namespace dcx {

SDL_Window *g_pRebirthSDLMainWindow;
static SDL_Surface *canvas;
static SDL_WindowFlags sdl_video_flags;
static int gr_installed;

void gr_flip()
{
	// The window surface can change when entering fullscreen or resizing.
	const auto screen = SDL_GetWindowSurface(g_pRebirthSDLMainWindow);
	if (!screen || !SDL_BlitSurfaceScaled(canvas, nullptr, screen, nullptr, SDL_SCALEMODE_NEAREST) ||
		!SDL_UpdateWindowSurface(g_pRebirthSDLMainWindow))
		Error("Could not update software display: %s", SDL_GetError());
}

}

namespace dsx {

void gr_set_mode_from_window_size()
{
	// Software presentation scales the fixed-resolution indexed canvas to
	// the current window surface in gr_flip; output-size changes need no
	// canvas reallocation.
}

int gr_set_mode(screen_mode mode)
{
	const unsigned w = SM_W(mode), h = SM_H(mode);
	if (!g_pRebirthSDLMainWindow)
	{
		g_pRebirthSDLMainWindow = SDL_CreateWindow(DESCENT_VERSION, w, h, sdl_video_flags);
		if (!g_pRebirthSDLMainWindow)
			Error("Could not create software display: %s", SDL_GetError());
		SDL_StartTextInput(g_pRebirthSDLMainWindow);
		if (const auto icon = SDL_LoadBMP(DXX_SDL_WINDOW_ICON_BITMAP))
		{
			SDL_SetWindowIcon(g_pRebirthSDLMainWindow, icon);
			SDL_DestroySurface(icon);
		}
		SDL_SetWindowSurfaceVSync(g_pRebirthSDLMainWindow, CGameCfg.VSync ? 1 : 0);
	}
	else if (!(SDL_GetWindowFlags(g_pRebirthSDLMainWindow) & SDL_WINDOW_FULLSCREEN))
	{
		if (!SDL_SetWindowSize(g_pRebirthSDLMainWindow, w, h))
			Error("Could not resize software display: %s", SDL_GetError());
		SDL_SyncWindow(g_pRebirthSDLMainWindow);
	}

	const auto new_canvas = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_INDEX8);
	if (!new_canvas || !SDL_CreateSurfacePalette(new_canvas))
		Error("Could not create indexed canvas: %s", SDL_GetError());
	*grd_curscreen = {};
	canvas = new_canvas;
	grd_curscreen->sdl_surface = RAII_SDL_Surface(canvas);
	grd_curscreen->set_screen_width_height(w, h);
	grd_curscreen->sc_aspect = fixdiv(grd_curscreen->get_screen_width() * CGameCfg.AspectX, grd_curscreen->get_screen_height() * CGameCfg.AspectY);
	gr_init_canvas(grd_curscreen->sc_canvas, reinterpret_cast<unsigned char *>(canvas->pixels), bm_mode::linear, w, h);
	grd_curscreen->sc_canvas.cv_bitmap.bm_rowsize = canvas->pitch;
	window_update_canvases();
	gr_set_default_canvas();

	SDL_HideCursor();
	gamefont_choose_game_font(w,h);
	gr_palette_load(gr_palette);
	gr_remap_color_fonts();

	return 0;
}

}

namespace dcx {

int gr_check_fullscreen(void)
{
	return g_pRebirthSDLMainWindow && (SDL_GetWindowFlags(g_pRebirthSDLMainWindow) & SDL_WINDOW_FULLSCREEN);
}

void gr_toggle_fullscreen()
{
	const auto fullscreen = gr_check_fullscreen();
	if (!SDL_SetWindowFullscreen(g_pRebirthSDLMainWindow, !fullscreen))
	{
		con_printf(CON_URGENT, "Could not change fullscreen mode: %s", SDL_GetError());
		return;
	}
	SDL_SyncWindow(g_pRebirthSDLMainWindow);
	CGameCfg.WindowMode = fullscreen;
	gr_remap_color_fonts();
}

}

namespace dsx {

int gr_init()
{
	// Only do this function once!
	if (gr_installed==1)
		return -1;

	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		Error("SDL library video initialisation failed: %s.",SDL_GetError());
	}

	grd_curscreen = std::make_unique<grs_screen>();

	if (!CGameCfg.WindowMode && !CGameArg.SysWindow)
		sdl_video_flags|=SDL_WINDOW_FULLSCREEN;

	if (CGameArg.SysNoBorders)
		sdl_video_flags|=SDL_WINDOW_BORDERLESS;

	// Set the mode.
	grd_curscreen->sc_canvas.cv_fade_level = GR_FADE_OFF;
	grd_curscreen->sc_canvas.cv_font = NULL;
	grd_curscreen->sc_canvas.cv_font_fg_color = 0;
	grd_curscreen->sc_canvas.cv_font_bg_color = 0;
	gr_set_current_canvas(grd_curscreen->sc_canvas);

	gr_installed = 1;

	return 0;
}

void gr_close()
{
	if (gr_installed==1)
	{
		gr_installed = 0;
		grd_curscreen.reset();
		grd_curcanv = nullptr;
		canvas = nullptr;
		SDL_DestroyWindow(g_pRebirthSDLMainWindow);
		g_pRebirthSDLMainWindow = nullptr;
		SDL_ShowCursor();
	}
}

}

namespace dcx {

// Palette functions follow.
static int last_r=0, last_g=0, last_b=0;

void gr_palette_step_up( int r, int g, int b )
{
	palette_array_t &p = gr_palette;
	SDL_Palette *palette;

	if ( (r==last_r) && (g==last_g) && (b==last_b) )
		return;

	last_r = r;
	last_g = g;
	last_b = b;

	palette = SDL_GetSurfacePalette(canvas);

	if (palette == NULL)
		return; // Display is not palettised

	std::array<SDL_Color, 256> colors{};
	range_for (const int i, xrange(256u))
	{
		const auto ir = static_cast<int>(p[i].r) + r + gr_palette_gamma;
		colors[i].r = std::clamp(ir, 0, 63) * 4;
		const auto ig = static_cast<int>(p[i].g) + g + gr_palette_gamma;
		colors[i].g = std::clamp(ig, 0, 63) * 4;
		const auto ib = static_cast<int>(p[i].b) + b + gr_palette_gamma;
		colors[i].b = std::clamp(ib, 0, 63) * 4;
		colors[i].a = SDL_ALPHA_OPAQUE;
	}
	SDL_SetPaletteColors(palette, colors.data(), 0, colors.size());
}

void gr_palette_load(const palette_array_t &pal)
{
	SDL_Palette *palette;
	std::array<uint8_t, 64> gamma;

	if (canvas && pal != gr_current_pal)
		SDL_FillSurfaceRect(canvas, nullptr, 0);

	copy_bound_palette(gr_current_pal, pal);

	if (canvas == NULL)
		return;

	palette = SDL_GetSurfacePalette(canvas);

	if (palette == NULL)
		return; // Display is not palettised

	range_for (const int i, xrange(64u))
		gamma[i] = static_cast<int>((pow((14.0 / 32.0), 1.0) * i) + 0.5);

	std::array<SDL_Color, 256> colors{};
	for (int i = 0, j = 0; j < 256; j++)
	{
		const auto c = gr_find_closest_color(gamma[gr_palette[j].r], gamma[gr_palette[j].g], gamma[gr_palette[j].b]);
		gr_fade_table[(gr_fade_level{14})][j] = c;
		colors[j].r = (min(gr_current_pal[i].r + gr_palette_gamma, 63)) * 4;
		colors[j].g = (min(gr_current_pal[i].g + gr_palette_gamma, 63)) * 4;
		colors[j].b = (min(gr_current_pal[i].b + gr_palette_gamma, 63)) * 4;
		colors[j].a = SDL_ALPHA_OPAQUE;
		i++;
	}

	SDL_SetPaletteColors(palette, colors.data(), 0, colors.size());
	reset_computed_colors();
	gr_remap_color_fonts();
}

void gr_palette_read(palette_array_t &pal)
{
	SDL_Palette *palette;
	unsigned i;

	palette = SDL_GetSurfacePalette(canvas);

	if (palette == NULL)
		return; // Display is not palettised

	for (i = 0; i < 256; i++)
	{
		pal[i].r = palette->colors[i].r / 4;
		pal[i].g = palette->colors[i].g / 4;
		pal[i].b = palette->colors[i].b / 4;
	}
}

}
