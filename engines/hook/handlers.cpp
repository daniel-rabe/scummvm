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

// Game flags are words at DS:86B7 + 2 * id (DS:86B5 is the crocodile counter, "flag -1").
static const uint16 kDsFlags = 0x86B7;
static uint16 flag(int id) { return kDsFlags + 2 * id; }

static const uint16 kDsActorSlots = 0x13FA;
static const uint16 kDsObjectSlots = 0x147E;
static const uint16 kSlotSize = 22;
static const uint16 kDsChannelA = 0xAC30;
static const uint16 kDsCounterA = 0xAC34;
static const uint16 kDsNewItemFlash = 0xAC1D;	// panel flash after an item arrives (30 ticks)
static const uint16 kDsTimer401E = 0x401E;		// counted down every tick by 0ACAE
static const uint16 kDsWalkThrough = 0xABC9;	// != 0: Peter keeps walking while channel A runs

void HookEngine::showMessage(uint16 off) {
	// [ABD1] = 4, [ABD5] = off: a message in the data segment, said by Peter
	say(dsMessage(off), kFacingDown);
}

Actor &HookEngine::slotActor(uint16 dsSlot) {
	// Original sprite slots: 13FA + 22k = actors (0 = Peter), 147E + 22k = room objects.
	// Sequences may address object slots past the room's own objects (room 0 uses 16 more).
	if (dsSlot >= kDsObjectSlots) {
		uint i = (dsSlot - kDsObjectSlots) / kSlotSize;
		if (i >= _objects.size())
			_objects.resize(i + 1);
		return _objects[i];
	}
	uint i = (dsSlot - kDsActorSlots) / kSlotSize;
	return i == 0 ? _player.actor : _actors[MIN<uint>(i, 5)];
}

void HookEngine::stopPeter() {
	// 0A328: deselect verb and item, Peter's target = where he stands (06A78)
	_prevVerb = _verb;
	_verb = 0;
	_itemSelected = false;
	_itemX = 0;
	_player.walkTo(_player.feetX(), _player.feetY());
}

bool HookEngine::loadPeterBank(uint16 nameOff) {
	// 0DDA8 + 0D75F: load a Peter sprite file (PANSPRIT / MOREPAN1 / MOREPAN2) into [02D9]
	Common::String name((const char *)dsPtr(nameOff));
	Buffer data;
	if (!_res.load(name.c_str(), data) || !_peterBank.load(data.data(), data.size(), _res.ds(kXlatPeter))) {
		warning("Hook: cannot load %s", name.c_str());
		return false;
	}
	setDsWord(0x86EF, nameOff);
	return true;
}

void HookEngine::runCue(uint16 table, uint16 ptrVar) {
	// 0A644 / 0A669: cue table {u16 tick, u16 code, u16 0} in the code segment, walked by a
	// pointer that is reset at counter 0; one entry fires when the counter equals its tick
	setDsWord(ptrVar == 0xAC3A ? 0xAC3C : 0xAC40, table);
	if (_seqCounter == 0)
		setDsWord(ptrVar, table);
	uint16 p = dsWord(ptrVar);
	if (_seqCounter != csWord(p))
		return;
	setDsWord(ptrVar, p + 6);
	runHandler(csWord(p + 2));
}

