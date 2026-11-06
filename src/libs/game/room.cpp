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

#include "reone/game/room.h"

#include <stack>

#include "reone/graphics/model.h"
#include "reone/graphics/modelnode.h"
#include "reone/graphics/types.h"
#include "reone/scene/node/model.h"

#include "reone/game/object.h"

using namespace reone::graphics;
using namespace reone::scene;

namespace reone {

namespace game {

void Room::addTenant(Object *object) {
    _tenants.insert(object);
}

void Room::removeTenant(Object *object) {
    _tenants.erase(object);
}

bool Room::boxHolds(const glm::vec2 &point) const {
    return point.x >= _boxMin.x && point.y >= _boxMin.y && _boxMax.x >= point.x && _boxMax.y >= point.y;
}

void Room::computeBox() {
    if (!_model) {
        return;
    }
    bool walkable = false;
    std::stack<std::reference_wrapper<const ModelNode>> nodes;
    nodes.push(*_model->model().rootNode());
    while (!nodes.empty()) {
        const ModelNode &node = nodes.top();
        nodes.pop();
        for (const auto &child : node.children()) {
            nodes.push(*child);
        }
        if (!node.isMesh() || (node.flags() & MdlNodeFlags::anim) != 0) {
            continue;
        }
        walkable = walkable || node.isAABBMesh();
        if (node.mesh()->backgroundGeometry) {
            _backgroundGeometry = true;
            continue;
        }
        for (const auto &vertex : node.mesh()->mesh->vertices()) {
            const glm::vec3 point {_position + glm::vec3(node.absoluteTransform() * glm::vec4(vertex.position, 1.0f))};
            _boxMin = glm::min(_boxMin, point);
            _boxMax = glm::max(_boxMax, point);
        }
    }
    // A room whose model has no walkmesh holds no point.
    if (!walkable) {
        _boxMin = kEmptyBoxMin;
        _boxMax = kEmptyBoxMax;
    }
}

void Room::setVisible(bool visible) {
    if (_visible == visible) {
        return;
    }
    if (_model) {
        _model->setEnabled(visible);
    }
    if (_grass) {
        _grass->setEnabled(visible);
    }
    _visible = visible;
}

} // namespace game

} // namespace reone
