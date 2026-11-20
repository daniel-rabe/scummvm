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

#include "hook/text.h"

namespace Hook {

static const uint32 kFontOffset = 0x39AA;	// in the code segment (image offset)
static const uint kFontGlyphs = 105;

bool Language::load(const Resources &res, const Common::String &name) {
	return res.load(name, _data) && _data.size() > 2;
}

Common::Array<Common::String> Language::lines(uint16 ref) const {
	Common::Array<Common::String> out;
	if (ref == 0 || ref + 2u > _data.size() || ref >= READ_LE_UINT16(_data.data()))
		return out;
	uint32 p = READ_LE_UINT16(_data.data() + ref);
	if (p >= _data.size())
		return out;
	uint count = _data[p++];
	for (uint i = 0; i < count && p < _data.size(); i++) {
		Common::String line;
		bool cut = false;
		for (; p < _data.size() && _data[p]; p++) {
			byte c = _data[p];
			if (c < 0x20) {
				// FRENCH 136, SPANISH 8: the next string (count byte included) ran into this
				// one. The original draws code bytes as glyphs; the message ends here instead.
				cut = true;
				break;
			}
			if (c == 0xE1)
				line += "SS";	// GERMAN: CP437 sharp s has no glyph (the original draws code bytes)
			else
				line += (char)c;
		}
		p++;
		out.push_back(line);
		if (cut)
			break;
	}
	return out;
}

Message readMessage(const byte *p, uint maxSize) {
	Message m;
	if (maxSize < 2)
		return m;
	uint n = READ_LE_UINT16(p);
	for (uint i = 0; i < n && 2 + 4 * i + 2 <= maxSize; i++)
		m.push_back(READ_LE_UINT16(p + 2 + 4 * i));
	return m;
}

void Font::load(const Resources &res) {
	_glyphs = res.image().data() + kFontOffset;
}

const byte *Font::glyph(byte c) const {
	// 394D: a..z are drawn as capitals; bytes >= 0x80 (code page 437 accents) map to c - 0x25.
	if (c >= 'a' && c <= 'z')
		c &= 0x5F;
	if (c >= 0x80)
		c -= 0x25;
	if (c < 0x20 || (uint)(c - 0x20) >= kFontGlyphs)
		return nullptr;
	return _glyphs + 64 * (c - 0x20);
}

void Font::drawString(Graphics::Surface &dst, int x, int y, const Common::String &s, byte colour,
					  const Common::Rect &clip) const {
	for (uint i = 0; i < s.size(); i++, x += kWidth) {
		const byte *g = glyph((byte)s[i]);
		if (!g)
			continue;
		for (int gy = 0; gy < kHeight; gy++) {
			int ty = y + gy;
			if (ty < clip.top || ty >= clip.bottom)
				continue;
			byte *row = (byte *)dst.getBasePtr(0, ty);
			for (int gx = 0; gx < kWidth; gx++) {
				int tx = x + gx;
				byte v = g[gy * kWidth + gx];
				if (v && tx >= clip.left && tx < clip.right)
					row[tx] = (v == 1) ? colour : v - 1;
			}
		}
	}
}

} // End of namespace Hook
