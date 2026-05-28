/*
 * Copyright (c) 2020-2023 The reone project contributors
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

#include "reone/game/object/encounter.h"

#include "reone/game/di/services.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/strings.h"

using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace game {

void Encounter::deserialize(const resource::Gff &gff) {
    std::string templateRes;
    if (gff.readResRef(templateRes, "TemplateResRef")) {
        if (auto ute = _services.resource.gffs.get(templateRes, ResType::Ute)) {
            deserializeAll(*ute);
        }
    }
    deserializeAll(gff);
}

void Encounter::deserializeAll(const resource::Gff &gff) {
    if (gff.readString(_tag, "Tag")) {
        boost::to_lower(_tag);
    }

    if (gff.readLocString(_locName, "LocalizedName", _services.resource.strings)) {
        _name = _locName.str();
    }

    gff.readBool(_active, "Active");
    gff.readBool(_reset, "Reset");
    gff.readInt(_resetTime, "ResetTime");
    gff.readInt(_respawns, "Respawns");
    gff.readInt(_spawnOption, "SpawnOption");
    gff.readInt(_maxCreatures, "MaxCreatures");
    gff.readInt(_recCreatures, "RecCreatures");
    gff.readBool(_playerOnly, "PlayerOnly");
    gff.readEnum(_faction, "Faction");

    // index into encdifficulty.2da
    gff.readInt(_difficultyIndex, "DifficultyIndex");
    gff.readInt(_difficulty, "Difficulty");

    gff.readFloat(_position[0], "XPosition");
    gff.readFloat(_position[1], "YPosition");
    gff.readFloat(_position[2], "ZPosition");

    gff.readResRef(_onEntered, "OnEntered");
    gff.readResRef(_onExit, "OnExit");
    gff.readResRef(_onExhausted, "OnExhausted");
    gff.readResRef(_onHeartbeat, "OnHeartbeat");
    gff.readResRef(_onUserDefined, "OnUserDefined");

    deserializeCreatures(gff);
    deserializeGeometry(gff);
    deserializeSpawnPoints(gff);

    updateTransform();
}

void Encounter::deserializeCreatures(const resource::Gff &gff) {
    for (const auto &creatureGff : gff.getList("CreatureList")) {
        EncounterCreature creature;
        creatureGff->readDword(creature._appearance, "Appearance");
        creatureGff->readFloat(creature._cr, "CR");
        creatureGff->readResRef(creature._resRef, "ResRef");
        creatureGff->readBool(creature._singleSpawn, "SingleSpawn");
        _creatures.push_back(std::move(creature));
    }
}

void Encounter::deserializeGeometry(const resource::Gff &gff) {
    for (const auto &pointGff : gff.getList("Geometry")) {
        glm::vec3 point;
        pointGff->readFloat(point[0], "X");
        pointGff->readFloat(point[1], "Y");
        pointGff->readFloat(point[2], "Z");
        _geometry.push_back(point);
    }
}

void Encounter::deserializeSpawnPoints(const resource::Gff &gff) {
    for (const auto &pointGff : gff.getList("SpawnPointList")) {
        SpawnPoint point;
        pointGff->readFloat(point.position[0], "X");
        pointGff->readFloat(point.position[1], "Y");
        pointGff->readFloat(point.position[2], "Z");
        pointGff->readFloat(point.orientation, "Orientation");
        _spawnPoints.push_back(point);
    }
}

} // namespace game

} // namespace reone
