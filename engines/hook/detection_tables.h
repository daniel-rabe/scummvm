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

namespace Hook {

const PlainGameDescriptor hookGames[] = {
	{ "hook", "Hook" },
	{ nullptr, nullptr }
};

const ADGameDescription gameDescriptions[] = {
	{
		// DOS, 3 x 720K floppies, multi-language (EN/FR/DE/ES/IT chosen by DOS country code).
		// HOOK.EXE = RNC-packed MZ executable + EJWLIB resource overlay.
		"hook",
		nullptr,
		AD_ENTRY1s("HOOK.EXE", "5f24b291ca2a99681dbbfce2963a232e", 1655151),
		Common::EN_ANY,
		Common::kPlatformDOS,
		ADGF_UNSTABLE,
		GUIO1(GUIO_NONE)
	},

	AD_TABLE_END_MARKER
};

} // End of namespace Hook
