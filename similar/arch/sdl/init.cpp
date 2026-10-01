/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
// Holds the main init and de-init functions for arch-related program parts

#include <SDL3/SDL.h>
#include "songs.h"
#include "key.h"
#include "digi.h"
#include "mouse.h"
#include "joy.h"
#include "gr.h"
#include "dxxerror.h"
#include "text.h"
#include "args.h"
#include "window.h"
#include "dxxsconf.h"

#if DXX_USE_SDLIMAGE
#include <SDL3_image/SDL_image.h>
#endif

namespace dsx {

static void arch_close(void)
{
	songs_uninit();

	gr_close();

#if DXX_MAX_JOYSTICKS
	if (!CGameArg.CtlNoJoystick)
	{
		joy_close();
		gamecontroller_close();
	}
#endif

	if (!CGameArg.CtlNoMouse)
		mouse_close();

	if (!CGameArg.SndNoSound)
	{
		digi_close();
	}
	SDL_Quit();
}

arch_atexit::~arch_atexit()
{
	arch_close();
}

arch_atexit arch_init()
{
	int t;

	if (!SDL_Init(SDL_INIT_VIDEO))
		Error("SDL library initialisation failed: %s.",SDL_GetError());
	/* Gameplay continues regardless of focus, so keep the window
	 * visible.
	 */
	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
	/* Support the Alt+Shift+F4 hotkey for renaming the Guide-Bot
	 */
	SDL_SetHint(SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4, "0");

	key_init();

	digi_select_system();

	if (!CGameArg.SndNoSound)
		digi_init();

	if (!CGameArg.CtlNoMouse)
		mouse_init();

#if DXX_MAX_JOYSTICKS
	if (!CGameArg.CtlNoJoystick)
	{
		joy_init();
		gamecontroller_init();
	}
#endif

	if ((t = gr_init()) != 0)
		Error(TXT_CANT_INIT_GFX,t);

	return {};
}

}
