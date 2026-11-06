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

#include "reone/scene/node/grass.h"
#include "reone/scene/node/model.h"
#include "reone/scene/node/walkmesh.h"
#include "reone/scene/user.h"

namespace reone {

namespace game {

class Object;

class Room : public scene::IUser {
public:
    /** The corners of a box that holds nothing, which every mesh widens. */
    static constexpr glm::vec3 kEmptyBoxMin {100000.0f};
    static constexpr glm::vec3 kEmptyBoxMax {-100000.0f};

    Room(
        std::string name,
        glm::vec3 position,
        std::shared_ptr<scene::ModelSceneNode> model,
        std::shared_ptr<scene::WalkmeshSceneNode> walkmesh,
        std::shared_ptr<scene::GrassSceneNode> grass) :
        _name(std::move(name)),
        _position(std::move(position)),
        _model(std::move(model)),
        _walkmesh(std::move(walkmesh)),
        _grass(std::move(grass)) {
        computeBox();
    }

    bool isVisible() const { return _visible; }
    /**
     * Whether the room's box holds a point seen from above, edges included.
     * The box spans the room's meshes, but for animated meshes and background
     * geometry; a room without such a mesh, or whose model has no walkmesh,
     * holds no point.
     */
    bool boxHolds(const glm::vec2 &point) const;
    /** The room's box (see boxHolds). */
    graphics::AABB box() const { return graphics::AABB(_boxMin, _boxMax); }
    /** Whether the room has background geometry. */
    bool hasBackgroundGeometry() const { return _backgroundGeometry; }

    const std::string &name() const { return _name; }
    const glm::vec3 &position() const { return _position; }

    /** Shows or hides the room's model and grass; its walkmesh and the objects in it are left as they are. */
    void setVisible(bool visible);

    // Scene Nodes

    std::shared_ptr<scene::ModelSceneNode> model() const { return _model; }
    std::shared_ptr<scene::WalkmeshSceneNode> walkmesh() const { return _walkmesh; }
    std::shared_ptr<scene::GrassSceneNode> grass() const { return _grass; }

    // END Scene Nodes

    // Tenants

    /** The objects found standing over the room (see Area::determineObjectRoom); it does not decide what is drawn. */
    void addTenant(Object *object);
    void removeTenant(Object *object);
    const std::set<Object *> tenants() const { return _tenants; }

    // END Tenants

private:
    std::string _name;
    glm::vec3 _position {0.0f};
    /** See boxHolds. */
    glm::vec3 _boxMin {kEmptyBoxMin};
    glm::vec3 _boxMax {kEmptyBoxMax};
    /** See hasBackgroundGeometry. */
    bool _backgroundGeometry {false};

    std::set<Object *> _tenants;
    bool _visible {true};

    // Scene Nodes

    std::shared_ptr<scene::ModelSceneNode> _model;
    std::shared_ptr<scene::WalkmeshSceneNode> _walkmesh;
    std::shared_ptr<scene::GrassSceneNode> _grass;

    // END Scene Nodes

    void computeBox();
};

} // namespace game

} // namespace reone
