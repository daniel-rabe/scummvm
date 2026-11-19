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

#include "common/scummsys.h"
#include "common/compression/rnc_deco.h"
#include "common/debug.h"
#include "common/endian.h"

#include "hook/detection.h"
#include "hook/resources.h"

namespace Hook {

static const uint kRncHeaderSize = 18;

/** Size of the MZ executable proper; the EJWLIB overlay starts right after it. */
static uint32 mzImageSize(const byte *hdr) {
	uint16 lastPage = READ_LE_UINT16(hdr + 2);
	uint16 pages = READ_LE_UINT16(hdr + 4);
	return lastPage ? (pages - 1) * 512 + lastPage : pages * 512;
}

bool Resources::unpackRnc(const byte *src, uint32 srcSize, Buffer &out) {
	if (srcSize < kRncHeaderSize || READ_BE_UINT32(src) != Common::RncDecoder::kRnc1Signature)
		return false;
	uint32 unpacked = READ_BE_UINT32(src + 4);
	uint32 packed = READ_BE_UINT32(src + 8);
	if (packed + kRncHeaderSize > srcSize)
		return false;
	out.resize(unpacked);
	Common::RncDecoder rnc;
	int32 result = rnc.unpackM1(src, packed + kRncHeaderSize, out.data());
	if (result != (int32)unpacked) {
		warning("Hook: RNC unpack failed (%d)", result);
		return false;
	}
	return true;
}

bool Resources::open(const Common::Path &exeName) {
	Common::File f;
	if (!f.open(exeName))
		return false;
	_exeName = exeName;

	byte hdr[0x20];
	if (f.read(hdr, sizeof(hdr)) != sizeof(hdr) || hdr[0] != 'M' || hdr[1] != 'Z')
		return false;
	uint32 exeSize = mzImageSize(hdr);

	// The packed executable: 0x20-byte MZ header followed by the RNC stream of the
	// original load module (the unpacker stub after it is not needed here).
	Buffer packed(exeSize - 0x20);
	f.seek(0x20);
	if (f.read(packed.data(), packed.size()) != packed.size() || !unpackRnc(packed.data(), packed.size(), _image)) {
		warning("Hook: cannot unpack the executable");
		return false;
	}
	debugC(1, kDebugResources, "Executable: %u bytes unpacked", _image.size());

	if (!readOverlay(f, exeSize))
		return false;
	debugC(1, kDebugResources, "Overlay: %u resources", _dir.size());
	return _image.size() > kDataSegment;
}

bool Resources::readOverlay(Common::File &f, uint32 base) {
	f.seek(base);
	uint16 count = f.readUint16LE();
	uint16 key = f.readUint16LE();
	char tag[6];
	if (f.read(tag, 6) != 6 || memcmp(tag, "EJWLIB", 6) != 0) {
		warning("Hook: no EJWLIB overlay in the executable");
		return false;
	}
	// Directory: count x 22 bytes, XOR'd with a running key (start = low byte,
	// increment = high byte) that continues across entries.
	byte k = key & 0xFF, inc = key >> 8;
	for (uint i = 0; i < count; i++) {
		byte e[22];
		if (f.read(e, sizeof(e)) != sizeof(e))
			return false;
		for (uint j = 0; j < sizeof(e); j++) {
			e[j] ^= k;
			k += inc;
		}
		Entry entry;
		entry.name = Common::String((const char *)e, strnlen((const char *)e, 14));
		entry.size = READ_LE_UINT32(e + 14);
		entry.offset = base + READ_LE_UINT32(e + 18);
		_dir.push_back(entry);
	}
	return true;
}

bool Resources::exists(const Common::String &name) const {
	for (const Entry &e : _dir)
		if (e.name.equalsIgnoreCase(name))
			return true;
	return false;
}

bool Resources::load(const Common::String &name, Buffer &out) const {
	for (const Entry &e : _dir) {
		if (!e.name.equalsIgnoreCase(name))
			continue;
		Common::File f;
		if (!f.open(_exeName))
			return false;
		Buffer raw(e.size);
		f.seek(e.offset);
		if (f.read(raw.data(), e.size) != e.size)
			return false;
		if (e.size >= 4 && READ_BE_UINT32(raw.data()) == Common::RncDecoder::kRnc1Signature)
			return unpackRnc(raw.data(), e.size, out);
		out = raw;	// HOOK.SAV is stored unpacked
		return true;
	}
	warning("Hook: resource %s not found", name.c_str());
	return false;
}

uint16 Resources::dsWord(uint16 off) const {
	return READ_LE_UINT16(ds(off));
}

} // End of namespace Hook
