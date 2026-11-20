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
#include "common/system.h"
#include "graphics/paletteman.h"

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
	case 12:	// ported: B121. Flag 31 counts down, then edge scrolling is back (flag 34)
		if (dsWord(flag(31))) {
			setDsWord(flag(31), dsWord(flag(31)) - 1);
			if (!dsWord(flag(31)))
				setDsWord(flag(34), 0);
		}
		break;
	case 29: {	// ported: B146. Fixed depths; Tinkerbell's cage (sequence 64C5) from node 6
		static const int16 depths[] = { 0x7F, 0x80, 0, 1, 2, 3 };
		for (int i = 0; i < 6; i++)
			slotActor(kDsObjectSlots + kSlotSize * i).depth = depths[i];
		if (dsWord(flag(9))) {
			const byte *d = _room.data().data();
			_room.setWord(READ_LE_UINT32(d + READ_LE_UINT32(d + 0x08)) + 0x54, 0x0C);
			uint16 hs = hotspotBase() + 0x60;
			setDsWord(hs + 4, 0);
			setDsWord(hs + 0x12, 0x0C);
			break;
		}
		if (sequenceRunning())
			break;
		const SpriteBank *b = bankFor(_player.actor.bank);
		const Frame *f = b ? b->frame(_player.actor.frame) : nullptr;
		uint16 n = _player.graph().nearest(_player.actor.x, _player.actor.y + (f ? f->height : 0));
		setDsWord(0xAC29, n);
		if (n == 6 && !_player.isWalking())
			startSequence(0x64C5);
		break;
	}
	case 30:	// ported: B1CF
		slotActor(0x14D6).depth = 0x72;
		slotActor(0x159C).depth = 0x74;
		break;
	case 31:	// ported: B1E0
		slotActor(0x14AA).depth = 0x64;
		slotActor(0x14C0).depth = 0xC8;
		break;
	case 33:	// ported: B1F2. Flags 20, 22, 24 remember items 26, 34, 29
		if (hasItem(0x1A))
			setDsWord(flag(20), 1);
		if (hasItem(0x22))
			setDsWord(flag(22), 1);
		if (hasItem(0x1D))
			setDsWord(flag(24), 1);
		break;
	case 34:	// ported: B21D
		slotActor(0x1494).depth = 0xC8;
		slotActor(0x14AA).depth = 0xC8;
		break;
	case 35:	// ported: B22E
		slotActor(0x147E).depth = 0x74;
		slotActor(0x1494).depth = 0x70;
		slotActor(0x14AA).depth = 0x72;
		if (hasItem(0x1E))
			setDsWord(flag(23), 1);
		break;
	case 39:	// ported: AB82
		_player.actor.depth = 0x80;
		slotActor(0x14D6).depth = 0x72;
		slotActor(0x159C).depth = 0x74;
		break;
	case 37:	// ported: B256. With the thimble (item 25) and flags 20, 21, 15: sequence 6CF3
		slotActor(0x14AA).depth = 0x80;
		if (!hasItem(0x19))
			break;
		setDsWord(flag(19), 1);
		if (!dsWord(flag(20)) || !dsWord(flag(21)) || !dsWord(flag(15)) || sequenceRunning() ||
			_player.graph().base() != 0)
			break;
		{
			uint16 n = _player.graph().nearest(_player.actor.x, _player.actor.y);
			setDsWord(0xAC29, n);
			if (n == (_player.isWalking() ? 5u : 4u))
				startSequence(0x6CF3);
		}
		break;
	default:
		break;
	}
}

uint16 HookEngine::hotspotBase() const {
	// [0E9E]: the current room's hotspot records in the data segment
	return dsWord(0x1962 + 4 * _room.number());
}

