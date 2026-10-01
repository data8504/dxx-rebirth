/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "dxxsconf.h"

#include <SDL3/SDL.h>
#if DXX_USE_SDLMIXER
#include <SDL3_mixer/SDL_mixer.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include "d_sdl_audio.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth MVE audio stream
#include <boost/test/unit_test.hpp>

namespace {

struct audio_stream_deleter
{
	static void operator()(SDL_AudioStream *const stream)
	{
		SDL_DestroyAudioStream(stream);
	}
};

using audio_stream_ptr = std::unique_ptr<SDL_AudioStream, audio_stream_deleter>;

std::vector<uint8_t> convert(const std::span<const int16_t> input, const std::span<const std::size_t> partitions)
{
	const SDL_AudioSpec source{SDL_AUDIO_S16, 1, 22050};
	const SDL_AudioSpec destination{SDL_AUDIO_S16, 2, 44100};
	audio_stream_ptr stream{SDL_CreateAudioStream(&source, &destination)};
	BOOST_REQUIRE(stream);
	std::vector<uint8_t> output;
	std::size_t offset{};
	const auto drain = [&] {
		const auto available = SDL_GetAudioStreamAvailable(stream.get());
		BOOST_REQUIRE(available >= 0);
		const auto old_size = output.size();
		output.resize(old_size + available);
		if (available)
			BOOST_REQUIRE_EQUAL(SDL_GetAudioStreamData(stream.get(), output.data() + old_size, available), available);
	};
	for (const auto count : partitions)
	{
		BOOST_REQUIRE(count <= input.size() - offset);
		BOOST_REQUIRE(SDL_PutAudioStreamData(stream.get(), input.data() + offset,
			static_cast<int>(count * sizeof(input.front()))));
		offset += count;
		drain();
	}
	BOOST_REQUIRE_EQUAL(offset, input.size());
	BOOST_REQUIRE(SDL_FlushAudioStream(stream.get()));
	drain();
	return output;
}

}

BOOST_AUTO_TEST_CASE(conversion_is_independent_of_input_partitioning)
{
	std::vector<int16_t> input(22050);
	for (std::size_t i = 0; i != input.size(); ++i)
		input[i] = static_cast<int16_t>(((i * 977u) % 60001u) - 30000);

	std::vector<std::size_t> regular;
	for (std::size_t left = input.size(); left;)
	{
		const auto count = std::min<std::size_t>(left, 733);
		regular.push_back(count);
		left -= count;
	}

	std::vector<std::size_t> irregular;
	for (std::size_t left = input.size(), count = 1; left; count = (count * 17 + 31) % 509 + 1)
	{
		const auto actual = std::min(left, count);
		irregular.push_back(actual);
		left -= actual;
	}

	const auto regularly_chunked = convert(input, regular);
	const auto irregularly_chunked = convert(input, irregular);
	BOOST_TEST(regularly_chunked == irregularly_chunked, boost::test_tools::per_element());
	BOOST_TEST(regularly_chunked.size() == input.size() * 2u * 2u * 2u);
}

BOOST_AUTO_TEST_CASE(unsigned_eight_bit_silence_stays_silent)
{
	const SDL_AudioSpec source{SDL_AUDIO_U8, 1, 11025};
	const SDL_AudioSpec destination{SDL_AUDIO_S16, 2, 44100};
	audio_stream_ptr stream{SDL_CreateAudioStream(&source, &destination)};
	BOOST_REQUIRE(stream);
	const std::vector<uint8_t> input(11025, 0x80);
	BOOST_REQUIRE(SDL_PutAudioStreamData(stream.get(), input.data(), input.size()));
	BOOST_REQUIRE(SDL_FlushAudioStream(stream.get()));
	const auto available = SDL_GetAudioStreamAvailable(stream.get());
	BOOST_REQUIRE_EQUAL(available, input.size() * 4u * 2u * sizeof(int16_t));
	std::vector<int16_t> output(available / sizeof(int16_t));
	BOOST_REQUIRE_EQUAL(SDL_GetAudioStreamData(stream.get(), output.data(), available), available);
	BOOST_TEST(std::all_of(output.begin(), output.end(), [](const int16_t value) { return value == 0; }));
}

