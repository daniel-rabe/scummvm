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

// Game logic that is table-driven in the original: inventory, item use and combination,
// picking up, LOOK events. Hand-written routines are reached through runHandler()
// (handlers.cpp); see docs/logic-ledger.md.

#include "common/debug.h"

#include "hook/hook.h"

namespace Hook {

// Inventory (docs/formats.md section 5, "Inventory items")
static const uint16 kDsInventoryBase = 0x8709;	// slot k of the bar = word at 8709 + 2*(scroll + k)
static const uint16 kDsInventoryList = 0x870B;	// 16 x u16
static const uint16 kDsInventoryScroll = 0x8705;
static const uint16 kDsInventoryMax = 0x8707;	// largest scroll value
static const uint16 kDsItemLook = 0x40C8;		// far ptrs to messages, item - 1
static const uint16 kDsCombine = 0x3F82;		// item-on-item lists, item - 1
static const uint16 kDsArrowLeft = 0x4164;		// {frame, x + 32, ?, y} (panel)
static const uint16 kDsArrowRight = 0x4182;
static const uint16 kDsObjectState = 0x4020;
static const uint16 kDsHotspotState = 0x1962;
static const uint16 kCsUseAlone = 0x8B60;		// code segment: {off, seg} per item - 1
static const uint16 kCsPickupHook = 0xA9AC;		// code segment: {off, 0} per item - 1
static const uint16 kCsLookEvents = 0x86AF;		// code segment: {off, 0} per event - 1
static const uint16 kMsgFoundSomething = 0xADFE;
static const uint16 kMsgNoCombination = 0xAD92;
static const uint16 kMsgGoldPiece = 0xAC78;

uint16 HookEngine::csWord(uint16 off) const {
	return READ_LE_UINT16(_res.image().data() + off);
}

uint16 HookEngine::barItem(int slot) const {
	return dsWord(kDsInventoryBase + 2 * (dsWord(kDsInventoryScroll) + slot));
}

bool HookEngine::hasItem(uint16 item) const {
	for (int i = 0; i < 16; i++)
		if (dsWord(kDsInventoryList + 2 * i) == item)
			return true;
	return false;
}

void HookEngine::addItem(uint16 item) {
	// 08594: first free slot; scroll so that it is visible
	for (int i = 0; i < 16; i++) {
		if (dsWord(kDsInventoryList + 2 * i) == 0) {
			setDsWord(kDsInventoryList + 2 * i, item);
			if (i - 3 >= 0) {
				setDsWord(kDsInventoryMax, i - 3);
				setDsWord(kDsInventoryScroll, i - 3);
			}
			return;
		}
	}
}

void HookEngine::removeItem(uint16 item) {
	// 08C7F
	int i = 0;
	while (i < 16 && dsWord(kDsInventoryList + 2 * i) != item)
		i++;
	if (i == 16)
		return;
	for (; i < 15; i++)
		setDsWord(kDsInventoryList + 2 * i, dsWord(kDsInventoryList + 2 * i + 2));
	setDsWord(kDsInventoryList + 30, 0);
	int max = (int16)dsWord(kDsInventoryMax) - 1;
	if (max < 0)
		max = 0;
	setDsWord(kDsInventoryMax, max);
	if (max <= (int16)dsWord(kDsInventoryScroll))
		setDsWord(kDsInventoryScroll, max);
}

Message HookEngine::dsMessage(uint16 off) const {
	return off < _ds.size() ? readMessage(dsPtr(off), _ds.size() - off) : Message();
}

void HookEngine::clickInventory(int x, int y) {
	// Arrows (0703D): left at panel (64..88, 44..68), right at (238..262, 44..68)
	if (y >= 44 && y <= 68) {
		if (x >= 64 && x <= 88) {
			if (dsWord(kDsInventoryScroll) > 0) {
				setDsWord(kDsInventoryScroll, dsWord(kDsInventoryScroll) - 1);
				if (_itemSelected)
					_itemX += 32;
			}
			setDsWord(kDsArrowLeft, 2);
			_arrowTimer = 1;
			return;
		}
		if (x >= 238 && x <= 262) {
			if (dsWord(kDsInventoryScroll) != dsWord(kDsInventoryMax)) {
				setDsWord(kDsInventoryScroll, dsWord(kDsInventoryScroll) + 1);
				if (_itemSelected)
					_itemX -= 32;
			}
			setDsWord(kDsArrowRight, 3);
			_arrowTimer = 1;
			return;
		}
	}
	// Item slots (056B7): x 96..223, y 44..68
	if (y < 44 || y > 68 || x < 96 || (x - 96) / 32 >= 4)
		return;
	if (_player.isWalking())
		return;
	uint16 verb = _verb & 0xF;
	int slot = (x - 96) / 32 + 1;
	uint16 item = barItem(slot);
	switch (verb) {
	case 1:	// LOOK (05741): the item's description
		if (!item)
			return;
		_prevVerb = _verb;
		_verb = 0;
		setPending(kPendingItemLook, item);
		_player.walkTo(_player.feetX(), _player.feetY());
		return;
	case 4:	// USE (05781)
		if (_itemSelected) {
			if ((_itemX >> 5) == ((x + 32) >> 5)) {
				_itemSelected = false;
				_itemX = 0;
			} else {
				combine(selectedItem(), item);
			}
			return;
		}
		if (!item)
			return;
		_itemSelected = true;
		_itemX = x + 32;
		useAlone(item);
		return;
	case 5:	// GIVE (05A28)
		if (_itemSelected) {
			_itemSelected = false;
			_itemX = 0;
		} else if (item) {
			_itemSelected = true;
			_itemX = x + 32;
		}
		return;
	default:
		return;
	}
}

uint16 HookEngine::selectedItem() const {
	// The selection is remembered as the slot's x position + 32 ([AC15]), as in the original.
	if (!_itemSelected)
		return 0;
	return barItem((_itemX >> 5) - 3);
}

void HookEngine::useAlone(uint16 item) {
	// 08B10: per-item code or {FFFF, message} in the data segment; nothing = keep selected
	if (_player.isWalking() || item == 0 || item > 38)
		return;
	uint16 off = csWord(kCsUseAlone + 4 * (item - 1)), seg = csWord(kCsUseAlone + 4 * (item - 1) + 2);
	if (!off)
		return;
	_prevVerb = _verb;
	_verb = 0;
	_itemSelected = false;
	_itemX = 0;
	if (seg) {
		setPending(kPendingMessage, dsWord(off + 2));
		_player.walkTo(_player.feetX(), _player.feetY());
	} else {
		runHandler(off);
	}
}

void HookEngine::combine(uint16 a, uint16 b) {
	// 057E7: DS:3F82[a - 1] -> u16 n, n x {u16 item b, u16 result, far ptr message}
	_prevVerb = _verb;
	_verb = 0;
	_itemSelected = false;
	_itemX = 0;
	if (a >= 1 && a <= 37) {
		uint16 list = dsWord(kDsCombine + 4 * (a - 1));
		for (uint i = 0; i < dsWord(list); i++) {
			uint16 e = list + 2 + 8 * i;
			if (dsWord(e) != b)
				continue;
			uint16 result = dsWord(e + 2);
			for (int k = 0; k < 16; k++) {
				uint16 w = kDsInventoryList + 2 * k;
				if (dsWord(w) == a)
					setDsWord(w, result);
				else if (dsWord(w) == b)
					setDsWord(w, 0);
			}
			compactInventory();
			setPending(kPendingMessage, dsWord(e + 4));
			_player.walkTo(_player.feetX(), _player.feetY());
			return;
		}
	}
	setPending(kPendingMessage, kMsgNoCombination);
	_player.walkTo(_player.feetX(), _player.feetY());
}

void HookEngine::compactInventory() {
	// 08AD4: close gaps
	int j = 0;
	for (int i = 0; i < 16; i++) {
		uint16 v = dsWord(kDsInventoryList + 2 * i);
		if (v)
			setDsWord(kDsInventoryList + 2 * j++, v);
	}
	for (; j < 16; j++)
		setDsWord(kDsInventoryList + 2 * j, 0);
	int max = (int16)dsWord(kDsInventoryMax) - 1;
	setDsWord(kDsInventoryMax, max < 0 ? 0 : max);
}

void HookEngine::startPickup(uint16 hs) {
	// 083D8: Peter turns to the hotspot and holds the pose for 8 ticks; the object is taken
	// on the last but one (083FA).
	_player.facing = dsWord(hs + 2) & 3;
	_pickupHotspot = hs;
	_pickupTimer = 8;
}

void HookEngine::pickupTick() {
	uint16 hs = _pickupHotspot;
	uint16 k = dsWord(hs + 8);
	uint16 rec = dsWord(kDsObjectState + 4 * _room.number()) + 2 + 16 * (k - 1);
	_player.actor.frame = (int16)dsWord(rec + 0x0E);
	if (_pickupTimer != 1)
		return;
	// 0851D: hide the object, clear the hotspot, give the item
	uint16 obj = dsWord(rec), hsRec = dsWord(kDsHotspotState + 4 * _room.number()) + 24 * dsWord(rec + 2);
	if (obj < _objects.size())
		_objects[obj].frame = -1;
	setDsWord(hsRec + 4, 0);
	setDsWord(hsRec + 8, 0);
	setDsWord(hsRec + 0xA, 0);
	setDsWord(rec + 4, 0);
	uint16 item = dsWord(rec + 0x0A);
	addItem(item);
	// 0A994: per-item hook after taking it
	if (item >= 1 && item <= 38 && csWord(kCsPickupHook + 4 * (item - 1)))
		runHandler(csWord(kCsPickupHook + 4 * (item - 1)));
	_prevVerb = _verb;
	_verb = 0;
}

void HookEngine::lookEventTick() {
	// 085D0, after the LOOK text has been said: [ABF3] = n finds item n ("I've found
	// something." first), 0x4Cnn runs event n (cs:86AF)
	uint16 v = _lookEvent;
	if ((v >> 8) == 0x4C) {
		_lookEvent = 0;
		uint16 n = v & 0xFF;
		if (n >= 1 && n <= 4)
			runHandler(csWord(kCsLookEvents + 4 * (n - 1)));
		return;
	}
	if (!(v & 0x8000)) {
		_lookEvent |= 0x8000;
		say(dsMessage(kMsgFoundSomething), kFacingDown);
		return;
	}
	_lookEvent = 0;
	uint16 item = v & 0x7FFF;
	if (item == 6) {
		runHandler(0xA045);
		say(dsMessage(kMsgGoldPiece), kFacingDown);
		return;
	}
	say(dsMessage(dsWord(kDsItemLook + 4 * (item - 1))), kFacingDown);
	addItem(item);
}

void HookEngine::itemLook(uint16 item) {
	if (item >= 1 && item <= 38)
		say(dsMessage(dsWord(kDsItemLook + 4 * (item - 1))), kFacingDown);
}

void HookEngine::arrowTick() {
	// 0716A: pressed arrows spring back after 2 ticks
	if (_arrowTimer && ++_arrowTimer == 3) {
		setDsWord(kDsArrowLeft, 0);
		setDsWord(kDsArrowRight, 1);
		_arrowTimer = 0;
	}
}

} // End of namespace Hook