void HookEngine::roomEntryHook() {
	// 0ACBB: the room's song and ambient reset, then per-room code (cs:ACDA). The ambient
	// calls of rooms 1..9 (0AFF0 / 0B001 -> 0B034) do nothing.
	roomSound();
	switch (_room.number()) {
	case 0:		// ported: AD82. The hat comes back to the passer-by's script switch
		if (dsWord(flag(4)) == 2)
			setDsWord(flag(4), 1);
		break;
	case 10: {	// ported: ADE0. Hotspot records and two objects depend on the costume (flag 0)
		uint16 src = dsWord(flag(0)) ? 0xAA41 : 0xAA25;
		for (int i = 0; i < 12; i++)
			setDsWord(0x95CD + 2 * i, dsWord(src + 2 * i));
		slotActor(0x1518).frame = (int16)dsWord(src + 24);
		slotActor(0x152E).frame = (int16)dsWord(src + 26);
		if (_player.graph().end() == 0x0A) {
			// link 1 of local node 2 in Peter's graph; without the costume the original
			// writes the 0 into the data segment instead of the room (kept as is)
			const byte *d = _room.data().data();
			uint16 links = READ_LE_UINT32(d + READ_LE_UINT32(d + 0x08));
			if (dsWord(flag(0)))
				_room.setWord(links + 0x12, 3);
			else
				setDsWord(links + 0x12, 0);
		}
		break;
	}
	case 11:	// ported: AE50. Tinkerbell gone, Peter's own sprites again
		setDsWord(0x8739, 0);
		loadPeterBank(0x2B2);	// PANSPRIT.BIN
		break;
	case 12:	// ported: AE7D
		if (hasItem(0x17))
			setDsWord(hotspotBase() + 0x18 + 0x10, 0);
		break;
	case 29:	// ported: AEA2. Tinkerbell freed: the cage is gone
		if (dsWord(flag(9)))
			for (int i = 0; i < 4; i++)
				slotActor(0x14AA + kSlotSize * i).frame = -1;
		break;
	case 30:	// ported: AEC5
		if (dsWord(flag(15))) {
			static const int16 frames[] = { 0x19, 0x1A, 0x19 };
			for (int i = 0; i < 3; i++) {
				Actor &a = slotActor(0x155A + kSlotSize * i);
				a.frame = frames[i];
				a.y = 0x4B;
			}
			setDsWord(hotspotBase() + 0x108 + 4, 3);
		}
		break;
	case 31:	// ported: AF01. The elastic between the trees
		slotActor(0x14AA).frame = dsWord(flag(18)) ? 2 : 0;
		slotActor(0x14C0).frame = dsWord(flag(18)) ? 3 : 1;
		break;
	case 32:	// ported: AF2A. Arriving by catapult
		if (dsWord(flag(13))) {
			startSequence(0x6DD7);
			setDsWord(kDsCounterA, 0);
			setDsWord(flag(13), 0);
		}
		break;
	case 33:	// ported: AF45
		if (dsWord(flag(27))) {
			slotActor(0x1502).frame = -1;
			Actor &a = slotActor(0x14EC);
			a.frame = dsWord(flag(20)) ? -1 : 2;
			a.x = 0x10D;
			a.y = 0x69;
		}
		break;
	case 37:	// ported: AF76
		if (hasItem(0x19)) {
			uint16 hs = hotspotBase() + 0x18;
			setDsWord(hs + 4, 0);
			setDsWord(hs + 0x14, 0);
		} else {
			setTinkerTarget(0xF8, 0x54, true);
		}
		break;
	case 38:	// ported: AFBD
		setTinkerTarget(0x10C, 0x12E, true);
		setDsWord(kDsWalkThrough, 0);
		break;
	case 39: {	// ported: AB9D. Peter is Pan: PANEL2, MOREPAN2, sequence 7775; the camp as in room 30
		setDsWord(0x1A0A, 1);
		// 0AC0B: the Pan panel (the inventory bar cache is reset; the bar is redrawn anyway)
		Common::String panel((const char *)dsPtr(0x50E));
		if (!_res.load(panel.c_str(), _panel) || _panel.size() < (uint)kScreenWidth * kPanelHeight)
			warning("Hook: cannot load %s", panel.c_str());
		loadPeterBank(0x2CC);		// MOREPAN2.BIN, but [86EF] names MOREPAN1 (sic)
		setDsWord(0x86EF, 0x2BF);
		startSequence(0x7775);
		static const int16 frames[] = { 0x19, 0x1A, 0x19 };
		for (int i = 0; i < 3; i++) {
			Actor &a = slotActor(0x155A + kSlotSize * i);
			a.frame = frames[i];
			a.y = 0x4B;
		}
		setDsWord(hotspotBase() + 0x108 + 4, 3);
		break;
	}
	case 40:	// ported: AC66. Sequence 7B6F; Tinkerbell flies in from the right
		startSequence(0x7B6F);
		setTinkerTarget(0x2F0, 0x3B, false);
		setDsWord(0x18C6, 0x31E);
		setDsWord(0x18C8, 0x30);
		setDsWord(0x8739, 1);
		break;
	case 41:	// ported: AC9A. Sequence 7D59; no conversation running, no line
		startSequence(0x7D59);
		_convActive = false;
		setDsWord(0xABFB, 0);
		break;
	default:
		break;
	}
}

