/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 *
 * SDL keyboard input support
 *
 *
 */

#include "dxxsconf.h"
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <ranges>

#include <SDL3/SDL.h>
#include <SDL3/SDL_version.h>
#include <SDL3/SDL_keycode.h>

#include "event.h"
#include "dxxerror.h"
#include "key.h"
#include "timer.h"
#include "window.h"
#include "console.h"
#include "args.h"

#include "dsx-ns.h"
#include "compiler-range_for.h"
#include <array>

namespace dcx {

//-------- Variable accessed by outside functions ---------
static bool keyd_repeat; // 1 = use repeats, 0 no repeats
pressed_keys keyd_pressed;
fix64			keyd_time_when_last_pressed;
std::array<unsigned char, KEY_BUFFER_SIZE>		unicode_frame_buffer;
static std::array<unsigned char, KEY_BUFFER_SIZE> unibuffer;

constexpr std::array<key_props, 256> key_properties = {{
{ "",       255,    SDLK_UNKNOWN                 }, // 0
{ "ESC",    255,    SDLK_ESCAPE        },
{ "1",      '1',    SDLK_1             },
{ "2",      '2',    SDLK_2             },
{ "3",      '3',    SDLK_3             },
{ "4",      '4',    SDLK_4             },
{ "5",      '5',    SDLK_5             },
{ "6",      '6',    SDLK_6             },
{ "7",      '7',    SDLK_7             },
{ "8",      '8',    SDLK_8             },
{ "9",      '9',    SDLK_9             }, // 10
{ "0",      '0',    SDLK_0             },
{ "-",      '-',    SDLK_MINUS         },
{ "=",      '=',    SDLK_EQUALS        },
{ "BSPC",   255,    SDLK_BACKSPACE     },
{ "TAB",    255,    SDLK_TAB           },
{ "Q",      'q',    SDLK_Q             },
{ "W",      'w',    SDLK_W             },
{ "E",      'e',    SDLK_E             },
{ "R",      'r',    SDLK_R             },
{ "T",      't',    SDLK_T             }, // 20
{ "Y",      'y',    SDLK_Y             },
{ "U",      'u',    SDLK_U             },
{ "I",      'i',    SDLK_I             },
{ "O",      'o',    SDLK_O             },
{ "P",      'p',    SDLK_P             },
{ "[",      '[',    SDLK_LEFTBRACKET   },
{ "]",      ']',    SDLK_RIGHTBRACKET  },
{ "ENTER",  255,    SDLK_RETURN        },
{ "LCTRL",  255,    SDLK_LCTRL         },
{ "A",      'a',    SDLK_A             }, // 30
{ "S",      's',    SDLK_S             },
{ "D",      'd',    SDLK_D             },
{ "F",      'f',    SDLK_F             },
{ "G",      'g',    SDLK_G             },
{ "H",      'h',    SDLK_H             },
{ "J",      'j',    SDLK_J             },
{ "K",      'k',    SDLK_K             },
{ "L",      'l',    SDLK_L             },
{ ";",      ';',    SDLK_SEMICOLON     },
{ "'",      '\'',   SDLK_APOSTROPHE         }, // 40
{ "`",      '`',    SDLK_GRAVE     },
{ "LSHFT",  255,    SDLK_LSHIFT        },
{ "\\",     '\\',   SDLK_BACKSLASH     },
{ "Z",      'z',    SDLK_Z             },
{ "X",      'x',    SDLK_X             },
{ "C",      'c',    SDLK_C             },
{ "V",      'v',    SDLK_V             },
{ "B",      'b',    SDLK_B             },
{ "N",      'n',    SDLK_N             },
{ "M",      'm',    SDLK_M             }, // 50
{ ",",      ',',    SDLK_COMMA         },
{ ".",      '.',    SDLK_PERIOD        },
{ "/",      '/',    SDLK_SLASH         },
{ "RSHFT",  255,    SDLK_RSHIFT        },
{ "PAD*",   '*',    SDLK_KP_MULTIPLY   },
{ "LALT",   255,    SDLK_LALT          },
{ "SPC",    ' ',    SDLK_SPACE         },
{ "CPSLK",  255,    SDLK_CAPSLOCK      },
{ "F1",     255,    SDLK_F1            },
{ "F2",     255,    SDLK_F2            }, // 60
{ "F3",     255,    SDLK_F3            },
{ "F4",     255,    SDLK_F4            },
{ "F5",     255,    SDLK_F5            },
{ "F6",     255,    SDLK_F6            },
{ "F7",     255,    SDLK_F7            },
{ "F8",     255,    SDLK_F8            },
{ "F9",     255,    SDLK_F9            },
{ "F10",    255,    SDLK_F10           },
{ "NMLCK",  255,    SDLK_NUMLOCKCLEAR  },
{ "SCLK",   255,    SDLK_SCROLLLOCK    }, // 70
{ "PAD7",   255,    SDLK_KP_7          },
{ "PAD8",   255,    SDLK_KP_8          },
{ "PAD9",   255,    SDLK_KP_9          },
{ "PAD-",   255,    SDLK_KP_MINUS      },
{ "PAD4",   255,    SDLK_KP_4          },
{ "PAD5",   255,    SDLK_KP_5          },
{ "PAD6",   255,    SDLK_KP_6          },
{ "PAD+",   255,    SDLK_KP_PLUS       },
{ "PAD1",   255,    SDLK_KP_1          },
{ "PAD2",   255,    SDLK_KP_2          }, // 80
{ "PAD3",   255,    SDLK_KP_3          },
{ "PAD0",   255,    SDLK_KP_0          },
{ "PAD.",   255,    SDLK_KP_PERIOD     },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "F11",    255,    SDLK_F11           },
{ "F12",    255,    SDLK_F12           },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 }, // 90
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "PAUSE",  255,    SDLK_PAUSE         },
#define SDLK_WORLD_0	SDLK_UNKNOWN
#define SDLK_WORLD_1	SDLK_UNKNOWN
#define SDLK_WORLD_2	SDLK_UNKNOWN
#define SDLK_WORLD_3	SDLK_UNKNOWN
#define SDLK_WORLD_4	SDLK_UNKNOWN
#define SDLK_WORLD_5	SDLK_UNKNOWN
#define SDLK_WORLD_6	SDLK_UNKNOWN
#define SDLK_WORLD_7	SDLK_UNKNOWN
#define SDLK_WORLD_8	SDLK_UNKNOWN
#define SDLK_WORLD_9	SDLK_UNKNOWN
#define SDLK_WORLD_10	SDLK_UNKNOWN
#define SDLK_WORLD_11	SDLK_UNKNOWN
#define SDLK_WORLD_12	SDLK_UNKNOWN
#define SDLK_WORLD_13	SDLK_UNKNOWN
#define SDLK_WORLD_14	SDLK_UNKNOWN
#define SDLK_WORLD_15	SDLK_UNKNOWN
#define SDLK_WORLD_16	SDLK_UNKNOWN
#define SDLK_WORLD_17	SDLK_UNKNOWN
#define SDLK_WORLD_18	SDLK_UNKNOWN
#define SDLK_WORLD_19	SDLK_UNKNOWN
#define SDLK_WORLD_20	SDLK_UNKNOWN
#define SDLK_WORLD_21	SDLK_UNKNOWN
#define SDLK_WORLD_22	SDLK_UNKNOWN
#define SDLK_WORLD_23	SDLK_UNKNOWN
#define SDLK_WORLD_24	SDLK_UNKNOWN
#define SDLK_WORLD_25	SDLK_UNKNOWN
#define SDLK_WORLD_26	SDLK_UNKNOWN
#define SDLK_WORLD_27	SDLK_UNKNOWN
#define SDLK_WORLD_28	SDLK_UNKNOWN
#define SDLK_WORLD_29	SDLK_UNKNOWN
#define SDLK_WORLD_30	SDLK_UNKNOWN
#define SDLK_WORLD_31	SDLK_UNKNOWN
#define SDLK_WORLD_32	SDLK_UNKNOWN
#define SDLK_WORLD_33	SDLK_UNKNOWN
#define SDLK_WORLD_34	SDLK_UNKNOWN
#define SDLK_WORLD_35	SDLK_UNKNOWN
#define SDLK_WORLD_36	SDLK_UNKNOWN
#define SDLK_WORLD_37	SDLK_UNKNOWN
#define SDLK_WORLD_38	SDLK_UNKNOWN
#define SDLK_WORLD_39	SDLK_UNKNOWN
#define SDLK_WORLD_40	SDLK_UNKNOWN
#define SDLK_WORLD_41	SDLK_UNKNOWN
#define SDLK_WORLD_42	SDLK_UNKNOWN
#define SDLK_WORLD_43	SDLK_UNKNOWN
#define SDLK_WORLD_44	SDLK_UNKNOWN
#define SDLK_WORLD_45	SDLK_UNKNOWN
#define SDLK_WORLD_46	SDLK_UNKNOWN
#define SDLK_WORLD_47	SDLK_UNKNOWN
#define SDLK_WORLD_48	SDLK_UNKNOWN
#define SDLK_WORLD_49	SDLK_UNKNOWN
#define SDLK_WORLD_50	SDLK_UNKNOWN
#define SDLK_WORLD_51	SDLK_UNKNOWN
{ "W0",     255,    SDLK_WORLD_0       },
{ "W1",     255,    SDLK_WORLD_1       },
{ "W2",     255,    SDLK_WORLD_2       }, // 100
{ "W3",     255,    SDLK_WORLD_3       },
{ "W4",     255,    SDLK_WORLD_4       },
{ "W5",     255,    SDLK_WORLD_5       },
{ "W6",     255,    SDLK_WORLD_6       },
{ "W7",     255,    SDLK_WORLD_7       },
{ "W8",     255,    SDLK_WORLD_8       },
{ "W9",     255,    SDLK_WORLD_9       },
{ "W10",    255,    SDLK_WORLD_10      },
{ "W11",    255,    SDLK_WORLD_11      },
{ "W12",    255,    SDLK_WORLD_12      }, // 110
{ "W13",    255,    SDLK_WORLD_13      },
{ "W14",    255,    SDLK_WORLD_14      },
{ "W15",    255,    SDLK_WORLD_15      },
{ "W16",    255,    SDLK_WORLD_16      },
{ "W17",    255,    SDLK_WORLD_17      },
{ "W18",    255,    SDLK_WORLD_18      },
{ "W19",    255,    SDLK_WORLD_19      },
{ "W20",    255,    SDLK_WORLD_20      },
{ "W21",    255,    SDLK_WORLD_21      },
{ "W22",    255,    SDLK_WORLD_22      }, // 120
{ "W23",    255,    SDLK_WORLD_23      },
{ "W24",    255,    SDLK_WORLD_24      },
{ "W25",    255,    SDLK_WORLD_25      },
{ "W26",    255,    SDLK_WORLD_26      },
{ "W27",    255,    SDLK_WORLD_27      },
{ "W28",    255,    SDLK_WORLD_28      },
{ "W29",    255,    SDLK_WORLD_29      },
{ "W30",    255,    SDLK_WORLD_30      },
{ "W31",    255,    SDLK_WORLD_31      },
{ "W32",    255,    SDLK_WORLD_32      }, // 130
{ "W33",    255,    SDLK_WORLD_33      },
{ "W34",    255,    SDLK_WORLD_34      },
{ "W35",    255,    SDLK_WORLD_35      },
{ "W36",    255,    SDLK_WORLD_36      },
{ "W37",    255,    SDLK_WORLD_37      },
{ "W38",    255,    SDLK_WORLD_38      },
{ "W39",    255,    SDLK_WORLD_39      },
{ "W40",    255,    SDLK_WORLD_40      },
{ "W41",    255,    SDLK_WORLD_41      },
{ "W42",    255,    SDLK_WORLD_42      }, // 140
{ "W43",    255,    SDLK_WORLD_43      },
{ "W44",    255,    SDLK_WORLD_44      },
{ "W45",    255,    SDLK_WORLD_45      },
{ "W46",    255,    SDLK_WORLD_46      },
{ "W47",    255,    SDLK_WORLD_47      },
{ "W48",    255,    SDLK_WORLD_48      },
{ "W49",    255,    SDLK_WORLD_49      },
{ "W50",    255,    SDLK_WORLD_50      },
{ "W51",    255,    SDLK_WORLD_51      },
{ "",       255,    SDLK_UNKNOWN                 }, // 150
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "PAD",    255,    SDLK_KP_ENTER      },
{ "RCTRL",  255,    SDLK_RCTRL         },
{ "LCMD",   255,    SDLK_LGUI          },
{ "RCMD",   255,    SDLK_RGUI          },
{ "",       255,    SDLK_UNKNOWN                 }, // 160
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 }, // 170
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 }, // 180
{ "PAD/",   255,    SDLK_KP_DIVIDE     },
{ "",       255,    SDLK_UNKNOWN                 },
{ "PRSCR",  255,    SDLK_PRINTSCREEN   },
{ "RALT",   255,    SDLK_RALT          },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 }, // 190
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "",       255,    SDLK_UNKNOWN                 },
{ "HOME",   255,    SDLK_HOME          },
{ "UP",     255,    SDLK_UP            }, // 200
{ "PGUP",   255,    SDLK_PAGEUP        },
{ "",       255,    SDLK_UNKNOWN                 },
{ "LEFT",   255,    SDLK_LEFT          },
{ "",       255,    SDLK_UNKNOWN                 },
{ "RIGHT",  255,    SDLK_RIGHT         },
{ "",       255,    SDLK_UNKNOWN                 },
{ "END",    255,    SDLK_END           },
{ "DOWN",   255,    SDLK_DOWN          },
{ "PGDN",   255,    SDLK_PAGEDOWN      },
{ "INS",    255,    SDLK_INSERT        }, // 210
{ "DEL",    255,    SDLK_DELETE        },
}};

