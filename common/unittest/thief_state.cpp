/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#define DXX_BUILD_DESCENT 2
#include "thief_state.h"

#include <algorithm>
#include <array>
#include <limits>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth thief state
#include <boost/test/unit_test.hpp>

namespace {

using namespace dsx;

constexpr std::array<ai_mode, 3> thief_modes{{
	ai_mode::AIM_THIEF_ATTACK,
	ai_mode::AIM_THIEF_RETREAT,
	ai_mode::AIM_THIEF_WAIT,
}};

}

BOOST_AUTO_TEST_CASE(capture_and_validation_preserve_each_phase_and_remaining_cooldown)
{
	for (const auto mode : thief_modes) {
		for (const fix remaining : {0, 1, F1_0 * 7 + F1_0 / 3, std::numeric_limits<fix>::max()}) {
			const auto result = capture_thief_state(mode, remaining);
			const auto received = build_thief_state_from_untrusted(static_cast<uint8_t>(mode), remaining);
			BOOST_REQUIRE(received.has_value());
			BOOST_CHECK(received->mode == mode);
			BOOST_TEST(received->next_action_time == remaining);
			BOOST_CHECK(result.mode == mode);
			BOOST_TEST(result.next_action_time == remaining);
		}
	}
}

BOOST_AUTO_TEST_CASE(capturing_expired_action_time_preserves_phase_with_zero_remaining)
{
	for (const auto mode : thief_modes) {
		for (const fix expired : {-1, -F1_0, std::numeric_limits<fix>::min()}) {
			const auto result = capture_thief_state(mode, expired);
			BOOST_CHECK(result.mode == mode);
			BOOST_TEST(result.next_action_time == 0);
		}
	}
}

BOOST_AUTO_TEST_CASE(non_thief_modes_are_rejected)
{
	for (unsigned raw_mode = 0; raw_mode != 256; ++raw_mode) {
		const auto result = build_thief_state_from_untrusted(static_cast<uint8_t>(raw_mode), F1_0);
		const auto valid = std::ranges::any_of(thief_modes, [raw_mode](const auto mode) {
			return static_cast<unsigned>(mode) == raw_mode;
		});
		BOOST_TEST(result.has_value() == valid);
	}
}

BOOST_AUTO_TEST_CASE(negative_received_cooldowns_are_rejected)
{
	for (const auto mode : thief_modes) {
		for (const fix remaining : {-1, -F1_0, std::numeric_limits<fix>::min()})
			BOOST_TEST(!build_thief_state_from_untrusted(static_cast<uint8_t>(mode), remaining).has_value());
	}
}
