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

#include "hook/console.h"
#include "hook/hook.h"

namespace Hook {

Console::Console() : GUI::Debugger() {
	registerCmd("room", WRAP_METHOD(Console, cmdRoom));
	registerCmd("scroll", WRAP_METHOD(Console, cmdScroll));
	registerCmd("res", WRAP_METHOD(Console, cmdResources));
	registerCmd("peter", WRAP_METHOD(Console, cmdPeter));
	registerCmd("hotspots", WRAP_METHOD(Console, cmdHotspots));
	registerCmd("walk", WRAP_METHOD(Console, cmdWalk));
}

bool Console::cmdPeter(int argc, const char **argv) {
	Actor &p = g_engine->peter();
	if (argc < 4) {
		debugPrintf("Peter: frame %d at %d,%d\nUsage: peter <x> <y> <frame>\n", p.frame, p.x, p.y);
		return true;
	}
	p.x = atoi(argv[1]);
	p.y = atoi(argv[2]);
	p.frame = atoi(argv[3]);
	g_engine->redraw();
	return false;
}

bool Console::cmdHotspots(int argc, const char **argv) {
	g_engine->_showHotspots = !g_engine->_showHotspots;
	g_engine->redraw();
	return false;
}

bool Console::cmdWalk(int argc, const char **argv) {
	g_engine->_showWalkGraph = !g_engine->_showWalkGraph;
	g_engine->redraw();
	return false;
}

bool Console::cmdRoom(int argc, const char **argv) {
	if (argc < 2) {
		const Room &r = g_engine->room();
		debugPrintf("Room %u, canvas %ux%u, scroll %d,%d\n", r.number(), r.width(), r.height(),
					g_engine->scrollX(), g_engine->scrollY());
		debugPrintf("Usage: room <0..%u>\n", Room::kNumRooms - 1);
		return true;
	}
	if (!g_engine->enterRoom(atoi(argv[1])))
		debugPrintf("Cannot load room %s\n", argv[1]);
	return false;	// close the console to show the room
}

bool Console::cmdScroll(int argc, const char **argv) {
	if (argc < 3) {
		debugPrintf("Usage: scroll <x> <y>\n");
		return true;
	}
	g_engine->setScroll(atoi(argv[1]), atoi(argv[2]));
	return false;
}

bool Console::cmdResources(int argc, const char **argv) {
	const Resources &res = g_engine->resources();
	for (uint i = 0; i < res.count(); i++)
		debugPrintf("%-14s%s", res.name(i).c_str(), (i % 5 == 4) ? "\n" : "");
	debugPrintf("\n%u resources, executable image %u bytes\n", res.count(), res.image().size());
	return true;
}

} // End of namespace Hook
