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

#include "hook/hook.h"
#include "hook/player.h"
#include "hook/room.h"

namespace Hook {

// Walk tables in the data segment
static const uint16 kDsLeftFrames = 0x0D82;		// 8 x i16
static const uint16 kDsHorizSteps = 0x0D92;		// 8 x i16
static const uint16 kDsVertSteps = 0x0DA2;		// 6 x i16
static const uint16 kDsRightFrames = 0x0DAE;	// 8 x i16
static const uint16 kDsDownFrames = 0x0DC0;		// 6 x i16
static const uint16 kDsUpFrames = 0x0DCC;		// 6 x i16
static const uint16 kDsIdleList = 0x0EA2;		// {frame, sound, delay}..., -1 = restart
static const uint16 kDsTalkFrames = 0x3DDA;		// 4 facings x 32 x i16

static const int kStandFrame = 0x1C;			// + facing
static const int kIdleDelay = 100;

void Player::enterRoom(Room &room, uint index) {
	_room = &room;
	_path.clear();
	_pathIndex = 0;
	_targetNode = _lastNode = _arrivedNode = 0;
	_forceArrive = false;
	_vScale = (uint32)(room.word(0x5E) << 16) | room.word(0x5C);

	// Entry table at header +58: {u16 node A, u16 node B} (global node numbers)
	uint16 table = room.word(0x58);
	uint16 a = room.word(table + 4 * index), b = room.word(table + 4 * index + 2);
	_graph.select(room, a);
	uint localA = a - _graph.base(), localB = b - _graph.base();
	Common::Point pa = _graph.node(localA), pb = _graph.node(localB);

	actor.bank = kBankPeter;
	actor.x = pa.x + 16;
	actor.y = pa.y - 48;
	actor.frame = kStandFrame + facing;
	_tx = actor.x;
	_ty = pa.y;

	updateDepth(room);
	// 0CAC8: walk on to node B (if it is a valid node of the same graph)
	if (localB >= 1 && localB <= _graph.count())
		walkTo(pb.x, pb.y);
}

void Player::updateDepth(const Room &room) {
	// 053EA: f = feet y (or header +52 above header +50); a = f - scene height + header +24
	int f = feetY();
	if (f <= room.sword(0x50))
		f = room.sword(0x52);
	int a = f - room.sword(0x1E) + room.sword(0x24);
	if (a < 0) {
		_depth = -a * 2;
		_depthScale = true;
	} else {
		_depth = 0;
		_depthScale = false;
	}
}

int Player::hotspotTarget(uint16 walkNode, int &x, int &y) const {
	// 05E94: walk node 0 = stay; a node outside the active graph counts as "no target".
	x = feetX();
	y = feetY();
	if (walkNode == 0)
		return -1;
	if (walkNode > _graph.end() || walkNode <= _graph.base())
		return 0;
	Common::Point p = _graph.node(walkNode - _graph.base());
	x = p.x;
	y = p.y;
	return 1;
}

bool Player::walkTo(int x, int y) {
	// 05D48: 06406, then the first step towards the first node (05D50)
	if (!startPath(x, y))
		return false;
	int tx, ty;
	if (!popNode(tx, ty))
		return false;
	startSegment(tx, ty);
	return true;
}

bool Player::startPath(int targetX, int targetY) {
	// 06406
	uint16 start = _targetNode ? _targetNode : _graph.nearest(feetX(), feetY());
	uint16 target = _graph.nearest(targetX, targetY);
	if (!start || !target || start == target) {
		_forceArrive = true;
		return false;
	}
	Common::Array<uint16> path = _graph.search(start, target);
	if (path.empty()) {
		_forceArrive = true;
		return false;
	}
	_path = path;
	// Read pointer: keep heading for the current target node, otherwise skip the nearest
	// node and head straight for the second one; skip one more if that would mean turning
	// back to the node just passed.
	_pathIndex = (start == _targetNode) ? -1 : 0;
	if (_pathIndex + 2 < (int)_path.size() && _lastNode == _path[_pathIndex + 2])
		_pathIndex++;
	return true;
}

bool Player::popNode(int &tx, int &ty) {
	// 064A8
	_arrivedNode = _targetNode;
	if (_forceArrive) {
		_arrivedNode = 1;
		_forceArrive = false;
	}
	if (_pathIndex + 1 >= (int)_path.size()) {
		_targetNode = 0;
		_lastNode = 0;
		return false;
	}
	_lastNode = _pathIndex >= 0 ? _path[_pathIndex] : 0;
	_pathIndex++;
	_targetNode = _path[_pathIndex];
	Common::Point p = _graph.node(_targetNode);
	tx = p.x + 16;
	ty = p.y;
	return true;
}

void Player::initMajorX(int dx, int dy) {
	// 0614F
	_dirMinorY = 1;
	dx = ABS(dx);
	if (dy < 0) {
		_dirMinorY = -1;
		dy = -dy;
	}
	_err = 2 * dy - dx;
	_errDec = 2 * dx;
	_errInc = 2 * dy;
}

void Player::initMajorY(int dx, int dy) {
	// 061AB
	_dirMinorX = 1;
	if (dx < 0) {
		_dirMinorX = -1;
		dx = -dx;
	}
	dy = ABS(dy);
	_err = 2 * dx - dy;
	_errDec = 2 * dy;
	_errInc = 2 * dx;
}

int Player::bresenham(int steps, int dir) {
	// 06202: minor-axis movement for `steps` pixels on the major axis
	int d = 0;
	if (steps <= 0 || _errDec <= 0)
		return 0;
	for (int i = 0; i < steps; i++) {
		while (_err >= 0) {
			_err -= _errDec;
			d += dir;
		}
		_err += _errInc;
	}
	return d;
}

void Player::startSegment(int tx, int ty) {
	// 05D50
	int ax = actor.x, bx = feetY();
	_tx = tx;
	_ty = ty;
	if (ABS(tx - ax) <= ABS(ty - bx)) {
		initMajorY(tx - ax, ty - bx);
		if (ty < bx)
			stepUp();
		else if (ty > bx)
			stepDown();
		else
			stop();
	} else {
		initMajorX(tx - ax, ty - bx);
		if (tx < ax)
			stepLeft();
		else if (tx > ax)
			stepRight();
		else
			stop();
	}
}

int Player::horizontalStep(int base) const {
	// 06262: base - floor(base * depth / 90) in the original's 16.16 arithmetic
	if (base <= 0)
		return 0;
	if (_depth == 0)
		return base;
	uint32 q = ((uint32)_depth << 16) / 90;
	uint32 acc = q * (uint32)base;
	int r = base - (int)((acc >> 16) & 0xFFFF);
	return r < 0 ? 0 : r;
}

int Player::verticalStep(int base) const {
	// 05EEB: scaled by the room's 16.16 factor (header +5C/+5E) in the depth region
	if (!_depthScale)
		return base;
	uint32 acc = _vScale;
	if (base != 1) {
		acc += _vScale;
		if (base != 2)
			acc += _vScale;
	}
	return (int)((acc >> 16) & 0xFFFF);
}

void Player::stepRight() {
	int step = horizontalStep((int16)_vm->dsWord(kDsHorizSteps + 2 * _hIndex));
	actor.y += bresenham(step, _dirMinorY);
	actor.x += step;
	actor.frame = (int16)_vm->dsWord(kDsRightFrames + 2 * _hIndex);
	facing = kFacingRight;
	_hIndex = (_hIndex + 1) & 7;
	_idleTimer = kIdleDelay;
	_idleDelay = 0;
	_idlePos = 0;
}

void Player::stepLeft() {
	int step = horizontalStep((int16)_vm->dsWord(kDsHorizSteps + 2 * _hIndex));
	actor.y += bresenham(step, _dirMinorY);
	actor.x -= step;
	actor.frame = (int16)_vm->dsWord(kDsLeftFrames + 2 * _hIndex);
	facing = kFacingLeft;
	_hIndex = (_hIndex + 1) & 7;
	_idleTimer = kIdleDelay;
	_idleDelay = 0;
	_idlePos = 0;
}

void Player::stepDown() {
	actor.frame = (int16)_vm->dsWord(kDsDownFrames + 2 * _vIndex);
	int dy = verticalStep((int16)_vm->dsWord(kDsVertSteps + 2 * _vIndex));
	actor.y += dy;
	actor.x += bresenham(dy, _dirMinorX);
	facing = kFacingDown;
	if (--_vIndex < 0)
		_vIndex = 5;
	_idleTimer = kIdleDelay;
	_idleDelay = 0;
	_idlePos = 0;
}

void Player::stepUp() {
	actor.frame = (int16)_vm->dsWord(kDsUpFrames + 2 * _vIndex);
	int dy = verticalStep((int16)_vm->dsWord(kDsVertSteps + 2 * _vIndex));
	actor.y -= dy;
	actor.x += bresenham(dy, _dirMinorX);
	facing = kFacingUp;
	if (++_vIndex == 6)
		_vIndex = 0;
	_idleTimer = kIdleDelay;
	_idleDelay = 0;
	_idlePos = 0;
}

void Player::stop() {
	// 05DB5: stand, and after 100 idle ticks play the fidget list (05E24)
	if (_idleTimer > 0) {
		_idleTimer--;
		actor.frame = kStandFrame + facing;
		return;
	}
	if (--_idleDelay >= 0)
		return;
	if ((int16)_vm->dsWord(kDsIdleList + 6 * _idlePos) == -1)
		_idlePos = 0;
	uint16 e = kDsIdleList + 6 * _idlePos;
	actor.frame = (int16)_vm->dsWord(e);
	// _vm->dsWord(e + 2): sound effect of the fidget (sound is milestone M5)
	_idleDelay = (int16)_vm->dsWord(e + 4);
	_idlePos++;
}

void Player::talkFrame(bool last) {
	// 07C56: mouth animation, 32 frames per facing, advancing every second tick
	_talkCounter = (_talkCounter + 1) & 63;
	int index = facing * 32 + (last ? 0 : _talkCounter / 2);
	actor.frame = (int16)_vm->dsWord(kDsTalkFrames + 2 * index);
	_idleTimer = kIdleDelay;
	_idleDelay = 0;
	_idlePos = 0;
}

bool Player::update(bool talking) {
	if (talking)
		return false;
	// 055EF: keep stepping until the current node is reached or passed
	int ax = actor.x, bx = feetY();
	switch (facing) {
	case kFacingLeft:
		if (_tx < ax) {
			stepLeft();
			return false;
		}
		break;
	case kFacingRight:
		if (_tx > ax) {
			stepRight();
			return false;
		}
		break;
	case kFacingUp:
		if (_ty < bx) {
			stepUp();
			return false;
		}
		break;
	default:
		if (_ty > bx + 2) {
			stepDown();
			return false;
		}
		break;
	}
	// 0562D: next node, or arrival
	int tx, ty;
	if (popNode(tx, ty)) {
		startSegment(tx, ty);
		return false;
	}
	if (_arrivedNode) {
		actor.frame = kStandFrame + facing;
		return true;
	}
	stop();
	return false;
}

} // End of namespace Hook
