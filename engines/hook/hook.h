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

class MusicDriver;

/**
 * Engine for Hook (Ocean / Shadow Software, 1992, DOS).
 *
 * Status: milestone M6. Verbs, inventory, conversations, GIVE, action sequences and the
 * hand-ported routines of all rooms (handlers.cpp, docs/logic-ledger.md), Tinkerbell's hints,
 * the flashback and the finale (M4); AdLib and MT-32/GM music and effects (M5, music.cpp /
 * sound.cpp); the intro, the title and end credits (intro.cpp), the in-game load/save panel
 * and ScummVM saves (saveload.cpp), all five languages (M6).
 * Keys: arrows scroll, PgUp/PgDn change room. Console: room, scroll, peter, hotspots,
 * walk, res.
 */
class HookEngine : public Engine {
public:
	static const int kScreenWidth = 320;
	static const int kScreenHeight = 200;
	static const int kViewHeight = 128;		///< split screen: view rows 0..127
	static const int kPanelHeight = 72;		///< panel rows 128..199
	static const int kRetraceHz = 70;
	/**
	 * A game tick: frame delay 5 through 0B61, which waits until the retrace counter reaches the
	 * delay, flips on the next retrace and only then clears the counter, so delay + 1 retraces
	 * (11.7 ticks a second; measured in DOSBox too).
	 */
	static const int kRetracesPerTick = 6;

	HookEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~HookEngine() override;

	bool hasFeature(EngineFeature f) const override {
		return f == kSupportsReturnToLauncher || f == kSupportsLoadingDuringRuntime ||
			   f == kSupportsSavingDuringRuntime;
	}

	// saveload.cpp
	bool canLoadGameStateCurrently(Common::U32String *msg = nullptr) override;
	bool canSaveGameStateCurrently(Common::U32String *msg = nullptr) override;
	Common::Error loadGameStream(Common::SeekableReadStream *stream) override;
	Common::Error saveGameStream(Common::WriteStream *stream, bool isAutosave = false) override;
	/** A HOOK.SAV slot (DS:86A7..A91E): the bytes the original's save writes (C678). */
	const byte *saveSlotData();
	/** Load a HOOK.SAV slot as the original's load does (C524), then enter its room. */
	Common::Error loadSlotData(const byte *data);
	/** 09377: the end of the game; the main loop runs the end credits and the intro. */
	void endGame() { _gameEnded = true; }

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

	// sound.cpp
	void playSong(uint8 n);
	void playEffect(uint8 n);
	void playRoomSound(uint16 offset, int volume);
	void syncSoundSettings() override;

protected:
	Common::Error run() override;

private:
	enum PendingType {
		kPendingNone,
		kPendingLook,		///< type 0: hotspot LOOK text (room file)
		kPendingItemLook,	///< type 1: item description (DS:40C8)
		kPendingRefusal,	///< type 2: PICK UP refusal via DS:4016
		kPendingMessage,	///< type 4: message in the data segment
		kPendingPickup,		///< [ABDD]: take the object of a hotspot
		kPendingTalk,		///< [ABF5]: start the hotspot's conversation
		kPendingUse,		///< [ABC1]: USE (with or without the selected item)
		kPendingGive		///< [ABE1]: GIVE the selected item
	};

	// logic.cpp: USE / GIVE on hotspots
	void useOnHotspot(uint16 hs);
	void useWithItem(uint16 hs);
	void startGive(uint16 hs);
	void giveTick();
	void giveReply(uint16 menu);

	// conversation.cpp
	uint16 convWord(uint16 off) const;
	void setConvWord(uint16 off, uint16 v);
	int refTicks(uint16 ref) const;
	void startConversation(uint16 structOff, bool inDs);
	void setPeterLine(uint16 ref, int speaker);
	void conversationTick();
	uint16 optionText(uint16 option, uint16 &after, bool &visible) const;
	void conversationMenu();
	void conversationReply();
	void npcSay(uint16 ref);
	void npcTalkTick();