void HookEngine::setTinkerTarget(int x, int y, bool jump) {
	// Tinkerbell flies towards the words that [41C7]/[41CB] point at; DS:0D7C/0D7E hold a
	// fixed target. `jump` also puts her there ([18C6]/[18C8]) and shows her ([8739]).
	if (jump) {
		setDsWord(0x18C6, x);
		setDsWord(0x18C8, y);
		setDsWord(0x8739, 1);
	}
	setDsWord(0x0D7C, x);
	setDsWord(0x0D7E, y);
	setDsWord(0x41C7, 0x0D7C);
	setDsWord(0x41CB, 0x0D7E);
}

void HookEngine::tinkerTick() {
	// 068E8: Tinkerbell (actors 1..5: her and her trail) - hidden unless [8739]
	if (!dsWord(0x8739)) {
		for (int i = 1; i < 6; i++)
			_actors[i].frame = -1;
		return;
	}
	syncSlotsToDs();	// the target pointers may point at Peter's slot (1400/1404)
	uint16 phase = (dsWord(0x1834) + 1) & 0x1F;
	setDsWord(0x1834, phase);
	int tx = (int16)dsWord(dsWord(0x41C7)), ty = (int16)dsWord(dsWord(0x41CB));
	int x = (int16)dsWord(0x18C6), y = (int16)dsWord(0x18C8);
	// 06916: x moves 4 px when more than 8 px off; y moves 2 px up, or down until close
	if (x - 8 > tx) {
		if (x - 16 >= tx)
			x -= 4;
	} else if (x - 8 < tx) {
		if (x <= tx)
			x += 4;
	}
	for (int guard = 0; guard < 1000 && y + 10 != ty; guard++) {
		if (y + 10 > ty) {
			if (y + 6 >= ty)
				y -= 2;
			break;
		}
		if (y + 14 > ty)
			break;
		y += 2;
	}
	setDsWord(0x18C6, x);
	setDsWord(0x18C8, y);
	for (int i = 1; i < 6; i++) {
		_actors[i].x = x - 10;
		_actors[i].y = y + 10 + 2 * (i - 1);
	}
	// 06A25 / 06A55: wobble from the tables at 183E (x) and 1886 (y), each trail sprite a
	// step behind ([8737] = y only)
	int a = 2 * phase;
	for (int i = 1; i < 6; i++, a -= 2) {
		if (!dsWord(0x8737))
			_actors[i].x += (int16)dsWord(0x183E + a);
		_actors[i].y += (int16)dsWord(0x1886 + a);
	}
	// 069C3: in front of Peter
	int depth = _player.depth() ? _room.sword(0x1E) - _room.sword(0x24) + 0x10 : _player.actor.depth;
	for (int i = 1; i < 6; i++)
		_actors[i].depth = depth + 6 - (i - 1);
	// 069FB: frames 2k + (counter & 1)
	uint16 blink = dsWord(0x1832) + 1;
	setDsWord(0x1832, blink);
	for (int i = 1; i < 6; i++)
		_actors[i].frame = 2 * (i - 1) + (blink & 1);
}

