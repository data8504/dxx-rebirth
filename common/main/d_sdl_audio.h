/*
 * Portions of this file are copyright Rebirth contributors and licensed as
 * described in COPYING.txt.
 * See COPYING.txt for license details.
 */

#pragma once
#include <SDL3/SDL_audio.h>

namespace dcx {

struct RAII_SDL_LockAudio
{
	SDL_AudioStream *const stream;
	explicit RAII_SDL_LockAudio(SDL_AudioStream *const s) : stream{s}
	{
		if (stream)
			SDL_LockAudioStream(stream);
	}
	~RAII_SDL_LockAudio()
	{
		if (stream)
			SDL_UnlockAudioStream(stream);
	}
	RAII_SDL_LockAudio(const RAII_SDL_LockAudio &) = delete;
	RAII_SDL_LockAudio &operator=(const RAII_SDL_LockAudio &) = delete;
};

}
