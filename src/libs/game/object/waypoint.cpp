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

#include "reone/game/object/waypoint.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/resources.h"
#include "reone/resource/strings.h"

using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace game {

void Waypoint::loadFromBlueprint(const std::string &resRef) {
    std::shared_ptr<Gff> utw(_services.resource.gffs.get(resRef, ResType::Utw));
    if (!utw) {
        return;
    }
    deserialize(*utw);
}

void Waypoint::deserialize(const resource::Gff &gff) {
    std::string templateRes;
    if (gff.readResRef(templateRes, "TemplateResRef")) {
        if (auto utw = _services.resource.gffs.get(templateRes, ResType::Utw)) {
            deserializeAll(*utw);
        }
    }
    deserializeAll(gff);
    updateTransform();
}

void Waypoint::deserializeAll(const resource::Gff &gff) {
    if (gff.readString(_tag, "Tag")) {
        boost::to_lower(_tag);
    }
    gff.readLocString(_locName, "LocalizedName", _services.resource.strings);
    gff.readFloat(_position[0], "XPosition");
    gff.readFloat(_position[1], "YPosition");
    gff.readFloat(_position[2], "ZPosition");
    {
        float cosine, sine;
        if (gff.readFloat(cosine, "XOrientation") && gff.readFloat(sine, "YOrientation")) {
            _orientation = glm::quat(glm::vec3(0.0f, 0.0f, -glm::atan(cosine, sine)));
        }
    }
    gff.readBool(_hasMapNote, "HasMapNote");
    gff.readBool(_mapNoteEnabled, "MapNoteEnabled");
    gff.readLocString(_mapNote, "MapNote", _services.resource.strings);

    gff.readBool(_commandable, "Commandable");

    // Not handled:
    // VarTable
    // SWVarTable
    // ActionList
    // Commandable
}

} // namespace game

} // namespace reone
