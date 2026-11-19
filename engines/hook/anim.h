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

#ifndef HOOK_ANIM_H
#define HOOK_ANIM_H

#include "common/array.h"

namespace Hook {

class Room;

enum BankId {
	kBankNone = -1,
	kBankRoom0 = 0,		///< room banks 0..3 = header +30/+36/+3C/+42
	kBankPeter = 4,		///< PANSPRIT.BIN
	kBankTinker = 5,	///< TINKER.BIN
	kNumBankIds
};

/** An engine sprite slot (original: 22-byte slots at DS:13FA actors / DS:147E objects). */
struct Actor {
	int bank = kBankNone;	///< +00 (original: pointer to a bank-segment variable)
	int16 frame = -1;		///< +04, -1 = hidden
	int16 x = 0;			///< +06 canvas x
	int16 y = 0;			///< +0A canvas y
	int16 depth = 0;		///< +0E y + frame height + dy, for draw order
};

/**
 * Room animation-script VM (original 066FA..068E7), one tick per game frame.
 * Opcodes: 2/3 MOVE_LEFT/RIGHT, 8 WAIT, 9 JUMP, 0A SET, 0B HIDE, 0C REPEAT, 0D RESET_TIMER;
 * every other opcode halts the script. See docs/formats.md section 5.
 */
class AnimVM {
public:
	void start(const Room &room);
	void tick(Room &room, Common::Array<Actor> &objects);
	uint16 phase() const { return _phase; }
	/** Move script slot `slot`'s pc by `delta` bytes (room 0's hook switches scripts this way). */
	void movePc(uint slot, int delta) { if (slot < _slots.size()) _slots[slot].pc += delta; }

private:
	struct Slot {
		uint16 pc;
		uint16 object;		///< 1-based
		uint16 repeat;
		uint32 timer;
	};

	/** Execute one opcode; true = continue with the next opcode in the same tick. */
	bool step(Room &room, Slot &s, Actor &a);

	Common::Array<Slot> _slots;
	uint16 _phase = 0;		///< DS:1728, cycles 0..7
};

} // End of namespace Hook

#endif // HOOK_ANIM_H