	// logic.cpp: table-driven game logic
	uint16 csWord(uint16 off) const;
	uint16 barItem(int slot) const;
	bool hasItem(uint16 item) const;
	void addItem(uint16 item);
	void removeItem(uint16 item);
	void compactInventory();
	uint16 selectedItem() const;
	Message dsMessage(uint16 off) const;
	void clickInventory(int x, int y);
	void useAlone(uint16 item);
	void combine(uint16 a, uint16 b);
	void startPickup(uint16 hs);
	void pickupTick();
	void lookEventTick();
	void itemLook(uint16 item);
	void arrowTick();
	// handlers.cpp: hand-ported x86 routines (docs/logic-ledger.md)
	void runHandler(uint16 address);
	void runCue(uint16 table, uint16 ptrVar);
	void abortSequences() { _seqAbort = true; }
	void showMessage(uint16 off);
	Actor &slotActor(uint16 dsSlot);
	void takeObjectState(uint16 k);
	void roomHookTick();
	void roomEntryHook();
	uint16 hotspotBase() const;
	void setTinkerTarget(int x, int y, bool jump);
	void tinkerTick();
	void rewindSequence(uint16 desc);
	void stopPeter();
	bool loadPeterBank(uint16 nameOff);
	void talkToHotspot(uint16 hs);
	bool tinkerHit(int canvasX, int canvasY) const;
	void tinkerTalk(int canvasX, int canvasY);
	void startScene(uint16 script);
	void modalTick();
	void sceneTick();
	void endScene();
	void startFade(bool in, int al);
	void fadeStep();
	void applyPalette();
	// sequence.cpp: action sequences
	void syncSlotsToDs();
	void syncSlotsFromDs();
	void sequencesTick();
	void runSequence(uint16 &desc, uint16 &counter);
	void trackStep(uint16 track);

public:
	bool sequenceRunning() const;
	void startSequence(uint16 desc, bool channelB = false);
	void giveItem(uint16 item) { addItem(item); }

private:

	bool loadGlobals();
	void resetGame();				///< the EXE's initial game state (no room yet)
	void newGame();					///< 0028C: room 0, entry 0
	void resetTransient();			///< what a load or a new game drops (no DS state)
	bool loadPanel(const char *name);
	bool runIntro();				///< 001A9 until a click; false = quit
	// saveload.cpp: the load/save panel (0C398, opened by USE on the save disk, 08C18)
	void optionsTick();
	void importOriginalSaves();		///< the original HOOK.SAV slots, once
	void closeOptions();
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
	void updateDepths();
	void drawSpeech(const Common::Rect &view);
	void drawSpeechLines(const Common::Rect &view, const Common::Array<Common::String> &lines, int ax, int bx,
						 byte colour, bool peter);
	void drawPanel();
	void drawDebug();
	const SpriteBank *bankFor(int id) const;
	void handleKey(const Common::KeyState &key);
	void setCursor();
	// testmode.cpp: headless dumps for development
	bool isTestMode() const;
	Common::Error runTestMode();
	Common::Error dumpFrames(const Common::Path &dir);
	bool hotspotPoint(int k, int &x, int &y) const;
	Common::Error dumpMusic(const Common::Path &dir);
	void initSound();
	void roomSound();
	void soundTick();
	void ambientTick();

	const ADGameDescription *_gameDescription;
	Graphics::Screen *_screen = nullptr;
	Resources _res;
	MusicDriver *_music = nullptr;
	Buffer _ds;
	Room _room;
	AnimVM _anim;
	Player _player;
	SpriteBank _peterBank;
	Language _lang;
	Font _font;

	Common::Array<Actor> _objects;	///< room objects (original DS:147E)

	Buffer _panel;					///< PANEL.BIN: 320x144 sheet, colours 0..31 (+0xE0)
	Common::String _panelName;		///< PANEL.BIN, PANEL2.BIN or PANEL3.BIN (load/save)
	Common::String _optionsReturn;	///< the panel to restore when the load/save panel closes
	bool _inGame = false;			///< not in the intro or credits (loading and saving allowed)
	bool _gameEnded = false;
	Buffer _icons;					///< INVENTRY.BIN: 38 icons of 32x24