bool HookEngine::tinkerHit(int cx, int cy) const {
	// 07A12: Tinkerbell is shown (not in rooms 29 and 37) and the point is in the 16x16 box
	// left of and below her slot (actor 1)
	if (_room.number() == 0x25 || _room.number() == 0x1D || !dsWord(0x8739))
		return false;
	int x = _actors[1].x, y = _actors[1].y;
	return x - 0x10 <= cx && x >= cx && y <= cy && y + 0x10 >= cy;
}

void HookEngine::tinkerTalk(int cx, int cy) {
	// ported: 880F. TALK to Tinkerbell: her chime (0B02C), then she answers through the
	// temporary conversation at DS:A997 with her line in [A9BD] and the bubble at [A9AB]/[A9AD].
	playEffect(1);
	if (_room.number() == 0x26) {
		// room 38: the three clues found (flags 10, 11, 12) start the flashback, sequence 7283
		setDsWord(0xA9AB, 0x10C);
		setDsWord(0xA9AD, 0x12E);
		if (dsWord(flag(12)) && dsWord(flag(11)) && dsWord(flag(10))) {
			startSequence(0x7283);
			return;
		}
		setDsWord(0xA9BD, 0x24);
	} else {
		// 0885F: a hint chosen by the play time: 8 lists of 256 ticks (cs:A9C5), each either
		// a line or {-1, 0, flag address, line}; a set flag moves the play time on by 0x800
		uint16 ref = 0;
		for (int guard = 0; guard < 64; guard++) {
			uint16 si = dsWord(0xA9C5 + (((dsWord(0x86FD) & 0x1FFF) >> 8) & 0xFFFC));
			if (dsWord(si) == 0xFFFF) {
				si += 8;
				if (dsWord(dsWord(si - 4))) {
					setDsWord(0x86FD, dsWord(0x86FD) + 0x800);
					continue;
				}
			}
			ref = dsWord(si);
			break;
		}
		setDsWord(0xA9BD, ref);
		setDsWord(0xA9AB, cx + 0x40);
		setDsWord(0xA9AD, cy - 0x32);
	}
	// 05D2E would also walk Peter towards the click; the conversation holds him here
	startConversation(0xA997, true);
}

void HookEngine::startScene(uint16 script) {
	// 0985F: the script is {u16 tick, u16 command, operands}; one entry per frame at most
	setDsWord(0xAC4C, 0);
	setDsWord(0xAC48, 0);	// the original keeps its script pointer in channel B's descriptor
	setDsWord(0xABF7, 0);
	setDsWord(0xABFB, 0);
	_scenePtr = script;
	_sceneCounter = 0;
	_modal = kModalScene;
}

void HookEngine::modalTick() {
	if (_modal == kModalScene) {
		sceneTick();
		applyScroll();	// the retrace interrupt keeps scrolling
		for (int r = 0; r < kRetracesPerTick; r++)
			fadeStep();
		return;
	}
	// 099D2: 65 frames with a frame delay of one retrace ([cs:3482] = 1): Peter rises 5 px and
	// the view 2 px a frame (down to y = 16); 20 frames before the end the screen fades out
	for (int r = 0; r < kRetracesPerTick && _modal == kModalFlyUp; r++) {
		_player.actor.y -= 5;
		if (_scrollY >= 0x10)
			_scrollY -= 2;
		if (_flyCounter == 0x14)
			startFade(false, 1);
		fadeStep();
		if (--_flyCounter < 0)
			_modal = kModalNone;
	}
}