namespace {

struct d_event_keycommand : d_event
{
	const unsigned keycode;
	const source src;
	constexpr d_event_keycommand(const event_type t, const unsigned keycode, const source src) :
		d_event{t}, keycode{keycode}, src{src}
	{
	}
};

static int key_ismodlck(int keycode)
{
	switch (keycode)
	{
		case KEY_LSHIFT:
		case KEY_RSHIFT:
		case KEY_LALT:
		case KEY_RALT:
		case KEY_LCTRL:
		case KEY_RCTRL:
		case KEY_LMETA:
		case KEY_RMETA:
			return KEY_ISMOD;
		case KEY_NUMLOCK:
		case KEY_SCROLLOCK:
		case KEY_CAPSLOCK:
			return KEY_ISLCK;
		default:
			return 0;
	}
}

}

unsigned char key_ascii()
{
	using std::move;
	using std::next;
	auto src = begin(unicode_frame_buffer);
	auto dst = next(begin(unibuffer), strlen(reinterpret_cast<const char *>(&unibuffer[0])));
	
	// move temporal chars from unicode_frame_buffer to empty space behind last unibuffer char (if any)
	for (; dst != end(unibuffer); ++dst)
		if (*src != '\0')
		{
			*dst = *src;
			*src = '\0';
			++src;
		}

	// unibuffer is not empty. store first char, remove it, shift all chars one step left and then print our char
	if (unibuffer[0] != '\0')
	{
		unsigned char retval = unibuffer[0];
		*move(next(unibuffer.begin()), unibuffer.end(), unibuffer.begin()) = 0;
		return retval;
	}
	else
		return 255;
}

