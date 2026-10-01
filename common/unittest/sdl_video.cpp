/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include <SDL3/SDL.h>
#include <array>
#include <memory>
#include <stdexcept>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth SDL video
#include <boost/test/unit_test.hpp>

namespace {

using surface_ptr = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
using window_ptr = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;

struct dummy_video
{
	dummy_video()
	{
		SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
		if (!SDL_Init(SDL_INIT_VIDEO))
			throw std::runtime_error(SDL_GetError());
	}
	~dummy_video()
	{
		SDL_Quit();
	}
};

}

// Exercise the software backend's SDL contracts without requiring game data
// or a display server. Odd widths expose assumptions that rows are contiguous.
BOOST_AUTO_TEST_CASE(indexed_palette_pitch_and_scaled_presentation)
{
	surface_ptr indexed{SDL_CreateSurface(321, 2, SDL_PIXELFORMAT_INDEX8), SDL_DestroySurface};
	BOOST_REQUIRE(indexed);
	const auto palette = SDL_CreateSurfacePalette(indexed.get());
	BOOST_REQUIRE(palette);
	BOOST_REQUIRE_GE(indexed->pitch, indexed->w);
	std::array<SDL_Color, 2> colors{{{0, 0, 0, SDL_ALPHA_OPAQUE}, {252, 16, 8, SDL_ALPHA_OPAQUE}}};
	BOOST_REQUIRE(SDL_SetPaletteColors(palette, colors.data(), 0, colors.size()));
	BOOST_REQUIRE(SDL_FillSurfaceRect(indexed.get(), nullptr, 0));
	auto pixels = static_cast<Uint8 *>(indexed->pixels);
	pixels[indexed->pitch + 320] = 1;
	surface_ptr output{SDL_CreateSurface(642, 4, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface};
	BOOST_REQUIRE(output);
	BOOST_REQUIRE(SDL_BlitSurfaceScaled(indexed.get(), nullptr, output.get(), nullptr, SDL_SCALEMODE_NEAREST));
	Uint8 r, g, b, a;
	BOOST_REQUIRE(SDL_ReadSurfacePixel(output.get(), 641, 3, &r, &g, &b, &a));
	BOOST_CHECK_EQUAL(r, 252);
	BOOST_CHECK_EQUAL(g, 16);
	BOOST_CHECK_EQUAL(b, 8);
	BOOST_CHECK_EQUAL(a, SDL_ALPHA_OPAQUE);
	BOOST_REQUIRE(SDL_ReadSurfacePixel(output.get(), 639, 3, &r, &g, &b, &a));
	BOOST_CHECK_EQUAL(r, 0);

	// Palette changes (including gamma/flash effects) must affect the next
	// presentation even when the bitmap bytes have not changed.
	colors[1] = {8, 16, 252, SDL_ALPHA_OPAQUE};
	BOOST_REQUIRE(SDL_SetPaletteColors(palette, colors.data(), 0, colors.size()));
	BOOST_REQUIRE(SDL_BlitSurfaceScaled(indexed.get(), nullptr, output.get(), nullptr, SDL_SCALEMODE_NEAREST));
	BOOST_REQUIRE(SDL_ReadSurfacePixel(output.get(), 641, 3, &r, &g, &b, &a));
	BOOST_CHECK_EQUAL(r, 8);
	BOOST_CHECK_EQUAL(b, 252);
}

BOOST_FIXTURE_TEST_CASE(window_surface_resize_fullscreen_and_text_input, dummy_video)
{
	window_ptr window{SDL_CreateWindow("SDL video test", 321, 200, 0), SDL_DestroyWindow};
	BOOST_REQUIRE(window);
	BOOST_REQUIRE(SDL_StartTextInput(window.get()));
	BOOST_CHECK(SDL_TextInputActive(window.get()));
	BOOST_REQUIRE(SDL_GetWindowSurface(window.get()));
	BOOST_REQUIRE(SDL_UpdateWindowSurface(window.get()));
	BOOST_REQUIRE(SDL_SetWindowSize(window.get(), 640, 480));
	BOOST_REQUIRE(SDL_SyncWindow(window.get()));
	const auto resized = SDL_GetWindowSurface(window.get());
	BOOST_REQUIRE(resized);
	BOOST_CHECK_EQUAL(resized->w, 640);
	BOOST_CHECK_EQUAL(resized->h, 480);
	BOOST_REQUIRE(SDL_UpdateWindowSurface(window.get()));
	BOOST_REQUIRE(SDL_SetWindowFullscreen(window.get(), true));
	BOOST_REQUIRE(SDL_SyncWindow(window.get()));
	BOOST_CHECK(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN);
	BOOST_REQUIRE(SDL_GetWindowSurface(window.get()));
	BOOST_REQUIRE(SDL_UpdateWindowSurface(window.get()));
	BOOST_REQUIRE(SDL_SetWindowFullscreen(window.get(), false));
	BOOST_REQUIRE(SDL_SyncWindow(window.get()));
	BOOST_CHECK(!(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN));
}
