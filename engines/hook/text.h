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

#ifndef HOOK_TEXT_H
#define HOOK_TEXT_H

#include "common/array.h"
#include "common/rect.h"
#include "common/str.h"
#include "graphics/surface.h"

#include "hook/resources.h"

namespace Hook {

/**
 * A language file (ENGLISH.BIN ...): u16 offset table; a text reference is a byte offset
 * into that table. A string is a u8 line count followed by that many NUL-terminated lines.
 */
class Language {
public:
	bool load(const Resources &res, const Common::String &name);
	/** Lines of the string for a text reference (empty if invalid). */
	Common::Array<Common::String> lines(uint16 ref) const;

private:
	Buffer _data;
};

/** A message: u16 count, then count x {u16 text ref, u16 0} (room files and the data segment). */
typedef Common::Array<uint16> Message;
Message readMessage(const byte *p, uint maxSize);

/**
 * The game font (code segment cs:39AA): 105 glyphs of 8x8 bytes, capitals only.
 * Pixel 0 transparent, 1 = text colour, n = palette colour n-1 (the outline).
 */
class Font {
public:
	static const int kHeight = 8;
	static const int kWidth = 8;

	void load(const Resources &res);
	void drawString(Graphics::Surface &dst, int x, int y, const Common::String &s, byte colour, const Common::Rect &clip) const;

private:
	const byte *glyph(byte c) const;
	const byte *_glyphs = nullptr;
};

} // End of namespace Hook

#endif // HOOK_TEXT_H
