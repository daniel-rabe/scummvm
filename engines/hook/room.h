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

#ifndef HOOK_ROOM_H
#define HOOK_ROOM_H

#include "hook/resources.h"

namespace Hook {

/**
 * A LOCATxx.BIN room file (after RNC unpacking). Header offsets used here:
 *  +02 u16  background canvas offset (paragraphs)
 *  +1A u16  canvas stride (bytes per row = canvas width)
 *  +46 u16  palette offset (768 bytes, 6-bit VGA; only colours 00..BB are the room's)
 */
class Room {
public:
	static const uint kNumRooms = 42;

	bool load(const Resources &res, uint number);

	uint number() const { return _number; }
	uint width() const { return _width; }
	uint height() const { return _height; }
	const byte *canvas() const { return _data.data() + _canvasOffset; }

	/** 8-bit RGB palette: room colours 00..BB plus the engine's fixed colours BC..FF. */
	void getPalette(const Resources &res, byte *rgb) const;

	const Buffer &data() const { return _data; }
	uint16 word(uint off) const;

private:
	Buffer _data;
	uint _number = 0;
	uint _canvasOffset = 0;
	uint _width = 0;
	uint _height = 0;
};

} // End of namespace Hook

#endif // HOOK_ROOM_H
