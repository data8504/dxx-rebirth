/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 * This is an alternate backend for the music system.
 * It uses SDL_mixer to provide a more reliable playback,
 * and allow processing of multiple audio formats.
 *
 *  -- MD2211 (2006-04-24)
 */

#include <span>
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <string.h>
#include <stdlib.h>

#include "args.h"
#include "digi.h"
#include "hmp.h"
#include "adlmidi_dynamic.h"
#include "digi_mixer_music.h"
#include "strutil.h"
#include "u_mem.h"
#include "config.h"
#include "console.h"
#include "physfsrwops.h"

/* MIX_LoadAudio_IO copies its source before returning, including encoded
 * music.  The IO wrapper and source buffer can therefore expire immediately. */
namespace dcx {

namespace {

struct Music_delete
{
	static void operator()(MIX_Audio *const audio) { MIX_DestroyAudio(audio); }
};

class current_music_t : public std::unique_ptr<MIX_Audio, Music_delete>
{
	using music_pointer = std::unique_ptr<MIX_Audio, Music_delete>;
public:
	std::vector<uint8_t> musicbuf;
	using music_pointer::reset;
	[[nodiscard]] bool reset(RWops_ptr rw)
	{
		if (!rw)
		{
			music_pointer::reset();
			return false;
		}
		music_pointer::reset(MIX_LoadAudio_IO(digi_mixer_get_mixer(), rw.get(), false, false));
		return static_cast<bool>(*this);
	}
};

static current_music_t current_music;
static void (*music_finished_hook)();

static void music_finished(void *, MIX_Track *)
{
	if (const auto hook = music_finished_hook)
		hook();
}

static bool play_music_track(const int loop, void (*const hook)())
{
	const auto track = digi_mixer_get_music_track();
	music_finished_hook = hook;
	MIX_SetTrackStoppedCallback(track, music_finished, nullptr);
	const auto options = SDL_CreateProperties();
	SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, loop ? -1 : 0);
	const auto played = MIX_PlayTrack(track, options);
	SDL_DestroyProperties(options);
	return played;
}

static bool mix_set_music_type_sdlmixer(int loop, void (*const hook_finished_track)())
{
	return MIX_SetTrackAudio(digi_mixer_get_music_track(), current_music.get())
		&& play_music_track(loop, hook_finished_track);
}

#if DXX_USE_ADLMIDI
static ADL_MIDIPlayer_t current_adlmidi;
struct music_stream_deleter
{
	static void operator()(SDL_AudioStream *const stream) { SDL_DestroyAudioStream(stream); }
};
static std::unique_ptr<SDL_AudioStream, music_stream_deleter> adlmidi_stream;
static ADL_MIDIPlayer *get_adlmidi()
{
	if (!CGameCfg.ADLMIDI_enabled)
		return nullptr;
	ADL_MIDIPlayer *adlmidi = current_adlmidi.get();
	if (!adlmidi)
	{
		SDL_AudioSpec spec;
		if (!MIX_GetMixerFormat(digi_mixer_get_mixer(), &spec))
			return nullptr;
		adlmidi = adl_init(spec.freq);
		if (adlmidi)
		{
			adl_switchEmulator(adlmidi, ADLMIDI_EMU_DOSBOX);
			adl_setNumChips(adlmidi, CGameCfg.ADLMIDI_num_chips);
			adl_setBank(adlmidi, CGameCfg.ADLMIDI_bank);
			adl_setSoftPanEnabled(adlmidi, 1);
			current_adlmidi.reset(adlmidi);
		}
	}
	return adlmidi;
}

static void mix_adlmidi(void *, SDL_AudioStream *, int, int);

static bool mix_set_music_type_adl(int loop, void (*const hook_finished_track)())
{
	ADL_MIDIPlayer *adlmidi = get_adlmidi();
	SDL_AudioSpec output;
	if (!adlmidi || !MIX_GetMixerFormat(digi_mixer_get_mixer(), &output))
		return false;
	const SDL_AudioSpec input{SDL_AUDIO_S16, 2, output.freq};
	adlmidi_stream.reset(SDL_CreateAudioStream(&input, &output));
	if (!adlmidi_stream || !SDL_SetAudioStreamGetCallback(adlmidi_stream.get(), mix_adlmidi, adlmidi)
		|| !MIX_SetTrackAudioStream(digi_mixer_get_music_track(), adlmidi_stream.get()))
		return false;
	adl_setLoopEnabled(adlmidi, loop);
	return play_music_track(0, hook_finished_track);
}
#endif

enum class CurrentMusicType : uint8_t
{
	None,
#if DXX_USE_ADLMIDI
	ADLMIDI,
#endif
	SDLMixer,
};

static CurrentMusicType current_music_type{CurrentMusicType::None};

static CurrentMusicType load_mus_data(const char *filename, std::span<const uint8_t> data, int loop, void (*const hook_finished_track)());
static CurrentMusicType load_mus_file(const char *filename, int loop, void (*const hook_finished_track)());

#ifdef _WIN32
// Windows native-MIDI stuff.
static std::unique_ptr<hmp_file> cur_hmp;
static uint8_t digi_win32_midi_song_playing;
static uint8_t already_playing;
#endif

}

