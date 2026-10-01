/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 * This is an alternate backend for the sound effect system.
 * It uses SDL_mixer to provide a more reliable playback,
 * and allow processing of multiple audio formats.
 *
 * This file is based on the original D1X arch/sdl/digi.c
 *
 *  -- MD2211 (2006-10-12)
 */

#include <bitset>
#include <span>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include "pstypes.h"
#include "dxxerror.h"
#include "sounds.h"
#include "digi.h"
#include "digi_mixer.h"
#include "digi_mixer_music.h"
#include "console.h"
#include "config.h"
#include "args.h"

#include "maths.h"
#include "piggy.h"
#include "u_mem.h"
#include <memory>
#include "compiler-cf_assert.h"
#include "d_bitset.h"
#include "d_range.h"
#include "d_underlying_value.h"
#include "d_uspan.h"
#include "d_zip.h"

#define MIX_DIGI_DEBUG 0

#define DXX_FEATURE_INTERNAL_RESAMPLER (DXX_FEATURE_INTERNAL_RESAMPLER_EMULATE_SDL1 || DXX_FEATURE_INTERNAL_RESAMPLER_EMULATE_SOUNDBLASTER16)

namespace dcx {

namespace {

constexpr SDL_AudioFormat MIX_OUTPUT_FORMAT{SDL_AUDIO_S16};
constexpr int MIX_OUTPUT_CHANNELS{2};

/* In mixer mode, always request 44Khz.  This guarantees a need to upsample,
 * but allows passing into the mixing subsystem sounds that are natively higher
 * quality, like a user's personal music collection.
 *
 * 44Khz is an integer multiple of both the Descent 1 sounds (11Khz) and the
 * Descent 2 sounds (variously, 11Khz or 22Khz), so the upsample should be
 * straightforward.
 */
constexpr auto digi_sample_rate{underlying_value(sound_sample_rate::_44k)};
MIX_Mixer *mixer;
MIX_Track *music_track;
enumerated_array<MIX_Track *, 64, sound_channel> tracks;
enumerated_array<float, 64, sound_channel> channel_volume;
float effects_volume{1};

/* channel management */
static sound_channel digi_mixer_find_channel(const unsigned max_channels)
{
	uint8_t i{};
	for (; i < max_channels; ++i)
		if (!MIX_TrackPlaying(tracks[sound_channel{i}]))
			break;
	return sound_channel{i};
}

struct mixer_audio_deleter
{
	static void operator()(MIX_Audio *const audio)
	{
		MIX_DestroyAudio(audio);
	}
};
using mixer_audio_ptr = std::unique_ptr<MIX_Audio, mixer_audio_deleter>;
static std::array<mixer_audio_ptr, ::dsx::MAX_SOUNDS> SoundChunks;

static uint8_t fix2byte(const fix f)
{
	if (f >= UINT8_MAX << 8)
		/* Values greater than this would produce incorrect results if
		 * shifted and truncated.  As a special case, coerce such values
		 * to the largest representable return value.
		 */
		return UINT8_MAX;
	return f >> 8;
}

uint8_t digi_initialised;
constexpr unsigned digi_mixer_max_channels = std::size(tracks);

}

MIX_Mixer *digi_mixer_get_mixer() { return mixer; }
MIX_Track *digi_mixer_get_music_track() { return music_track; }

/* Initialise audio */
int digi_mixer_init()
{
#if MIX_DIGI_DEBUG
	con_printf(CON_DEBUG, "digi_init %u (SDL_Mixer)", MAX_SOUNDS.value);
#endif
	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) Error("SDL audio initialisation failed: %s.", SDL_GetError());
	if (!MIX_Init())
		return 1;
	const SDL_AudioSpec spec{MIX_OUTPUT_FORMAT, MIX_OUTPUT_CHANNELS, digi_sample_rate};
	mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
	if (!mixer)
	{
		//edited on 10/05/98 by Matt Mueller - should keep running, just with no sound.
		con_printf(CON_URGENT,"\nError: Couldn't open audio: %s", SDL_GetError());
		CGameArg.SndNoSound = 1;
		MIX_Quit();
		return 1;
	}

	/* The fixed effects pool stays separate from music and movie tracks. */
	for (auto &track : tracks)
		if (!(track = MIX_CreateTrack(mixer)))
		{
			MIX_DestroyMixer(std::exchange(mixer, nullptr));
			tracks = {};
			MIX_Quit();
			return 1;
		}
	if (!(music_track = MIX_CreateTrack(mixer)))
	{
		MIX_DestroyMixer(std::exchange(mixer, nullptr));
		tracks = {};
		MIX_Quit();
		return 1;
	}

