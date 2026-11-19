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

#ifndef HOOK_WALK_H
#define HOOK_WALK_H

#include "common/array.h"
#include "common/rect.h"

namespace Hook {

class Room;

/**
 * The active walk graph of a room (header +06/+08/+0C/+10). Node numbers stored in the
 * data are global across all graphs of the room; the graph that holds the room-entry node
 * becomes the active one (0C95D). Node coordinates are canvas coordinates of the feet.
 */
class WalkGraph {
public:
	/** Select the graph that contains global node `node` (1-based). */
	bool select(const Room &room, uint node);

	uint count() const { return _count; }
	uint base() const { return _base; }			///< global number of local node 0 (original [10C8])
	uint end() const { return _base + _count; }	///< original [10C6]
	Common::Point node(uint n) const;			///< local node, 1-based
	uint16 link(uint n, uint k) const;			///< k-th neighbour (0..7) of local node n, 0 = none

	/** 6529: nearest node by Manhattan distance; ties go to the later node. 0 if no nodes. */
	uint nearest(int x, int y) const;

	/**
	 * 6596: depth-first search from `start` to `target` (local, 1-based). The original only
	 * refuses to step straight back to the parent node, so the data's graphs are loop-free.
	 * Returns the node list start..target, or empty if there is no path.
	 */
	Common::Array<uint16> search(uint16 start, uint16 target) const;

private:
	const Room *_room = nullptr;
	uint32 _links = 0;
	uint32 _nodes = 0;
	uint _count = 0;
	uint _base = 0;
};

} // End of namespace Hook

#endif // HOOK_WALK_H
