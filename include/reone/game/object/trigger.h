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

#include "reone/resource/format/gffreader.h"

#include "../object.h"

namespace reone {

namespace game {

class Trigger : public Object {
public:
    enum class DebugState {
        Default,
        Tested,
        Inside,
        Entered
    };

    Trigger(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Object(
            id,
            ObjectType::Trigger,
            std::move(sceneName),
            game,
            services) {
    }

    static bool classof(Object *from) {
        return from->type() == ObjectType::Trigger;
    }

    void loadFromBlueprint(const std::string &resRef);
    void deserialize(const resource::Gff &gff);

    void update(float dt) override;

    void addTenant(const std::shared_ptr<Object> &object);

    bool isIn(const glm::vec2 &point) const;
    bool isTenant(const std::shared_ptr<Object> &object) const;

    const std::vector<glm::vec3> &geometry() const { return _geometry; }
    DebugState debugState() const;
    glm::vec4 debugColor() const;

    void markDebugTested(bool inside);
    void markDebugEntered();

    const std::string &getOnEnter() const { return _onEnter; }
    const std::string &getOnExit() const { return _onExit; }

    const std::string &linkedToModule() const { return _linkedToModule; }
    const std::string &linkedTo() const { return _linkedTo; }

private:
    // Serializable
    std::string _onEnter;
    std::string _onExit;
    std::string _onDisarm;
    std::string _onTrapTriggered;

    uint8_t _trapType {0};
    bool _trapOneShot {true};
    std::string _linkedTo;
    uint8_t _linkedToFlags {0};
    std::string _linkedToModule;
    bool _autoRemoveKey {false};
    resource::LocString _locName;
    Faction _faction {Faction::Invalid};
    std::string _keyName;
    bool _trapDisarmable {false};
    bool _trapDetectable {false};
    int32_t _triggerType {0};
    float _highlightHeight {0.1f};
    uint16_t _loadScreenId {0};
    resource::LocString _transitionDestin;
    bool _setByPlayerParty {false};
    std::vector<glm::vec3> _geometry;
    // END Serializable

    std::set<std::shared_ptr<Object>> _tenants;
    float _debugTestAge {0.0f};
    float _debugInsideAge {0.0f};
    float _debugEnterAge {0.0f};

    void deserializeAll(const resource::Gff &gff);
    void loadAppearance();

    void syncDebugVisual();
};

} // namespace game

} // namespace reone
