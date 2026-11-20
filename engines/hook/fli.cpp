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

#include "common/endian.h"
#include "common/textconsole.h"

#include "hook/fli.h"

namespace Hook {

enum {
	kFliHeaderSize = 128,
	kFrameMagic = 0xF1FA,
	kChunkColor = 11,
	kChunkLineDelta = 12,
	kChunkBlack = 13,
	kChunkByteRun = 15,
	kChunkCopy = 16
};

bool FliDecoder::load(Buffer &data) {
	if (data.size() < kFliHeaderSize || READ_LE_UINT16(data.data() + 4) != 0xAF11 ||
		READ_LE_UINT16(data.data() + 8) != kWidth || READ_LE_UINT16(data.data() + 10) != kHeight)
		return false;
	_data.swap(data);
	_frames = READ_LE_UINT16(_data.data() + 6);
	memset(_palette, 0, sizeof(_palette));
	rewind();
	return true;
}

void FliDecoder::rewind() {
	_frame = 0;
	_pos = kFliHeaderSize;
	memset(_pixels, 0, sizeof(_pixels));
}

bool FliDecoder::nextFrame() {
	if (_frame >= _frames || _pos + 16 > _data.size())
		return false;
	const byte *f = _data.data() + _pos;
	uint32 size = READ_LE_UINT32(f);
	if (READ_LE_UINT16(f + 4) != kFrameMagic || size < 16 || _pos + size > _data.size()) {
		warning("Hook: bad FLI frame %u", _frame);
		_frame = _frames;
		return false;
	}
	uint chunks = READ_LE_UINT16(f + 6);
	const byte *p = f + 16, *end = f + size;
	for (uint i = 0; i < chunks && p + 6 <= end; i++) {
		uint32 csize = READ_LE_UINT32(p);
		if (csize < 6 || p + csize > end)
			break;
		const byte *c = p + 6, *cend = p + csize;
		switch (READ_LE_UINT16(p + 4)) {
		case kChunkColor:
			color(c, cend);
			break;
		case kChunkLineDelta:
			lineDelta(c, cend);
			break;
		case kChunkBlack:
			memset(_pixels, 0, sizeof(_pixels));
			break;
		case kChunkByteRun:
			byteRun(c, cend);
			break;
		case kChunkCopy:
			if (cend - c >= kWidth * kHeight)
				memcpy(_pixels, c, kWidth * kHeight);
			break;
		default:
			break;
		}
		p += csize;
	}
	_pos += size;
	_frame++;
	return true;
}

void FliDecoder::color(const byte *p, const byte *end) {
	// u16 packets, each {u8 skip, u8 count (0 = 256), count x 6-bit RGB}
	if (p + 2 > end)
		return;
	uint packets = READ_LE_UINT16(p);
	p += 2;
	uint index = 0;
	while (packets-- && p + 2 <= end) {
		index += *p++;
		uint n = *p++;
		if (!n)
			n = 256;
		for (; n && index < 256 && p + 3 <= end; n--, index++, p += 3)
			for (int k = 0; k < 3; k++)
				_palette[3 * index + k] = (p[k] & 0x3F) * 255 / 63;
	}
}

void FliDecoder::lineDelta(const byte *p, const byte *end) {
	// u16 first line, u16 line count; per line u8 packets {u8 skip, s8 n: n > 0 copy n bytes,
	// n < 0 repeat one byte -n times}
	if (p + 4 > end)
		return;
	int y = READ_LE_UINT16(p), lines = READ_LE_UINT16(p + 2);
	p += 4;
	for (; lines > 0 && y < kHeight && p < end; lines--, y++) {
		byte *row = _pixels + y * kWidth;
		int x = 0, packets = *p++;
		while (packets-- && p + 2 <= end) {
			x += *p++;
			int n = (int8)*p++;
			if (n > 0) {
				for (; n && p < end; n--, x++, p++)
					if (x < kWidth)
						row[x] = *p;
			} else {
				byte v = p < end ? *p++ : 0;
				for (; n; n++, x++)
					if (x < kWidth)
						row[x] = v;
			}
		}
	}
}

void FliDecoder::byteRun(const byte *p, const byte *end) {
	// per line: u8 packet count (ignored), then s8 n: n > 0 repeat one byte n times,
	// n < 0 copy -n bytes, until the line is full
	for (int y = 0; y < kHeight && p < end; y++) {
		byte *row = _pixels + y * kWidth;
		p++;
		int x = 0;
		while (x < kWidth && p < end) {
			int n = (int8)*p++;
			if (n > 0) {
				byte v = p < end ? *p++ : 0;
				for (; n && x < kWidth; n--)
					row[x++] = v;
			} else {
				for (; n && x < kWidth && p < end; n++)
					row[x++] = *p++;
			}
		}
	}
}

} // End of namespace Hook
