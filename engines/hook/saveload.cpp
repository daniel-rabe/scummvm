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
#include "common/endian.h"
#include "common/savefile.h"
#include "common/stream.h"
#include "common/translation.h"

#include "hook/hook.h"

namespace Hook {

/*
 * A save is what HOOK.SAV holds per slot (docs/formats.md "Save games"): the data segment
 * DS:86A7..A91E, in_use first and the 16-bit byte sum of the rest last. Nothing else is
 * saved; a load enters the saved room at its saved entry, as the original does.
 *
 * ScummVM file: "HOOK" u32, u8 version, u16 size, the slot bytes, then the extended header.
 * The panel's slots 1..5 are ScummVM slots 1..5.
 */

static const uint32 kSaveMagic = MKTAG('H', 'O', 'O', 'K');
static const byte kSaveVersion = 1;
static const uint16 kDsSave = 0x86A7;
static const uint16 kSaveSize = 0x2278;
static const uint16 kDsOptions = 0xCE9B;
static const uint16 kDsVerbButtons = 0x0D40;
static const uint16 kVerbLatched = 0x8000;
static const uint16 kDsPanelName = 0x0504;		// "panel.bin"
static const uint16 kDsPanel2Name = 0x050E;		// "panel2.bin"

static uint16 checksum(const byte *p) {
	uint16 sum = 0;
	for (uint i = 0; i < kSaveSize - 2u; i++)
		sum += p[i];
	return sum;
}

bool HookEngine::canLoadGameStateCurrently(Common::U32String *msg) {
	return _inGame;
}

bool HookEngine::canSaveGameStateCurrently(Common::U32String *msg) {
	// The original saves from its panel only (Peter standing, no scene running), and not from
	// room 39 on, where the panel is gone
	return _inGame && _room.number() < 0x27 && _modal == kModalNone && !sequenceRunning() &&
		   !_convActive && _speechTotal == 0 && _roomCountdown <= 0 && !_pickupTimer && !_giveTimer;
}

Common::Error HookEngine::saveGameStream(Common::WriteStream *stream, bool isAutosave) {
	// ported: C678. Peter, the view and the walk node go in (none of it is read back)
	setDsWord(0x86A9, _player.actor.x);
	setDsWord(0x86AB, _player.actor.y);
	setDsWord(0x86AD, _scrollX);
	setDsWord(0x86AF, _scrollY);
	setDsWord(0x86B1, _player.facing);
	setDsWord(0x86A7, 1);
	setDsWord(kDsSave + kSaveSize - 2, checksum(dsPtr(kDsSave)));
	stream->writeUint32BE(kSaveMagic);
	stream->writeByte(kSaveVersion);
	stream->writeUint16LE(kSaveSize);
	stream->write(dsPtr(kDsSave), kSaveSize);
	return stream->err() ? Common::kWritingFailed : Common::kNoError;
}

Common::Error HookEngine::loadGameStream(Common::SeekableReadStream *stream) {
	// ported: C524
	if (stream->readUint32BE() != kSaveMagic || stream->readByte() > kSaveVersion ||
		stream->readUint16LE() != kSaveSize)
		return Common::kUnknownError;
	byte data[kSaveSize];
	if (stream->read(data, kSaveSize) != kSaveSize || READ_LE_UINT16(data + kSaveSize - 2) != checksum(data) ||
		!READ_LE_UINT16(data))
		return Common::kReadingFailed;
	memcpy(_ds.data() + kDsSave, data, kSaveSize);

	// Peter's sprite file and the panel; then the room as on a room change (0C8B2)
	loadPeterBank(dsWord(0x86EF));
	loadPanel((const char *)dsPtr(dsWord(0x86F7) >= 0x27 ? kDsPanel2Name : kDsPanelName));
	resetTransient();
	_inGame = true;
	if (!enterRoom(dsWord(0x86F7), dsWord(0x86F9)))
		return Common::kReadingFailed;
	return Common::kNoError;
}

void HookEngine::optionsTick() {
	// ported: C398. USE on the save disk (08C18) requests the panel ([CE9B] = 1); it comes up
	// once Peter stands and the view is not scrolling. Slots 1..5 are the verb buttons.
	uint16 state = dsWord(kDsOptions);
	if (!state)
		return;
	if (_room.number() >= 0x27) {
		setDsWord(kDsOptions, 0);
		return;
	}
	if (state == 1) {
		if (_player.isWalking() || _scrollRequest[0] || _scrollRequest[1] || _scrollRequest[2] || _scrollRequest[3])
			return;
		_optionsReturn = _panelName;
		loadPanel("PANEL3.BIN");
		setDsWord(kDsOptions, 3);
		return;
	}
	if (!_clickLeft)
		return;
	_clickLeft = false;
	playEffect(0);	// C464
	if (_mouse.y < kViewHeight) {
		closeOptions();	// C47A: a click in the view
		return;
	}
	int x = _mouse.x, y = _mouse.y - kViewHeight;
	// 073DE: a number button latches like a verb
	for (uint i = 0; i < 5; i++) {
		uint16 b = kDsVerbButtons + 12 * i;
		if (x >= (int16)dsWord(b) && y >= (int16)dsWord(b + 2) && x < (int16)dsWord(b + 4) && y < (int16)dsWord(b + 6)) {
			uint16 v = kVerbLatched | (i + 1);
			_verb = (_verb == v) ? 0 : v;
			return;
		}
	}
	int slot = _verb & 0xF;
	if (y < 0x32 || y >= 0x3E || !slot)
		return;
	if (x >= 0x69 && x < 0x94) {
		// C524: an empty or bad slot leaves the panel up
		debugC(1, kDebugScript, "load slot %d", slot);
		loadGameState(slot);
	} else if (x >= 0xB2 && x < 0xDC) {
		// C678
		debugC(1, kDebugScript, "save slot %d", slot);
		if (saveGameState(slot, Common::String::format("Slot %d", slot)).getCode() == Common::kNoError)
			closeOptions();
	}
}

void HookEngine::closeOptions() {
	// C487 / C6EE: the normal panel back, no verb, no selected item
	loadPanel(_optionsReturn.empty() ? "PANEL.BIN" : _optionsReturn.c_str());
	_verb = _prevVerb = 0;
	_itemSelected = false;
	_itemX = 0;
	setDsWord(0xABC9, 0);
	setDsWord(kDsOptions, 0);
}

} // End of namespace Hook
