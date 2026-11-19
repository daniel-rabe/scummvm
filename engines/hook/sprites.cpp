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
#include "hook/sprites.h"

namespace Hook {

bool SpriteBank::load(const byte *data, uint size, const byte *xlat) {
	clear();
	if (size < 4)
		return true;
	// The frame table has no count: it runs up to the lowest address an entry points at.
	// FFFF:FFFF entries are empty; an entry pointing into the table itself ends it.
	uint32 lowest = size;
	uint n = 0;
	while (4 * n + 4 <= MIN<uint32>(lowest, size)) {
		uint16 off = READ_LE_UINT16(data + 4 * n), para = READ_LE_UINT16(data + 4 * n + 2);
		if (off != 0xFFFF || para != 0xFFFF) {
			uint32 addr = para * 16 + off;
			if (addr < 4 * (n + 1))
				break;
			lowest = MIN(lowest, addr);
		}
		n++;
	}
	_frames.resize(n);
	_valid.resize(n);
	for (uint i = 0; i < n; i++) {
		uint16 off = READ_LE_UINT16(data + 4 * i), para = READ_LE_UINT16(data + 4 * i + 2);
		_valid[i] = (off != 0xFFFF || para != 0xFFFF) && decode(data, size, para * 16 + off, xlat, _frames[i]);
	}
	return true;
}

bool SpriteBank::decode(const byte *data, uint size, uint32 offset, const byte *xlat, Frame &f) {
	// The shipped data never uses the anchor/jump/mirror chain words (FFFE/FFFD/FFFF/FFFC)
	// that the original lookup (D80C) supports; they are rejected here.
	if (offset + 10 > size || READ_LE_UINT16(data + offset) >= 0xFFFC) {
		warning("Hook: unsupported sprite frame at %u", offset);
		return false;
	}
	const byte *h = data + offset;
	f.width = READ_LE_INT16(h);
	f.height = READ_LE_INT16(h + 2);
	f.dx = READ_LE_INT16(h + 4);
	f.dy = READ_LE_INT16(h + 6);
	f.flags = READ_LE_UINT16(h + 8);
	uint count = f.width * f.height;
	f.pixels.resize(count);
	f.mask.resize(count);
	for (uint i = 0; i < count; i++) {
		f.pixels[i] = 0;
		f.mask[i] = 0;
	}
	uint32 q = offset + 10;

	if (f.flags & 0x4000) {
		// Raw frame (raw blitter 15AA): colour 0 is transparent, no remap
		if (q + count > size)
			return false;
		for (uint i = 0; i < count; i++) {
			f.pixels[i] = data[q + i];
			f.mask[i] = data[q + i] ? 1 : 0;
		}
		return true;
	}

	// Run-length rows (blitter 1345): u8 length, then segments
	// [u8 x | 0x80 = more] followed by four planar runs {u8 n, n pixels};
	// run p covers columns x+p, x+p+4, ...
	for (int y = 0; y < f.height; y++) {
		if (q >= size)
			return false;
		uint32 end = q + 1 + data[q];
		uint32 r = q + 1;
		for (;;) {
			if (r >= end)
				return false;
			byte s = data[r++];
			int x0 = s & 0x7F;
			for (int p = 0; p < 4; p++) {
				if (r >= end)
					return false;
				byte n = data[r++];
				for (int i = 0; i < n; i++) {
					int x = x0 + p + 4 * i;
					if (x < f.width && r + i < end) {
						f.pixels[y * f.width + x] = xlat ? xlat[data[r + i]] : data[r + i];
						f.mask[y * f.width + x] = 1;
					}
				}
				r += n;
			}
			if (!(s & 0x80))
				break;
		}
		q = end;
	}
	return true;
}

const Frame *SpriteBank::frame(int n) const {
	if (n < 0 || n >= (int)_frames.size() || !_valid[n])
		return nullptr;
	return &_frames[n];
}

void SpriteBank::draw(Graphics::Surface &dst, const Frame &f, int x, int y, const Common::Rect &clip) {
	int left = x + f.dx, top = y + f.dy;
	for (int j = 0; j < f.height; j++) {
		int ty = top + j;
		if (ty < clip.top || ty >= clip.bottom)
			continue;
		byte *row = (byte *)dst.getBasePtr(0, ty);
		const byte *src = f.pixels.data() + j * f.width;
		const byte *msk = f.mask.data() + j * f.width;
		for (int i = 0; i < f.width; i++) {
			int tx = left + i;
			if (msk[i] && tx >= clip.left && tx < clip.right)
				row[tx] = src[i];
		}
	}
}

} // End of namespace Hook
