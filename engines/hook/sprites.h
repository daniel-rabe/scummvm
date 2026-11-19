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

#ifndef HOOK_SPRITES_H
#define HOOK_SPRITES_H

#include "common/array.h"
#include "common/rect.h"
#include "graphics/surface.h"

namespace Hook {

/**
 * Load-time colour remap (original: D75F): every run-length pixel byte v becomes xlat[v],
 * with a 256-byte window into the data segment starting at one of these offsets. The
 * tables overlap (values past the first 32 run into the next table), so the real bytes
 * must be used, not an offset. Raw frames are never remapped.
 */
enum XlatTable {
	kXlatPeter = 0x0695,	///< PANSPRIT/MOREPAN*, room banks +0x36/+0x42, +0x3C in some rooms
	kXlatTinker = 0x06B5,
	kXlatArrows = 0x06C5
};

struct Frame {
	int16 width = 0;
	int16 height = 0;
	int16 dx = 0;		///< draw offset added to the object position
	int16 dy = 0;
	uint16 flags = 0;
	Common::Array<byte> pixels;
	Common::Array<byte> mask;	///< 1 = opaque
};

/**
 * A sprite bank (docs/formats.md section 7): a frame table of {u16 offset, u16 paragraph}
 * entries followed by frames, each a 10-byte header {w, h, dx, dy, flags} plus either
 * run-length planar rows or (flags & 0x4000) raw w*h bytes.
 * All frames are decoded at load time.
 */
class SpriteBank {
public:
	/** `xlat`: 256-byte remap table, or nullptr for banks used as stored. */
	bool load(const byte *data, uint size, const byte *xlat);
	void clear() { _frames.clear(); _valid.clear(); }

	uint size() const { return _frames.size(); }
	const Frame *frame(int n) const;

	/** Draw a frame with its top-left at (x + dx, y + dy), clipped to `clip`. */
	static void draw(Graphics::Surface &dst, const Frame &f, int x, int y, const Common::Rect &clip);

private:
	bool decode(const byte *data, uint size, uint32 offset, const byte *xlat, Frame &f);

	Common::Array<Frame> _frames;
	Common::Array<bool> _valid;
};

} // End of namespace Hook

#endif // HOOK_SPRITES_H
