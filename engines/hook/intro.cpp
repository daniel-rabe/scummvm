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

#include "common/debug.h"
#include "common/endian.h"
#include "common/events.h"
#include "common/file.h"
#include "common/system.h"
#include "engines/engine.h"
#include "graphics/paletteman.h"
#include "graphics/screen.h"
#include "image/png.h"

#include "hook/hook.h"
#include "hook/intro.h"
#include "hook/sprites.h"

namespace Hook {

static const int kW = FliDecoder::kWidth;
static const int kH = FliDecoder::kHeight;
static const int kRetraceHz = 70;

static const uint16 kDsPlaylist = 0xB24D;		// the intro playlist
static const uint16 kDsCues = 0xB63D;			// {u16 scene, u16 frame, u16 song, u16 effect}, FFFD = none
static const uint16 kDsPanelColours = 0x06E5;	// colours E0..FF (6-bit)
static const uint16 kCsCreditsEnding = 0xB95C;	// code segment: u16 delay, {u16 frame, u16 delay}..., FFFF
static const uint16 kCsCreditsTitle = 0xB960;
static const int kStillRetraces = 0x15E;		// FC stills stay 350 retraces (5 s)
static const int kScrollSteps = 0xF0;			// SCENE8.VGA: 239 lines, one per retrace
static const int kScrollHold = 0xC8;
static const int kCreditSlots = 0x1F;

Intro::Intro(HookEngine *vm, Graphics::Screen *screen) : _vm(vm), _screen(screen) {
	memset(_back, 0, sizeof(_back));
	memset(_pal, 0, sizeof(_pal));
	_clock = g_system->getMillis();
}

// --- timing and output ---

bool Intro::retrace() {
	Common::Event e;
	while (g_system->getEventManager()->pollEvent(e)) {
		switch (e.type) {
		case Common::EVENT_LBUTTONDOWN:
		case Common::EVENT_RBUTTONDOWN:
			_skip = true;	// 001FD / 0B8F7: a click starts the game
			break;
		case Common::EVENT_KEYDOWN:
			// The original quits on ESC (DB1E) and calibrates the joystick on J (B2B7).
			// Here ESC, space and return skip like a click.
			if (e.kbd.keycode == Common::KEYCODE_ESCAPE || e.kbd.keycode == Common::KEYCODE_SPACE ||
				e.kbd.keycode == Common::KEYCODE_RETURN)
				_skip = true;
			break;
		default:
			break;
		}
	}
	if (Engine::shouldQuit())
		_quit = true;

	// 024DA / 02562: one fade step per retrace
	if (_fadeDir) {
		_level = CLIP(_level + _fadeDir, 0, 32);
		if (_level == 0 || _level == 32)
			_fadeDir = 0;
		setPalette();
	}
	_screen->update();

	_retraces++;
	if (_dumpEvery)
		return !_quit;
	uint32 due = _clock + _retraces * 1000 / kRetraceHz, now = g_system->getMillis();
	if (now + 1000 < due || now > due + 250) {
		// paused (the main menu) or a clock jump: start counting again
		_clock = now;
		_retraces = 0;
	} else if (now < due) {
		g_system->delayMillis(due - now);
	}
	return !_quit;
}

bool Intro::wait(int retraces) {
	for (int i = 0; i < retraces; i++)
		if (!retrace() || _skip)
			return false;
	return true;
}

bool Intro::tick() {
	bool ok = wait(_delay);
	flip();
	return ok;
}

bool Intro::waitFade() {
	while (_fadeDir)
		if (!retrace())
			return false;
	return true;
}

void Intro::startFadeOut() {
	_fadeDir = -1;
}

void Intro::startFadeIn() {
	// 00BEB: from black to _pal
	_level = 0;
	_fadeDir = 1;
	setPalette();
}

void Intro::fadeOut() {
	bool skip = _skip;
	_skip = false;
	startFadeOut();
	waitFade();
	_skip = skip;
}

void Intro::setPalette() {
	byte pal[256 * 3];
	for (int i = 0; i < 256 * 3; i++)
		pal[i] = _pal[i] * _level / 32;
	g_system->getPaletteManager()->setPalette(pal, 0, 256);
}

void Intro::flip() {
	_screen->copyRectToSurface(_back, kW, 0, 0, kW, kH);
	_screen->markAllDirty();
	if (_dumpEvery && ++_flips % _dumpEvery == 0)
		dump("flip");
}

void Intro::dump(const char *what) {
	byte pal[256 * 3];
	for (int i = 0; i < 256 * 3; i++)
		pal[i] = _pal[i] * _level / 32;
	Common::DumpFile out;
	Common::String name = Common::String::format("%05u_%s_s%u_f%u.png", _flips, what, _scene, _frame);
	Common::Path path = _dumpDir.appendComponent(name);
	if (!out.open(path) || !Image::writePNG(out, *_screen->surfacePtr(), pal))
		warning("Hook: cannot write %s", path.toString().c_str());
}

// --- the intro ---

Intro::Result Intro::run() {
	_screen->clear(0);
	setPalette();
	for (;;) {
		// ported: 01A9. Stop the music, back to the start of the playlist and the cues
		_vm->playSong(0xFF);
		_pos = kDsPlaylist;
		_cue = kDsCues;
		_scene = _frame = 0;
		_framesLeft = _replays = _hold = 0;
		_scrolling = false;
		_delay = 4;
		for (;;) {
			bool end = false;
			step(end);
			if (end || stopped())
				break;
			cues();
			if (!_scrolling)
				tick();		// 0BFA2
			if (stopped())
				break;
		}
		if (_skip)
			fadeOut();
		if (stopped())
			return result();
		// 0020F: the playlist ended, the title credits; a click there starts the game
		Result r = credits(false);
		if (r != kDone || _dumpEvery)
			return r;
	}
}

void Intro::step(bool &end) {
	// ported: B9D0
	if (_scrolling) {
		scrollStep();
		return;
	}
	if (_framesLeft > 0) {
		_framesLeft--;
		_frame++;
		_fli.nextFrame();
		memcpy(_back, _fli.pixels(), sizeof(_back));
		return;
	}
	if (_replays > 0) {
		// 0BCB6 again: from the first frame's image
		_replays--;
		_fli.rewind();
		_fli.nextFrame();
		memcpy(_back, _fli.pixels(), sizeof(_back));
		_framesLeft = _fli.frameCount() - 1;
		_frame = 1;
		return;
	}
	if (_hold > 0) {
		_hold--;
		return;
	}
	const byte *ds = _vm->resources().ds(0);
	uint16 name = _pos;
	for (;;) {
		byte op = ds[_pos++];
		if (op == 0xFE) {
			_delay = ds[_pos++];
			name = _pos;
			continue;
		}
		Common::String s;
		if (op >= 0xFB && op <= 0xFD) {
			s = (const char *)ds + _pos;
			_pos += s.size() + 1;
		}
		switch (op) {
		case 0xFF:
			end = true;
			return;
		case 0xFD:
			startScroll(s);
			return;
		case 0xFC:
			still(s, true);
			return;
		case 0xFB:
			still(s, false);
			return;
		case 0:
			// name\0 u8 replays u8 hold: an FLI
			s = (const char *)ds + name;
			_replays = ds[_pos++];
			_hold = ds[_pos++];
			startFli(s);
			return;
		default:
			break;	// a character of the name
		}
	}
}

void Intro::cues() {
	// ported: B693. One cue per tick, when both the scene and the frame have been reached
	const byte *c = _vm->resources().ds(_cue);
	if (_scene < READ_LE_UINT16(c) || _frame < READ_LE_UINT16(c + 2))
		return;
	uint16 song = READ_LE_UINT16(c + 4), fx = READ_LE_UINT16(c + 6);
	if (song != 0xFFFD)
		_vm->playSong(song);
	if (fx != 0xFFFD)
		_vm->playEffect(fx);
	_cue += 8;
}

void Intro::startFli(const Common::String &name) {
	// 0BA45: the previous scene fades out while this one loads (on the original hardware the
	// loading takes longer than the fade: wait for it), then frame 1 and a fade-in.
	startFadeOut();
	if (!waitFade())
		return;
	Buffer data;
	if (!_vm->resources().load(name, data) || !_fli.load(data)) {
		warning("Hook: cannot play %s", name.c_str());
		_framesLeft = _replays = _hold = 0;
		return;
	}
	debugC(1, kDebugScript, "intro: %s, %u frames, delay %d", name.c_str(), _fli.frameCount(), _delay);
	_fli.nextFrame();
	memcpy(_back, _fli.pixels(), sizeof(_back));
	memcpy(_pal, _fli.palette(), sizeof(_pal));
	_framesLeft = _fli.frameCount() - 1;
	_frame = 1;
	_scene++;
	tick();			// 0BA6D
	startFadeIn();	// 0BF77
}

bool Intro::loadVga(const Common::String &name, bool palette) {
	// 64000 pixels, then a 6-bit palette
	Buffer data;
	if (!_vm->resources().load(name, data) || data.size() < (uint)kW * kH) {
		warning("Hook: cannot load %s", name.c_str());
		return false;
	}
	memcpy(_back, data.data(), sizeof(_back));
	if (palette && data.size() >= (uint)kW * kH + 768)
		for (int i = 0; i < 768; i++)
			_pal[i] = (data[kW * kH + i] & 0x3F) * 255 / 63;
	return true;
}

void Intro::still(const Common::String &name, bool hold) {
	// 0BAC3 (FC): fade out, show, fade in, 5 seconds, fade out, clear.
	// 0BB98 (FB): fade out, show, fade in (HOOK.VGA: it stays under the title credits).
	debugC(1, kDebugScript, "intro: still %s", name.c_str());
	startFadeOut();
	if (!waitFade())
		return;
	if (!wait(_delay) || !loadVga(name, true))
		return;
	flip();
	startFadeIn();
	if (_dumpEvery) {
		_level = 32;
		dump(name.c_str());
	}
	if (!hold)
		return;
	wait(kStillRetraces);
	if (stopped())
		return;
	startFadeOut();
	waitFade();
	memset(_back, 0, sizeof(_back));
	flip();
}

void Intro::startScroll(const Common::String &name) {
	// 0BA86: a 320x480 still under the current palette, scrolled up one line per retrace
	if (!waitFade())
		return;
	if (!_vm->resources().load(name, _tall) || _tall.size() < (uint)kW * 480) {
		warning("Hook: cannot load %s", name.c_str());
		return;
	}
	debugC(1, kDebugScript, "intro: scroll %s", name.c_str());
	if (!wait(_delay))
		return;
	_scrollY = 0;
	drawScroll();
	_scrollLeft = kScrollSteps;
	_scrolling = true;
}

void Intro::drawScroll() {
	memcpy(_back, _tall.data() + _scrollY * kW, sizeof(_back));
	flip();
}

void Intro::scrollStep() {
	// 0BC08
	if (--_scrollLeft > 0) {
		if (retrace()) {
			_scrollY++;
			drawScroll();
		}
		return;
	}
	_scrolling = false;
	if (!wait(kScrollHold))
		return;
	startFadeOut();
	if (!waitFade())
		return;
	_scrollY = 0;
	wait(_delay);
}

// --- the credits ---

Intro::Result Intro::credits(bool ending) {
	_vm->playSong(5);	// ported: B6C3
	Buffer bankData;
	SpriteBank bank;
	if (!_vm->resources().load("CREDITS.BIN", bankData) || !bank.load(bankData.data(), bankData.size(), nullptr)) {
		warning("Hook: cannot load CREDITS.BIN");
		return kDone;
	}
	if (ending) {
		// After the ending: the game fades out, then DECK4.VGA with the panel colours
		g_system->getPaletteManager()->grabPalette(_pal, 0, 256);
		_level = 32;
		startFadeOut();
		waitFade();
		_screen->clear(0);
		if (!wait(_delay) || !loadVga("DECK4.VGA", true))
			return result();
		const byte *panel = _vm->resources().ds(kDsPanelColours);
		for (int i = 0; i < 0x20 * 3; i++)
			_pal[0xE0 * 3 + i] = (panel[i] & 0x3F) * 255 / 63;
		flip();
		startFadeIn();
		if (!waitFade())
			return result();
	}
	// The background: DECK4.VGA, or the HOOK.VGA title still left by the playlist
	byte background[kW * kH];
	memcpy(background, _back, sizeof(background));

	struct Slot {
		int frame = -1, x = 0, y = 0;
	} slots[kCreditSlots];
	uint16 p = ending ? kCsCreditsEnding : kCsCreditsTitle;
	const byte *cs = _vm->resources().image().data();
	int delay = READ_LE_UINT16(cs + p);
	p += 2;
	_delay = 2;

	Graphics::Surface surface;
	surface.init(kW, kH, kW, _back, Graphics::PixelFormat::createFormatCLUT8());
	Common::Rect clip(0, 0, kW, kH);
	for (;;) {
		// 0B7E0: restore the background, draw the lines, wait
		memcpy(_back, background, sizeof(_back));
		for (const Slot &s : slots)
			if (s.frame >= 0)
				if (const Frame *f = bank.frame(s.frame))
					SpriteBank::draw(surface, *f, s.x, s.y, clip);
		tick();
		if (stopped())
			break;
		// 0B86E: every line moves up a pixel and is dropped above y = -10
		for (Slot &s : slots)
			if (s.frame >= 0 && --s.y <= -10)
				s.frame = -1;
		if (delay) {
			delay--;
			continue;
		}
		uint16 frame = READ_LE_UINT16(cs + p);
		if (frame == 0xFFFF)
			break;
		delay = READ_LE_UINT16(cs + p + 2);
		p += 4;
		for (Slot &s : slots) {
			if (s.frame < 0) {
				const Frame *f = bank.frame(frame);
				s.frame = frame;
				s.y = 0xCA;
				s.x = 0xA0 - (f ? f->width / 2 : 0);
				break;
			}
		}
	}
	// 0B926: fade out, music off
	if (!_quit)
		fadeOut();
	_vm->playSong(0);
	return result();
}

} // End of namespace Hook
