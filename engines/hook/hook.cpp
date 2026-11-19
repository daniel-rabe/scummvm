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
#include "common/debug-channels.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/util.h"
#include "graphics/framelimiter.h"
#include "graphics/paletteman.h"
#include "image/png.h"

#include "hook/console.h"
#include "hook/hook.h"

namespace Hook {

HookEngine *g_engine;

HookEngine::HookEngine(OSystem *syst, const ADGameDescription *gameDesc) : Engine(syst),
	_gameDescription(gameDesc) {
	g_engine = this;
}

HookEngine::~HookEngine() {
	delete _screen;
}

Common::Error HookEngine::run() {
	if (!_res.open("HOOK.EXE"))
		return Common::Error(Common::kReadingFailed, "HOOK.EXE");

	initGraphics(kScreenWidth, kScreenHeight);
	_screen = new Graphics::Screen();
	setDebugger(new Console());

	// Development aid (remove before upstreaming): with "dump_rooms=<dir>" in the game's
	// config section, write every room canvas as PNG and quit.
	if (ConfMan.hasKey("dump_rooms"))
		return dumpRooms(Common::Path(ConfMan.get("dump_rooms"), '/'));

	if (!enterRoom(0))
		return Common::Error(Common::kReadingFailed, "LOCAT00.BIN");

	Graphics::FrameLimiter limiter(g_system, 60);
	Common::Event e;
	while (!shouldQuit()) {
		while (g_system->getEventManager()->pollEvent(e)) {
			if (e.type == Common::EVENT_KEYDOWN)
				handleKey(e.kbd);
		}
		if (_dirty)
			drawView();
		limiter.delayBeforeSwap();
		_screen->update();
		limiter.startFrame();
	}
	return Common::kNoError;
}

bool HookEngine::enterRoom(uint number) {
	if (!_room.load(_res, number))
		return false;
	byte pal[256 * 3];
	_room.getPalette(_res, pal);
	g_system->getPaletteManager()->setPalette(pal, 0, 256);
	setScroll(0, 0);
	return true;
}

void HookEngine::setScroll(int x, int y) {
	_scrollX = CLIP<int>(x, 0, MAX<int>(0, _room.width() - kScreenWidth));
	_scrollY = CLIP<int>(y, 0, MAX<int>(0, _room.height() - kScreenHeight));
	_dirty = true;
}

Common::Error HookEngine::dumpRooms(const Common::Path &dir) {
	for (uint n = 0; n < Room::kNumRooms; n++) {
		Room r;
		if (!r.load(_res, n)) {
			warning("Hook: room %u failed to load", n);
			continue;
		}
		byte pal[256 * 3];
		r.getPalette(_res, pal);
		Graphics::Surface s;
		s.init(r.width(), r.height(), r.width(), const_cast<byte *>(r.canvas()),
			   Graphics::PixelFormat::createFormatCLUT8());
		Common::DumpFile out;
		Common::Path path = dir.appendComponent(Common::String::format("room%02u.png", n));
		if (!out.open(path) || !Image::writePNG(out, s, pal))
			warning("Hook: cannot write %s", path.toString().c_str());
	}
	return Common::kNoError;
}

void HookEngine::drawView() {
	_screen->clear(0);
	int w = MIN<int>(kScreenWidth, _room.width() - _scrollX);
	int h = MIN<int>(kScreenHeight, _room.height() - _scrollY);
	for (int y = 0; y < h; y++)
		memcpy(_screen->getBasePtr(0, y), _room.canvas() + (_scrollY + y) * _room.width() + _scrollX, w);
	_screen->markAllDirty();
	_dirty = false;
}

void HookEngine::handleKey(const Common::KeyState &key) {
	switch (key.keycode) {
	case Common::KEYCODE_LEFT:
		setScroll(_scrollX - 8, _scrollY);
		break;
	case Common::KEYCODE_RIGHT:
		setScroll(_scrollX + 8, _scrollY);
		break;
	case Common::KEYCODE_UP:
		setScroll(_scrollX, _scrollY - 8);
		break;
	case Common::KEYCODE_DOWN:
		setScroll(_scrollX, _scrollY + 8);
		break;
	case Common::KEYCODE_PAGEUP:
		enterRoom((_room.number() + Room::kNumRooms - 1) % Room::kNumRooms);
		break;
	case Common::KEYCODE_PAGEDOWN:
		enterRoom((_room.number() + 1) % Room::kNumRooms);
		break;
	default:
		break;
	}
}

} // End of namespace Hook
