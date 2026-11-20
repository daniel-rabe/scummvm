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

#include "common/algorithm.h"
#include "common/config-manager.h"
#include "common/debug-channels.h"
#include "common/endian.h"
#include "common/file.h"
#include "common/system.h"
#include "engines/util.h"
#include "graphics/cursorman.h"
#include "graphics/paletteman.h"

#include "hook/console.h"
#include "hook/hook.h"
#include "hook/intro.h"
#include "hook/music.h"

namespace Hook {

HookEngine *g_engine;

// Data-segment tables (offsets in the DS of the supported HOOK.EXE build)
static const uint16 kDsHotspotState = 0x1962;	// per room: far ptr to 24-byte hotspot records
static const uint16 kDsObjectState = 0x4020;	// per room: far ptr to the object-state list
static const uint16 kDsInventory = 0x870B;		// 16 x u16 item ids
static const uint16 kDsInventoryScroll = 0x8705;
static const uint16 kDsVerbButtons = 0x0D40;	// 5 x {x1, y1, x2, y2, src x, src y}, panel coords
static const uint16 kDsRefusals = 0x4016;		// PICK UP refusals: far ptrs, index -pickup - 1
static const uint16 kCsSpeakerColour = 0x6E5C;	// code segment: speech colour (DB, DA, C1, DE)
static const uint16 kDsCursor = 0x0575;			// 12x24 pointer, planar, values 1..3 (+0xDC)
static const uint16 kDsOptions = 0xCE9B;		// load/save panel: 1 = requested, 3 = shown

// Stock replies (data-segment messages)
static const uint16 kMsgUseNothing = 0xAD98;	// USE without hotspot
static const uint16 kMsgTalkThat = 0xAD9E;		// TALK to a hotspot that does not talk
static const uint16 kMsgUseWithItem = 0xADAA;
static const uint16 kMsgLookFallback[3] = { 0xADBC, 0xADC2, 0xADB6 };	// [AC25] = 0, 1, 2
static const uint16 kMsgTalkBusy = 0xADC8;		// talk = 0x8000
static const uint16 kMsgTalkBusy2 = 0xADDA;		// talk = 0x800n
static const uint16 kMsgGiveNotNow = 0xADCE;	// GIVE to something that cannot take it

// Verbs ([0E60] low nibble)
enum { kVerbLook = 1, kVerbTalk = 2, kVerbPickUp = 3, kVerbUse = 4, kVerbGive = 5 };
static const uint16 kVerbLatched = 0x8000;

// Hotspot record fields (docs/formats.md section 5, "Hotspots and verbs")
enum {
	kHsExitRoom = 0x00, kHsFacing = 0x02, kHsLook = 0x04, kHsLookVar = 0x06, kHsPickUp = 0x08,
	kHsUse = 0x0A, kHsGive = 0x0E, kHsLookEvent = 0x10, kHsWalkNode = 0x12, kHsTalk = 0x14,
	kHotspotSize = 24
};

static const int kObjectXOffset = 16;			// 0CFA8: objects are drawn at x - 16
static const int kPeterXOffset = 30;			// 0CF76: actor 0 at x - 16 - 14
static const int kInventoryX = 64, kInventoryY = 44, kInventorySlots = 6;
static const int kIconWidth = 32, kIconHeight = 24;
static const int kTextTicksPerChar = 3;			// DS:3DD4
static const int kScrollRequest = 0x18;
static const int kScrollLeftLimit = 16;

HookEngine::HookEngine(OSystem *syst, const ADGameDescription *gameDesc) : Engine(syst),
	_gameDescription(gameDesc), _player(this) {
	g_engine = this;
}

HookEngine::~HookEngine() {
	delete _music;
	delete _screen;
}

uint16 HookEngine::dsWord(uint16 off) const {
	return off + 2u <= _ds.size() ? READ_LE_UINT16(_ds.data() + off) : 0;
}

void HookEngine::setDsWord(uint16 off, uint16 v) {
	if (off + 2u <= _ds.size())
		WRITE_LE_UINT16(_ds.data() + off, v);
}

Common::Error HookEngine::run() {
	if (!_res.open("HOOK.EXE"))
		return Common::Error(Common::kReadingFailed, "HOOK.EXE");
	if (!loadGlobals())
		return Common::Error(Common::kReadingFailed, "PANSPRIT.BIN / PANEL.BIN / INVENTRY.BIN / ENGLISH.BIN");

	initGraphics(kScreenWidth, kScreenHeight);
	_screen = new Graphics::Screen();
	setDebugger(new Console());

	// Development aid (testmode.cpp): headless dumps selected by config keys
	if (isTestMode())
		return runTestMode();

	initSound();
	setCursor();
	importOriginalSaves();

	// A save chosen in the launcher skips the intro; otherwise the intro and title (0000),
	// then a new game
	bool loaded = false;
	if (ConfMan.hasKey("save_slot")) {
		_inGame = true;
		loaded = loadGameState(ConfMan.getInt("save_slot")).getCode() == Common::kNoError;
	}
	if (!loaded) {
		if (!runIntro())
			return Common::kNoError;
		newGame();
	}

	Common::Event e;
	while (!shouldQuit()) {
		CursorMan.showMouse(true);
		uint32 clock = g_system->getMillis(), ticks = 0;
		while (!shouldQuit() && !_gameEnded) {
			while (g_system->getEventManager()->pollEvent(e))
				handleEvent(e);
			tick();
			drawScreen();
			_screen->update();
			// kRetracesPerTick retraces of 1/70 s a tick (not a whole number of milliseconds)
			uint32 due = clock + ++ticks * kRetracesPerTick * 1000 / kRetraceHz, now = g_system->getMillis();
			if (now + 1000 < due || now > due + 250) {
				clock = now;	// paused (the main menu) or a clock jump: start counting again
				ticks = 0;
			} else if (now < due) {
				g_system->delayMillis(due - now);
			}
		}
		if (!_gameEnded)
			break;
		// 000B2: after the ending, the end credits, then the intro and title again. The
		// original restarts without resetting the game state; here a click starts a new game.
		_gameEnded = false;
		_inGame = false;
		CursorMan.showMouse(false);
		Intro intro(this, _screen);
		Intro::Result r = intro.credits(true);
		if (r == Intro::kDone)
			r = intro.run();
		if (r == Intro::kQuit)
			break;
		newGame();
	}
	return Common::kNoError;
}

bool HookEngine::runIntro() {
	_inGame = false;
	CursorMan.showMouse(false);
	Intro intro(this, _screen);
	return intro.run() != Intro::kQuit;
}

void HookEngine::newGame() {
	// ported: 028C. Room [86F7] = 0, entry [86F9] = 0 (001A9 stopped the music)
	resetGame();
	setTotalPlayTime(0);
	playSong(0xFF);
	enterRoom(0, 0);
	_inGame = true;
}

void HookEngine::resetGame() {
	const byte *ds = _res.ds(0);
	_ds = Buffer(ds, _res.image().size() - Resources::kDataSegment);
	loadPeterBank(0x02B2);	// PANSPRIT.BIN, its name in [86EF] (0C193)
	loadPanel("PANEL.BIN");
	// Actors 1..5 (Tinkerbell's trail) start as in the data segment
	for (int i = 1; i < 6; i++) {
		uint16 slot = 0x13FA + 22 * i;
		_actors[i] = Actor();
		_actors[i].bank = kBankTinker;
		_actors[i].frame = (int16)_res.dsWord(slot + 4);
		_actors[i].x = (int16)_res.dsWord(slot + 6);
		_actors[i].y = (int16)_res.dsWord(slot + 0xA);
	}
	_player.facing = kFacingDown;
	resetTransient();
}

void HookEngine::resetTransient() {
	_verb = _prevVerb = 0;
	_hover = 0;
	_pending = kPendingNone;
	_itemSelected = false;
	_itemX = 0;
	_arrowTimer = 0;
	_pickupHotspot = 0;
	_pickupTimer = 0;
	_lookEvent = 0;
	_seqAbort = false;
	_convActive = false;
	_convReply = false;
	_convPause = 0;
	_giveHotspot = 0;
	_giveTimer = 0;
	_speech.clear();
	_speechTicks = _speechTotal = 0;
	_modal = kModalNone;
	_sceneFade = false;
	_fadeDir = 0;
	_clickLeft = _clickRight = false;
	for (uint16 v : { 0xABC9, 0xAC13, 0xAC15, 0xCE9B })
		setDsWord(v, 0);
}

bool HookEngine::loadPanel(const char *name) {
	if (!_res.load(name, _panel) || _panel.size() < (uint)kScreenWidth * kPanelHeight) {
		warning("Hook: cannot load %s", name);
		return false;
	}
	_panelName = name;
	_dirty = true;
	return true;
}

bool HookEngine::loadGlobals() {

	if (!_res.load("INVENTRY.BIN", _icons))
		return false;
	// 0001D: the language file (the original picks it by the DOS country code)
	static const struct {
		Common::Language lang;
		const char *file;
	} languages[] = {
		{ Common::FR_FRA, "FRENCH.BIN" }, { Common::DE_DEU, "GERMAN.BIN" },
		{ Common::ES_ESP, "SPANISH.BIN" }, { Common::IT_ITA, "ITALIAN.BIN" }
	};
	const char *langFile = "ENGLISH.BIN";
	for (const auto &l : languages)
		if (_gameDescription->language == l.lang)
			langFile = l.file;
	if (!_lang.load(_res, langFile))
		return false;
	Buffer arrows;
	if (!_res.load("ARROWS.BIN", arrows) || !_arrowsBank.load(arrows.data(), arrows.size(), _res.ds(kXlatArrows)))
		return false;
	Buffer tinker;
	if (!_res.load("TINKER.BIN", tinker) || !_tinkerBank.load(tinker.data(), tinker.size(), _res.ds(kXlatTinker)))
		return false;
	_font.load(_res);
	resetGame();
	return !_panel.empty() && _peterBank.size();
}

bool HookEngine::enterRoom(uint number, uint entry) {
	if (!_room.load(_res, number))
		return false;
	// 0CB3E: no sequences on either channel, no edge-scroll lock (flag 34)
	for (uint16 v : { 0xAC30, 0xAC48, 0xAC34, 0xAC4C, 0x86FB })
		setDsWord(v, 0);
	_room.getPalette(_res, _basePalette);
	for (int i = 0; i < 256; i++)
		_palLevel[i] = 32;
	_fadeDir = 0;
	applyPalette();
	_modal = kModalNone;

	_objects.clear();
	for (const RoomObject &o : _room.objects()) {
		Actor a;
		a.bank = _room.bankForPara(o.bankPara);
		a.frame = o.frame;
		a.x = o.x;
		a.y = o.y;
		_objects.push_back(a);
	}
	applyObjectState();
	_anim.start(_room);
	updateDepths();

	_pending = kPendingNone;
	_speech.clear();
	_speechTicks = _speechTotal = 0;
	for (int i = 0; i < 4; i++)
		_scrollRequest[i] = 0;
	_roomCountdown = 10;	// 0CABF: exits stay disabled for the first 10 ticks
	_roomGuard = true;

	setDsWord(0x86F7, number);
	setDsWord(0x86F9, entry);
	setDsWord(0xABF7, 0);
	setDsWord(0xAC44, 0);
	_convActive = false;
	_player.enterRoom(_room, entry);
	// 0CA5A: scroll x = Peter - 176, clamped to 16 .. header +1C - 320 (then floored at 15)
	int sx = _player.actor.x - 0xB0;
	if (sx <= kScrollLeftLimit)
		sx = kScrollLeftLimit;
	else if (sx + kScreenWidth >= _room.sword(0x1C))
		sx = _room.sword(0x1C) - kScreenWidth;
	if (sx < kScrollLeftLimit)
		sx = kScrollLeftLimit - 1;
	_scrollX = sx;
	_scrollY = _room.sword(0x16);
	_dirty = true;
	debugC(1, kDebugScript, "Room %u entry %u: Peter %d,%d, scroll %d,%d", number, entry,
		   _player.actor.x, _player.actor.y, _scrollX, _scrollY);
	updateDepths();
	roomEntryHook();	// 0CB3B -> 0ACBB
	return true;
}

void HookEngine::applyObjectState() {
	// 0844B: DS:[4020 + room*4] -> u16 n, n x 16-byte records
	// {object, hotspot, visible, frame, look, ?, pickup, ?}
	uint16 list = dsWord(kDsObjectState + 4 * _room.number());
	uint16 n = dsWord(list);
	uint16 hsBase = dsWord(kDsHotspotState + 4 * _room.number());
	for (uint i = 0; i < n; i++) {
		uint16 rec = list + 2 + 16 * i;
		uint16 obj = dsWord(rec), hs = hsBase + kHotspotSize * dsWord(rec + 2);
		bool visible = dsWord(rec + 4) != 0;
		if (obj < _objects.size())
			_objects[obj].frame = visible ? (int16)dsWord(rec + 6) : -1;
		setDsWord(hs + kHsLook, visible ? dsWord(rec + 8) : 0);
		setDsWord(hs + kHsPickUp, visible ? (dsWord(rec + 0xC) ? dsWord(rec + 0xC) : i + 1) : 0);
	}
}

void HookEngine::setScroll(int x, int y) {
	_scrollX = CLIP<int>(x, kScrollLeftLimit, MAX<int>(kScrollLeftLimit, _room.sword(0x1C) - kScreenWidth));
	_scrollY = CLIP<int>(y, 0, MAX<int>(0, (int)_room.sceneHeight() - kViewHeight));
	_dirty = true;
}

uint16 HookEngine::hotspotRecord(uint k) const {
	return dsWord(kDsHotspotState + 4 * _room.number()) + kHotspotSize * k;
}

uint16 HookEngine::hitTest(int cx, int cy) const {
	// 06E6B: first rectangle that contains the point wins
	uint16 rects = _room.word(0x7C);
	for (uint k = 0; k < _room.word(0x7A); k++) {
		int x1 = _room.sword(rects + 8 * k), y1 = _room.sword(rects + 8 * k + 2);
		int x2 = _room.sword(rects + 8 * k + 4), y2 = _room.sword(rects + 8 * k + 6);
		if (x1 <= cx && y1 <= cy && x2 >= cx && y2 >= cy)
			return hotspotRecord(k);
	}
	return 0;
}

void HookEngine::handleEvent(const Common::Event &e) {
	switch (e.type) {
	case Common::EVENT_KEYDOWN:
		handleKey(e.kbd);
		break;
	case Common::EVENT_MOUSEMOVE:
		_mouse = e.mouse;
		break;
	case Common::EVENT_LBUTTONDOWN:
		_mouse = e.mouse;
		_clickLeft = true;
		break;
	case Common::EVENT_RBUTTONDOWN:
		_mouse = e.mouse;
		_clickRight = true;
		break;
	default:
		break;
	}
}

void HookEngine::tick() {
	updateDepths();	// 06A78 ran after the previous frame was drawn
	if (_modal != kModalNone) {
		// a modal loop inside a sequence callback: no input, no animation, no sequences
		modalTick();
		_clickLeft = _clickRight = false;
		_dirty = true;
		return;
	}
	// 32-bit play time [86FD] (saved; it also picks Tinkerbell's hint)
	uint16 t = dsWord(0x86FD) + 1;
	setDsWord(0x86FD, t);
	if (!t)
		setDsWord(0x86FF, dsWord(0x86FF) + 1);
	_anim.tick(_room, _objects);

	// 053EA: depth factor, hotspot under the mouse, edge scrolling, exit zones
	_player.updateDepth(_room);
	int canvasX = _mouse.x + _scrollX + 1, canvasY = _mouse.y + _scrollY;
	_hover = (_mouse.y < kViewHeight) ? hitTest(canvasX, canvasY) : 0;
	// 05455: no edge scrolling (nor while flag 34 is set) and no exits while channel A runs
	if (!sequenceRunning()) {
		if (!dsWord(0x86FB))
			edgeScroll();
		checkExits();
	}

	// 0C873: room change countdown
	if (_roomCountdown > 0 && --_roomCountdown == 0) {
		if (_roomGuard) {
			_roomGuard = false;
			_roomCountdown = -1;
		} else {
			enterRoom(dsWord(0x86F7), dsWord(0x86F9));
			_clickLeft = _clickRight = false;
			return;
		}
	}

	arrowTick();
	_selectAnim = (_selectAnim + 1) & 3;
	optionsTick();	// 0C398
	bool talking = _speechTotal > 0;
	if (talking) {
		speechTick();
		_clickLeft = _clickRight = false;
	} else if (_pickupTimer > 0 && --_pickupTimer > 0) {
		// 055AF: holding the pick-up pose
		pickupTick();
		_clickLeft = _clickRight = false;
	} else if (_convActive) {
		// 05469: a running conversation takes the input
		conversationTick();
		_clickLeft = _clickRight = false;
	} else if (sequenceRunning() && !dsWord(0xABC9)) {
		// 055A0: channel A drives the scene; no input, Peter is moved by the sequence
		// (unless [ABC9]: scripted walking, see below)
		_clickLeft = _clickRight = false;
	} else if (_giveTimer > 0) {
		// 055C1: holding the item out, then the GIVE logic
		if (--_giveTimer == 0)
			giveTick();
		_clickLeft = _clickRight = false;
	} else {
		if (_lookEvent)
			lookEventTick();
		if (dsWord(0xABC9)) {
			// 0547F: scripted walking - no mouse input; [ABCB] = n sends Peter to node n of
			// the room's first walk graph (05D32)
			if (uint16 n = dsWord(0xABCB)) {
				setDsWord(0xABCB, 0);
				const byte *d = _room.data().data();
				uint32 nodes = READ_LE_UINT32(d + READ_LE_UINT32(d + 0x0C));
				_player.walkTo(_room.sword(nodes + 4 * (n - 1)), _room.sword(nodes + 4 * (n - 1) + 2));
			}
			_clickLeft = _clickRight = false;
		}
		if (_room.number() >= 0x27) {
			// 0C398: from room 39 on there are no verbs, no inventory and no walking
			_verb = _prevVerb = 0;
			_itemSelected = false;
			_itemX = 0;
			_clickLeft = _clickRight = false;
		}
		// 054DD: no clicks while the load/save panel is up
		if (_speechTotal == 0 && _roomCountdown <= 0 && !dsWord(kDsOptions)) {
			if (_clickRight)
				rightClick();
			if (_clickLeft)
				leftClick(_mouse.x, _mouse.y);
		}
		_clickLeft = _clickRight = false;
		if (_player.update(_speechTotal > 0))
			dispatchPending();
	}

	sequencesTick();
	soundTick();	// A813
	roomHookTick();
	tinkerTick();

	applyScroll();
	for (int r = 0; r < kRetracesPerTick; r++)
		fadeStep();
	_dirty = true;
}

void HookEngine::edgeScroll() {
	// 066B0: request 24 retraces of scrolling when Peter is near a view edge
	if (_room.number() == 0x29)
		return;
	int sy = _player.feetY() - _scrollY;
	if (sy >= _room.sword(0x2C))
		_scrollRequest[3] = kScrollRequest;
	else if (sy <= _room.sword(0x2A))
		_scrollRequest[2] = kScrollRequest;
	int sx = MAX(0, _player.actor.x - _scrollX);
	if (sx >= _room.sword(0x26))
		_scrollRequest[0] = kScrollRequest;
	else if (sx <= _room.sword(0x28))
		_scrollRequest[1] = kScrollRequest;
}

void HookEngine::applyScroll() {
	// 03525 runs from the retrace interrupt: one pixel per retrace while a request lasts
	// (not in room 41, nor while [AC2B] is set: Peter flying up drives the scroll himself)
	if (_room.number() == 0x29 || (dsWord(0xAC2B) & 0xFF))
		return;
	int maxX = _room.sword(0x1C) - kScreenWidth, maxY = (int)_room.sceneHeight() - kViewHeight;
	for (int r = 0; r < kRetracesPerTick; r++) {
		if (_scrollRequest[0] > 0) {
			_scrollRequest[0]--;
			if (_scrollX != maxX)
				_scrollX++;
		}
		if (_scrollRequest[1] > 0) {
			_scrollRequest[1]--;
			if (_scrollX != kScrollLeftLimit)
				_scrollX--;
		}
		if (_scrollRequest[2] > 0) {
			_scrollRequest[2]--;
			if (_scrollY != 0)
				_scrollY--;
		}
		if (_scrollRequest[3] > 0) {
			_scrollRequest[3]--;
			if (_scrollY != maxY)
				_scrollY++;
		}
	}
}

void HookEngine::checkExits() {
	// 06AE0: exit zones (header +54: u16 n, n x {x, y, room, entry}), +-4 / +-3 around the feet
	if (_roomCountdown >= 0)
		return;
	int fx = _player.feetX(), fy = _player.feetY();
	uint16 list = _room.word(0x54);
	for (uint i = 0; i < _room.word(list); i++) {
		uint16 e = list + 2 + 8 * i;
		int x = _room.sword(e), y = _room.sword(e + 2);
		if (x - 4 <= fx && y - 3 <= fy && x + 4 >= fx && y + 3 >= fy) {
			_roomCountdown = 1;
			setDsWord(0x86F7, _room.word(e + 4));
			setDsWord(0x86F9, _room.word(e + 6));
			return;
		}
	}
}

void HookEngine::leftClick(int x, int y) {
	if (y >= kViewHeight)
		clickPanel(x, y - kViewHeight);
	else
		clickView(x + _scrollX + 1, y + _scrollY);
}

void HookEngine::rightClick() {
	// 054F2: drop pending actions and swap the verb with the previous one
	_pending = kPendingNone;
	playEffect(0);	// 05507
	SWAP(_verb, _prevVerb);
	if (_prevVerb && _verb)
		_verb = 0;
}

void HookEngine::clickPanel(int x, int y) {
	// 073DE: verb buttons; clicking the latched verb releases it
	for (uint i = 0; i < 5; i++) {
		uint16 b = kDsVerbButtons + 12 * i;
		int x1 = (int16)dsWord(b), y1 = (int16)dsWord(b + 2), x2 = (int16)dsWord(b + 4), y2 = (int16)dsWord(b + 6);
		if (x >= x1 && y >= y1 && x < x2 && y < y2) {
			uint16 v = kVerbLatched | (i + 1);
			playEffect(0);	// 07454
			_verb = (_verb == v) ? 0 : v;
			_pending = kPendingNone;
			return;
		}
	}
	clickInventory(x, y);
}

void HookEngine::walkToHotspot(uint16 hs) {
	int x, y;
	_player.hotspotTarget(dsWord(hs + kHsWalkNode), x, y);
	_player.walkTo(x, y);
}

void HookEngine::setPending(PendingType type, uint16 arg) {
	_pending = type;
	_pendingArg = arg;
}

void HookEngine::clickView(int cx, int cy) {
	uint16 verb = _verb & 0xF;
	uint16 hs = _hover;
	if (verb) {
		// 08800: a verb is used once; right-click brings it back
		_prevVerb = _verb;
		_verb = 0;
	}

	switch (verb) {
	case kVerbLook: {
		// 05A4C: Tinkerbell first (05AFA: her chime and Peter's line)
		if (tinkerHit(cx, cy)) {
			playEffect(1);
			setPending(kPendingMessage, 0xADEC);
			_player.walkTo(_player.feetX(), _player.feetY());
			return;
		}
		if (hs && dsWord(hs + kHsLook)) {
			setPending(kPendingLook, hs);
			uint16 v = dsWord(hs + kHsLookVar) + 1;
			setDsWord(hs + kHsLookVar, v > 3 ? 1 : v);
			int x, y;
			if (_player.hotspotTarget(dsWord(hs + kHsWalkNode), x, y) != 0) {
				_player.walkTo(x, y);
				return;
			}
		} else if (hs && dsWord(hs + kHsExitRoom)) {
			walkToHotspot(hs);
			return;
		}
		_fallbackLook = (_fallbackLook + 1) % 3;
		setPending(kPendingMessage, kMsgLookFallback[_fallbackLook]);
		_player.walkTo(_player.feetX(), _player.feetY());
		return;
	}
	case kVerbTalk: {
		// 05B1C: Tinkerbell first (not in room 37), then the hotspot's conversation
		if (_room.number() != 0x25 && tinkerHit(cx, cy)) {
			tinkerTalk(cx, cy);
			return;
		}
		if (!hs)
			return;
		uint16 talk = dsWord(hs + kHsTalk);
		if (talk == 0) {
			if (!dsWord(hs + kHsLook))
				return;
			setPending(kPendingMessage, kMsgTalkThat);
		} else if ((talk & 0xFFF0) == 0x8000) {
			setPending(kPendingMessage, (talk & 0xF) ? kMsgTalkBusy2 : kMsgTalkBusy);
		} else {
			// 05BB7: a conversation needs a walk node inside the active graph
			int x, y;
			if (_player.hotspotTarget(dsWord(hs + kHsWalkNode), x, y) == 1)
				setPending(kPendingTalk, hs);
			_player.walkTo(x, y);
			return;
		}
		walkToHotspot(hs);
		return;
	}
	case kVerbPickUp: {
		// 05BCB (taking an object: M4)
		if (!hs)
			return;
		int16 pick = (int16)dsWord(hs + kHsPickUp);
		if (pick == 0)
			return;
		if (pick < 0) {
			if (!dsWord(hs + kHsLook))
				return;
			uint16 v = dsWord(hs + kHsLookVar) + 1;
			setDsWord(hs + kHsLookVar, v > 3 ? 3 : v);
			setPending(kPendingRefusal, (uint16)(-pick));
		} else {
			setPending(kPendingPickup, hs);
		}
		walkToHotspot(hs);
		return;
	}
	case kVerbUse: {
		// 05C2F
		int16 use = hs ? (int16)dsWord(hs + kHsUse) : 0;
		if (use == 0) {
			setPending(kPendingMessage, _itemSelected ? kMsgUseWithItem : kMsgUseNothing);
			_player.walkTo(_player.feetX(), _player.feetY());
			return;
		}
		if (use < -1 || (use == -1 && (!dsWord(hs + kHsLook) || !dsWord(hs + kHsPickUp))))
			return;
		setPending(kPendingUse, hs);
		walkToHotspot(hs);
		return;
	}
	case kVerbGive: {
		// 05CA4
		if (!_itemSelected)
			return;
		uint16 talk = hs ? dsWord(hs + kHsTalk) : 0;
		if (hs && (talk >> 8) == 0x80) {
			setPending(kPendingMessage, talk == 0x8000 ? kMsgTalkBusy : kMsgGiveNotNow);
			walkToHotspot(hs);
		} else if (talk == 0) {
			setPending(kPendingMessage, kMsgGiveNotNow);
			_player.walkTo(_player.feetX(), _player.feetY());
		} else {
			setPending(kPendingGive, hs);
			walkToHotspot(hs);
		}
		return;
	}
	default:
		// 05D1D: no verb - walk to the hotspot's node or to the clicked point
		_pending = kPendingNone;
		if (hs)
			walkToHotspot(hs);
		else
			_player.walkTo(cx, cy);
		return;
	}
}

void HookEngine::dispatchPending() {
	// 07A77: message types 0 (hotspot LOOK), 2 (refusal), 4 (data-segment message)
	PendingType type = _pending;
	uint16 arg = _pendingArg;
	_pending = kPendingNone;
	switch (type) {
	case kPendingLook: {
		uint16 look = dsWord(arg + kHsLook), var = dsWord(arg + kHsLookVar);
		if (!look || !var)
			return;
		uint16 variants = _room.word(0x80);
		uint16 table = _room.word(variants + 4 * (var - 1));
		uint16 msg = _room.word(table + 4 * (look - 1));
		if (msg < _room.data().size())
			say(readMessage(_room.data().data() + msg, _room.data().size() - msg), dsWord(arg + kHsFacing));
		// 07CF2: a LOOK event fires after the text; one-shot unless its high byte is 'L'
		uint16 ev = dsWord(arg + kHsLookEvent);
		if (ev) {
			_lookEvent = ev;
			if ((ev >> 8) != 0x4C)
				setDsWord(arg + kHsLookEvent, 0);
		}
		break;
	}
	case kPendingItemLook:
		itemLook(arg);
		break;
	case kPendingPickup:
		startPickup(arg);
		break;
	case kPendingUse:
		useOnHotspot(arg);
		break;
	case kPendingGive:
		startGive(arg);
		break;
	case kPendingTalk:
		talkToHotspot(arg);
		break;
	case kPendingRefusal: {
		uint16 msg = dsWord(kDsRefusals + 4 * (arg - 1));
		say(readMessage(dsPtr(msg), _ds.size() - msg), _player.facing);
		break;
	}
	case kPendingMessage:
		say(readMessage(dsPtr(arg), _ds.size() - arg), kFacingDown);
		break;
	default:
		break;
	}
}

static int messageChars(const Common::Array<Common::String> &lines) {
	int n = 0;
	for (const Common::String &l : lines)
		n += l.size();
	return n;
}

void HookEngine::say(const Message &m, int facing) {
	// 07B73: each part stays for (characters x 3) ticks; Peter animates his mouth meanwhile
	if (m.empty())
		return;
	_speech = m;
	_speechPart = 0;
	_speechTotal = 0;
	if (!_lang.lines(m[0]).empty())
		debugC(1, kDebugScript, "say \"%s\"", _lang.lines(m[0])[0].c_str());
	for (uint16 ref : m)
		_speechTotal += messageChars(_lang.lines(ref)) * kTextTicksPerChar;
	_speechTicks = messageChars(_lang.lines(m[0])) * kTextTicksPerChar;
	_speechColour = _res.image()[kCsSpeakerColour + 0];	// speaker 0 = Peter: yellow
	_player.facing = facing & 3;
	_player.startTalking();
	if (_speechTotal <= 0)
		_speech.clear();
}

void HookEngine::speechTick() {
	_speechTotal--;
	_player.talkFrame(_speechTotal == 0);
	if (--_speechTicks <= 0 && ++_speechPart < _speech.size())
		_speechTicks = messageChars(_lang.lines(_speech[_speechPart])) * kTextTicksPerChar;
	if (_speechTotal <= 0) {
		_speechTotal = 0;
		_speech.clear();
	}
}

const SpriteBank *HookEngine::bankFor(int id) const {
	if (id >= kBankRoom0 && id < kBankRoom0 + (int)Room::kNumBanks)
		return &_room.bank(id - kBankRoom0);
	if (id == kBankPeter)
		return &_peterBank;
	if (id == kBankTinker)
		return &_tinkerBank;
	return nullptr;
}

void HookEngine::drawScreen() {
	if (!_dirty)
		return;
	drawView();
	drawPanel();
	drawDebug();
	_screen->markAllDirty();
	_dirty = false;
}

void HookEngine::updateDepths() {
	// 06A78: depth = y + frame height + dy for every visible slot
	auto upd = [&](Actor &a) {
		const SpriteBank *b = bankFor(a.bank);
		const Frame *f = (b && a.frame >= 0) ? b->frame(a.frame) : nullptr;
		if (f)
			a.depth = a.y + f->height + f->dy;
	};
	upd(_player.actor);
	for (int i = 1; i < 6; i++)
		upd(_actors[i]);
	for (Actor &a : _objects)
		upd(a);
}

void HookEngine::drawView() {
	Common::Rect view(0, 0, kScreenWidth, kViewHeight);
	_screen->fillRect(view, 0);
	int w = MIN<int>(kScreenWidth, _room.width() - _scrollX);
	int h = MIN<int>(kViewHeight, (int)_room.sceneHeight() - _scrollY);
	for (int y = 0; y < h; y++)
		memcpy(_screen->getBasePtr(0, y), _room.canvas() + (_scrollY + y) * _room.width() + _scrollX, w);

	// Sprites in depth order (06AAC): by the slot's stored depth (+0E), ties keep slot order,
	// actor 0 first. The depth is recomputed after drawing (06A78, see updateDepths), so a
	// room hook or a track can override it for the next frame.
	struct Item {
		const Actor *a;
		const Frame *f;
		int order;
		int xOffset;
	};
	Common::Array<Item> items;
	auto add = [&](const Actor &a, int order, int xOffset) {
		const SpriteBank *b = bankFor(a.bank);
		const Frame *f = b ? b->frame(a.frame) : nullptr;
		if (f)
			items.push_back(Item{ &a, f, order, xOffset });
	};
	add(_player.actor, 0, kPeterXOffset);
	for (int i = 1; i < 6; i++)
		add(_actors[i], i, kObjectXOffset);
	for (uint i = 0; i < _objects.size(); i++)
		add(_objects[i], 6 + i, kObjectXOffset);
	Common::sort(items.begin(), items.end(), [](const Item &p, const Item &q) {
		return p.a->depth != q.a->depth ? p.a->depth < q.a->depth : p.order < q.order;
	});
	Graphics::Surface &s = *_screen->surfacePtr();
	for (const Item &it : items)
		SpriteBank::draw(s, *it.f, it.a->x - it.xOffset - _scrollX, it.a->y - _scrollY, view);
	drawSpeech(view);
}

void HookEngine::drawSpeech(const Common::Rect &view) {
	// 06BB1: Peter's line - from say() or from a sequence writing its text ref to [ABF7]
	uint16 ref = dsWord(0xABF7);
	if (!ref && !_speech.empty() && _speechPart < _speech.size())
		ref = _speech[_speechPart];
	if (ref)
		drawSpeechLines(view, _lang.lines(ref), _player.actor.x, _player.actor.y, _speechColour, true);
	// 06B7B: another character's line, [AC44] at [ABB1]/[ABB3], speaker colour 2 (3 in the
	// flashback, cs:9950)
	if (dsWord(0xAC44))
		drawSpeechLines(view, _lang.lines(dsWord(0xAC44)), (int16)dsWord(0xABB1), (int16)dsWord(0xABB3),
						_res.image()[0x6E5C + (_sceneFade ? 3 : 2)], false);
}

void HookEngine::drawSpeechLines(const Common::Rect &view, const Common::Array<Common::String> &lines, int ax, int bx,
								 byte colour, bool peter) {
	// 06C0B: lines above the anchor, centred on it, kept inside the view
	if (lines.empty())
		return;
	if (peter && _player.depth()) {
		// 06BE8: in Peter's depth region the text sits on the sprite's top edge, 4-aligned
		const Frame *f = _peterBank.frame(_player.actor.frame);
		bx += 0x30 - (f ? f->height : 0x30);
		if ((bx & 3) == 3)
			bx += 4;
		bx &= ~3;
	}
	int maxW = 0;
	for (const Common::String &l : lines)
		maxW = MAX<int>(maxW, l.size() * Font::kWidth);
	bx -= (int)lines.size() * Font::kHeight + 4;
	if (bx < 0)
		bx = 0;
	int left = _scrollX + 0x11, right = _scrollX + 0x13E;
	int x;
	if (ax - maxW / 2 <= left)
		x = left;
	else if (ax + maxW / 2 >= right)
		x = right - maxW;
	else
		x = ax - maxW / 2;
	// Some translated lines (Italian: 40 characters) are wider than the text area; the original
	// draws them past the screen's right edge. Keep them on screen.
	if (x + maxW > _scrollX + kScreenWidth)
		x = MAX(_scrollX, _scrollX + kScreenWidth - maxW);
	Graphics::Surface &s = *_screen->surfacePtr();
	for (uint i = 0; i < lines.size(); i++) {
		int lw = lines[i].size() * Font::kWidth;
		_font.drawString(s, x + (maxW - lw) / 2 - _scrollX, bx + Font::kHeight * i - _scrollY, lines[i], colour, view);
	}
}

void HookEngine::drawPanel() {
	// Split-screen panel: the top 72 rows of the panel sheet, colours + 0xE0 (0F0B, 0F1C).
	for (int y = 0; y < kPanelHeight; y++) {
		byte *dst = (byte *)_screen->getBasePtr(0, kViewHeight + y);
		const byte *src = _panel.data() + y * kScreenWidth;
		for (int x = 0; x < kScreenWidth; x++)
			dst[x] = src[x] + 0xE0;
	}
	// The latched verb shows its pressed image from the lower half of the sheet.
	if (_verb & kVerbLatched) {
		uint16 b = kDsVerbButtons + 12 * ((_verb & 0xF) - 1);
		int x1 = (int16)dsWord(b), y1 = (int16)dsWord(b + 2), x2 = (int16)dsWord(b + 4), y2 = (int16)dsWord(b + 6);
		int sx = (int16)dsWord(b + 8), sy = (int16)dsWord(b + 10);
		for (int y = 0; y < y2 - y1 && sy + y < 144; y++) {
			byte *dst = (byte *)_screen->getBasePtr(x1, kViewHeight + y1 + y);
			const byte *src = _panel.data() + (sy + y) * kScreenWidth + sx;
			for (int x = 0; x < x2 - x1; x++)
				dst[x] = src[x] + 0xE0;
		}
	}
	// 06EEE: the load/save panel has no inventory, arrows or selection box
	if (dsWord(kDsOptions))
		return;
	// Inventory icons (722C): 6 slots from DS:8709 + scroll (the outer two sit under the
	// arrows), icon k = item k, opaque.
	for (int k = 0; k < kInventorySlots; k++) {
		uint16 item = barItem(k);
		uint32 off = item * kIconWidth * kIconHeight;
		if (!item || off + kIconWidth * kIconHeight > _icons.size())
			continue;
		for (int y = 0; y < kIconHeight; y++) {
			byte *dst = (byte *)_screen->getBasePtr(kInventoryX + kIconWidth * k, kViewHeight + kInventoryY + y);
			for (int x = 0; x < kIconWidth; x++)
				dst[x] = _icons[off + y * kIconWidth + x] + 0xE0;
		}
	}
	// Arrows (071E5): ARROWS frames at the records DS:4164 / DS:4182 {frame, x + 32, ?, y}
	Graphics::Surface &s = *_screen->surfacePtr();
	Common::Rect panel(0, kViewHeight, kScreenWidth, kScreenHeight);
	for (uint16 rec : { (uint16)0x4164, (uint16)0x4182 }) {
		const Frame *f = _arrowsBank.frame((int16)dsWord(rec));
		if (f)
			SpriteBank::draw(s, *f, (int16)dsWord(rec + 2) - 32, kViewHeight + (int16)dsWord(rec + 6), panel);
	}
	// Selection box (06F17 / 06FE2): ARROWS frames 4,5,6,5 cycling on the selected item, or on
	// the item under the mouse while LOOK / USE / GIVE is chosen
	int boxX = -1;
	if (_itemSelected)
		boxX = _itemX;
	else if (((_verb & 0xF) == 1 || (_verb & 0xF) == 4 || (_verb & 0xF) == 5) && _mouse.y >= kViewHeight + 44 &&
			 _mouse.y < kViewHeight + 68 && _mouse.x >= 96 && _mouse.x < 224 && barItem((_mouse.x - 96) / 32 + 1))
		boxX = _mouse.x + 32;
	if (boxX >= 0x60 && boxX <= 0x120) {
		const Frame *f = _arrowsBank.frame((int16)dsWord(0x0E64 + 2 * _selectAnim));
		if (f)
			SpriteBank::draw(s, *f, (boxX & ~31) + 2 - 0x22, kViewHeight + 44, panel);
	}
}

void HookEngine::drawDebug() {
	Graphics::Surface view = _screen->getSubArea(Common::Rect(0, 0, kScreenWidth, kViewHeight));
	if (_showHotspots) {
		uint16 rects = _room.word(0x7C);
		for (uint k = 0; k < _room.word(0x7A); k++) {
			Common::Rect r(_room.sword(rects + 8 * k) - _scrollX, _room.sword(rects + 8 * k + 2) - _scrollY,
						   _room.sword(rects + 8 * k + 4) - _scrollX + 1, _room.sword(rects + 8 * k + 6) - _scrollY + 1);
			r.clip(Common::Rect(0, 0, kScreenWidth, kViewHeight));
			if (r.isValidRect() && !r.isEmpty())
				view.frameRect(r, hotspotRecord(k) == _hover ? 0xDE : 0xDF);
		}
	}
	if (_showWalkGraph) {
		const WalkGraph &g = _player.graph();
		for (uint i = 1; i <= g.count(); i++) {
			Common::Point a = g.node(i);
			for (uint j = 0; j < 8; j++) {
				uint16 to = g.link(i, j);
				if (!to || to > g.count())
					continue;
				Common::Point b = g.node(to);
				view.drawLine(CLIP<int>(a.x - _scrollX, 0, kScreenWidth - 1), CLIP<int>(a.y - _scrollY, 0, kViewHeight - 1),
							  CLIP<int>(b.x - _scrollX, 0, kScreenWidth - 1), CLIP<int>(b.y - _scrollY, 0, kViewHeight - 1), 0xDD);
			}
		}
	}
}

void HookEngine::setCursor() {
	// The pointer (297B): 12x24, stored plane by plane (3 bytes per plane per row);
	// non-zero pixels get 0xDC added at start-up (0C91D) -> colours DD..DF.
	const byte *src = _res.ds(kDsCursor);
	byte img[12 * 24];
	for (int y = 0; y < 24; y++)
		for (int p = 0; p < 4; p++)
			for (int i = 0; i < 3; i++) {
				byte v = src[y * 12 + p * 3 + i];
				img[y * 12 + 4 * i + p] = v ? v + 0xDC : 0;
			}
	CursorMan.replaceCursor(img, 12, 24, 0, 0, 0);
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
