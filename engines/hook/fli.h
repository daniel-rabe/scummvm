/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef HOOK_FLI_H
#define HOOK_FLI_H

#include "hook/resources.h"

namespace Hook {

/**
 * Autodesk Animator FLI (magic 0xAF11, 320x200) as used by the intro. Video::FlicDecoder only
 * reads FLC files, and these use the older chunks: COLOR (11, 6-bit palette), LC (12, line
 * delta), BLACK (13), BRUN (15) and COPY (16). Playback timing is the engine's (playlist
 * frame delay), not the file's speed field.
 */
class FliDecoder {
public:
	static const int kWidth = 320;
	static const int kHeight = 200;

	bool load(Buffer &data);
	uint frameCount() const { return _frames; }
	/** Back to the first frame (the original restarts at the first frame's image chunk). */
	void rewind();
	/** Decode the next frame into pixels(); false past the last frame. */
	bool nextFrame();

	const byte *pixels() const { return _pixels; }
	/** 8-bit RGB, from the first COLOR chunk. */
	const byte *palette() const { return _palette; }

private:
	void color(const byte *p, const byte *end);
	void lineDelta(const byte *p, const byte *end);
	void byteRun(const byte *p, const byte *end);

	Buffer _data;
	uint _frames = 0;
	uint _frame = 0;
	uint32 _pos = 0;
	byte _pixels[kWidth * kHeight];
	byte _palette[256 * 3];
};

} // End of namespace Hook

#endif // HOOK_FLI_H