BOOST_AUTO_TEST_CASE(dummy_device_callback_and_explicit_stream_lock)
{
	BOOST_REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
	BOOST_REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
	const SDL_AudioSpec spec{SDL_AUDIO_U8, 2, 11025};
	std::atomic<unsigned> callbacks{};
	const auto callback = [](void *const userdata, SDL_AudioStream *const stream, const int additional, int)
	{
		if (additional <= 0)
			return;
		const std::vector<uint8_t> silence((additional + 1) & ~1, 0x80);
		if (SDL_PutAudioStreamData(stream, silence.data(), silence.size()))
			++*static_cast<std::atomic<unsigned> *>(userdata);
	};
	audio_stream_ptr stream{SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, callback, &callbacks)};
	BOOST_REQUIRE(stream);
	{
		const dcx::RAII_SDL_LockAudio lock{stream.get()};
		BOOST_REQUIRE(SDL_ResumeAudioStreamDevice(stream.get()));
	}
	for (unsigned attempt = 0; !callbacks.load() && attempt != 100; ++attempt)
		SDL_Delay(10);
	BOOST_TEST(callbacks.load() > 0u);
	stream.reset();
	const auto stopped_callbacks = callbacks.load();
	SDL_Delay(20);
	BOOST_TEST(callbacks.load() == stopped_callbacks);
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

#if DXX_USE_SDLMIXER
namespace {

struct mixer_fixture
{
	const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 44100};
	MIX_Mixer *mixer{};
	MIX_Track *track{};
	mixer_fixture()
	{
		BOOST_REQUIRE(MIX_Init());
		mixer = MIX_CreateMixer(&spec);
		BOOST_REQUIRE(mixer);
		track = MIX_CreateTrack(mixer);
		BOOST_REQUIRE(track);
	}
	~mixer_fixture()
	{
		MIX_DestroyMixer(mixer);
		MIX_Quit();
	}
};

struct mixer_audio_deleter
{
	static void operator()(MIX_Audio *const audio) { MIX_DestroyAudio(audio); }
};
using mixer_audio_ptr = std::unique_ptr<MIX_Audio, mixer_audio_deleter>;

}

BOOST_FIXTURE_TEST_CASE(cached_effect_pan_gain_loop_and_stop, mixer_fixture)
{
	const std::vector<float> pcm(1024, .5f);
	const SDL_AudioSpec mono{SDL_AUDIO_F32, 1, spec.freq};
	mixer_audio_ptr audio{MIX_LoadRawAudio(mixer, pcm.data(), pcm.size() * sizeof(float), &mono)};
	BOOST_REQUIRE(audio);
	BOOST_REQUIRE(MIX_SetTrackAudio(track, audio.get()));
	const MIX_StereoGains pan{1.f, 0.f};
	BOOST_REQUIRE(MIX_SetTrackStereo(track, &pan));
	BOOST_REQUIRE(MIX_SetTrackGain(track, .5f));
	const auto options = SDL_CreateProperties();
	BOOST_REQUIRE(options);
	BOOST_REQUIRE(SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, -1));
	BOOST_REQUIRE(MIX_PlayTrack(track, options));
	SDL_DestroyProperties(options);
	std::vector<float> output(4096);
	BOOST_REQUIRE(MIX_Generate(mixer, output.data(), output.size() * sizeof(float)) >= 0);
	BOOST_TEST(MIX_TrackPlaying(track));
	for (std::size_t i = 0; i != output.size(); i += 2)
	{
		BOOST_TEST(output[i] == .25f, boost::test_tools::tolerance(.0001f));
		BOOST_TEST(output[i + 1] == 0.f);
	}
	BOOST_REQUIRE(MIX_StopTrack(track, 0));
	BOOST_TEST(!MIX_TrackPlaying(track));
	BOOST_REQUIRE(MIX_PlayTrack(track, 0));
	BOOST_REQUIRE(MIX_Generate(mixer, output.data(), output.size() * sizeof(float)) >= 0);
	BOOST_TEST(!MIX_TrackPlaying(track));
}

