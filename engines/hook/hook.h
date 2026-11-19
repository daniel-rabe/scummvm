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

#include "hook/anim.h"
#include "hook/detection.h"
#include "hook/resources.h"
#include "hook/room.h"
#include "hook/sprites.h"

namespace Hook {

/**
 * Engine for Hook (Ocean / Shadow Software, 1992, DOS).
 *
 * Status: milestone M2 (graphics and animation). Rooms are shown with their objects,
 * idle animations (animation-script VM), Peter in his start pose, the panel with the
 * starting inventory and the original mouse pointer.
 * Keys: arrows scroll, PgUp/PgDn change room. Console: room, scroll, peter, hotspots,
 * walk, res.
 */
class HookEngine : public Engine {
public:
	static const int kScreenWidth = 320;
	static const int kScreenHeight = 200;
	static const int kViewHeight = 128;		///< split screen: view rows 0..127
	static const int kPanelHeight = 72;		///< panel rows 128..199
	static const int kTicksPerSecond = 14;	///< 70 Hz retrace / frame delay 5 (0B61)

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

	Actor &peter() { return _peter; }
	bool _showHotspots = false;
	bool _showWalkGraph = false;
	void redraw() { _dirty = true; }

protected:
	Common::Error run() override;

private:
	bool loadGlobals();
	void applyObjectState();
	void tick();
	void drawScreen();
	void drawView();
	void drawPanel();
	void drawDebug();
	const SpriteBank *bankFor(int id) const;
	void handleKey(const Common::KeyState &key);
	void setCursor();
	Common::Error dumpFrames(const Common::Path &dir);

	const ADGameDescription *_gameDescription;
	Graphics::Screen *_screen = nullptr;
	Resources _res;
	Room _room;
	AnimVM _anim;
	SpriteBank _peterBank;

	Common::Array<Actor> _objects;	///< room objects (original DS:147E)
	Actor _peter;					///< actor 0 (original DS:13FA)

	Buffer _panel;					///< PANEL.BIN: 320x144 sheet, colours 0..31 (+0xE0)
	Buffer _icons;					///< INVENTRY.BIN: 38 icons of 32x24
	uint16 _inventory[16];
	uint16 _inventoryScroll = 0;

	int _scrollX = 0;
	int _scrollY = 0;
	bool _dirty = true;
};

extern HookEngine *g_engine;

} // End of namespace Hook

#endif // HOOK_H
