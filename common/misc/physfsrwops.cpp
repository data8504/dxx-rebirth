/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 * This code provides a glue layer between PhysicsFS and Simple Directmedia
 *  Layer's (SDL) RWops i/o abstraction.
 *
 * License: this code is public domain. I make no warranty that it is useful,
 *  correct, harmless, or environmentally safe.
 *
 * This particular file may be used however you like, including copying it
 *  verbatim into a closed-source project, exploiting it commercially, and
 *  removing any trace of my name from the source (although I hope you won't
 *  do that). I welcome enhancements and corrections to this file, but I do
 *  not require you to send me patches if you make changes. This code has
 *  NO WARRANTY.
 *
 * Unless otherwise stated, the rest of PhysicsFS falls under the zlib license.
 *  Please see LICENSE in the root of the source tree.
 *
 * SDL falls under the LGPL license. You can get SDL at https://www.libsdl.org/
 *
 *  This file was written by Ryan C. Gordon. (icculus@clutteredmind.org).
 */

#include <limits>
#include "physfsrwops.h"
#include "physfsx.h"

namespace {

static void physfs_error()
{
	SDL_SetError("PhysicsFS error: %s", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
}

static Sint64 SDLCALL physfs_size(void *userdata)
{
	const auto size{PHYSFS_fileLength(static_cast<PHYSFS_File *>(userdata))};
	if (size < 0)
		physfs_error();
	return size;
}

static Sint64 SDLCALL physfs_seek(void *userdata, const Sint64 offset, const SDL_IOWhence whence)
{
	auto *const handle{static_cast<PHYSFS_File *>(userdata)};
	Sint64 base;
	switch (whence)
	{
		case SDL_IO_SEEK_SET: base = 0; break;
		case SDL_IO_SEEK_CUR: base = PHYSFS_tell(handle); break;
		case SDL_IO_SEEK_END: base = PHYSFS_fileLength(handle); break;
		default:
			SDL_SetError("Invalid seek origin.");
			return -1;
	}
	if (base < 0)
	{
		physfs_error();
		return -1;
	}
	if (offset < -base || offset > std::numeric_limits<Sint64>::max() - base)
	{
		SDL_SetError("Seek position is outside the supported range.");
		return -1;
	}
	const auto position{base + offset};
	if (!PHYSFS_seek(handle, static_cast<PHYSFS_uint64>(position)))
	{
		physfs_error();
		return -1;
	}
	return position;
}

static size_t SDLCALL physfs_read(void *userdata, void *buffer, const size_t size, SDL_IOStatus *status)
{
	auto *const handle{static_cast<PHYSFS_File *>(userdata)};
	if (!std::in_range<PHYSFS_sint64>(size))
	{
		SDL_SetError("I/O request is too large.");
		*status = SDL_IO_STATUS_ERROR;
		return 0;
	}
	const auto count{PHYSFS_readBytes(handle, buffer, size)};
	if (count < 0)
	{
		physfs_error();
		*status = SDL_IO_STATUS_ERROR;
		return 0;
	}
	if (static_cast<size_t>(count) < size)
	{
		if (PHYSFS_eof(handle))
			*status = SDL_IO_STATUS_EOF;
		else
		{
			physfs_error();
			*status = SDL_IO_STATUS_ERROR;
		}
	}
	return count;
}

static size_t SDLCALL physfs_write(void *, const void *, size_t, SDL_IOStatus *status)
{
	SDL_SetError("This PhysicsFS stream is read-only.");
	*status = SDL_IO_STATUS_ERROR;
	return 0;
}

static bool SDLCALL physfs_close(void *userdata)
{
	if (PHYSFS_close(static_cast<PHYSFS_File *>(userdata)))
		return true;
	physfs_error();
	return false;
}

}

std::pair<RWops_ptr, PHYSFS_ErrorCode> PHYSFSRWOPS_openRead(const char *fname)
{
	RAIIPHYSFS_File handle{PHYSFS_openRead(fname)};
	if (!handle)
	{
		const auto error{PHYSFS_getLastErrorCode()};
		SDL_SetError("PhysicsFS error: %s", PHYSFS_getErrorByCode(error));
		return {nullptr, error};
	}
	SDL_IOStreamInterface interface;
	SDL_INIT_INTERFACE(&interface);
	interface.size = physfs_size;
	interface.seek = physfs_seek;
	interface.read = physfs_read;
	interface.write = physfs_write;
	interface.close = physfs_close;
	RWops_ptr stream{SDL_OpenIO(&interface, handle.get())};
	if (!stream)
		return {nullptr, PHYSFS_ERR_OTHER_ERROR};
	handle.release();
	return {std::move(stream), PHYSFS_ERR_OK};
}