	digi_initialised = 1;

	digi_mixer_set_digi_volume((CGameCfg.DigiVolume * 32768) / 8);

	return 0;
}

/* Shut down audio */
void digi_mixer_close() {
#if MIX_DIGI_DEBUG
	con_printf(CON_DEBUG, "digi_close (SDL_Mixer)");
#endif
	if (!digi_initialised) return;
	mix_free_music();
	digi_initialised = 0;
	MIX_DestroyMixer(std::exchange(mixer, nullptr));
	music_track = nullptr;
	tracks = {};
	for (auto &audio : SoundChunks)
		audio.reset();
	MIX_Quit();
}

namespace {

#if DXX_FEATURE_INTERNAL_RESAMPLER

enum class upscale_factor : uint8_t
{
	/* The enum values must be the ratio of the destination divided by the
	 * source, because the numeric value is used to compute how much buffer
	 * space to allocate.  Do not renumber these constants.
	 */
#if DXX_BUILD_DESCENT == 2
	from_22khz_to_44khz = 2,
#endif
	from_11khz_to_44khz = 4,
};

#if DXX_FEATURE_INTERNAL_RESAMPLER_EMULATE_SDL1
namespace emulate_sdl1 {

static unique_span<uint8_t> convert_audio(const std::span<const uint8_t> input, const std::size_t output_per_input)
{
	using output_type = int16_t;
	unique_span<uint8_t> result{input.size() * output_per_input * sizeof(output_type)};
	const auto result_span{result.span()};
	const std::span<output_type> output{reinterpret_cast<output_type *>(result_span.data()), result_span.size_bytes() / sizeof(output_type)};
	auto output_iter = output.begin();
	for (const auto input_value : input)
	{
		/* Assert that the minimum and maximum possible values in the input can
		 * be represented in the output without truncation. */
		using input_type = decltype(input)::value_type;
		constexpr int8_t convert_u8_to_s8{INT8_MIN};
		static_assert(std::in_range<output_type>(output_type{input_type{0}} + convert_u8_to_s8));
		static_assert(std::in_range<output_type>(output_type{input_type{UINT8_MAX}} + convert_u8_to_s8));
		const auto output_value = (output_type{input_value} + convert_u8_to_s8) << 8;
		const auto output_next_iter = std::next(output_iter, output_per_input);
		assert(output_iter != output.end());
		std::fill(std::exchange(output_iter, output_next_iter), output_next_iter, output_value);
	}
	assert(output_iter == output.end());
	return result;
}

}
#endif

#if DXX_FEATURE_INTERNAL_RESAMPLER_EMULATE_SOUNDBLASTER16
namespace emulate_soundblaster16 {

/*
 * Blackman windowed-sinc filter coefficients at 1/4 bandwidth of upsampled
 * frequency. Chosen for linear phase and approximates ~10th order IIR
 *
 * MATLAB/Octave code:

	N = 51;   % Num coeffs (odd)
	B = 0.25;  % 1/4 band
	half = (N-1)/2;
	n = (-half:half); % the sample index
	% Windowed sinc
	h_ideal = 2 * B .* sinc(B*n);
	h_win = blackman(N);
	b = h_win .* h_ideal.';
	% Convert to fix point to ultimately apply to signed 16-bit data
	b_s16 = int32(round(b * (2^16 -1)));  % coeffs!

 */
constexpr std::size_t FILTER_LEN = 51;
static constexpr std::array<int32_t, FILTER_LEN> coeffs_quarterband{{
		0, 0, -7, -25, -35, 0, 94, 200, 205, 0, -395, -751, -702, 0, 1178, 2127,
		1907, 0, -3050, -5490, -5011, 0, 9275, 20326, 29311, 32767, 29311,
		20326, 9275, 0, -5011, -5490, -3050, 0, 1907, 2127, 1178, 0, -702,
		-751, -395, 0, 205, 200, 94, 0, -35, -25, -7, 0, 0
}}
#if DXX_BUILD_DESCENT == 2
,
// Coefficient set for half-band (e.g. 22050 -> 44100)
coeffs_halfband{{
		0, 0, -11, 0, 49, 0, -133, 0, 290, 0, -558, 0, 992, 0, -1666, 0, 2697, 0,
		-4313, 0, 7086, 0, -13117, 0, 41452, 65535, 41452, 0, -13117, 0, 7086, 0,
		-4313, 0, 2697, 0, -1666, 0, 992, 0, -558, 0, 290, 0, -133, 0, 49, 0, -11,
		0, 0
}}
#endif
;

// Fixed-point FIR filtering
// Not optimal: consider optimization with 1/4, 1/2 band filters, and symmetric kernels
static auto filter_fir(const unique_span<int8_t> signal_storage, const std::span<const int32_t, FILTER_LEN> coeffs)
{
	const auto signal{signal_storage.span()};
	const std::size_t outsize = signal.size();
	unique_span<int16_t> result(outsize);
	const auto output{result.span()};
	// A FIR filter is just a 1D convolution
	// Keep only signalLen samples
	for (const auto nn : xrange(outsize))
	{
		// Determine start/stop indices for convolved chunk
		constexpr std::size_t coeffsLen = FILTER_LEN;
		/* Avoid use of `std::max` here, since `nn + 1 < coeffsLen` would cause
		 * unsigned subtraction to underflow.
		 */
		const std::size_t min_idx = (nn + 1 > coeffsLen ? nn + 1 - coeffsLen : 0u);
		const std::size_t max_idx = nn;
		if (min_idx > max_idx)
			continue;

		int32_t cur_output{0};  // Increase bit size for fixed point expansion
		// Sum over each sample * coefficient in this column
		for (const auto kk : xrange(min_idx, max_idx + 1))
		{
			const auto product = int32_t{signal[kk]} * coeffs[nn - kk];
			cur_output = cur_output + product;
		}

		// Save and fit back into int16
		output[nn] = static_cast<int16_t>(cur_output >> 8);  // Arithmetic shift
	}
	return result;
}

static auto upsample(const std::span<const uint8_t> input, const upscale_factor upFactor)
{
	const std::size_t factor = underlying_value(upFactor);
	switch (upFactor)
	{
		case upscale_factor::from_11khz_to_44khz:
#if DXX_BUILD_DESCENT == 2
		case upscale_factor::from_22khz_to_44khz:
#endif
			break;
		default:
			cf_assert(false);
	}
	const auto upsampledLen = input.size() * factor;
	/* Caution: `output` is sparsely initialized, so the value-initialization
	 * from `make_unique` is necessary.  This site cannot be converted to
	 * `make_unique_for_overwrite`.
	 */
	unique_span<int8_t> result(upsampledLen);
	const auto output{result.span()};
	using output_type = decltype(output)::value_type;
	for (const auto ii : xrange(input.size()))
	{
		/* Assert that the minimum and maximum possible values in the input can
		 * be represented in the output without truncation. */
		using conversion_type = int16_t;
		using input_type = decltype(input)::value_type;
		constexpr int8_t convert_u8_to_s8{INT8_MIN};
		static_assert(std::in_range<output_type>(conversion_type{input_type{0}} + convert_u8_to_s8));
		static_assert(std::in_range<output_type>(conversion_type{input_type{UINT8_MAX}} + convert_u8_to_s8));
		// Save input sample, convert to signed
		output[ii*factor] = conversion_type{input[ii]} + convert_u8_to_s8;
	}
	return result;
}

static auto replicateChannel(const unique_span<int16_t> input_storage, const std::size_t output_per_input)
{
	const std::size_t chFactor = MIX_OUTPUT_CHANNELS;
	const auto input{input_storage.span()};
	using output_type = int16_t;
	unique_span<uint8_t> result{output_per_input * sizeof(output_type)};
	const auto output = reinterpret_cast<output_type *>(result.get());
	for (const auto ii : xrange(input.size()))
	{
		// Duplicate and interleave as many channels as needed
		std::fill_n(std::next(output, ii * chFactor), chFactor, input[ii]);
	}
	return result;
}

static auto convert_audio(const std::span<const uint8_t> input, const std::size_t output_per_input, const upscale_factor upFactor)
{
	// We expect a 4x upscaling 11025 -> 44100
	// But maybe 2x for d2x in some cases
	auto &coeffs =
#if DXX_BUILD_DESCENT == 2
		(upFactor == upscale_factor::from_22khz_to_44khz)
		? coeffs_halfband
		/* Otherwise, assume upscale_factor::from_11khz_to_44khz */
		:
#endif
		coeffs_quarterband;

	return replicateChannel(
	// First upsample
	// Apply LPF filter to smooth out upscaled points
	// There will be some uniform amplitude loss here, but less than -3dB
		filter_fir(upsample(input, upFactor), coeffs),
		output_per_input);
}

}
#endif
#endif

#if DXX_FEATURE_EXTERNAL_RESAMPLER_SDL_NATIVE
namespace sdl_native {

static unique_span<uint8_t> convert_audio(const sound_effect sound_idx, const std::span<const uint8_t> data, const sound_sample_rate ssfreq)
{
	const int freq{underlying_value(ssfreq)};
	const SDL_AudioSpec source{SDL_AUDIO_U8, 1, freq};
	const SDL_AudioSpec destination{MIX_OUTPUT_FORMAT, MIX_OUTPUT_CHANNELS, digi_sample_rate};
	Uint8 *converted{};
	int length{};
	if (!SDL_ConvertAudioSamples(&source, data.data(), data.size(), &destination, &converted, &length))
	{
		con_printf(CON_URGENT, "SDL_ConvertAudioSamples failed for sound %u: %s", sound_idx, SDL_GetError());
		return {};
	}
	/* SDL allocates with SDL_malloc; unique_span owns a new[] buffer. */
	unique_span<uint8_t> result{static_cast<std::size_t>(length)};
	memcpy(result.get(), converted, length);
	SDL_free(converted);
	return result;
}

}
#endif

}

}

