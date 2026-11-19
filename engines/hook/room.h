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
#include "hook/sprites.h"

namespace Hook {

/** A room object record (22 bytes at header +0x4C). */
struct RoomObject {
	uint16 bankPara;	///< +02: paragraph of one of the four room banks
	int16 frame;		///< +04: -1 = hidden
	int16 x;			///< +06
	int16 y;			///< +0A
	uint16 layer;		///< +14: 4 or 5 (meaning not known yet)
};

/** An animation-script entry (6 bytes at header +0x70). */
struct RoomScript {
	uint16 pc;			///< start offset in the room file
	uint16 object;		///< 1-based object number
};

/**
 * A LOCATxx.BIN room file (after RNC unpacking); see docs/formats.md section 6.
 * The data stays mutable: the REPEAT script opcode keeps its counter in the file image.
 */
class Room {
public:
	static const uint kNumRooms = 42;
	static const uint kNumBanks = 4;
	static const uint16 kBankHeader[kNumBanks];

	bool load(const Resources &res, uint number);

	uint number() const { return _number; }
	uint width() const { return _width; }
	uint height() const { return _height; }
	uint sceneHeight() const { return _sceneHeight; }
	const byte *canvas() const { return _data.data() + _canvasOffset; }

	/** 8-bit RGB palette: room colours 00..BB plus the engine's fixed colours BC..FF. */
	void getPalette(const Resources &res, byte *rgb) const;

	const SpriteBank &bank(uint i) const { return _banks[i]; }
	/** Bank index (0..3) for a bank paragraph as stored in object records, or -1. */
	int bankForPara(uint16 para) const;

	const Common::Array<RoomObject> &objects() const { return _objects; }
	const Common::Array<RoomScript> &scripts() const { return _scripts; }

	Buffer &data() { return _data; }
	const Buffer &data() const { return _data; }
	uint16 word(uint off) const;
	int16 sword(uint off) const { return (int16)word(off); }
	void setWord(uint off, uint16 v);

private:
	Buffer _data;
	uint _number = 0;
	uint _canvasOffset = 0;
	uint _width = 0;
	uint _height = 0;
	uint _sceneHeight = 0;
	SpriteBank _banks[kNumBanks];
	uint16 _bankPara[kNumBanks] = { 0, 0, 0, 0 };
	Common::Array<RoomObject> _objects;
	Common::Array<RoomScript> _scripts;
};

} // End of namespace Hook

#endif // HOOK_ROOM_H
