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

#include "common/endian.h"

#include "hook/room.h"
#include "hook/walk.h"

namespace Hook {

bool WalkGraph::select(const Room &room, uint node) {
	_room = &room;
	const Buffer &d = room.data();
	uint graphs = room.word(0x06);
	uint32 linkTable = READ_LE_UINT32(d.data() + 0x08);
	uint32 nodeTable = READ_LE_UINT32(d.data() + 0x0C);
	uint32 countTable = READ_LE_UINT32(d.data() + 0x10);
	uint total = 0;
	_count = 0;
	for (uint g = 0; g < graphs; g++) {
		_links = READ_LE_UINT32(d.data() + linkTable + 4 * g);
		_nodes = READ_LE_UINT32(d.data() + nodeTable + 4 * g);
		_count = room.word(countTable + 2 * g);
		total += _count;
		if ((int)total >= (int)node)
			break;
	}
	_base = total - _count;
	return _count != 0;
}

Common::Point WalkGraph::node(uint n) const {
	if (!_room || n < 1 || n > _count)
		return Common::Point(0, 0);
	return Common::Point(_room->sword(_nodes + 4 * (n - 1)), _room->sword(_nodes + 4 * (n - 1) + 2));
}

uint16 WalkGraph::link(uint n, uint k) const {
	if (!_room || n < 1 || n > _count || k >= 8)
		return 0;
	return _room->word(_links + 16 * (n - 1) + 2 * k);
}

uint WalkGraph::nearest(int x, int y) const {
	int best = 0x7FFF;
	uint found = 0;
	for (uint n = 1; n <= _count; n++) {
		Common::Point p = node(n);
		int d = ABS(p.x - x) + ABS(p.y - y);
		if (d <= best) {
			best = d;
			found = n;
		}
	}
	return found;
}

Common::Array<uint16> WalkGraph::search(uint16 start, uint16 target) const {
	struct SearchFrame {
		uint16 node;
		uint k;
	};
	Common::Array<uint16> path;
	Common::Array<SearchFrame> stack;
	path.push_back(start);
	uint16 cur = start;
	uint k = 0;
	for (int guard = 0; guard < 100000; guard++) {
		uint16 nb = link(cur, k);
		if (nb == target) {
			path.push_back(target);
			return path;
		}
		if (nb == 0) {
			// no more neighbours: backtrack to the parent and try its next link
			if (stack.empty())
				break;
			SearchFrame f = stack.back();
			stack.pop_back();
			path.pop_back();
			cur = f.node;
			k = f.k + 1;
			continue;
		}
		if (path.size() >= 2 && nb == path[path.size() - 2]) {
			k++;	// never straight back to the parent
			continue;
		}
		stack.push_back(SearchFrame{ cur, k });
		path.push_back(nb);
		cur = nb;
		k = 0;
	}
	path.clear();
	return path;
}

} // End of namespace Hook
