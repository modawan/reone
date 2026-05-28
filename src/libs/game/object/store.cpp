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

#include "reone/game/object/store.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/strings.h"

using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace game {

void Store::deserialize(const resource::Gff &gff) {
    std::string templateRes;
    if (gff.readResRef(templateRes, "ResRef")) {
        if (auto utm = _services.resource.gffs.get(templateRes, ResType::Utm)) {
            deserializeAll(*utm);
        }
    }
    deserializeAll(gff);
    updateTransform();
}

void Store::deserializeAll(const resource::Gff &gff) {
    if (gff.readString(_tag, "Tag")) {
        boost::to_lower(_tag);
    }

    if (gff.readLocString(_locName, "LocalizedName", _services.resource.strings)) {
        _name = _locName.str();
    }

    gff.readInt(_markDown, "MarkDown");
    gff.readInt(_markUp, "MarkUp");
    gff.readResRef(_onOpenStore, "OnOpenStore");
    gff.readByte(_buySellFlag, "BuySellFlag");

    gff.readFloat(_position[0], "XPosition");
    gff.readFloat(_position[1], "YPosition");
    gff.readFloat(_position[2], "ZPosition");
    {
        float cosine, sine;
        if (gff.readFloat(cosine, "XOrientation") && gff.readFloat(sine, "YOrientation")) {
            _orientation = glm::quat(glm::vec3(0.0f, 0.0f, -glm::atan(cosine, sine)));
        }
    }

    gff.readBool(_commandable, "Commandable");

    deserializeItems(gff);
}

void Store::deserializeItems(const resource::Gff &gff) {
    for (const auto &itemGff : gff.getList("ItemList")) {
        std::shared_ptr<Item> item = _game.newItem();
        item->deserialize(*itemGff);
        addItem(item);
    }
}

} // namespace game

} // namespace reone
