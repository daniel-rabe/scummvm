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

#include "hook/anim.h"
#include "hook/detection.h"
#include "hook/room.h"

namespace Hook {

enum {
	kOpMoveLeft = 2,
	kOpMoveRight = 3,
	kOpWait = 8,
	kOpJump = 9,
	kOpSet = 0x0A,
	kOpHide = 0x0B,
	kOpRepeat = 0x0C,
	kOpResetTimer = 0x0D
};

void AnimVM::start(const Room &room) {
	_slots.clear();
	for (const RoomScript &rs : room.scripts()) {
		Slot s;
		s.pc = rs.pc;
		s.object = rs.object;
		s.repeat = 0;
		s.timer = 0;
		_slots.push_back(s);
	}
}

void AnimVM::tick(Room &room, Common::Array<Actor> &objects) {
	_phase = (_phase + 1) & 7;
	for (Slot &s : _slots) {
		if (s.object < 1 || s.object > objects.size())
			continue;
		Actor &a = objects[s.object - 1];
		for (int guard = 0; guard < 64 && step(room, s, a); guard++) {
		}
	}
}

bool AnimVM::step(Room &room, Slot &s, Actor &a) {
	uint16 pc = s.pc;
	switch (room.word(pc)) {
	case kOpMoveLeft:
	case kOpMoveRight: {
		// base, limit, ?, steps (8 x i16, indexed by the phase), ?
		bool left = room.word(pc) == kOpMoveLeft;
		a.frame = room.sword(pc + 2) + _phase;
		int16 step = room.sword(room.word(pc + 8) + 2 * _phase);
		a.x += left ? -step : step;
		int16 limit = room.sword(pc + 4);
		if (left ? a.x <= limit : a.x >= limit)
			s.pc += 12;
		return false;
	}
	case kOpWait:
		if (s.timer == 0) {
			s.timer = room.word(pc + 2) | (room.word(pc + 4) << 16);
			return false;
		}
		if (--s.timer == 0) {
			s.pc += 6;
			return true;
		}
		return false;
	case kOpJump:
		s.pc = room.word(pc + 2);
		return false;
	case kOpSet:
		a.frame = room.sword(pc + 2);
		a.x = room.sword(pc + 4);
		a.y = room.sword(pc + 6);
		s.pc += 8;
		return false;
	case kOpHide:
		a.frame = -1;
		s.pc += 2;
		return true;
	case kOpRepeat: {
		// The count word in the room data is the live counter; the slot keeps the
		// original value and restores it when the loop is done.
		if (s.repeat == 0)
			s.repeat = room.word(pc + 6);
		uint16 left = room.word(pc + 6) - 1;
		room.setWord(pc + 6, left);
		if (left != 0) {
			s.pc = room.word(pc + 2);
			return false;
		}
		room.setWord(pc + 6, s.repeat);
		s.repeat = 0;
		s.pc += 8;
		return false;
	}
	case kOpResetTimer:
		s.timer = 0;
		s.pc += 2;
		return false;
	default:
		return false;	// the original's bare 'ret' handlers: the script halts here
	}
}

} // End of namespace Hook