	uint16 _verb = 0;				///< [0E60]: 0 or 0x8000 | 1..5 (LOOK, TALK, PICK UP, USE, GIVE)
	uint16 _prevVerb = 0;			///< [0E62]
	uint16 _hover = 0;				///< [AC09]: DS address of the hotspot under the mouse
	int _fallbackLook = 0;			///< [AC25]
	PendingType _pending = kPendingNone;
	uint16 _pendingArg = 0;

	bool _itemSelected = false;		///< [AC11]
	int _itemX = 0;					///< [AC15]: x of the selected slot + 32
	int _arrowTimer = 0;			///< [4194]
	uint16 _pickupHotspot = 0;		///< [ABE5]
	int _pickupTimer = 0;			///< [ABED]
	uint16 _lookEvent = 0;			///< [ABF3]
	int _selectAnim = 0;			///< [AC17]
	SpriteBank _arrowsBank;			///< ARROWS.BIN
	SpriteBank _tinkerBank;			///< TINKER.BIN
	Actor _actors[6];				///< actors 1..5 (original DS:1410..); [0] unused, Peter is _player
	uint16 _seqCounter = 0;			///< [AB79] counter of the channel being run
	uint16 _seqDesc = 0;			///< [AB75] descriptor being run
	bool _seqAbort = false;			///< a callback left the sequence runner (see sequencesTick)

	bool _convActive = false;		///< [AC36] != 0
	bool _convInDs = false;			///< struct and menus in the data segment (GIVE replies)
	uint16 _convStruct = 0;			///< [AC36]
	bool _convReply = false;		///< [AE12]
	int _convPeterTicks = 0;		///< [ABEB] while in a conversation
	int _convNpcTicks = 0;			///< [AC1B]
	int _convPause = 0;				///< [AE10]
	int _npcTalkCounter = 0;		///< [3DD8] for the NPC
	int _stockLine = 0;				///< [ABAD]
	uint16 _giveHotspot = 0;		///< [AC27]
	int _giveTimer = 0;				///< [ABF1]

	Message _speech;				///< refs of the message being said
	uint _speechPart = 0;
	int _speechTicks = 0;			///< [ABD3] ticks left for the current part
	int _speechTotal = 0;			///< [ABEB] ticks left for the whole message
	byte _speechColour = 0x9C;		///< DS:6E5C[speaker]

	int _roomCountdown = 0;			///< [ABBF]: >0 counting, <0 idle
	bool _roomGuard = false;		///< [AFA2]
	int _scrollRequest[4] = { 0, 0, 0, 0 };	///< [AB93] right, [AB95] left, [AB97] up, [AB99] down

	Common::Point _mouse;
	bool _clickLeft = false;
	bool _clickRight = false;

	int _scrollX = 0;
	int _scrollY = 0;
	bool _dirty = true;

	// Modal loops that run inside a sequence callback in the original (the sequences stand still)
	enum ModalType {
		kModalNone,
		kModalScene,	///< 0985F: the flashback scene script ([AC48] / [AC4C] reused)
		kModalFlyUp		///< 099D2: Peter flies up out of the view (65 frames of one retrace)
	};
	ModalType _modal = kModalNone;
	uint16 _scenePtr = 0;
	uint16 _sceneCounter = 0;
	int _flyCounter = 0;

	// Palette fades (00B98 out / 00BEB in, stepped by the retrace interrupt, 024DA / 02562)
	bool _sceneFade = false;		///< cs:9950: fades touch colours 64..95 only, speaker colour 3
	byte _basePalette[256 * 3];		///< the room palette (DS:07B1)
	int _palLevel[256];				///< 0..32 per colour
	int _fadeDir = 0;				///< +1 in, -1 out, 0 idle
	int _fadeSteps = 0;				///< cs:1B57
	int _fadeDelta = 1;				///< cs:1B59
};

extern HookEngine *g_engine;

} // End of namespace Hook

#endif // HOOK_H
