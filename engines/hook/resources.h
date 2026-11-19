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

#ifndef HOOK_RESOURCES_H
#define HOOK_RESOURCES_H

#include "common/array.h"
#include "common/file.h"
#include "common/str.h"

namespace Hook {

typedef Common::Array<byte> Buffer;

/**
 * Everything the game reads comes out of HOOK.EXE:
 *  - the executable itself is RNC-packed (MZ header + RNC stream + unpacker stub); its
 *    unpacked load module holds the code and the data segment with all game tables;
 *  - an EJWLIB overlay appended after the executable holds the 81 resource files
 *    (rooms, sprites, language files...), each RNC-packed except HOOK.SAV.
 * See docs/formats.md sections 2 and 3 in the reverse-engineering project.
 */
class Resources {
public:
	/** Offset of the data segment (DS = 0DFD) in the unpacked load module. */
	static const uint32 kDataSegment = 0x0DFD * 16;

	bool open(const Common::Path &exeName);

	/** Load a resource from the overlay by name (case-insensitive), RNC-unpacked. */
	bool load(const Common::String &name, Buffer &out) const;
	bool exists(const Common::String &name) const;
	uint count() const { return _dir.size(); }
	const Common::String &name(uint i) const { return _dir[i].name; }

	/** The unpacked load module (code segment 0 starts at offset 0). */
	const Buffer &image() const { return _image; }
	const byte *ds(uint16 off) const { return _image.data() + kDataSegment + off; }
	uint16 dsWord(uint16 off) const;

	/** Unpack an RNC method-1 buffer in place of `out`; false if it is not valid RNC. */
	static bool unpackRnc(const byte *src, uint32 srcSize, Buffer &out);

private:
	struct Entry {
		Common::String name;
		uint32 offset;	// absolute offset in HOOK.EXE
		uint32 size;
	};

	bool readOverlay(Common::File &f, uint32 base);

	Common::Path _exeName;
	Common::Array<Entry> _dir;
	Buffer _image;
};

} // End of namespace Hook

#endif // HOOK_RESOURCES_H
