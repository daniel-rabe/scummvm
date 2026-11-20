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

#ifndef HOOK_INTRO_H
#define HOOK_INTRO_H

#include "common/path.h"

#include "hook/fli.h"

namespace Graphics {
class Screen;
}

namespace Hook {

class HookEngine;

/**
 * The full-screen parts (docs/formats.md "Boot and intro"): the intro playlist (B9D0) with its
 * effect cues (B693), and the title credits (B6C3). Everything is paced by the 70 Hz retrace
 * like the original: a playlist tick lasts `delay` retraces (cs:3482), fades step once per
 * retrace (00B98 / 00BEB, 32 steps).
 */
class Intro {
public:
	enum Result { kDone, kSkip, kQuit };

	Intro(HookEngine *vm, Graphics::Screen *screen);

	/** 001A9: the intro, then the title credits, again and again until a click (kSkip) or quit. */
	Result run();
	/** 0B6C3: the credits; after the ending over DECK4.VGA and starting with "THE END". */
	Result credits(bool ending);
	/** Fade the current screen to black (whatever is skipped or interrupted). */
	void fadeOut();
	/** Development aid: no delays, a PNG every `every` flips (and at every still) into `dir`. */
	void setDump(const Common::Path &dir, int every) { _dumpDir = dir; _dumpEvery = every; }

private:
	// timing and output
	bool retrace();					///< one retrace: input, fade step, present; false = stop
	bool wait(int retraces);
	bool tick();					///< 00B61 + flip: the frame delay, then show the back buffer
	bool waitFade();				///< 00C4E loop: until the running fade is done
	void startFadeOut();
	void startFadeIn();
	void setPalette();
	void flip();
	bool stopped() const { return _skip || _quit; }
	Result result() const { return _quit ? kQuit : _skip ? kSkip : kDone; }

	// the playlist (DS:B24D)
	void step(bool &end);			///< 0B9D0, once per tick
	void cues();					///< 0B693
	void startFli(const Common::String &name);
	void still(const Common::String &name, bool hold);
	void startScroll(const Common::String &name);
	void scrollStep();
	void drawScroll();
	bool loadVga(const Common::String &name, bool palette);

	HookEngine *_vm;
	Graphics::Screen *_screen;

	byte _back[FliDecoder::kWidth * FliDecoder::kHeight];
	byte _pal[256 * 3];
	int _level = 32;				///< 0..32
	int _fadeDir = 0;
	uint32 _clock = 0;
	uint32 _retraces = 0;
	bool _skip = false;
	bool _quit = false;
	int _delay = 4;					///< cs:3482

	uint16 _pos = 0;				///< [B24B]
	uint16 _cue = 0;				///< [B63B]
	uint16 _scene = 0;				///< [B234]: FLIs started
	uint16 _frame = 0;				///< [B236]: frame of the current FLI
	int _framesLeft = 0;			///< [B240]
	int _replays = 0;				///< [B238]
	int _hold = 0;					///< [B239]
	bool _scrolling = false;		///< [B248]
	int _scrollLeft = 0;			///< [B249]
	int _scrollY = 0;				///< cs:347D
	FliDecoder _fli;
	Buffer _tall;					///< SCENE8.VGA, 320x480

	Common::Path _dumpDir;
	int _dumpEvery = 0;
	int _flips = 0;
	void dump(const char *what);
};

} // End of namespace Hook

#endif // HOOK_INTRO_H