void HookEngine::sceneTick() {
	// 098CE
	uint16 p = _scenePtr;
	if ((int16)dsWord(p) < 0) {
		endScene();
		return;
	}
	if (dsWord(p) == _sceneCounter) {
		int16 cmd = (int16)dsWord(p + 2);
		p += 4;
		if (cmd < 0) {
			endScene();
			return;
		}
		switch (cmd) {	// cs:993C
		case 0:		// ported: 9951. FADE_IN (the palette of DS:07B1, as cs:9950 is set)
			startFade(true, 0);
			break;
		case 1:		// ported: 9958. FADE_OUT
		case 4:
			startFade(false, 0);
			break;
		case 2: {	// ported: 9961. SHOW n: object 1 = frame n of room bank +3C at (0x80, 0x11C)
			Actor &a = slotActor(0x1494);
			a.bank = kBankRoom0 + 2;	// bank variable 0308
			a.frame = (int16)dsWord(p);
			a.x = 0x80;
			a.y = 0x11C;
			a.depth = 0;
			p += 2;
			break;
		}
		case 3:		// ported: 9985. SAY {x, y (unused), u32 text ref}: bubble at (0xB0, view top + 0x28)
			setDsWord(0xABB1, 0xB0);
			setDsWord(0xABB3, _scrollY + 0x28);
			setDsWord(0xAC44, dsWord(p + 4));
			p += 8;
			break;
		default:
			warning("Hook: unknown scene command %d", cmd);
			endScene();
			return;
		}
		_scenePtr = p;
	}
	_sceneCounter++;
}

void HookEngine::endScene() {
	// 09903: Peter faces the viewer, no scripted walking, the picture (object 1) is gone
	setDsWord(0xAC4C, 0);
	setDsWord(0xAC48, 0);
	_player.facing = 3;
	setDsWord(kDsWalkThrough, 0);
	slotActor(0x1494).frame = -1;
	_modal = kModalNone;
}

void HookEngine::startFade(bool in, int al) {
	// 00B98 / 00BEB: 32 steps of 1/32, or 10 steps of 3/32 when al = 0 outside the flashback.
	// A fade-in starts from black (colours 0xDC..0xFF, the panel, keep theirs: cs:25F1 = 1).
	bool fast = !_sceneFade && al == 0;
	_fadeSteps = fast ? 10 : 32;
	_fadeDelta = fast ? 3 : 1;
	_fadeDir = in ? 1 : -1;
	if (in)
		for (int i = 0; i < 0xDC; i++)
			_palLevel[i] = 0;
	applyPalette();
}

void HookEngine::fadeStep() {
	// 024DA / 02562, once per retrace: only colours 64..95 while cs:9950 is set
	if (!_fadeDir)
		return;
	int lo = _sceneFade ? 0x40 : 0, hi = _sceneFade ? 0x60 : (_fadeDir > 0 ? 0xDC : 0x100);
	if (_fadeSteps > 0) {
		_fadeSteps--;
		for (int i = lo; i < hi; i++)
			_palLevel[i] = CLIP(_palLevel[i] + _fadeDir * _fadeDelta, 0, 32);
	} else {
		// 025C7: the end - the target values in the range
		for (int i = lo; i < hi; i++)
			_palLevel[i] = _fadeDir > 0 ? 32 : 0;
		_fadeDir = 0;
	}
	applyPalette();
}