BOOST_FIXTURE_TEST_CASE(movie_stream_survives_segment_gaps_and_detaches_before_free, mixer_fixture)
{
	audio_stream_ptr stream{SDL_CreateAudioStream(&spec, &spec)};
	BOOST_REQUIRE(stream);
	BOOST_REQUIRE(MIX_SetTrackAudioStream(track, stream.get()));
	const auto options = SDL_CreateProperties();
	BOOST_REQUIRE(options);
	BOOST_REQUIRE(SDL_SetBooleanProperty(options, MIX_PROP_PLAY_HALT_WHEN_EXHAUSTED_BOOLEAN, false));
	BOOST_REQUIRE(MIX_PlayTrack(track, options));
	SDL_DestroyProperties(options);
	std::vector<float> input(1024, .25f), output(1024);
	BOOST_REQUIRE(SDL_PutAudioStreamData(stream.get(), input.data(), input.size() * sizeof(float)));
	BOOST_REQUIRE(MIX_Generate(mixer, output.data(), output.size() * sizeof(float)) >= 0);
	BOOST_TEST(output == input, boost::test_tools::per_element());
	BOOST_REQUIRE(MIX_Generate(mixer, output.data(), output.size() * sizeof(float)) >= 0);
	BOOST_TEST(std::all_of(output.begin(), output.end(), [](const float value) { return value == 0.f; }));
	BOOST_TEST(MIX_TrackPlaying(track));
	BOOST_REQUIRE(SDL_PutAudioStreamData(stream.get(), input.data(), input.size() * sizeof(float)));
	BOOST_REQUIRE(MIX_Generate(mixer, output.data(), output.size() * sizeof(float)) >= 0);
	BOOST_TEST(output == input, boost::test_tools::per_element());
	BOOST_REQUIRE(MIX_SetTrackAudioStream(track, nullptr));
	stream.reset();
	BOOST_REQUIRE(MIX_Generate(mixer, output.data(), output.size() * sizeof(float)) >= 0);
	BOOST_TEST(std::all_of(output.begin(), output.end(), [](const float value) { return value == 0.f; }));
	BOOST_REQUIRE(MIX_StopTrack(track, 0));
	BOOST_TEST(!MIX_TrackPlaying(track));
}

BOOST_FIXTURE_TEST_CASE(music_completion_can_restart_and_explicit_stop_suppresses_hook, mixer_fixture)
{
	const std::vector<float> pcm(1024, .25f);
	mixer_audio_ptr audio{MIX_LoadRawAudio(mixer, pcm.data(), pcm.size() * sizeof(float), &spec)};
	BOOST_REQUIRE(audio);
	BOOST_REQUIRE(MIX_SetTrackAudio(track, audio.get()));
	unsigned completions{};
	const auto finished = [](void *const userdata, MIX_Track *const track)
	{
		auto &count = *static_cast<unsigned *>(userdata);
		if (++count == 1)
			MIX_PlayTrack(track, 0);
	};
	BOOST_REQUIRE(MIX_SetTrackStoppedCallback(track, finished, &completions));
	BOOST_REQUIRE(MIX_PlayTrack(track, 0));
	/* The game removes the hook before explicitly stopping music. */
	BOOST_REQUIRE(MIX_SetTrackStoppedCallback(track, nullptr, nullptr));
	BOOST_REQUIRE(MIX_StopTrack(track, 0));
	BOOST_TEST(completions == 0u);
	BOOST_REQUIRE(MIX_SetTrackStoppedCallback(track, finished, &completions));
	BOOST_REQUIRE(MIX_PlayTrack(track, 0));
	std::vector<float> output(3072);
	BOOST_REQUIRE(MIX_Generate(mixer, output.data(), output.size() * sizeof(float)) >= 0);
	BOOST_TEST(completions == 2u);
	BOOST_TEST(!MIX_TrackPlaying(track));
	BOOST_TEST(std::all_of(output.begin(), output.begin() + 2048, [](const float value) { return value == .25f; }));
	BOOST_TEST(std::all_of(output.begin() + 2048, output.end(), [](const float value) { return value == 0.f; }));
}
#endif
