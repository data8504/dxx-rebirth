/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "args.h"
#include "console.h"
#include "dxxerror.h"
#include "event.h"
#include "joy.h"
#include "key.h"
#include "kconfig.h"
#include "timer.h"
#include <SDL3/SDL.h>
#include <physfs.h>
#include <stdexcept>
#include <string>
#include <vector>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth SDL3 input
#include <boost/test/unit_test.hpp>

namespace {

struct captured_event
{
	::dcx::event_type type;
	unsigned index;
	int value;
};

std::vector<captured_event> events;
std::string text;
bool capture_text{};

struct input_fixture
{
	input_fixture()
	{
		SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
		if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
			throw std::runtime_error(SDL_GetError());
		if (!PHYSFS_init(boost::unit_test::framework::master_test_suite().argv[0]))
			throw std::runtime_error(PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
		::dcx::CGameArg = {};
		::dcx::key_init();
		events.clear();
		text.clear();
		capture_text = false;
	}
	~input_fixture()
	{
		::dcx::gamecontroller_close();
		::dcx::joy_close();
		PHYSFS_deinit();
		SDL_Quit();
	}

	SDL_JoystickID attach(const bool gamepad)
	{
		SDL_VirtualJoystickDesc desc{};
		SDL_INIT_INTERFACE(&desc);
		desc.type = gamepad ? SDL_JOYSTICK_TYPE_GAMEPAD : SDL_JOYSTICK_TYPE_FLIGHT_STICK;
		desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
		desc.nbuttons = ::dcx::GAMECONTROLLER_BUTTON_COUNT;
		desc.button_mask = (1u << desc.nbuttons) - 1;
		desc.axis_mask = (1u << desc.naxes) - 1;
		desc.name = "DXX SDL3 input test";
		const auto id = SDL_AttachVirtualJoystick(&desc);
		BOOST_REQUIRE_NE(id, 0u);
		return id;
	}
};

}

/* Exercise the production input backends with a captured engine event sink.
 * Console output, frame time, and menu metadata do not require a game session.
 */
namespace dcx {

CArg CGameArg;
joyaxis_text_t joyaxis_text;
joybutton_text_t joybutton_text;

#undef con_printf
void con_printf(con_priority_wrapper, const char *, ...) {}
void con_puts(con_priority_wrapper, std::span<const char>) {}
void Warning(const char *, ...) {}
fix64 timer_query() { return 0; }

window_event_result event_send(const d_event &event)
{
	switch (event.type)
	{
		case event_type::key_command:
		case event_type::key_release:
			events.push_back({event.type, static_cast<unsigned>(event_key_get(event)), 0});
			if (capture_text)
				if (const auto character = key_ascii(); character != 255)
					text.push_back(character);
			break;
		case event_type::joystick_button_down:
		case event_type::joystick_button_up:
			events.push_back({event.type, static_cast<unsigned>(event_joystick_get_button(event)), 0});
			break;
		case event_type::joystick_moved:
		{
			const auto &axis = event_joystick_get_axis(event);
			events.push_back({event.type, axis.axis, axis.value});
			break;
		}
		default:
			break;
	}
	return window_event_result::handled;
}

}

BOOST_FIXTURE_TEST_CASE(committed_utf8_text_is_decoded_once, input_fixture)
{
	capture_text = true;
	dcx::key_text_handler("Az\xc3\xa9\xe2\x82\xac!");
	BOOST_TEST(text == std::string("Az\xe9!"));
	BOOST_TEST(events.size() == 4u);
	BOOST_TEST(dcx::key_ascii() == 255);
}

BOOST_FIXTURE_TEST_CASE(modified_symbols_preserve_physical_key_bindings, input_fixture)
{
	SDL_KeyboardEvent key{};
	key.type = SDL_EVENT_KEY_DOWN;
	key.down = true;
	key.scancode = SDL_SCANCODE_LSHIFT;
	dcx::key_handler(&key);
	events.clear();
	key.scancode = SDL_SCANCODE_A;
	key.key = 'A';
	key.mod = SDL_KMOD_SHIFT;
	dcx::key_handler(&key);
	BOOST_REQUIRE_EQUAL(events.size(), 1u);
	BOOST_TEST(events.back().index == (KEY_A | KEY_SHIFTED));
	BOOST_TEST(dcx::key_ascii() == 255);
	key.down = false;
	key.type = SDL_EVENT_KEY_UP;
	dcx::key_handler(&key);
	BOOST_TEST(!dcx::keyd_pressed[KEY_A]);
}

BOOST_FIXTURE_TEST_CASE(disabled_repeat_does_not_repeat_physical_commands, input_fixture)
{
	dcx::key_toggle_repeat0();
	SDL_KeyboardEvent key{};
	key.type = SDL_EVENT_KEY_DOWN;
	key.down = true;
	key.scancode = SDL_SCANCODE_A;
	dcx::key_handler(&key);
	key.repeat = true;
	dcx::key_handler(&key);
	BOOST_TEST(events.size() == 1u);
	key.type = SDL_EVENT_KEY_UP;
	key.down = false;
	key.repeat = false;
	dcx::key_handler(&key);
	BOOST_TEST(!dcx::keyd_pressed[KEY_A]);
}

BOOST_FIXTURE_TEST_CASE(gamepad_ids_and_hotplug_preserve_saved_bindings, input_fixture)
{
	const auto unused = attach(false);
	const auto id = attach(true);
	BOOST_REQUIRE(SDL_DetachVirtualJoystick(unused));
	BOOST_REQUIRE(SDL_IsGamepad(id));
	dcx::gamecontroller_init();
	BOOST_REQUIRE_EQUAL(dcx::num_controllers, 1);
	SDL_GamepadDeviceEvent device{};
	device.which = id;
	dcx::gc_device_added(&device);
	BOOST_TEST(dcx::num_controllers == 1);

	SDL_GamepadButtonEvent button{};
	button.which = id;
	button.button = SDL_GAMEPAD_BUTTON_SOUTH;
	button.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
	button.down = true;
	dcx::gc_button_handler(&button);
	BOOST_REQUIRE_EQUAL(events.size(), 1u);
	BOOST_TEST(events.back().index == 0u);
	button.which = 0;
	dcx::gc_button_handler(&button);
	BOOST_TEST(events.size() == 1u);

	SDL_GamepadAxisEvent axis{};
	axis.which = id;
	axis.axis = SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
	axis.value = 32767;
	dcx::gc_axisbutton_handler(&axis);
	BOOST_TEST(events.back().index == 29u);
	dcx::gc_axis_handler(&axis);
	BOOST_TEST(events.back().value == 127);
	events.clear();
	dcx::gc_device_removed(&device);
	BOOST_TEST(dcx::num_controllers == 0);
	BOOST_REQUIRE_EQUAL(events.size(), 3u);
	BOOST_TEST(events[0].index == 0u);
	BOOST_TEST(events[1].index == 29u);
	BOOST_TEST(events[2].value == 0);
}

BOOST_FIXTURE_TEST_CASE(raw_joystick_instance_id_is_not_an_enumeration_index, input_fixture)
{
	const auto unused = attach(false);
	const auto id = attach(false);
	BOOST_REQUIRE(SDL_DetachVirtualJoystick(unused));
	dcx::joy_init();
	SDL_JoyButtonEvent button{};
	button.which = id;
	button.button = 0;
	button.type = SDL_EVENT_JOYSTICK_BUTTON_DOWN;
	button.down = true;
	dcx::joy_button_handler(&button);
	BOOST_REQUIRE_EQUAL(events.size(), 1u);
	BOOST_TEST(events.back().index == 0u);
	button.button = 255;
	dcx::joy_button_handler(&button);
	BOOST_TEST(events.size() == 1u);
}
