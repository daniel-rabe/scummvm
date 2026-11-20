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

#include "common/config-manager.h"
#include "common/debug.h"
#include "common/file.h"
#include "common/system.h"
#include "common/tokenizer.h"
#include "image/png.h"

#include "hook/hook.h"
#include "hook/intro.h"
#include "hook/music.h"

namespace Hook {

static const uint16 kVerbLatched = 0x8000;

/*
 * Development aids for headless tests (not meant for upstream). Config keys in the game's section:
 *  - dump_frames=<dir>: one composed screen per room as PNG (dump_ticks ticks after entering);
 *    with dump_clicks=x,y[,verb];... scripted clicks from dump_room / dump_entry, optional
 *    dump_items, dump_scroll, dump_flags, dump_seq, a PNG and a debug line after each click;
 *  - dump_frames=<dir> with dump_song=N or dump_fx=N (dump_device, dump_ticks): the driver's
 *    output as text;
 *  - dump_intro=<dir> (dump_every): the intro, the title and the end credits as PNGs.
 */

bool HookEngine::isTestMode() const {
	return ConfMan.hasKey("dump_frames") || ConfMan.hasKey("dump_intro");
}

Common::Error HookEngine::runTestMode() {
	if (ConfMan.hasKey("dump_intro")) {
		initSound();
		Intro intro(this, _screen);
		intro.setDump(Common::Path(ConfMan.get("dump_intro"), '/'),
					  ConfMan.hasKey("dump_every") ? ConfMan.getInt("dump_every") : 10);
		intro.run();
		intro.credits(true);
		return Common::kNoError;
	}
	Common::Path dir(ConfMan.get("dump_frames"), '/');
	if (ConfMan.hasKey("dump_song") || ConfMan.hasKey("dump_fx"))
		return dumpMusic(dir);
	return dumpFrames(dir);
}

Common::Error HookEngine::dumpFrames(const Common::Path &dir) {
	// Optional scripted input for headless tests: "dump_clicks=x,y;x,y;..." clicks (screen
	// coordinates) in room 0, each followed by "dump_ticks" ticks; a frame is written after each.
	int ticks = ConfMan.hasKey("dump_ticks") ? ConfMan.getInt("dump_ticks") : 0;
	auto write = [&](const Common::String &name) {
		drawScreen();
		byte pal[256 * 3];
		for (int i = 0; i < 256 * 3; i++)
			pal[i] = _basePalette[i] * _palLevel[i / 3] / 32;
		Common::DumpFile out;
		Common::Path path = dir.appendComponent(name);
		if (!out.open(path) || !Image::writePNG(out, *_screen->surfacePtr(), pal))
			warning("Hook: cannot write %s", path.toString().c_str());
	};
	if (ConfMan.hasKey("dump_clicks")) {
		enterRoom(ConfMan.hasKey("dump_room") ? ConfMan.getInt("dump_room") : 0,
				  ConfMan.hasKey("dump_entry") ? ConfMan.getInt("dump_entry") : 0);
		if (ConfMan.hasKey("dump_items")) {
			Common::StringTokenizer items(ConfMan.get("dump_items"), ",");
			while (!items.empty())
				addItem(atoi(items.nextToken().c_str()));
		}
		if (ConfMan.hasKey("dump_scroll")) {
			int sx = 0, sy = 0;
			sscanf(ConfMan.get("dump_scroll").c_str(), "%d,%d", &sx, &sy);
			setScroll(sx, sy);
		}
		if (ConfMan.hasKey("dump_flags")) {
			// "id=value,id=value"
			Common::StringTokenizer flags(ConfMan.get("dump_flags"), ",");
			while (!flags.empty()) {
				int id = 0, v = 0;
				sscanf(flags.nextToken().c_str(), "%d=%d", &id, &v);
				setDsWord(0x86B7 + 2 * id, v);
			}
		}
		if (ConfMan.hasKey("dump_seq"))
			startSequence((uint16)strtol(ConfMan.get("dump_seq").c_str(), nullptr, 16));
		Common::StringTokenizer tok(ConfMan.get("dump_clicks"), ";");
		int step = 0;
		while (!tok.empty()) {
			Common::String c = tok.nextToken();
			int x = 0, y = 0, verb = 0;
			sscanf(c.c_str(), "%d,%d,%d", &x, &y, &verb);
			if (verb && verb < 8)
				_verb = kVerbLatched | verb;
			_mouse = Common::Point(x, y);
			tick();
			if (verb == 9)
				_clickRight = true;	// "x,y,9" = right-click
			else if (verb != 8)
				_clickLeft = true;	// "x,y,8" = no click
			for (int t = 0; t < ticks; t++)
				tick();
			_dirty = true;
			write(Common::String::format("step%02d.png", step++));
			debug("step %d: room %u Peter %d,%d frame %d facing %d scroll %d,%d speech %d tink %d,%d", step,
				  _room.number(), _player.actor.x, _player.actor.y, _player.actor.frame, _player.facing,
				  _scrollX, _scrollY, (int)_speech.size(), _actors[1].x, _actors[1].y);
			if (_gameEnded) {
				// as in run(): the end credits (dumped), then a new game
				debug("the end after step %d", step);
				Intro intro(this, _screen);
				intro.setDump(dir, 20);
				Intro::Result r = intro.credits(true);
				_gameEnded = false;
				newGame();
				debug("credits result %d, new game: room %u, flag 1 = %u, items %u %u", (int)r, _room.number(),
					  dsWord(0x86B9), dsWord(0x870B), dsWord(0x870D));
				write("newgame.png");
				break;
			}
		}
		return Common::kNoError;
	}
	for (uint n = 0; n < Room::kNumRooms; n++) {
		if (!enterRoom(n)) {
			warning("Hook: room %u failed to load", n);
			continue;
		}
		for (int t = 0; t < ticks; t++)
			tick();
		_dirty = true;
		write(Common::String::format("frame%02u.png", n));
	}
	return Common::kNoError;
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
