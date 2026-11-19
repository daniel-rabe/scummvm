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
#include "common/events.h"
#include "common/file.h"
#include "common/system.h"
#include "engines/util.h"
#include "graphics/cursorman.h"
#include "graphics/framelimiter.h"
#include "graphics/paletteman.h"
#include "image/png.h"

#include "hook/console.h"
#include "hook/hook.h"

namespace Hook {

HookEngine *g_engine;

// Data-segment tables (offsets in the DS of the supported HOOK.EXE build)
static const uint16 kDsPeterSlot = 0x13FA;		// actor 0: +04 frame, +06 x, +0A y
static const uint16 kDsInventory = 0x870B;		// 16 x u16 item ids
static const uint16 kDsInventoryScroll = 0x8705;
static const uint16 kDsObjectState = 0x4020;	// per room: far ptr to the object-state list
static const uint16 kDsCursor = 0x0575;			// 12x24 pointer, planar, values 1..3 (+0xDC)

static const int kObjectXOffset = 16;			// 0CFA8: objects are drawn at x - 16
static const int kPeterXOffset = 30;			// 0CF76: actor 0 at x - 16 - 14
static const int kInventoryX = 64, kInventoryY = 44, kInventorySlots = 6;
static const int kIconWidth = 32, kIconHeight = 24;

HookEngine::HookEngine(OSystem *syst, const ADGameDescription *gameDesc) : Engine(syst),
	_gameDescription(gameDesc) {
	g_engine = this;
	memset(_inventory, 0, sizeof(_inventory));
}

HookEngine::~HookEngine() {
	delete _screen;
}

Common::Error HookEngine::run() {
	if (!_res.open("HOOK.EXE"))
		return Common::Error(Common::kReadingFailed, "HOOK.EXE");
	if (!loadGlobals())
		return Common::Error(Common::kReadingFailed, "PANSPRIT.BIN / PANEL.BIN / INVENTRY.BIN");

	initGraphics(kScreenWidth, kScreenHeight);
	_screen = new Graphics::Screen();
	setDebugger(new Console());

	// Development aid (remove before upstreaming): with "dump_frames=<dir>" in the game's
	// config section, write one composed screen per room as PNG and quit.
	if (ConfMan.hasKey("dump_frames"))
		return dumpFrames(Common::Path(ConfMan.get("dump_frames"), '/'));

	if (!enterRoom(0))
		return Common::Error(Common::kReadingFailed, "LOCAT00.BIN");
	setCursor();
	CursorMan.showMouse(true);

	Graphics::FrameLimiter limiter(g_system, kTicksPerSecond);
	Common::Event e;
	while (!shouldQuit()) {
		while (g_system->getEventManager()->pollEvent(e)) {
			if (e.type == Common::EVENT_KEYDOWN)
				handleKey(e.kbd);
		}
		tick();
		drawScreen();
		limiter.delayBeforeSwap();
		_screen->update();
		limiter.startFrame();
	}
	return Common::kNoError;
}

bool HookEngine::loadGlobals() {
	Buffer pan;
	if (!_res.load("PANSPRIT.BIN", pan) || !_peterBank.load(pan.data(), pan.size(), _res.ds(kXlatPeter)))
		return false;
	if (!_res.load("PANEL.BIN", _panel) || _panel.size() < (uint)kScreenWidth * kPanelHeight)
		return false;
	if (!_res.load("INVENTRY.BIN", _icons))
		return false;
	for (int i = 0; i < 16; i++)
		_inventory[i] = _res.dsWord(kDsInventory + 2 * i);
	_inventoryScroll = _res.dsWord(kDsInventoryScroll);

	// Actor 0 starts in the pose the data segment holds for a new game.
	_peter.bank = kBankPeter;
	_peter.frame = (int16)_res.dsWord(kDsPeterSlot + 4);
	_peter.x = (int16)_res.dsWord(kDsPeterSlot + 6);
	_peter.y = (int16)_res.dsWord(kDsPeterSlot + 0xA);
	return true;
}

bool HookEngine::enterRoom(uint number) {
	if (!_room.load(_res, number))
		return false;
	byte pal[256 * 3];
	_room.getPalette(_res, pal);
	g_system->getPaletteManager()->setPalette(pal, 0, 256);

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
	// Until Peter's movement and scrolling (53EA) are ported in M3: centre on him.
	setScroll(_peter.x - kScreenWidth / 2, _peter.y - 100);
	return true;
}

void HookEngine::applyObjectState() {
	// 0844B: DS:[4020 + room*4] -> u16 n, n x 16-byte records
	// {object, hotspot, visible, frame, look, ?, pickup, ?}. Only the object side is
	// applied here; the hotspot side belongs to the game logic (M3/M4).
	uint16 list = _res.dsWord(kDsObjectState + 4 * _room.number());
	uint16 n = _res.dsWord(list);
	for (uint i = 0; i < n; i++) {
		uint16 rec = list + 2 + 16 * i;
		uint16 obj = _res.dsWord(rec);
		if (obj >= _objects.size())
			continue;
		_objects[obj].frame = _res.dsWord(rec + 4) ? (int16)_res.dsWord(rec + 6) : -1;
	}
}

void HookEngine::setScroll(int x, int y) {
	_scrollX = CLIP<int>(x, 0, MAX<int>(0, _room.width() - kScreenWidth));
	_scrollY = CLIP<int>(y, 0, MAX<int>(0, (int)_room.sceneHeight() - kViewHeight));
	_dirty = true;
}

void HookEngine::tick() {
	_anim.tick(_room, _objects);
	_dirty = true;
}

const SpriteBank *HookEngine::bankFor(int id) const {
	if (id >= kBankRoom0 && id < kBankRoom0 + (int)Room::kNumBanks)
		return &_room.bank(id - kBankRoom0);
	if (id == kBankPeter)
		return &_peterBank;
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

void HookEngine::drawView() {
	Common::Rect view(0, 0, kScreenWidth, kViewHeight);
	_screen->fillRect(view, 0);
	int w = MIN<int>(kScreenWidth, _room.width() - _scrollX);
	int h = MIN<int>(kViewHeight, (int)_room.sceneHeight() - _scrollY);
	for (int y = 0; y < h; y++)
		memcpy(_screen->getBasePtr(0, y), _room.canvas() + (_scrollY + y) * _room.width() + _scrollX, w);

	// Sprites in depth order (06A78 / 06AAC): depth = y + height + dy; ties keep slot order,
	// actor 0 first.
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
	add(_peter, 0, kPeterXOffset);
	for (uint i = 0; i < _objects.size(); i++)
		add(_objects[i], 6 + i, kObjectXOffset);
	Common::sort(items.begin(), items.end(), [](const Item &p, const Item &q) {
		int dp = p.a->y + p.f->height + p.f->dy, dq = q.a->y + q.f->height + q.f->dy;
		return dp != dq ? dp < dq : p.order < q.order;
	});
	Graphics::Surface &s = *_screen->surfacePtr();
	for (const Item &it : items)
		SpriteBank::draw(s, *it.f, it.a->x - it.xOffset - _scrollX, it.a->y - _scrollY, view);
}

void HookEngine::drawPanel() {
	// Split-screen panel: the top 72 rows of the panel sheet, colours + 0xE0 (0F0B, 0F1C).
	for (int y = 0; y < kPanelHeight; y++) {
		byte *dst = (byte *)_screen->getBasePtr(0, kViewHeight + y);
		const byte *src = _panel.data() + y * kScreenWidth;
		for (int x = 0; x < kScreenWidth; x++)
			dst[x] = src[x] + 0xE0;
	}
	// Inventory icons (722C): 6 slots from the scroll position, icon k = item k, opaque.
	for (int k = 0; k < kInventorySlots; k++) {
		uint idx = _inventoryScroll + k;
		uint16 item = idx < 16 ? _inventory[idx] : 0;
		uint32 off = item * kIconWidth * kIconHeight;
		if (!item || off + kIconWidth * kIconHeight > _icons.size())
			continue;
		for (int y = 0; y < kIconHeight; y++) {
			byte *dst = (byte *)_screen->getBasePtr(kInventoryX + kIconWidth * k, kViewHeight + kInventoryY + y);
			for (int x = 0; x < kIconWidth; x++)
				dst[x] = _icons[off + y * kIconWidth + x] + 0xE0;
		}
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
				view.frameRect(r, 0xDF);
		}
	}
	if (_showWalkGraph) {
		// Header +06 graph count, +08/+0C/+10 tables of links / nodes / node counts.
		const Buffer &d = _room.data();
		uint16 n = _room.word(0x06);
		for (uint g = 0; g < n; g++) {
			uint32 links = READ_LE_UINT32(d.data() + READ_LE_UINT32(d.data() + 0x08) + 4 * g);
			uint32 nodes = READ_LE_UINT32(d.data() + READ_LE_UINT32(d.data() + 0x0C) + 4 * g);
			uint16 count = _room.word(READ_LE_UINT32(d.data() + 0x10) + 2 * g);
			for (uint i = 0; i < count; i++) {
				int x0 = _room.sword(nodes + 4 * i) - _scrollX, y0 = _room.sword(nodes + 4 * i + 2) - _scrollY;
				for (uint j = 0; j < 8; j++) {
					int16 to = _room.sword(links + 16 * i + 2 * j);
					if (to <= 0 || to > (int)count)
						continue;
					int x1 = _room.sword(nodes + 4 * (to - 1)) - _scrollX, y1 = _room.sword(nodes + 4 * (to - 1) + 2) - _scrollY;
					view.drawLine(CLIP(x0, 0, kScreenWidth - 1), CLIP(y0, 0, kViewHeight - 1),
								  CLIP(x1, 0, kScreenWidth - 1), CLIP(y1, 0, kViewHeight - 1), g ? 0xDE : 0xDD);
				}
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

Common::Error HookEngine::dumpFrames(const Common::Path &dir) {
	int ticks = ConfMan.hasKey("dump_ticks") ? ConfMan.getInt("dump_ticks") : 0;
	for (uint n = 0; n < Room::kNumRooms; n++) {
		if (!enterRoom(n)) {
			warning("Hook: room %u failed to load", n);
			continue;
		}
		for (int t = 0; t < ticks; t++)
			tick();
		_dirty = true;
		drawScreen();
		byte pal[256 * 3];
		_room.getPalette(_res, pal);
		Common::DumpFile out;
		Common::Path path = dir.appendComponent(Common::String::format("frame%02u.png", n));
		if (!out.open(path) || !Image::writePNG(out, *_screen->surfacePtr(), pal))
			warning("Hook: cannot write %s", path.toString().c_str());
	}
	return Common::kNoError;
}

} // End of namespace Hook