void HookEngine::applyPalette() {
	byte pal[256 * 3];
	for (int i = 0; i < 256 * 3; i++)
		pal[i] = _basePalette[i] * _palLevel[i / 3] / 32;
	g_system->getPaletteManager()->setPalette(pal, 0, 256);
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
	case 0x9ABA:	// item 23 in room 35, channel B idle: sequences 705F and 706F
		if (_room.number() == 0x23 && !dsWord(0xAC48)) {
			startSequence(0x705F);
			startSequence(0x706F, true);
		} else {
			showMessage(0xADAA);
		}
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

	// Neverland (rooms 12-34): sequence callbacks are all cue tables
	case 0x9AE9:	// SEQ 6ABD (room 31: the catapult)
		runCue(0x9AF3, 0xAC3A);
		return;
	case 0x9B6E:	// SEQ 6C3F (room 33: the pan-pipes)
		runCue(0x9B78, 0xAC3A);
		return;
	case 0x9BEE:	// SEQ 6CF3 (room 37)
		runCue(0x9BF8, 0xAC3A);
		return;
	case 0x9C9B:	// SEQ 6E47 (room 30)
		runCue(0x9CA5, 0xAC3A);
		return;
	case 0x9D06:	// SEQ 6DD7 (room 32: landing)
		runCue(0x9D10, 0xAC3A);
		return;
	case 0x9D24:	// SEQ 706F (room 35)
		runCue(0x9D2E, 0xAC3A);
		return;
	case 0x9DA5:	// SEQ 67D7 (room 34: the tree stump)
		runCue(0x9DAF, 0xAC3A);
		return;
	case 0x9DE9:	// SEQ 68C5 (room 34: the bike)
		runCue(0x9DF3, 0xAC3A);
		return;
	case 0x9E39:	// SEQ 6725 (room 34: the weights)
		runCue(0x9E43, 0xAC3A);
		return;
	case 0x9E71:	// SEQ 705F (room 35), second cue runner
		runCue(0x9E7B, 0xAC3E);
		return;
	case 0x9EA5:	// SEQ 6A09 (room 32)
		runCue(0x9EAF, 0xAC3A);
		return;

	case 0x9B01:	// 6ABD tick 78: catapulted to room 32 (flags 13, 14), sequence rewound
		setDsWord(0x86F7, 0x20);
		setDsWord(0x86F9, 0);
		_roomCountdown = 1;
		_player.actor.bank = kBankPeter;
		setDsWord(flag(13), 1);
		setDsWord(flag(14), 1);
		abortSequences();
		rewindSequence(_seqDesc);
		return;
	case 0x9B66:
		_player.actor.bank = kBankRoom0 + 3;
		return;
	case 0x9B8C:	// 6C3F tick 14: the pipes are played - the bow (item 37) is gone
		slotActor(0x1502).frame = -1;
		removeItem(0x25);
		return;
	case 0x9B9A: {	// 6C3F tick 16
		_player.actor.bank = kBankPeter;
		showMessage(0xAD8C);
		setDsWord(flag(27), 1);
		uint16 hs = hotspotBase();
		setDsWord(hs + 0x60 + 4, 0);
		setDsWord(hs + 0x60 + 0x0A, 0xFFFF);
		setDsWord(hs + 0xA8 + 4, 1);
		setDsWord(dsWord(0x4020 + 4 * _room.number()) + 2 + 4, 1);	// object-state entry 1 visible
		applyObjectState();
		return;
	}
	case 0x9C0C:	// 6CF3 tick 20: Peter walks on his own (graph of node 9)
		_player.actor.bank = kBankPeter;
		_player.selectGraph(9);
		setDsWord(kDsWalkThrough, 1);
		_player.updateDepth(_room);
		return;
	case 0x9C94:	// 6CF3 tick 30: ... to node 13
		setDsWord(0xABCB, 0x0D);
		return;
	case 0x9CC5:
		_player.actor.bank = kBankPeter;
		return;
	case 0x9CC9: {
		Actor &a = slotActor(0x155A);
		a.frame = 0x19;
		a.y = 0x4B;
		return;
	}
	case 0x9CD7: {
		Actor &a = slotActor(0x1586);
		a.frame = 0x19;
		a.y = 0x4B;
		return;
	}
	case 0x9CE5: {	// 6E47 tick 54: flag 15
		Actor &a = slotActor(0x1570);
		a.frame = 0x1A;
		a.y = 0x4B;
		setDsWord(hotspotBase() + 0x108 + 4, 3);
		setDsWord(flag(15), 1);
		return;
	}
	case 0x9D3C:	// 706F tick 0: something to pick up appears (object-state entry 3)
		if (dsWord(flag(23)) || hasItem(0x1E))
			return;
		setDsWord(hotspotBase() + 8, 3);
		setDsWord(hotspotBase() + 4, 0x0A);
		setDsWord(dsWord(0x4020 + 4 * _room.number()) + 0x22 + 4, 1);
		applyObjectState();
		return;
	case 0x9D74:	// 706F tick 116: ... and goes again unless taken
		if (hasItem(0x1E))
			return;
		setDsWord(hotspotBase() + 8, 0xFFFF);
		setDsWord(hotspotBase() + 4, 5);
		setDsWord(dsWord(0x4020 + 4 * _room.number()) + 0x22 + 4, 0);
		applyObjectState();
		return;
	case 0x9DBD:	// Peter on the tree stump (room bank), object 5 hidden
		_player.actor.bank = kBankRoom0 + 3;
		slotActor(0x14EC).frame = -1;
		_player.actor.x = 0x108;
		_player.actor.y = 0x47;
		return;
	case 0x9DD9:
		_player.actor.bank = kBankPeter;
		slotActor(0x14EC).frame = 2;
		return;
	case 0x9E01:	// Peter on the bike
		_player.actor.bank = kBankRoom0 + 3;
		_player.actor.x = 0xBD;
		_player.actor.y = 0x44;
		slotActor(0x14D6).frame = -1;
		return;
	case 0x9E1D:	// (the original also writes [13F2] = B2, the renderer's scratch: no effect)
		_player.actor.bank = kBankPeter;
		slotActor(0x14D6).frame = 1;
		_player.actor.y = 0x47;
		return;
	case 0x9E51:	// Peter at the weights
		_player.actor.bank = kBankRoom0 + 3;
		slotActor(0x14C0).frame = -1;
		return;
	case 0x9E61:
		_player.actor.bank = kBankPeter;
		slotActor(0x14C0).frame = 0;
		return;
	case 0x9E89:
		_player.actor.bank = kBankRoom0 + 3;
		_player.actor.frame = 0;
		return;
	case 0x9E97:
		_player.actor.bank = kBankPeter;
		_player.actor.frame = 0x1D;
		return;
	// The finale (rooms 38-41)
	case 0x97D6:	// SEQ 7283 (room 38: the flashback)
		runCue(0x97E0, 0xAC3A);
		return;
	case 0x9630:	// SEQ 7775 (room 39)
		runCue(0x963A, 0xAC3A);
		return;
	case 0x95FE:	// SEQ 7B6F (room 40)
		runCue(0x9608, 0xAC3A);
		return;
	case 0x95D2:	// SEQ 7D59 (room 41)
		runCue(0x95DC, 0xAC3A);
		return;
	case 0x94BA:	// SEQ 8463 (room 41: the fight moves left)
		runCue(0x94C4, 0xAC3A);
		return;
	case 0x93A2:	// SEQ 8579 (room 41: ... and right)
		runCue(0x93AC, 0xAC3A);
		return;
	case 0x9358:	// SEQ 858D (room 41: the end)
		runCue(0x9362, 0xAC3A);
		return;

	case 0x9830:	// 7283 tick 0: scripted walking, Peter faces the middle
		setDsWord(kDsWalkThrough, 1);
		_player.facing = _player.actor.x <= 0xED ? 1 : 0;
		return;
	case 0x9818:	// ported: 9818. 7283 tick 110: song 6
		playSong(6);
		return;
	case 0x984C:	// 7283 tick 160: item 28
		addItem(0x1C);
		return;
	case 0x9852:	// 7283 tick 308: fade out, from now on colours 64..95 only
		startFade(false, 1);
		_sceneFade = true;
		return;
	case 0x985F:	// 7283 tick 326: the flashback scene script (DS:75EF)
		startScene(0x75EF);
		return;
	case 0x99AB:	// 7283 tick 342: everything fades in again
		_sceneFade = false;
		startFade(true, 1);
		return;
	case 0x99BF:	// 7283 tick 426: Peter flies (room bank +42); no interrupt scrolling
		setDsWord(0xAC2B, 1);
		_player.actor.bank = kBankRoom0 + 3;
		_player.actor.frame = 0;
		return;
	case 0x99D2:	// 7283 tick 436: up and away
		_flyCounter = 0x40;
		_modal = kModalFlyUp;
		return;
	case 0x9A55:	// 7283 tick 437: on to room 39 as Pan, sequence rewound
		setDsWord(0x86F7, 0x27);
		setDsWord(0x86F9, 0);
		_roomCountdown = 1;
		setDsWord(0xAC2B, 0);
		_player.actor.bank = kBankPeter;
		abortSequences();
		rewindSequence(_seqDesc);
		return;

	case 0x9742:	// 7775 tick 28: scroll right to x = 0x80 (an unsigned count, as in the original)
		_scrollRequest[0] = (uint16)(0x80 - _scrollX);
		return;
	case 0x974D:	// 7775 tick 42: graph of node 8, walk to node 9
		_player.selectGraph(8);
		setDsWord(kDsWalkThrough, 1);
		setDsWord(0xABCB, 9);
		return;
	case 0x9735:	// 7775 tick 56
		setDsWord(kDsWalkThrough, 0);
		_player.actor.frame = 0x1D;
		return;
	case 0x972F:	// 7775 tick 192: item 3
		addItem(3);
		return;
	case 0x96D1:	// 7775 tick 229: on to room 40 (flags 13, 14), sequence rewound
		setDsWord(0x86F7, 0x28);
		setDsWord(0x86F9, 0);
		_roomCountdown = 1;
		setDsWord(flag(13), 1);
		setDsWord(flag(14), 1);
		abortSequences();
		rewindSequence(_seqDesc);
		return;

	case 0x961C:	// 7B6F tick 38: Peter's own sprites, talk to the first hotspot (pose kept)
		_player.actor.bank = kBankPeter;
		setDsWord(0xAC23, 4);
		talkToHotspot(hotspotBase());
		return;
	case 0x95E4:	// 7D59 tick 0: Peter from the room bank, frame 4, talk to the first hotspot
		_player.actor.bank = kBankRoom0 + 3;
		_player.actor.frame = 4;
		setDsWord(0xAC23, 4);
		talkToHotspot(hotspotBase());
		return;

	// 8579 / 8463: the fight shoves objects 0, 5 and 6 and Peter 1..3 px per tick right / left
	case 0x947D:
	case 0x948E:
	case 0x949F:
	case 0x9595:
	case 0x95A6:
	case 0x95B7: {
		int sign = address < 0x9500 ? 1 : -1;
		int n = (address == 0x947D || address == 0x9595) ? 1 : (address == 0x948E || address == 0x95A6) ? 2 : 3;
		slotActor(0x147E).x += sign * n;
		slotActor(0x14EC).x += sign * n;
		_player.actor.x += sign * 2;
		slotActor(0x1502).x += sign * 2;
		return;
	}
	case 0x9468:	// 8579 tick 14: scroll right, Hook's bubble (conversation 1) with it
	case 0x9580: {	// 8463 tick 14: ... left
		bool right = address == 0x9468;
		_scrollRequest[right ? 0 : 1] = 0x40;
		uint16 bubble = _room.word(0x84) + 0x16;
		_room.setWord(bubble, _room.word(bubble) + (right ? 0x3C : -0x3C));
		return;
	}
	case 0x9370:	// 858D tick 42
		_player.actor.frame = 0x11;
		return;
	case 0x9377:	// 858D tick 96: the end. The original restarts at the title screen (000B2),
		// which is not ported yet
		debugC(1, kDebugScript, "the end");
		abortSequences();
		quitGame();
		return;

	case 0x9EC3:	// exercise done: flag 16 + 1, flag 26 - 1, graph of node 14
		setDsWord(flag(16), dsWord(flag(16)) + 1);
		if (dsWord(flag(26)))
			setDsWord(flag(26), dsWord(flag(26)) - 1);
		_player.selectGraph(0x0E);
		return;

	default:
		warning("Hook: game logic routine %04X is not ported yet", address);
		return;
	}
}

} // End of namespace Hook
