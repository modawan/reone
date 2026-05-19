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

#pragma once

#include "../object.h"
#include "reone/resource/strings.h"

namespace reone {

namespace game {

class Waypoint : public Object {
public:
    Waypoint(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Object(
            id,
            ObjectType::Waypoint,
            std::move(sceneName),
            game,
            services) {
    }

    static bool classof(Object *from) {
        return from->type() == ObjectType::Waypoint;
    }

    void loadFromBlueprint(const std::string &resRef);
    void deserialize(const resource::Gff &gff);

    bool isMapNoteEnabled() const { return _mapNoteEnabled; }

    const std::string &mapNote() const { return _mapNote.str(); }

private:
    // Serializable
    bool _hasMapNote {false};
    bool _mapNoteEnabled {false};
    resource::LocString _mapNote;
    resource::LocString _locName;
    // END Serializable

    void deserializeAll(const resource::Gff &gff);
};

} // namespace game

} // namespace reone