void pressed_keys::update(const std::size_t keycode, const uint8_t down)
{
	constexpr unsigned all_modifiers_combined = KEY_SHIFTED | KEY_ALTED | KEY_CTRLED | KEY_DEBUGGED | KEY_METAED;
	constexpr unsigned all_modifiers_shifted = all_modifiers_combined >> modifier_shift;
	static_assert(all_modifiers_combined == all_modifiers_shifted << modifier_shift, "shift error");
	static_assert(all_modifiers_shifted == static_cast<uint8_t>(all_modifiers_shifted), "truncation error");
	uint8_t mask;
	keyd_pressed.update_pressed(keycode, down);
	switch (keycode)
	{
		case KEY_LSHIFT:
		case KEY_RSHIFT:
			mask = KEY_SHIFTED >> modifier_shift;
			break;
		case KEY_LALT:
		case KEY_RALT:
			mask = KEY_ALTED >> modifier_shift;
			break;
		case KEY_LCTRL:
		case KEY_RCTRL:
			mask = KEY_CTRLED >> modifier_shift;
			break;
		case KEY_DELETE:
			mask = KEY_DEBUGGED >> modifier_shift;
			break;
		case KEY_LMETA:
		case KEY_RMETA:
			mask = KEY_METAED >> modifier_shift;
			break;
		default:
			return;
	}
	if (down)
		modifier_cache |= mask;
	else
		modifier_cache &= ~mask;
}