/*
 *  Plays a music file from an absolute path or a relative path
 */

int mix_play_file(const char *filename, int loop, void (*const entry_hook_finished_track)())
{
	mix_free_music();	// stop and free what we're already playing, if anything

	const auto hook_finished_track = entry_hook_finished_track ? entry_hook_finished_track : mix_free_music;
	// It's a .hmp!
	if (const auto fptr = strrchr(filename, '.'); fptr && !d_stricmp(fptr, ".hmp"))
	{
		if (auto &&[v, hoe] = hmp2mid(filename); hoe == hmp_open_error::None)
		{
			current_music_type = load_mus_data(filename, v, loop, hook_finished_track);
			if (current_music_type != CurrentMusicType::None)
			{
				current_music.musicbuf = std::move(v);
				return 1;
			}
		}
		else
			/* hmp2mid printed an error message, so there is no need for another one here */
			return 0;
	}

	// try loading music via given filename
	{
		current_music_type = load_mus_file(filename, loop, hook_finished_track);
		if (current_music_type != CurrentMusicType::None)
			return 1;
	}

	// allow the shell convention tilde character to mean the user's home folder
	// chiefly used for default jukebox level song music referenced in 'descent.m3u' for Mac OS X
	if (*filename == '~')
	{
		const auto sep = PHYSFS_getDirSeparator();
		const auto lensep = strlen(sep);
		std::array<char, PATH_MAX> full_path;
		snprintf(full_path.data(), PATH_MAX, "%s%s", PHYSFS_getUserDir(),
				 &filename[1 + (!strncmp(&filename[1], sep, lensep)
			? lensep
			: 0)]);
		current_music_type = load_mus_file(full_path.data(), loop, hook_finished_track);
		if (current_music_type != CurrentMusicType::None)
			return 1;
	}

	// still nothin'? Let's open via PhysFS in case it's located inside an archive
	{
		if (RAIIPHYSFS_File filehandle{PHYSFS_openRead(filename)})
		{
			const auto len{PHYSFS_fileLength(filehandle)};
			/* Set an arbitrary cap on how large a single music file can be.
			 * This should be high enough that no normal music file can trigger
			 * it.  The cap is intended to prevent creating a very large
			 * allocation.
			 */
			if (len > 1024u * 1024u * 256u)
			{
				con_printf(CON_NORMAL, "error: music PhysFS file \"%s\" exceeds size limit of 256MB", filename);
				current_music_type = CurrentMusicType::None;
				return 1;
			}
			/* `current_music.musicbuf` is unused after the `mix_free_music`
			 * above.  The contents of the memory managed by `musicbuf` are
			 * undefined, but the vector may have `capacity() > 0`.  Use that
			 * potentially allocated buffer here, which may save a new
			 * allocation.
			 */
			current_music.musicbuf.resize(len);
			const auto bufsize{PHYSFSX_readBytes(filehandle, current_music.musicbuf.data(), len)};
			/* Adjust size to match what is actually used by the loaded data.
			 * This should be the same length as was used above, but set it to
			 * be thorough.
			 */
			current_music.musicbuf.resize(bufsize);
			current_music_type = load_mus_data(filename, current_music.musicbuf, loop, hook_finished_track);
			if (current_music_type != CurrentMusicType::None)
				return 1;
		}
		else
			con_printf(CON_VERBOSE, "warning: failed to open PhysFS file \"%s\"", filename);
	}

	con_printf(CON_CRITICAL, "Music %s could not be loaded: %s", filename, SDL_GetError());
	mix_stop_music();

	return 0;
}