namespace dsx {

namespace {

/*
 * Play-time conversion. Performs output conversion only once per sound effect used.
 * Once the sound sample has been converted, it is cached in SoundChunks[]
 */
static void mixdigi_convert_sound(const sound_effect sound_idx, mixer_audio_ptr &sci, const digi_sound &gs, const sound_sample_rate freq)
{
	const auto data = gs.span();
	if (data.empty())
		return;

	digi_mixer_method method = CGameArg.SndMixerMethod;
	unique_span<uint8_t> cvtbuf;
#if !DXX_FEATURE_EXTERNAL_RESAMPLER_SDL_NATIVE
	(void)sound_idx;
#endif
	switch (method)
	{
#if DXX_FEATURE_INTERNAL_RESAMPLER
#if DXX_FEATURE_INTERNAL_RESAMPLER_EMULATE_SDL1
		case digi_mixer_method::emulate_sdl1:
#endif
#if DXX_FEATURE_INTERNAL_RESAMPLER_EMULATE_SOUNDBLASTER16
		case digi_mixer_method::emulate_soundblaster16:
#endif
			{
				/* Only a small set of conversions are supported.  List them out
				 * explicitly instead of using division.  This also allows the
				 * conversion factor to be an `enum class`, which emphasizes its
				 * limited legal values.
				 */
				const auto upFactor = ({
					upscale_factor r;
					if (freq == sound_sample_rate::_11k)
						r = upscale_factor::from_11khz_to_44khz;
#if DXX_BUILD_DESCENT == 2
					else if (freq == sound_sample_rate::_22k)
						r = upscale_factor::from_22khz_to_44khz;
#endif
					else
						return;
					r;
					});
				const std::size_t output_per_input = underlying_value(upFactor) * MIX_OUTPUT_CHANNELS;
				switch (method)
				{
#if DXX_FEATURE_EXTERNAL_RESAMPLER_SDL_NATIVE
					case digi_mixer_method::sdl_native:
						/* This is unreachable, since the case labels of the
						 * outer switch only match emulation paths.
						 */
						return;
#endif
#if DXX_FEATURE_INTERNAL_RESAMPLER_EMULATE_SDL1
					case digi_mixer_method::emulate_sdl1:
						cvtbuf = emulate_sdl1::convert_audio(data, output_per_input);
						break;
#endif
#if DXX_FEATURE_INTERNAL_RESAMPLER_EMULATE_SOUNDBLASTER16
					case digi_mixer_method::emulate_soundblaster16:
						cvtbuf = emulate_soundblaster16::convert_audio(data, data.size() * output_per_input, upFactor);
						break;
#endif
				}
			}
			break;
#endif
#if DXX_FEATURE_EXTERNAL_RESAMPLER_SDL_NATIVE
		case digi_mixer_method::sdl_native:
			cvtbuf = sdl_native::convert_audio(sound_idx, data, freq);
			break;
#endif
	}
	if (!cvtbuf.size())
		return;
	const SDL_AudioSpec spec{MIX_OUTPUT_FORMAT, MIX_OUTPUT_CHANNELS, digi_sample_rate};
	sci.reset(MIX_LoadRawAudio(mixer, cvtbuf.get(), cvtbuf.size(), &spec));
}

static MIX_Audio *mixdigi_convert_sound(const sound_effect i)
{
	auto &sci = SoundChunks[i];
	if (!sci)
	{
		auto &gs = GameSounds[i];
#if DXX_BUILD_DESCENT == 1
		const auto freq{gs.freq};
#elif DXX_BUILD_DESCENT == 2
		const auto freq{GameArg.SndDigiSampleRate};
#endif
		//proceed only if not converted yet
		mixdigi_convert_sound(i, sci, gs, freq);
	}
	return sci.get();
}

}

// Volume 0-F1_0
sound_channel digi_mixer_start_sound(sound_effect soundnum, const fix volume, const sound_pan pan, const int looping, const int loop_start, const int loop_end, sound_object *)
{
	if (!digi_initialised)
		return sound_channel::None;

	if (soundnum == sound_effect::None)
		return sound_channel::None;

	const unsigned max_channels = digi_mixer_max_channels;
	if (max_channels > tracks.size())
		return sound_channel::None;
	const auto c = digi_mixer_find_channel(max_channels);
	const auto channel = underlying_value(c);
	if (channel >= max_channels)
		return sound_channel::None;

	const auto audio = mixdigi_convert_sound(soundnum);
	if (!audio)
		return sound_channel::None;

	const int mix_pan = fix2byte(static_cast<fix>(pan));
#if MIX_DIGI_DEBUG
	con_printf(CON_DEBUG, "digi_start_sound %d, volume=%d, pan %d (start=%d, end=%d)", soundnum, volume, mix_pan, loop_start, loop_end);
#else
	(void)loop_start;
	(void)loop_end;
#endif

	const auto track = tracks[c];
	MIX_SetTrackAudio(track, audio);
	const MIX_StereoGains gains{(255 - mix_pan) / 255.f, mix_pan / 255.f};
	MIX_SetTrackStereo(track, &gains);
	channel_volume[c] = fix2byte(volume) / 255.f;
	MIX_SetTrackGain(track, channel_volume[c] * effects_volume);
	const auto options = SDL_CreateProperties();
	SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, looping ? -1 : 0);
	const auto played = MIX_PlayTrack(track, options);
	SDL_DestroyProperties(options);
	if (!played)
		return sound_channel::None;
	return c;
}

}