window_event_result key_handler(const SDL_KeyboardEvent *const kevent)
{
	if (!keyd_repeat && kevent->repeat)
		return window_event_result::ignored;
	// Read SDLK symbol and state
	/* SDL3 key events contain modified symbols.  Bindings use the original
	 * unmodified layout symbols, with modifiers carried separately.
	 */
	const auto event_keysym = SDL_GetKeyFromScancode(kevent->scancode, SDL_KMOD_NONE, true);
	if (event_keysym == SDLK_UNKNOWN)
		return window_event_result::ignored;
	const auto key_state = kevent->down;

	//=====================================================
	const auto re = key_properties.rend();
	const auto &&fi{std::ranges::find(key_properties.rbegin(), re, event_keysym, &key_props::sym)};
	if (fi == re)
		return window_event_result::ignored;
	unsigned keycode = std::distance(key_properties.begin(), std::next(fi).base());
	if (keycode == 0)
		return window_event_result::ignored;

	/* 
	 * process the key if:
	 * - it's a valid key AND
	 * - if the keystate has changed OR
	 * - key state same as last one and game accepts key repeats but keep out mod/lock keys
	 */
	if (key_state != keyd_pressed[keycode] || (keyd_repeat && !key_ismodlck(keycode)))
	{
		// now update the key props
		keyd_pressed.update(keycode, key_state);
		const auto raw_keycode{keycode};
		keycode |= keyd_pressed.get_modifiers();

		// We allowed the key to be added to the queue for now,
		// because there are still input loops without associated windows
		const d_event_keycommand event{key_state ? event_type::key_command : event_type::key_release, keycode, d_event_keycommand::source::keyboard};
		con_printf(CON_DEBUG, "Sending event event_type::key_%s: %s %s %s %s %s %s",
				(key_state)                  ? "command": "release",
				(keycode & KEY_METAED)	? "META" : "",
				(keycode & KEY_DEBUGGED)	? "DEBUG" : "",
				(keycode & KEY_CTRLED)	? "CTRL" : "",
				(keycode & KEY_ALTED)	? "ALT" : "",
				(keycode & KEY_SHIFTED)	? "SHIFT" : "",
				key_properties[raw_keycode].key_text
				);
		return event_send(event);
	}
	return window_event_result::ignored;
}

