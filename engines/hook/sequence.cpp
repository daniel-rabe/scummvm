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

// Action sequences (original 088E0 / 08947 / 08A26): keyframe tracks that write into the
// data segment - sprite slots, speech, sound requests, the other channel. The engine's
// sprite slots are mirrored into the data segment around every sequence tick and handler
// call, so the data can address them exactly as in the original.

#include "common/debug.h"

#include "hook/hook.h"

namespace Hook {

static const uint16 kDsActorSlots = 0x13FA;		// actors 0..5, 22 bytes each
static const uint16 kDsObjectSlots = 0x147E;	// room objects
static const uint16 kSlotSize = 22;
static const uint16 kDsScrollRequests = 0xAB93;
static const uint16 kDsChannelA = 0xAC30;		// far ptr, counter at +4
static const uint16 kDsChannelB = 0xAC48;

// Bank variables in the data segment that slot +0 points at
static const uint16 kBankVars[] = {
	0x0306, 0x0304, 0x0308, 0x030A,	// room banks: header +30, +36, +3C, +42
	0x02D9,							// PANSPRIT (Peter)
	0x02E6							// TINKER
};

static uint16 bankVar(int bank) {
	return (bank >= 0 && bank < (int)ARRAYSIZE(kBankVars)) ? kBankVars[bank] : 0;
}

static int bankFromVar(uint16 var) {
	for (uint i = 0; i < ARRAYSIZE(kBankVars); i++)
		if (kBankVars[i] == var)
			return i;
	return kBankNone;
}

void HookEngine::syncSlotsToDs() {
	auto put = [&](uint16 slot, const Actor &a) {
		setDsWord(slot + 0x00, bankVar(a.bank));
		setDsWord(slot + 0x04, (uint16)a.frame);
		setDsWord(slot + 0x06, (uint16)a.x);
		setDsWord(slot + 0x0A, (uint16)a.y);
	};
	put(kDsActorSlots, _player.actor);
	for (int i = 1; i < 6; i++)
		put(kDsActorSlots + kSlotSize * i, _actors[i]);
	for (uint i = 0; i < _objects.size(); i++)
		put(kDsObjectSlots + kSlotSize * i, _objects[i]);
	// scroll requests [AB93] right, [AB95] left, [AB97] up, [AB99] down
	for (int i = 0; i < 4; i++)
		setDsWord(kDsScrollRequests + 2 * i, (uint16)_scrollRequest[i]);
}

void HookEngine::syncSlotsFromDs() {
	auto get = [&](uint16 slot, Actor &a) {
		a.bank = bankFromVar(dsWord(slot + 0x00));
		a.frame = (int16)dsWord(slot + 0x04);
		a.x = (int16)dsWord(slot + 0x06);
		a.y = (int16)dsWord(slot + 0x0A);
	};
	get(kDsActorSlots, _player.actor);
	for (int i = 1; i < 6; i++)
		get(kDsActorSlots + kSlotSize * i, _actors[i]);
	for (uint i = 0; i < _objects.size(); i++)
		get(kDsObjectSlots + kSlotSize * i, _objects[i]);
	for (int i = 0; i < 4; i++)
		_scrollRequest[i] = (int16)dsWord(kDsScrollRequests + 2 * i);
}

bool HookEngine::sequenceRunning() const {
	return dsWord(kDsChannelA) != 0;
}

void HookEngine::startSequence(uint16 desc, bool channelB) {
	setDsWord(channelB ? kDsChannelB : kDsChannelA, desc);
}

void HookEngine::sequencesTick() {
	if (!dsWord(kDsChannelA) && !dsWord(kDsChannelB))
		return;
	syncSlotsToDs();
	_seqAbort = false;
	for (uint16 ch : { kDsChannelA, kDsChannelB }) {
		uint16 desc = dsWord(ch);
		uint16 counter = dsWord(ch + 4);
		runSequence(desc, counter);
		if (_seqAbort) {
			// A callback returned straight to the main loop (it pops the caller's frame):
			// no store-back for this channel, channel B is skipped this tick.
			_seqAbort = false;
			break;
		}
		setDsWord(ch, desc);
		setDsWord(ch + 4, counter);
	}
	syncSlotsFromDs();
}

void HookEngine::runSequence(uint16 &desc, uint16 &counter) {
	// 08947
	if (!desc)
		return;
	uint16 n = dsWord(desc);
	uint16 tick = dsWord(desc + 2) + 1;
	setDsWord(desc + 2, tick);
	uint16 length = dsWord(desc + 4);
	uint16 p = desc + 6;
	if (tick == length) {
		// end: rewind every track so the sequence can be played again
		setDsWord(desc + 2, 0);
		for (uint i = 0; i < n; i++) {
			if (dsWord(p) == 0xFFFF) {
				p += 6;
				continue;
			}
			uint16 track = dsWord(p);
			setDsWord(track + 0x0A, 0);
			setDsWord(track, dsWord(track + 4));
			p += 4;
		}
		desc = 0;
		counter = 0;
		return;
	}
	_seqCounter = counter;
	for (uint i = 0; i < n; i++) {
		if (dsWord(p) == 0xFFFF) {
			uint16 code = dsWord(p + 2);
			p += 6;
			syncSlotsFromDs();
			runHandler(code);
			syncSlotsToDs();
			if (_seqAbort)
				return;
		} else {
			trackStep(dsWord(p));
			p += 4;
		}
	}
	counter++;
}

void HookEngine::trackStep(uint16 track) {
	// 08A26: per destination, FFFE n = wait (ends this step), FFFD a = jump, else copy words
	uint16 cursor = dsWord(track);
	uint16 words = dsWord(track + 8) + 1;
	uint16 count = dsWord(track + 0x0C);
	for (uint k = 0; k < count; k++) {
		uint16 dest = dsWord(track + 0x0E + 4 * k);
		for (int guard = 0; guard < 16; guard++) {
			uint16 w = dsWord(cursor);
			if (w == 0xFFFE) {
				if (dsWord(track + 0x0A) == 0)
					setDsWord(track + 0x0A, dsWord(cursor + 2));
				uint16 left = dsWord(track + 0x0A) - 1;
				setDsWord(track + 0x0A, left);
				if (left == 0)
					setDsWord(track, cursor + 4);
				return;
			}
			if (w == 0xFFFD) {
				cursor = dsWord(cursor + 2);
				setDsWord(track, cursor);
				continue;
			}
			break;
		}
		for (uint i = 0; i < words; i++) {
			setDsWord(dest + 2 * i, dsWord(cursor));
			cursor += 2;
		}
	}
	setDsWord(track, cursor);
}

} // End of namespace Hook
