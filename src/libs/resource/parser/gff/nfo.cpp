/*
 * Copyright (c) 2026 The reone project contributors
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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "reone/resource/parser/gff/nfo.h"

#include "reone/resource/gff.h"

namespace reone {
namespace resource {

NFO parseNFO(const Gff &gff) {
    NFO nfo;

    nfo.areaName = gff.getString("AREANAME");
    nfo.lastModule = gff.getString("LASTMODULE");
    nfo.timePlayed = gff.getUint("TIMEPLAYED");
    nfo.cheatUsed = gff.getBool("CHEATUSED");
    nfo.savegameName = gff.getString("SAVEGAMENAME");
    nfo.gameplayHint = gff.getUint("GAMEPLAYHINT");
    nfo.storyHint = gff.getUint("STORYHINT");
    nfo.live1 = gff.getString("LIVE1");
    nfo.live2 = gff.getString("LIVE2");
    nfo.live3 = gff.getString("LIVE3");
    nfo.live4 = gff.getString("LIVE4");
    nfo.live5 = gff.getString("LIVE5");
    nfo.live6 = gff.getString("LIVE6");
    nfo.liveContent = gff.getUint("LIVECONTENT");
    nfo.portrait0 = gff.getString("PORTRAIT0");

    return nfo;
}

} // namespace resource
} // namespace reone
