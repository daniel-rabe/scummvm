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
#include "common/events.h"
#include "common/keyboard.h"
#include "engines/engine.h"
#include "graphics/screen.h"

#include "hook/anim.h"
#include "hook/detection.h"
#include "hook/player.h"
#include "hook/resources.h"
#include "hook/room.h"
#include "hook/sprites.h"
#include "hook/text.h"

namespace Hook {

/**
 * Engine for Hook (Ocean / Shadow Software, 1992, DOS).
 *
 * Status: milestone M3 (movement and interaction). Peter walks the room's walk graph,
 * hotspots react to the verbs (LOOK texts, the engine's stock replies), exit zones change
 * rooms. Puzzle logic (TALK, PICK UP, USE, GIVE effects) is milestone M4.
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
	static const int kRetracesPerTick = 5;

	HookEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~HookEngine() override;

	bool hasFeature(EngineFeature f) const override {
		return f == kSupportsReturnToLauncher;
	}

	const Resources &resources() const { return _res; }
	const Room &room() const { return _room; }

	/** Game state: a live copy of the original data segment (offsets as in HOOK.EXE). */
	uint16 dsWord(uint16 off) const;
	void setDsWord(uint16 off, uint16 v);
	const byte *dsPtr(uint16 off) const { return _ds.data() + off; }

	bool enterRoom(uint number, uint entry = 0);
	void setScroll(int x, int y);
	int scrollX() const { return _scrollX; }
	int scrollY() const { return _scrollY; }

	Actor &peter() { return _player.actor; }
	Player &player() { return _player; }
	bool _showHotspots = false;
	bool _showWalkGraph = false;
	void redraw() { _dirty = true; }

protected:
	Common::Error run() override;

private:
	enum PendingType {
		kPendingNone,
		kPendingLook,		///< type 0: hotspot LOOK text (room file)
		kPendingRefusal,	///< type 2: PICK UP refusal via DS:4016
		kPendingMessage		///< type 4: message in the data segment
	};

	bool loadGlobals();
	void applyObjectState();
	void tick();
	void handleEvent(const Common::Event &e);
	void leftClick(int x, int y);
	void rightClick();
	void clickView(int canvasX, int canvasY);
	void clickPanel(int x, int y);
	void walkToHotspot(uint16 hs);
	void setPending(PendingType type, uint16 arg);
	void dispatchPending();
	void say(const Message &m, int facing);
	void speechTick();
	uint16 hotspotRecord(uint k) const;
	uint16 hitTest(int canvasX, int canvasY) const;
	void edgeScroll();
	void applyScroll();
	void checkExits();

	void drawScreen();
	void drawView();
	void drawSpeech(const Common::Rect &view);
	void drawPanel();
	void drawDebug();
	const SpriteBank *bankFor(int id) const;
	void handleKey(const Common::KeyState &key);
	void setCursor();
	Common::Error dumpFrames(const Common::Path &dir);

	const ADGameDescription *_gameDescription;
	Graphics::Screen *_screen = nullptr;
	Resources _res;
	Buffer _ds;
	Room _room;
	AnimVM _anim;
	Player _player;
	SpriteBank _peterBank;
	Language _lang;
	Font _font;

	Common::Array<Actor> _objects;	///< room objects (original DS:147E)

	Buffer _panel;					///< PANEL.BIN: 320x144 sheet, colours 0..31 (+0xE0)
	Buffer _icons;					///< INVENTRY.BIN: 38 icons of 32x24

	uint16 _verb = 0;				///< [0E60]: 0 or 0x8000 | 1..5 (LOOK, TALK, PICK UP, USE, GIVE)
	uint16 _prevVerb = 0;			///< [0E62]
	uint16 _hover = 0;				///< [AC09]: DS address of the hotspot under the mouse
	int _fallbackLook = 0;			///< [AC25]
	PendingType _pending = kPendingNone;
	uint16 _pendingArg = 0;

	Message _speech;				///< refs of the message being said
	uint _speechPart = 0;
	int _speechTicks = 0;			///< [ABD3] ticks left for the current part
	int _speechTotal = 0;			///< [ABEB] ticks left for the whole message
	byte _speechColour = 0x9C;		///< DS:6E5C[speaker]

	int _roomCountdown = 0;			///< [ABBF]: >0 counting, <0 idle
	bool _roomGuard = false;		///< [AFA2]
	int _nextRoom = 0, _nextEntry = 0;
	int _scrollRequest[4] = { 0, 0, 0, 0 };	///< [AB93] right, [AB95] left, [AB97] up, [AB99] down

	Common::Point _mouse;
	bool _clickLeft = false;
	bool _clickRight = false;

	int _scrollX = 0;
	int _scrollY = 0;
	bool _dirty = true;
};

extern HookEngine *g_engine;

} // End of namespace Hook

#endif // HOOK_H