namespace dcx {

void digi_mixer_set_channel_volume(const sound_channel channel, const int volume)
{
	if (!digi_initialised) return;
	channel_volume[channel] = fix2byte(volume) / 255.f;
	MIX_SetTrackGain(tracks[channel], channel_volume[channel] * effects_volume);
}

void digi_mixer_set_channel_pan(const sound_channel channel, const sound_pan pan)
{
	int mix_pan = fix2byte(static_cast<fix>(pan));
	if (!digi_initialised) return;
	const MIX_StereoGains gains{(255 - mix_pan) / 255.f, mix_pan / 255.f};
	MIX_SetTrackStereo(tracks[channel], &gains);
}

void digi_mixer_stop_sound(const sound_channel channel)
{
	if (!digi_initialised) return;
	const auto c = underlying_value(channel);
#if MIX_DIGI_DEBUG
	con_printf(CON_DEBUG, "%s:%u: %d", __FUNCTION__, __LINE__, c);
#endif
	(void)c;
	MIX_StopTrack(tracks[channel], 0);
}

void digi_mixer_end_sound(const sound_channel channel)
{
	digi_mixer_stop_sound(channel);
}

void digi_mixer_set_digi_volume( int dvolume )
{
	/*
	* Double-scale Mix_Volume to more closely match old pre-20a0166c Rebirth builds
	* This also preserves the (unintentionally?) more accurate percieved volume scaling of those builds
	* (e.g. perceived sound volume will match perceived midi volume more closely)
	*/
	digi_volume = dvolume;
	if (!digi_initialised) return;
	effects_volume = std::min<unsigned>(128, fix2byte(fixmul(dvolume, dvolume))) / 128.f;
	for (uint8_t i = 0; i != tracks.size(); ++i)
	{
		const sound_channel channel{i};
		MIX_SetTrackGain(tracks[channel], channel_volume[channel] * effects_volume);
	}
}

int digi_mixer_is_channel_playing(const sound_channel c)
{
	return digi_initialised && MIX_TrackPlaying(tracks[c]);
}

void digi_mixer_stop_all_channels()
{
	if (!digi_initialised) return;
	for (const auto track : tracks)
		MIX_StopTrack(track, 0);
}

}