// What to do when stopping song playback
void mix_free_music()
{
	mix_stop_music();
	const auto track = digi_mixer_get_music_track();
	if (track)
		MIX_SetTrackAudio(track, nullptr);
#if DXX_USE_ADLMIDI
	/* Detach the track before releasing the procedural source. */
	adlmidi_stream.reset();
#endif
	current_music.reset();
#if DXX_HAVE_POISON_VALGRIND || !defined(NDEBUG)
	/* In a debug build, destroy the storage buffer, so that any underlying
	 * allocation is freed and any stale pointers become dangling.  This may
	 * help analysis tools recognize those pointers as dangling.
	 *
	 * In a non-debug build, let the buffer remain as it was.  There should not
	 * be any dangling pointers, but if there are, they will be less likely to
	 * crash if the buffer remains intact.  Also, letting the buffer remain may
	 * avoid a new allocation on the next music load.
	 */
	current_music.musicbuf = {};
#endif
	current_music_type = CurrentMusicType::None;
}

void mix_set_music_volume(int vol)
{
	if (const auto track = digi_mixer_get_music_track())
		MIX_SetTrackGain(track, vol / 8.f);
}

void mix_stop_music()
{
	if (const auto track = digi_mixer_get_music_track())
	{
		/* Explicit stops must not advance a playlist or recurse into free. */
		MIX_SetTrackStoppedCallback(track, nullptr, nullptr);
		MIX_StopTrack(track, 0);
	}
	music_finished_hook = nullptr;
}

void mix_pause_music()
{
	MIX_PauseTrack(digi_mixer_get_music_track());
}

void mix_resume_music()
{
	MIX_ResumeTrack(digi_mixer_get_music_track());
}

void mix_pause_resume_music()
{
	const auto track = digi_mixer_get_music_track();
	if (MIX_TrackPaused(track))
		MIX_ResumeTrack(track);
	else if (MIX_TrackPlaying(track))
		MIX_PauseTrack(track);
}

