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

// Conversations (TALK; original 07B35, 07E2B..083D5). The menu shows one option at a time
// as Peter's line (colour of speaker 1); right-click cycles, left-click chooses. The struct
// and its menus live in the room file, or in the data segment for the GIVE replies.

#include "common/debug.h"

#include "hook/hook.h"

namespace Hook {

static const uint16 kDsFlags = 0x86B7;
static const uint16 kDsStockLines = 0x3F5E;		// u32 text refs, index 1..8
static const uint16 kDsNpcTalkPattern = 0x3F1A;	// 16 x {0 = idle frame, 1 = talk frame}
static const uint16 kCsActions = 0x8320;		// 17 action handlers
static const uint16 kCsSpeakerColour = 0x6E5C;
static const int kTicksPerChar = 3;				// DS:3DD4

uint16 HookEngine::convWord(uint16 off) const {
	return _convInDs ? dsWord(off) : _room.word(off);
}

void HookEngine::setConvWord(uint16 off, uint16 v) {
	if (_convInDs)
		setDsWord(off, v);
	else
		_room.setWord(off, v);
}

int HookEngine::refTicks(uint16 ref) const {
	int n = 0;
	for (const Common::String &l : _lang.lines(ref))
		n += l.size();
	return n * kTicksPerChar;
}

void HookEngine::startConversation(uint16 structOff, bool inDs) {
	// 07B35
	_convStruct = structOff;
	_convInDs = inDs;
	_convActive = true;
	_convReply = false;
	_convPeterTicks = _convNpcTicks = _convPause = 0;
	_player.actor.frame = 0x1C + _player.facing;
}

void HookEngine::setPeterLine(uint16 ref, int speaker) {
	setDsWord(0xABF7, ref);
	_speechColour = _res.image()[kCsSpeakerColour + speaker];
}

void HookEngine::conversationTick() {
	// 07E2B
	if (sequenceRunning())
		return;
	if (!_convReply) {
		conversationMenu();
		return;
	}
	conversationReply();
}

uint16 HookEngine::optionText(uint16 option, uint16 &after, bool &visible) const {
	// 08163 / 08074: an option may start with a flag condition {negative, k, |k| flags}
	visible = true;
	if ((int16)convWord(option) >= 0) {
		after = option + 4;
		return convWord(option);
	}
	int k = (int16)convWord(option + 4);
	uint16 p = option + 8;
	for (int i = 0; i < ABS(k); i++, p += 4) {
		bool set = dsWord(kDsFlags + 2 * convWord(p)) != 0;
		if (k > 0 ? !set : set)
			visible = false;
	}
	after = p + 4;
	return convWord(p);
}

void HookEngine::conversationMenu() {
	// 07F5E
	uint16 s = _convStruct;
	uint16 menu = convWord(s);
	uint16 n = convWord(menu);
	if (_clickLeft) {
		uint16 option = convWord(menu + 2 + 4 * convWord(s + 4));
		if (convWord(option) == 0) {
			conversationReply();
			return;
		}
		uint16 after;
		bool visible;
		uint16 text = optionText(option, after, visible);
		_convPeterTicks = refTicks(text);
		_convReply = true;
	} else if (_clickRight) {
		// 08002: next option
		setConvWord(s + 6, 4);
		setConvWord(s + 4, convWord(s + 4) + 1 == n ? 0 : convWord(s + 4) + 1);
	}
	// 0802D: show the selected option, skipping hidden ones
	for (uint guard = 0; guard <= n; guard++) {
		uint16 option = convWord(menu + 2 + 4 * convWord(s + 4));
		if (convWord(option) == 0) {
			conversationReply();	// no text (the GIVE replies): run the reply at once
			return;
		}
		uint16 after;
		bool visible;
		uint16 text = optionText(option, after, visible);
		if (!visible) {
			setConvWord(s + 6, 4);
			setConvWord(s + 4, convWord(s + 4) + 1 == n ? 0 : convWord(s + 4) + 1);
			continue;
		}
		if ((int16)convWord(option) < 0)
			setConvWord(s + 6, after - option);
		setPeterLine(text, _convReply ? 0 : 1);
		return;
	}
}

void HookEngine::conversationReply() {
	// 07E3D: let the current line finish, pause 3 ticks, then run the next reply command
	uint16 s = _convStruct;
	if (_convPeterTicks) {
		if (--_convPeterTicks) {
			_player.talkFrame(false);
		} else {
			setDsWord(0xABF7, 0);
			_convPause = kTicksPerChar;
			_player.actor.frame = 0x1C + _player.facing;
		}
		return;
	}
	if (_convPause) {
		_convPause--;
		return;
	}
	if (_convNpcTicks) {
		npcTalkTick();
		return;
	}
	uint16 option = convWord(convWord(s) + 2 + 4 * convWord(s + 4));
	uint16 p = option + convWord(s + 6);
	int16 op = (int16)convWord(p);
	p += 4;
	switch (op) {
	case -1:	// 081D9 END: back to the start menu, close
		setConvWord(s, convWord(s + 8));
		setConvWord(s + 4, 0);
		setConvWord(s + 6, 4);
		_convActive = false;
		_convReply = false;
		_prevVerb = _verb;
		_verb = 0;
		return;
	case -2:	// 0821A NPC says a line
		npcSay(convWord(p));
		setConvWord(s + 6, p + 4 - option);
		return;
	case -3:	// 081FC GOTO menu
		_convReply = false;
		setConvWord(s, convWord(p));
		setConvWord(s + 4, 0);
		setConvWord(s + 6, 4);
		return;
	case -4:	// 08281 Peter says a line
		_convPeterTicks = refTicks(convWord(p));
		setPeterLine(convWord(p), 0);
		setConvWord(s + 6, p + 4 - option);
		return;
	case -5:	// 082DA pose
		if (_player.facing < 2)
			_player.actor.frame = (_player.facing ^ 1) + 0x2F;
		setConvWord(s + 6, p - option);
		return;
	case -6: {	// 082FC run action n
		uint16 n = convWord(p);
		setConvWord(s + 6, p + 4 - option);
		if (n < 17)
			runHandler(csWord(kCsActions + 2 * n));
		return;
	}
	case -7: {	// 0834A stock refusal
		int idx;
		if (_room.number() == 30) {
			idx = 8;
		} else {
			_stockLine = (_stockLine + 1) & 7;
			if (!_stockLine)
				_stockLine = 1;
			idx = _stockLine;
		}
		setConvWord(s + 6, p - option);
		npcSay(dsWord(kDsStockLines + 4 * idx));
		return;
	}
	default:
		warning("Hook: unknown conversation opcode %d", op);
		_convActive = false;
		return;
	}
}

void HookEngine::npcSay(uint16 ref) {
	uint16 s = _convStruct;
	_convNpcTicks = refTicks(ref);
	setDsWord(0xAC44, ref);
	setDsWord(0xABB1, convWord(s + 0x14));
	setDsWord(0xABB3, convWord(s + 0x16));
	npcTalkTick();
}

void HookEngine::npcTalkTick() {
	// 07EC3: the speaking object alternates its two talk frames; idle frame at the end
	uint16 s = _convStruct;
	int16 speaker = (int16)convWord(s + 0x0C);
	int obj = speaker >= 0 ? (speaker - 0x16) / 22 : -1;	// +0C is relative to DS:1468
	if (--_convNpcTicks <= 0) {
		_convNpcTicks = 0;
		if (obj >= 0 && obj < (int)_objects.size())
			_objects[obj].frame = (int16)convWord(s + 0x10);
		setDsWord(0xAC44, 0);
		_convPause = kTicksPerChar;
		return;
	}
	if (obj >= 0 && obj < (int)_objects.size()) {
		_npcTalkCounter = (_npcTalkCounter + 1) & 15;
		int which = dsWord(kDsNpcTalkPattern + 2 * _npcTalkCounter);
		_objects[obj].frame = (int16)convWord(s + 0x10 + 2 * which);
	}
}

} // End of namespace Hook