void HookEngine::roomHookTick() {
	// 0AAC3: timer [401E], frame counter flag 37, then the room's hook (cs:AADA)
	if (dsWord(kDsTimer401E))
		setDsWord(kDsTimer401E, dsWord(kDsTimer401E) - 1);
	setDsWord(0x8701, dsWord(0x8701) + 1);
	switch (_room.number()) {
	case 0:		// ported: B044. Once the hat is caught, the passer-by switches to its second script
		if (dsWord(flag(4)) == 1) {
			uint16 scripts = _room.word(0x70);
			_anim.movePc(0, (int)_room.word(scripts + 6) - (int)_room.word(scripts));
			setDsWord(flag(4), 2);
		}
		break;
	case 3: {	// ported: B070. Standing on node 10 with the magnet (item 4): sequence 5B23, link 10 -> 11
		if (sequenceRunning())
			break;
		uint16 n = _player.graph().nearest(_player.actor.x, _player.actor.y);
		setDsWord(0xAC29, n);
		if (n != 0x0A || _player.isWalking() || !hasItem(4))
			break;
		startSequence(0x5B23);
		const byte *d = _room.data().data();
		uint32 linkList = READ_LE_UINT32(d + 0x08);
		_room.setWord(READ_LE_UINT32(d + linkList) + 0x92, 0x0B);
		break;
	}
	case 4:		// ported: B0C8. The lazy pirate after three mugs
		if ((int16)dsWord(flag(29)) >= 3) {
			setDsWord(flag(30), 1);
			slotActor(0x1518).frame = 7;
			_room.setWord(_room.word(0x84) + 2 + 0x24, 0xFFFF);
			if (!dsWord(flag(3)) && !dsWord(flag(0))) {
				setDsWord(0x8FE5 + 8, 1);
				setDsWord(0x90A5 + 4, 0x0A);
				setDsWord(0x90A5 + 0x14, 0x8000);
			}
		} else {
			setDsWord(0x8FE5 + 8, 0xFFFF);
		}
		break;
	default:
		break;
	}
}

