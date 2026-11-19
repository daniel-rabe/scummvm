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
	// Conversation actions 0..16 (cs:8320)
	case 0x870D:	// 0: the gold tooth is pulled
		startSequence(0x41D3);
		return;
	case 0x8719:
		setDsWord(kDsFlags + 2 * 7, 0);
		return;
	case 0x8720:
		setDsWord(kDsFlags + 2 * 7, 1);
		return;
	case 0x8727:	// item 9 (nine gold pieces) becomes item 5 (a single one); flag 8 = 0
		for (int i = 0; i < 16; i++)
			if (dsWord(0x870B + 2 * i) == 9) {
				setDsWord(0x870B + 2 * i, 5);
				break;
			}
		setDsWord(kDsFlags + 2 * 8, 0);
		return;
	case 0x8745:
		startSequence(0x43E5);
		return;
	case 0x8751:
		startSequence(0x6E47);
		return;
	case 0x8759:	// item 27 (Tootle's marbles); flag 21 = 1, flag 25 = 0
		addItem(0x1B);
		setDsWord(kDsFlags + 2 * 21, 1);
		setDsWord(kDsFlags + 2 * 25, 0);
		return;
	case 0x87A7:
		startSequence(0x7D65);
		return;
	case 0x87AF:
		startSequence(0x7D77);
		return;
	case 0x87B7:
		startSequence(0x7D89);
		return;
	case 0x87BF:
		startSequence(0x7D9B);
		return;
	case 0x87C7:
		startSequence(0x7DAD);
		return;
	case 0x87CF:
		startSequence(0x8463);
		return;
	case 0x87D7:
		startSequence(0x8579);
		return;
	case 0x87DF:
		startSequence(0x81C3);
		return;
	case 0x87E7:	// 15: on to room 41, entry 0
		setDsWord(0x86F9, 0);
		setDsWord(0x86F7, 0x29);
		_roomCountdown = 1;
		return;
	case 0x8342:	// 16
		startSequence(0x858D);
		return;
	default:
		warning("Hook: game logic routine %04X is not ported yet", address);
		return;
	}
}

} // End of namespace Hook
