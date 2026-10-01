/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 * Header file for music playback through SDL_mixer
 *
 *  -- MD2211 (2006-04-24)
 */

#pragma once

#ifdef __cplusplus
struct MIX_Mixer;
struct MIX_Track;
namespace dcx {

/* The effects backend owns the mixer; music and movies own their tracks. */
MIX_Mixer *digi_mixer_get_mixer();
MIX_Track *digi_mixer_get_music_track();

int mix_play_music(const char *, int);
int mix_play_file(const char *, int, void (*)());
void mix_set_music_volume(int);
void mix_stop_music();
void mix_pause_music();
void mix_resume_music();
void mix_pause_resume_music();
void mix_free_music();

}
#endif