void HookEngine::runHandler(uint16 address) {
	debugC(1, kDebugScript, "handler %04X", address);
	uint16 c = _seqCounter;
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
		setDsWord(flag(8), 0);
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
	// LOOK event 0x4C01 (room 38 clue): scroll to x = 0x80 and up, flag 31 = 100, flag 34 = 1
	case 0x86D4: {
		_scrollRequest[2] = 0x40;
		int d = 0x80 - _scrollX;
		_scrollRequest[0] = d >= 0 ? d : 0;
		_scrollRequest[1] = d >= 0 ? 0 : -d;
		setDsWord(flag(31), 0x64);
		setDsWord(flag(34), 1);
		_scrollRequest[3] = 0;
		return;
	}

	// USE on its own (cs:8B60): items 1, 10, 11 and 22 - the hat, hook and coat (room 5)
	case 0x8C0C:
	case 0x8C0F:
	case 0x8C12:
	case 0x8C15:
		// 08C1F / 08C25: ax = 1 for item 22, else 0
		if (dsWord(flag(4)) && dsWord(flag(2)) && dsWord(flag(3))) {
			if (_room.number() != 5)
				showMessage(0xAD66);
			else if (address == 0x8C0C)
				startSequence(0x450B);
			else
				showMessage(0xAD60);
		} else {
			showMessage(0xAD6C);
		}
		return;
	case 0x8C06:	// item 16: {FFFF, message} data, handled by useAlone (never reached)
		return;
	case 0x8C18:	// item 36
		setDsWord(0xCE9B, 1);
		return;

	// PICK UP hooks (cs:A9AC)
	case 0xAA9C:	// after item 11: flag 3
		setDsWord(flag(3), 1);
		setDsWord(kDsNewItemFlash, 0x1E);
		return;
	case 0xAAA9:	// after item 9: flag 8, message
		setDsWord(flag(8), 1);
		showMessage(0xADE6);
		return;

	// USE without an item (DS:A91F)
	case 0xA473:	// room 0: knock on the door
		startSequence(0x45AB);
		return;
	case 0xA47B:	// room 11: the clam opens once the cog is free
		if (dsWord(0x96D5 + 4) == 7)
			startSequence(0x46C7);
		else
			showMessage(0xADE0);
		return;
	case 0xA49E:	// room 34: gym equipment
		startSequence(0x6725);
		setDsWord(flag(17), dsWord(flag(17)) + 1);
		return;
	case 0xA4AB:
		startSequence(0x68C5);
		setDsWord(flag(17), dsWord(flag(17)) + 1);
		return;
	case 0xA4B8:
		startSequence(0x67D7);
		setDsWord(flag(17), dsWord(flag(17)) + 1);
		return;
	case 0xA4C5:	// room 32
		if (_player.graph().base() != 0)
			showMessage(0xADAA);
		else
			startSequence(0x6A09);
		return;
	case 0xA513:	// room 31: the catapult once the elastic is fitted and flag 16 >= 3
		if (dsWord(flag(18)) && (int16)dsWord(flag(16)) >= 3)
			startSequence(0x6ABD);
		else
			showMessage(0xAD98);
		return;

	// USE with an item (DS:A953..)
	case 0xA4E6:	// room 31: elastic (item 29) tied to the trees
		setDsWord(flag(18), 1);
		{
			uint16 hs = dsWord(0x1962 + 4 * _room.number()) + 0x48;	// [0E9E]
			setDsWord(hs + 0x0A, 0x0C);
			setDsWord(hs + 4, 2);
		}
		slotActor(0x14AA).frame = 2;
		slotActor(0x14AA + kSlotSize).frame = 3;
		removeItem(0x1D);
		return;
	case 0xA53B:	// room 33: the pan-pipes need item 37
		if (hasItem(0x25))
			startSequence(0x6C3F);
		else
			showMessage(0xAD98);
		return;
	case 0xA55D:	// room 0: the anchor on the crocodile (two warnings first)
		if (dsWord(flag(-1)) == 0) {
			showMessage(0xAE04);
			setDsWord(flag(-1), 1);
		} else if (dsWord(flag(-1)) == 1) {
			showMessage(0xAE0A);
			setDsWord(flag(-1), 2);
		} else {
			startSequence(0x48C5);
		}
		return;
	case 0xA598:	// room 0, dropped from hotspot 6
		if (dsWord(flag(0)) != 0 || dsWord(flag(-1)) == 0) {
			showMessage(0xAE04);
			setDsWord(flag(-1), dsWord(flag(-1)) + 1);
		} else if (dsWord(flag(-1)) == 1) {
			showMessage(0xAE0A);
			setDsWord(flag(-1), 2);
		} else {
			startSequence(0x4D7F);
		}
		return;
	case 0xA5B5:	// room 5: the pole on the jacket
		startSequence(0x522D);
		return;
	case 0xA5BD:	// room 10: the magnet at the X
		if (_player.graph().base() == 0)
			showMessage(0xADAA);
		else if (!hasItem(4))
			startSequence(0x54C5);
		return;
	case 0xA603:	// room 11: the pole in the cog
		startSequence(0x55D5);
		return;

	// Sequence callbacks: they run every tick of their sequence, _seqCounter = [AB79]
	case 0xA034:	// SEQ 41D3 (the gold tooth)
		if (c == 0x6E) {
			runHandler(0xA045);
			setDsWord(flag(1), dsWord(flag(1)) - 1);
		}
		return;
	case 0xA045:	// a gold piece: 6 -> 7 -> 8, or a new item 6
		for (int i = 0; i < 16; i++) {
			uint16 w = 0x870B + 2 * i;
			if (dsWord(w) == 6 || dsWord(w) == 7) {
				setDsWord(w, dsWord(w) + 1);
				return;
			}
		}
		addItem(6);
		return;
	case 0xA071:	// SEQ 4645 (channel B, room 5 washing line)
		if ((int16)c < 0x48)
			return;
		if (!sequenceRunning()) {
			uint16 hs = hitTest(_player.actor.x, _player.actor.y + 0x30);
			if (hs && dsWord(hs + 0x0A) == 0xFFFE) {
				// Peter walked under the line: stop channel B, start 4659 + 579D
				setDsWord(0xABF7, 0);
				_speechTotal = _speechTicks = 0;
				_speech.clear();
				uint16 b = dsWord(0xAC48);
				uint16 p = b + 6;
				for (uint i = 0; i < dsWord(b); i++) {
					if (dsWord(p) == 0xFFFF) {
						p += 6;
						continue;
					}
					uint16 track = dsWord(p);
					setDsWord(track + 0x0A, 0);
					setDsWord(track, dsWord(track + 4));
					p += 4;
				}
				setDsWord(b + 2, 0);
				setDsWord(kDsCounterA, 0);
				setDsWord(0xAC4C, 0);
				setDsWord(kDsChannelA, 0x4659);
				setDsWord(0xAC48, 0x579D);
				abortSequences();
				return;
			}
		}
		runHandler(0xA105);
		return;
	case 0xA105:
		if (!dsWord(kDsTimer401E))
			setDsWord(kDsTimer401E, 500);
		return;
	case 0xA114:	// SEQ 46C7 (the clam): on to room 12
		if (c == 0x3A) {
			setDsWord(0x86F7, 0x0C);
			setDsWord(0x86F9, 0);
			_roomCountdown = 1;
			setDsWord(kDsCounterA, 0);
			setDsWord(kDsChannelA, 0);
			abortSequences();
		}
		return;
	case 0xA162:	// SEQ 48C5 / 4D7F: 16 extra sprites in room bank +42
		if (c == 0)
			for (int i = 0; i < 16; i++)
				slotActor(0x1586 + kSlotSize * i).bank = kBankRoom0 + 3;
		return;
	case 0xA182:	// SEQ 48C5: the anchor rope
	case 0xA28C: {	// SEQ 4D7F
		bool anchor = address == 0xA182;
		if ((int16)c < 0x54) {
			setDsWord(0x5677, anchor ? 0x5775 : 0x5699);
			_player.selectGraph(anchor ? 0x10 : 0x12);
			return;
		}
		if ((int16)c > 0x6D) {
			stopPeter();
			return;
		}
		runHandler(0xA359);
		// 10-byte rows {x, dx, ?, n, frame}: n sprites from (x, 0) in steps of (dx, 8); 48C5
		// walks its table backwards (one row on, two back), 4D7F forwards
		uint16 di = dsWord(0x5677);
		int x = (int16)dsWord(di) + (anchor ? 0x20 : 0);
		int y = 0;
		int dx = (int16)dsWord(di + 2);
		int n = (int16)dsWord(di + 6);
		uint16 slot = 0x1586;
		if (n > 0) {
			int16 frame = (int16)dsWord(di + 8);
			di += 10;
			for (int i = 0; i < n; i++, slot += kSlotSize) {
				Actor &a = slotActor(slot);
				a.frame = frame;
				a.x = x;
				a.y = y;
				x += dx;
				y += 8;
			}
		} else {
			di += 8;
		}
		if (anchor)
			di -= 0x14;
		setDsWord(0x5677, di);
		for (int i = n; i < 16; i++, slot += kSlotSize)
			slotActor(slot).frame = -1;
		return;
	}
	case 0xA359: {	// the hat catch (counter 0x5D..0x63, Peter under object 5)
		if (dsWord(flag(4)) || (int16)c < 0x5D || (int16)c > 0x63)
			return;
		int hx = slotActor(0x14EC).x, px = _player.actor.x;
		if (hx + 0x18 < px || hx > px)
			return;
		setDsWord(kDsNewItemFlash, 0x1E);
		showMessage(0xAC4E);
		setDsWord(flag(4), 1);
		addItem(1);
		return;
	}
	case 0xA3A9:	// SEQ 522D (the jacket): the timer decides whether it falls
		if (c == 0)
			slotActor(0x1518).x = _player.actor.x - 9;
		if (c == 0x1C) {
			setDsWord(0xABB1, 0xF0);
			setDsWord(0xABB3, 0x20);
			if (dsWord(kDsTimer401E)) {
				setDsWord(kDsCounterA, 0);
				setDsWord(kDsChannelA, 0x5987);
				abortSequences();
			}
		}
		return;
	case 0xA40E:	// SEQ 54C5: the magnet finds item 4
		if (c == 0x58) {
			addItem(4);
			showMessage(0xAC64);
		}
		return;
	case 0xA430:	// SEQ 55D5: the cog is free
		if (c == 0x22) {
			setDsWord(0x96D5 + 4, 7);
			setDsWord(0x96BD + 4, 8);
		}
		return;
	case 0xA448:	// SEQ 5987: the jacket falls
		if (c == 0x38) {
			setDsWord(kDsNewItemFlash, 0x1E);
			showMessage(0xAC90);
			setDsWord(flag(2), 3);
			takeObjectState(3);
		}
		return;

	// Cue tables (0A644)
	case 0x934C:	// SEQ 48C5 / 4D7F
		runCue(0x9356, 0xAC3A);
		return;
	case 0x9F61:	// SEQ 450B
		runCue(0x9F6B, 0xAC3A);
		return;
	case 0xA60B:	// SEQ 5B23
		runCue(0xA615, 0xAC3A);
		return;
	case 0xA71E:	// SEQ 64C5
		runCue(0xA728, 0xAC3A);
		return;

	// Cue entries
	case 0x9F51:
	case 0xA6DB:
	case 0xA780:	// Peter drawn from room bank +42
		_player.actor.bank = kBankRoom0 + 3;
		return;
	case 0x9F59:
	case 0xA6E3:
		_player.actor.bank = kBankPeter;
		return;
	case 0x9F7F:	// 450B tick 0: Peter keeps walking while it runs
		if (_player.depth() != 0) {
			setDsWord(kDsWalkThrough, 1);
			_player.resetStepPhase();
		}
		return;
	case 0x9F99:	// 450B tick 16: hat, hook and coat put on - Peter becomes the pirate
		removeItem(1);
		removeItem(0x0A);
		removeItem(0x0B);
		setDsWord(flag(0), 1);
		loadPeterBank(0x2BF);	// MOREPAN1.BIN
		showMessage(0xAD5A);
		return;
	case 0x9FEE:	// 450B tick 62
		removeItem(0x16);
		setDsWord(kDsNewItemFlash, 0x1E);
		setDsWord(kDsWalkThrough, 0);
		return;
	case 0xA63B:
		slotActor(0x14EC).frame = 0x10;
		return;
	case 0xA68E:	// 5B23 tick 1116: on to room 11
		setDsWord(0x86F7, 0x0B);
		_roomCountdown = 1;
		setDsWord(0x86F9, 0);
		setDsWord(kDsCounterA, 0);
		setDsWord(kDsChannelA, 0);
		abortSequences();
		setDsWord(flag(0), 0);
		return;
	case 0xA6EB:	// Tinkerbell's target = (0x128, 0x30)
		setDsWord(0x0D7C, 0x128);
		setDsWord(0x0D7E, 0x30);
		setDsWord(0x41C7, 0x0D7C);
		setDsWord(0x41CB, 0x0D7E);
		return;
	case 0xA706:
	case 0xA712: {
		int d = _scrollX - (address == 0xA706 ? 0x150 : 0x80);
		if (d >= 0)
			_scrollRequest[1] = d;
		return;
	}
	case 0xA748:	// 64C5: four objects gone, flag 9
		for (int i = 0; i < 4; i++)
			slotActor(0x14AA + kSlotSize * i).frame = -1;
		setDsWord(flag(9), 1);
		return;
	case 0xA76A:	// Tinkerbell follows Peter again
		_player.actor.bank = kBankPeter;
		setDsWord(0x41C7, 0x1400);
		setDsWord(0x41CB, 0x1404);
		return;
	case 0xA788: {	// 64C5 tick 0: objects 6 and 7 next to Peter, scroll to x = Peter - 0xB8
		int x = _player.actor.x - 0x10 + 0x0E;	// [13F2] = Peter's x - 16 (set by the renderer)
		Actor &a = slotActor(0x1502), &b = slotActor(0x1518);
		a.frame = 0;
		a.x = x;
		a.y = 0;
		b.frame = 4;
		b.x = x;
		b.y = 0x40;
		int d = _scrollX - 0xB8;
		if (d >= 0)
			_scrollRequest[1] = d;
		else
			_scrollRequest[0] = -d;
		return;
	}
	case 0xA7C5:	// 64C5 tick 32: Tinkerbell appears next to Peter and starts following
		setDsWord(0x8739, 1);
		setDsWord(0x18C6, _player.actor.x + 0xC8);
		setDsWord(0x18C8, _player.actor.y + 0x18);
		setDsWord(0x0D7C, _player.actor.x + 0x40);
		setDsWord(0x0D7E, _player.actor.y + 0x18);
		setDsWord(0x41CB, 0x0D7E);	// sic: [41C7] is left as it was
		setDsWord(0x41CF, 0x1408);
		setDsWord(0x8737, 0);
		return;

	default:
		warning("Hook: game logic routine %04X is not ported yet", address);
		return;
	}
}

} // End of namespace Hook