window_event_result key_text_handler(const char *text)
{
	window_event_result result{window_event_result::ignored};
	/* The game font and menu buffers use single-byte characters.  Decode SDL's
	 * UTF-8 and deliver only characters representable by that existing format.
	 * A neutral key command lets menu text consumers process committed text,
	 * which SDL delivers independently of (and after) physical key events.
	 */
	while (const auto character = SDL_StepUTF8(&text, nullptr))
	{
		if (character <= 31 || character >= 255)
			continue;
		unicode_frame_buffer[0] = character;
		result = std::max(event_send(d_event_keycommand{event_type::key_command, 0, d_event_keycommand::source::keyboard}), result);
		unicode_frame_buffer = {};
		if (result == window_event_result::deleted)
			break;
	}
	return result;
}

void key_init()
{
	key_toggle_repeat(1);

	keyd_time_when_last_pressed = timer_query();
	// Clear the keyboard array
	key_flush();
}

namespace {

static void restore_sticky_key(const bool *keystate, const unsigned key, const SDL_Scancode scancode)
{
	keyd_pressed.update_pressed(key, keystate[scancode]);
}

}

void key_flush()
{
	//Clear the unicode buffer
	unicode_frame_buffer = {};
	unibuffer = {};
	keyd_pressed = {};
	if (unlikely(CGameArg.CtlNoStickyKeys))
		return;
	const auto &keystate = SDL_GetKeyboardState(nullptr);
	restore_sticky_key(keystate, KEY_CAPSLOCK, SDL_SCANCODE_CAPSLOCK);
	restore_sticky_key(keystate, KEY_SCROLLOCK, SDL_SCANCODE_SCROLLLOCK);
	restore_sticky_key(keystate, KEY_NUMLOCK, SDL_SCANCODE_NUMLOCKCLEAR);
}

void event_keycommand_send(unsigned key) {
	/* This function is only used for synthesizing keypresses via joystick
	 * input.
	 */
	event_send(d_event_keycommand{event_type::key_command, key, d_event_keycommand::source::joystick});
}

int event_key_get(const d_event &event)
{
	auto &e = static_cast<const d_event_keycommand &>(event);
	assert(e.type == event_type::key_command || e.type == event_type::key_release);
	return e.keycode;
}

d_event::source event_key_get_source(const d_event &event)
{
	return static_cast<const d_event_keycommand &>(event).src;
}

// same as above but without mod states
int event_key_get_raw(const d_event &event)
{
	return event_key_get(event) & ~(KEY_SHIFTED | KEY_ALTED | KEY_CTRLED | KEY_DEBUGGED | KEY_METAED);
}

void key_toggle_repeat1()
{
		keyd_repeat = 1;
	key_flush();
}

void key_toggle_repeat0()
{
	keyd_repeat = 0;
	key_flush();
}

}
