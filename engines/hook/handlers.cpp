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

// Hand-ported game logic. Every routine is named after its address in the original
// code segment and listed in docs/logic-ledger.md of the reverse-engineering project.

#include "common/debug.h"

#include "hook/hook.h"

namespace Hook {

// Game flags are words at DS:86B7 + 2 * id.
static const uint16 kDsFlags = 0x86B7;

void HookEngine::runHandler(uint16 address) {
	debugC(1, kDebugScript, "handler %04X", address);
	switch (address) {
	// LOOK events 0x4C02..0x4C04 (cs:86AF): the clues in room 38 set flags 10, 11, 12
	case 0x86BF:
		setDsWord(kDsFlags + 2 * 10, 1);
		return;
	case 0x86C6:
		setDsWord(kDsFlags + 2 * 11, 1);
		return;
	case 0x86CD:
		setDsWord(kDsFlags + 2 * 12, 1);
		return;
	default:
		warning("Hook: game logic routine %04X is not ported yet", address);
		return;
	}
}

} // End of namespace Hook