namespace {

static CurrentMusicType load_mus_data(const char *const filename, const std::span<const uint8_t> data, int loop, void (*const hook_finished_track)())
{
#if DXX_USE_ADLMIDI
	const auto adlmidi = get_adlmidi();
	if (adlmidi && adl_openData(adlmidi, data.data(), data.size()) == 0)
	{
		if (mix_set_music_type_adl(loop, hook_finished_track))
			return CurrentMusicType::ADLMIDI;
	}
	else
#endif
	{
		if (current_music.reset(RWops_ptr{SDL_IOFromConstMem(data.data(), data.size())}))
		{
			if (mix_set_music_type_sdlmixer(loop, hook_finished_track))
				return CurrentMusicType::SDLMixer;
		}
		else
			con_printf(CON_VERBOSE, "warning: failed to load music from data from file \"%s\"", filename);
	}
	return CurrentMusicType::None;
}

static CurrentMusicType load_mus_file(const char *filename, int loop, void (*const hook_finished_track)())
{
#if DXX_USE_ADLMIDI
	const auto adlmidi = get_adlmidi();
	if (adlmidi && adl_openFile(adlmidi, filename) == 0)
	{
		if (mix_set_music_type_adl(loop, hook_finished_track))
			return CurrentMusicType::ADLMIDI;
	}
	else
#endif
	if (RWops_ptr rw{SDL_IOFromFile(filename, "rb")})
	{
		if (current_music.reset(std::move(rw)))
		{
			if (mix_set_music_type_sdlmixer(loop, hook_finished_track))
				return CurrentMusicType::SDLMixer;
		}
		else
			con_printf(CON_VERBOSE, "warning: failed to load music from filesystem file \"%s\"", filename);
	}
	else
		/* The caller speculatively tries to treat inputs variously as PhysFS
		 * files and filesystem files.  Failure to open this path as a
		 * filesystem file is not necessarily an error.
		 */
		con_printf(CON_VERBOSE, "warning: failed to open filesystem file \"%s\"", filename);
	return CurrentMusicType::None;
}

#if DXX_USE_ADLMIDI
static int16_t sat16(int32_t x)
{
	x = (x < INT16_MIN) ? INT16_MIN : x;
	x = (x > INT16_MAX) ? INT16_MAX : x;
	return x;
}

static void mix_adlmidi(void *const userdata, SDL_AudioStream *const stream, const int additional, int)
{
	if (additional <= 0)
		return;
	ADLMIDI_AudioFormat format;
	format.containerSize = sizeof(int16_t);
	format.sampleOffset = 2 * format.containerSize;
	format.type = ADLMIDI_SampleType_S16;

	auto *const adlmidi = static_cast<ADL_MIDIPlayer *>(userdata);
	const int requested_samples = ((additional + 3) / 4) * 2;
	auto samples = std::make_unique<int16_t[]>(requested_samples);
	auto bytes = reinterpret_cast<uint8_t *>(samples.get());
	const int sampleCount = adl_playFormat(adlmidi, requested_samples, bytes, bytes + format.containerSize, &format);
	if (sampleCount <= 0)
	{
		SDL_FlushAudioStream(stream);
		return;
	}
	const auto amplify = [](int16_t i) { return sat16(2 * i); };
	std::transform(samples.get(), samples.get() + sampleCount, samples.get(), amplify);
	SDL_PutAudioStreamData(stream, samples.get(), sampleCount * sizeof(int16_t));
	if (sampleCount < requested_samples)
		SDL_FlushAudioStream(stream);
}
#endif

}

#ifdef _WIN32
void digi_win32_pause_midi_song()
{
	hmp_pause(cur_hmp.get());
}

void digi_win32_resume_midi_song()
{
	hmp_resume(cur_hmp.get());
}

void digi_win32_stop_midi_song()
{
	if (!digi_win32_midi_song_playing)
		return;
	digi_win32_midi_song_playing = 0;
	cur_hmp.reset();
	hmp_reset();
}

void digi_win32_set_midi_volume( int mvolume )
{
	hmp_setvolume(cur_hmp.get(), mvolume*MIDI_VOLUME_SCALE/8);
}

int digi_win32_play_midi_song( const char * filename, int loop )
{
	if (!already_playing)
	{
		hmp_reset();
		already_playing = 1;
	}
	digi_win32_stop_midi_song();

	if (filename == NULL)
		return 0;

	if ((cur_hmp = std::get<0>(hmp_open(filename))))
	{
		/*
		 * FIXME: to be implemented as soon as we have some kind or checksum function - replacement for ugly hack in hmp.c for descent.hmp
		 * if (***filesize check*** && ***CRC32 or MD5 check***)
		 *	(((*cur_hmp).trks)[1]).data[6] = 0x6C;
		 */
		if (hmp_play(cur_hmp.get(),loop) != 0)
			return 0;	// error
		digi_win32_midi_song_playing = 1;
		digi_win32_set_midi_volume(CGameCfg.MusicVolume);
		return 1;
	}

	return 0;
}
#endif

}
