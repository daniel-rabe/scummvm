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

#ifndef HOOK_H
#define HOOK_H

#include "common/scummsys.h"
#include "common/error.h"
#include "common/keyboard.h"
#include "engines/engine.h"
#include "graphics/screen.h"

#include "hook/detection.h"
#include "hook/resources.h"
#include "hook/room.h"

namespace Hook {

/**
 * Engine for Hook (Ocean / Shadow Software, 1992, DOS).
 *
 * Milestone M1 (skeleton): loads every resource straight from HOOK.EXE and shows the
 * room backgrounds. Keys: arrows scroll, PgUp/PgDn change room; console: room, scroll, res.
 */
class HookEngine : public Engine {
public:
	static const int kScreenWidth = 320;
	static const int kScreenHeight = 200;

	HookEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~HookEngine() override;

	bool hasFeature(EngineFeature f) const override {
		return f == kSupportsReturnToLauncher;
	}

	const Resources &resources() const { return _res; }
	const Room &room() const { return _room; }

	bool enterRoom(uint number);
	void setScroll(int x, int y);
	int scrollX() const { return _scrollX; }
	int scrollY() const { return _scrollY; }

protected:
	Common::Error run() override;

private:
	void drawView();
	Common::Error dumpRooms(const Common::Path &dir);
	void handleKey(const Common::KeyState &key);

	const ADGameDescription *_gameDescription;
	Graphics::Screen *_screen = nullptr;
	Resources _res;
	Room _room;
	int _scrollX = 0;
	int _scrollY = 0;
	bool _dirty = true;
};

extern HookEngine *g_engine;

} // End of namespace Hook

#endif // HOOK_H
