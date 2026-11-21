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
 *    dump_load=<file> starts from a HOOK.SAV slot, dump_save=N,M writes one after those steps;
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

bool HookEngine::hotspotPoint(int k, int &x, int &y) const {
	// A visible point of hotspot k's rectangle that hit-tests to it, nearest to its centre
	if (k < 0 || k >= (int)_room.word(0x7A))
		return false;
	uint16 rects = _room.word(0x7C);
	int x1 = _room.sword(rects + 8 * k), y1 = _room.sword(rects + 8 * k + 2);
	int x2 = _room.sword(rects + 8 * k + 4), y2 = _room.sword(rects + 8 * k + 6);
	int mx = (x1 + x2) / 2, my = (y1 + y2) / 2, best = -1;
	for (int cy = y1; cy <= y2; cy++) {
		for (int cx = x1; cx <= x2; cx++) {
			int sx = cx - _scrollX - 1, sy = cy - _scrollY;
			if (sx < 0 || sy < 0 || sx >= kScreenWidth || sy >= kViewHeight)
				continue;
			if (hitTest(cx, cy) != hotspotRecord(k))
				continue;
			int d = (cx - mx) * (cx - mx) + (cy - my) * (cy - my);
			if (best < 0 || d < best) {
				best = d;
				x = sx;
				y = sy;
			}
		}
	}
	return best >= 0;
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
		if (ConfMan.hasKey("dump_load")) {
			// "dump_load=file": start from a HOOK.SAV slot (0x2278 raw bytes), as the original's
			// load panel does (a replay checkpoint shared with the original, tools/dosbox/play.sh)
			Common::File f;
			byte data[0x2278];
			if (!f.open(Common::FSNode(Common::Path(ConfMan.get("dump_load")))) || f.read(data, sizeof(data)) != sizeof(data) ||
				loadSlotData(data).getCode() != Common::kNoError)
				error("Hook: cannot load %s", ConfMan.get("dump_load").c_str());
			// let Peter's entry walk end (play.sh waits about 6 s after LOAD in the original)
			for (int i = 0; i < 60; i++)
				tick();
			debug("loaded: room %u Peter %d,%d phase %d/%d", _room.number(), _player.actor.x, _player.actor.y,
				  _player.stepPhaseH(), _player.stepPhaseV());
		}
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
			// "x,y[,verb[,ticks]]": ticks overrides dump_ticks for this step. "h,k,verb,ticks"
			// clicks hotspot k of the current room and "w,x,y,verb,ticks" a canvas point, both
			// resolved to screen pixels with the current scroll and logged ("click x y").
			int x = 0, y = 0, verb = 0, stepTicks = -1;
			if (c.hasPrefix("h,")) {
				int k = -1;
				sscanf(c.c_str() + 2, "%d,%d,%d", &k, &verb, &stepTicks);
				if (!hotspotPoint(k, x, y)) {
					warning("Hook: hotspot %d of room %u not visible", k, _room.number());
					verb = 8;	// no click
				}
			} else if (c.hasPrefix("t,")) {
				// "t,verb,ticks": Tinkerbell (07A12: the 16x16 box left of and below her slot)
				sscanf(c.c_str() + 2, "%d,%d", &verb, &stepTicks);
				x = _actors[1].x - 8 - _scrollX - 1;
				y = _actors[1].y + 8 - _scrollY;
			} else if (c.hasPrefix("w,")) {
				int wx = 0, wy = 0;
				sscanf(c.c_str() + 2, "%d,%d,%d,%d", &wx, &wy, &verb, &stepTicks);
				x = wx - _scrollX - 1;
				y = wy - _scrollY;
				// the original's cursor stays on the screen: a point beyond the view's edge is
				// clicked at the edge (else the replay in DOSBox clicks somewhere else)
				// (dump_clamp: clicked at the edge, as the original does)
				if (x < 0 || x > 319 || y < 0 || y >= kViewHeight) {
					warning("Hook: canvas point %d,%d is off screen", wx, wy);
					if (ConfMan.hasKey("dump_clamp")) {
						x = CLIP(x, 0, 319);
						y = CLIP(y, 0, (int)kViewHeight - 1);
					}
				}
			} else {
				sscanf(c.c_str(), "%d,%d,%d,%d", &x, &y, &verb, &stepTicks);
			}
			if (verb == 10) {
				// "@hat" (verb 10): time the swing to the passer-by as tools/dosbox/hat.py does in
				// the original. A359 catches the hat at swing counter 5Dh..63h when the passer-by
				// (room object 5) has hx <= Peter's x <= hx + 18h; Peter's x per counter from 4D7F.
				// His walk repeats: record one cycle, then wait for a press that works for a
				// counter lag of 1 to 3 ticks.
				static const int kPeterX[7] = {271, 247, 230, 202, 175, 157, 141};
				// two cycles: the period is the shift that repeats the recording best
				const int len = 420;
				Common::Array<int> hx;
				for (int i = 0; i < len; i++) {
					tick();
					hx.push_back(_objects.size() > 5 ? _objects[5].x : -1000);
				}
				int period = 0, bestMiss = len;
				for (int p = 150; p <= 260; p++) {
					int miss = 0;
					for (int i = 0; i + p < len; i++)
						miss += ABS(hx[i] - hx[i + p]) > 6;
					if (miss < bestMiss) {
						bestMiss = miss;
						period = p;
					}
				}
				auto at = [&](int i) { return hx[len - period + (i - len) % period]; };	// i >= len
				int wait = -1;
				for (int d = 1; d <= period && wait < 0; d++) {
					bool ok = true;
					for (int lag = 0; lag <= 2 && ok; lag++) {
						bool any = false;
						for (int k = 0; k < 7; k++) {
							int h = at(len - 1 + d + 0x5D + k + lag);
							if (h <= kPeterX[k] && kPeterX[k] <= h + 0x18)
								any = true;
						}
						ok = any;
					}
					if (ok)
						wait = d;
				}
				for (int i = 1; i < wait; i++)
					tick();
				debug("hat: period %d (%d misses), waited %d + %d ticks", period, bestMiss, len, wait - 1);
				verb = 0;
			}
			if (verb != 8)
				debug("click %d %d%s", x, y, verb == 9 ? " r" : "");
			else if (c.hasPrefix("h,") || c.hasPrefix("w,") || c.hasPrefix("t,"))
				debug("click - -");	// not visible: no click (resolved.txt waits instead)
			if (verb && verb < 8)
				_verb = kVerbLatched | verb;
			_mouse = Common::Point(x, y);
			tick();
			if (verb == 9)
				_clickRight = true;	// "x,y,9" = right-click
			else if (verb != 8)
				_clickLeft = true;	// "x,y,8" = no click
			for (int t = 0; t < (stepTicks >= 0 ? stepTicks : ticks); t++) {
				tick();
				if (ConfMan.hasKey("dump_trace")) {
					// "dump_trace=N": room object N's position and frame every tick
					// ("dump_trace=-1": Peter, with his walk-cycle phase)
					int n = ConfMan.getInt("dump_trace");
					if (n < 0)
						debug("tick %u: room %u Peter %d,%d frame %d phase %d/%d depth %d A %04X B %04X/%u", dsWord(0x86FD),
							  _room.number(), _player.actor.x, _player.actor.y, _player.actor.frame, _player.stepPhaseH(),
							  _player.stepPhaseV(), _player.depth(), dsWord(0xAC30), dsWord(0xAC48), dsWord(0xAB79));
					else if ((uint)n < _objects.size())
						debug("tick %u: room %u obj %u %d,%d frame %d play %u", dsWord(0x86FD), _room.number(), n,
							  _objects[n].x, _objects[n].y, _objects[n].frame, dsWord(0x86FD));
				}
			}
			_dirty = true;
			write(Common::String::format("step%03d.png", step++));
			Common::String inv = Common::String::format(" bar %u:", dsWord(0x8705));
			for (int i = 0; i < 16 && dsWord(0x870B + 2 * i); i++)
				inv += Common::String::format(" %u", dsWord(0x870B + 2 * i));
			if (ConfMan.hasKey("dump_watch")) {
				// "dump_watch=401E,86BB": data segment words (hex) added to the step line
				Common::StringTokenizer w(ConfMan.get("dump_watch"), ",");
				while (!w.empty()) {
					Common::String a = w.nextToken();
					inv += Common::String::format(" [%s]=%u", a.c_str(), dsWord((uint16)strtol(a.c_str(), nullptr, 16)));
				}
			}
			inv += Common::String::format(" phase %d/%d", _player.stepPhaseH(), _player.stepPhaseV());
			debug("step %d: room %u Peter %d,%d frame %d facing %d scroll %d,%d speech %d tink %d,%d items%s", step,
				  _room.number(), _player.actor.x, _player.actor.y, _player.actor.frame, _player.facing,
				  _scrollX, _scrollY, (int)_speech.size(), _actors[1].x, _actors[1].y, inv.c_str());
			if (ConfMan.hasKey("dump_save")) {
				// "dump_save=N,M": after those steps a HOOK.SAV slot (raw) as saveNNN.bin, a
				// checkpoint for the original (tools/dosbox/mksav.py) and for dump_load
				Common::StringTokenizer s(ConfMan.get("dump_save"), ",");
				while (!s.empty()) {
					if (atoi(s.nextToken().c_str()) != step)
						continue;
					Common::DumpFile out;
					Common::Path path = dir.appendComponent(Common::String::format("save%03d.bin", step));
					if (!out.open(path) || out.write(saveSlotData(), 0x2278) != 0x2278)
						warning("Hook: cannot write %s", path.toString().c_str());
					debug("saved after step %d: room %u entry %u", step, dsWord(0x86F7), dsWord(0x86F9));
				}
			}
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
