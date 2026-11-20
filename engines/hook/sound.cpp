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

// Engine side of the sound (docs/formats.md section 5, "Sound", "Engine side"): room songs,
// effects, the request block written by action sequences and the per-room ambient streams.
// Everything goes through the driver's request bytes, as in the original.

#include "common/config-manager.h"
#include "common/debug.h"
#include "common/file.h"

#include "hook/hook.h"
#include "hook/music.h"

namespace Hook {

static const uint16 kDsRoomSongs = 0xB6A7;		// u8 per room, FF = none
static const uint16 kDsRoomSounds = 0xB6D1;		// u16 per room -> effect table (byte offsets)
static const uint16 kDsAmbient = 0xC743;		// u16 per room -> u16 n, n x u16 stream
static const uint16 kDsAmbientCount = 0xB7B7;	// 8 x u16 frame counters
static const uint16 kDsAmbientCursor = 0xB7C7;	// 8 x u16 byte offsets into the streams
static const uint16 kDsSoundRequest = 0xB7AD;	// go, ?, ?, room sound, volume

void HookEngine::initSound() {
	_music = MusicDriver::create(_res);
	if (!_music)
		debugC(1, kDebugScript, "sound: no music device");
}

void HookEngine::syncSoundSettings() {
	Engine::syncSoundSettings();
	if (_music)
		_music->syncVolume();
}

void HookEngine::playSong(uint8 n) {
	// ported: 0238
	debugC(1, kDebugScript, "sound: song %u", n);
	if (_music)
		_music->playSong(n);
}

void HookEngine::playEffect(uint8 n) {
	// ported: 0261. Effect n + 1 at full volume (B024 = 0: the click, B02C = 1: Tinkerbell)
	debugC(1, kDebugScript, "sound: effect %u", n + 1);
	if (_music)
		_music->requestEffect(n + 1, 0x7F);
}

void HookEngine::playRoomSound(uint16 offset, int volume) {
	// A816 / A920 / 5E4B: a byte offset into the room's effect table, driver effect = byte + 1
	// (only the fidget, 5E4B, also sets the effect volume)
	uint16 table = dsWord(kDsRoomSounds + 2 * _room.number());
	uint8 n = (uint8)(dsWord(table + offset) + 1);
	debugC(1, kDebugScript, "sound: room sound %u -> effect %u", offset, n);
	if (_music)
		_music->requestEffect(n, volume);
}

void HookEngine::roomSound() {
	// ported: ACBB (head). The room's song, then the ambient streams restart (A97F)
	playSong((uint8)_ds[kDsRoomSongs + _room.number()]);
	for (int i = 0; i < 8; i++) {
		setDsWord(kDsAmbientCount + 2 * i, 0);
		setDsWord(kDsAmbientCursor + 2 * i, 0);
	}
}

void HookEngine::soundTick() {
	// ported: A813
	ambientTick();
	if (!dsWord(kDsSoundRequest))
		return;
	setDsWord(kDsSoundRequest, 0);
	playRoomSound(dsWord(kDsSoundRequest + 6), -1);	// the volume [B7B5] is clamped, never sent
}

void HookEngine::ambientTick() {
	// ported: A858. Per stream and frame one entry: FFFF n = wait until the counter reaches n,
	// FFFE = restart, v a c s = room sound s
	uint16 list = dsWord(kDsAmbient + 2 * _room.number());
	int n = (int16)dsWord(list);
	for (int i = 0; i < n && i < 8; i++) {
		uint16 count = kDsAmbientCount + 2 * i, cursor = kDsAmbientCursor + 2 * i;
		for (int guard = 0; guard < 4; guard++) {
			uint16 p = dsWord(list + 2 + 2 * i) + dsWord(cursor);
			uint16 w = dsWord(p);
			if (!(w & 0x8000)) {
				playRoomSound(dsWord(p + 6), -1);
				setDsWord(cursor, dsWord(cursor) + 8);
			} else if (w == 0xFFFF) {
				if (dsWord(p + 2) == dsWord(count)) {
					setDsWord(cursor, dsWord(cursor) + 4);
					setDsWord(count, 0);
				}
			} else if (w == 0xFFFE) {
				setDsWord(count, 0);
				setDsWord(cursor, 0);
				continue;
			}
			break;
		}
		setDsWord(count, dsWord(count) + 1);
	}
}

Common::Error HookEngine::dumpMusic(const Common::Path &dir) {
	// Development aid: "dump_song=N" or "dump_fx=N" (+ "dump_device=adlib|mt32|gm",
	// "dump_ticks=T") writes the driver's output for T ticks to <dir>/music.txt
	Common::String dev = ConfMan.hasKey("dump_device") ? ConfMan.get("dump_device") : "adlib";
	MusicDriver::Device d = dev == "mt32" ? MusicDriver::kMT32 : dev == "gm" ? MusicDriver::kGM : MusicDriver::kAdLib;
	MusicDriver *m = MusicDriver::createLogger(_res, d);
	if (!m)
		return Common::Error(Common::kReadingFailed, "sound driver");
	if (ConfMan.hasKey("dump_song"))
		m->playSong(ConfMan.getInt("dump_song"));
	else
		m->requestEffect(ConfMan.getInt("dump_fx"), 0x7F);
	int ticks = ConfMan.hasKey("dump_ticks") ? ConfMan.getInt("dump_ticks") : 1000;
	for (int t = 0; t < ticks; t++)
		m->tick();
	Common::DumpFile f;
	if (!f.open(dir.appendComponent("music.txt"), true)) {
		delete m;
		return Common::Error(Common::kWritingFailed);
	}
	for (const Common::String &s : m->log())
		f.writeString(s + "\n");
	f.close();
	delete m;
	return Common::kNoError;
}

} // End of namespace Hook
