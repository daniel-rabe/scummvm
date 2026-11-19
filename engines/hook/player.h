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

#ifndef HOOK_PLAYER_H
#define HOOK_PLAYER_H

#include "common/array.h"

#include "hook/anim.h"
#include "hook/walk.h"

namespace Hook {

class HookEngine;
class Room;

enum Facing {
	kFacingLeft = 0,
	kFacingRight = 1,
	kFacingUp = 2,
	kFacingDown = 3
};

/**
 * Peter (actor 0): walk-graph pathing, Bresenham walking with the original step and frame
 * tables, depth-dependent step scaling, idle fidget and talk animation.
 * Ported from 0406..066AF and 05578..061FF. Slot coordinates: x = feet x + 16,
 * y = feet y - 48 (original DS:1400/1404).
 */
class Player {
public:
	explicit Player(HookEngine *vm) : _vm(vm) {}

	Actor actor;
	int facing = kFacingDown;	///< [140C]

	/** 0C95D: place Peter at entry `index` of the room and start walking to its second node. */
	void enterRoom(Room &room, uint index);

	/** Walk towards canvas point (x, y) (feet coordinates). False if already there. */
	bool walkTo(int x, int y);

	/** Walk target for a hotspot record (5E94): -1 = no walk node, 0 = outside graph, 1 = ok. */
	int hotspotTarget(uint16 walkNode, int &x, int &y) const;

	/** One game tick of the movement state machine; returns true on the tick Peter arrives. */
	bool update(bool talking);

	void startTalking() { _talkCounter = 0; }
	void talkFrame(bool last);
	bool isWalking() const { return _targetNode != 0; }

	int depth() const { return _depth; }
	int feetX() const { return actor.x - 16; }
	int feetY() const { return actor.y + 48; }
	const WalkGraph &graph() const { return _graph; }
	/** 0A215: switch to the walk graph that contains global node `node`. */
	void selectGraph(uint node) { if (_room) _graph.select(*_room, node); }
	/** Restart the walk-cycle frame tables ([0D80]/[0DBE] = 0). */
	void resetStepPhase() { _hIndex = _vIndex = 0; }

	/** 53EA: depth factor from the feet y and the room header (0 = far region / none). */
	void updateDepth(const Room &room);

private:
	bool startPath(int targetX, int targetY);
	bool popNode(int &tx, int &ty);
	void startSegment(int tx, int ty);
	void stepLeft();
	void stepRight();
	void stepUp();
	void stepDown();
	void stop();
	void walkingTick();
	int horizontalStep(int base) const;
	int verticalStep(int base) const;
	int bresenham(int steps, int dir);
	void initMajorX(int dx, int dy);
	void initMajorY(int dx, int dy);

	HookEngine *_vm;
	WalkGraph _graph;
	const Room *_room = nullptr;

	Common::Array<uint16> _path;	///< original path list at DS:AF16 (downwards)
	int _pathIndex = 0;				///< read pointer [AF1C] as an index into _path (-1 = before it)
	uint16 _targetNode = 0;			///< [ABB7]
	uint16 _lastNode = 0;			///< [ABBD]
	uint16 _arrivedNode = 0;		///< [ABB9]
	bool _forceArrive = false;		///< [ABBB]
	int _tx = 0, _ty = 0;			///< [AB7D]/[AB7B] current segment target (slot x, feet y)

	int _hIndex = 0;				///< [0D80] 0..7
	int _vIndex = 0;				///< [0DBE] 0..5
	int _err = 0, _errDec = 0, _errInc = 0;	///< [ABA3]/[ABA5]/[ABA7]
	int _dirMinorY = 1, _dirMinorX = 1;		///< [ABA9]/[ABAB]
	int _depth = 0;					///< [0E42]
	bool _depthScale = false;		///< [0E46] != 0
	uint32 _vScale = 0;				///< [0E00]/[0E02], 16.16

	int _idleTimer = 0;				///< [0E48]
	int _idleDelay = 0;				///< [0E4E]
	uint _idlePos = 0;				///< [0E4A] as an index into DS:0EA2
	int _talkCounter = 0;			///< [3DD8]
};

} // End of namespace Hook

#endif // HOOK_PLAYER_H
