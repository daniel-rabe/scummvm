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

// DS:419D: one byte per room; bank +0x3C is remapped only where it is set (0CE3D).
static const uint16 kRemapBank3CTable = 0x419D;

// Header offsets of the four sprite banks: u16 paragraph + size.
const uint16 Room::kBankHeader[Room::kNumBanks] = { 0x30, 0x36, 0x3C, 0x42 };

uint16 Room::word(uint off) const {
	return off + 2 <= _data.size() ? READ_LE_UINT16(_data.data() + off) : 0;
}

void Room::setWord(uint off, uint16 v) {
	if (off + 2 <= _data.size())
		WRITE_LE_UINT16(_data.data() + off, v);
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
	// The canvas runs to the end of the file (room 38: 372 rows; rooms 03/40 fewer than 200).
	// Header +1E is the scene height, +16 the vertical scroll limit (scene height - 128);
	// rows below the scene hold patch graphics for animations.
	_height = (_data.size() - _canvasOffset) / _width;
	_sceneHeight = MIN<uint>(word(0x1E) ? word(0x1E) : 128, _height);

	// Sprite banks: +30/+36/+3C carry a u32 size, +42 a u16 size.
	for (uint i = 0; i < kNumBanks; i++) {
		uint16 h = kBankHeader[i];
		_bankPara[i] = word(h);
		uint32 start = _bankPara[i] * 16;
		uint32 size = (h == 0x42) ? word(h + 2) : READ_LE_UINT32(_data.data() + h + 2);
		const byte *xlat = res.ds(kXlatPeter);
		if (h == 0x30 || (h == 0x3C && !res.ds(kRemapBank3CTable)[number]))
			xlat = nullptr;
		if (start < _data.size())
			_banks[i].load(_data.data() + start, MIN<uint32>(size, _data.size() - start), xlat);
		else
			_banks[i].clear();
	}

	_objects.clear();
	uint16 objBase = word(0x4C);
	for (uint i = 0; i < word(0x4A); i++) {
		uint off = objBase + 22 * i;
		RoomObject o;
		o.bankPara = word(off + 0x02);
		o.frame = sword(off + 0x04);
		o.x = sword(off + 0x06);
		o.y = sword(off + 0x0A);
		o.layer = word(off + 0x14);
		_objects.push_back(o);
	}

	_scripts.clear();
	uint16 scrBase = word(0x70);
	for (uint i = 0; i < word(0x6A); i++) {
		RoomScript s;
		s.pc = word(scrBase + 6 * i);
		s.object = word(scrBase + 6 * i + 4);
		_scripts.push_back(s);
	}

	debugC(1, kDebugResources, "%s: canvas %ux%u, scene height %u, %u objects, %u scripts",
		   name.c_str(), _width, _height, _sceneHeight, _objects.size(), _scripts.size());
	return true;
}

int Room::bankForPara(uint16 para) const {
	for (uint i = 0; i < kNumBanks; i++)
		if (_bankPara[i] == para && _banks[i].size())
			return i;
	return -1;
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
