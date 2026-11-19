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

#include "common/debug.h"
#include "common/endian.h"

#include "hook/detection.h"
#include "hook/room.h"

namespace Hook {

// Fixed colours copied over every room palette by the engine (0C28C):
// DS:0751 -> BC..DB (Peter), DS:0745 -> DC..DF, DS:06E5 -> E0..FF (panel, icons).
static const uint16 kFixedPeter = 0x0751;
static const uint16 kFixedMisc = 0x0745;
static const uint16 kFixedPanel = 0x06E5;
static const uint kFirstFixedColour = 0xBC;

uint16 Room::word(uint off) const {
	return off + 2 <= _data.size() ? READ_LE_UINT16(_data.data() + off) : 0;
}

bool Room::load(const Resources &res, uint number) {
	if (number >= kNumRooms)
		return false;
	Common::String name = Common::String::format("LOCAT%02u.BIN", number);
	if (!res.load(name, _data))
		return false;
	_number = number;
	_canvasOffset = word(0x02) * 16;
	_width = word(0x1A);
	if (!_width || _canvasOffset >= _data.size())
		return false;
	// Some rooms store fewer than 200 rows; the canvas ends with the file.
	_height = MIN<uint>(200, (_data.size() - _canvasOffset) / _width);
	debugC(1, kDebugResources, "%s: canvas %ux%u", name.c_str(), _width, _height);
	return true;
}

void Room::getPalette(const Resources &res, byte *rgb) const {
	const byte *vga = _data.data() + word(0x46);
	for (uint i = 0; i < kFirstFixedColour * 3; i++)
		rgb[i] = vga[i] * 255 / 63;
	const byte *peter = res.ds(kFixedPeter), *misc = res.ds(kFixedMisc), *panel = res.ds(kFixedPanel);
	for (uint i = 0; i < 0x20 * 3; i++)
		rgb[0xBC * 3 + i] = peter[i] * 255 / 63;
	for (uint i = 0; i < 4 * 3; i++)
		rgb[0xDC * 3 + i] = misc[i] * 255 / 63;
	for (uint i = 0; i < 0x20 * 3; i++)
		rgb[0xE0 * 3 + i] = panel[i] * 255 / 63;
}

} // End of namespace Hook
