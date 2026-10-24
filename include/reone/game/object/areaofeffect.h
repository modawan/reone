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

#pragma once

#include <array>
#include <functional>
#include <set>

#include "reone/resource/gff.h"

#include "../object.h"

namespace reone {

namespace game {

class Creature;

/**
 * A persistent area of effect: a circle or rectangle, from a vfx_persistent
 * row, that runs a script when a creature enters or leaves it and a heartbeat
 * script every six seconds. It stands at a point, or goes with the creature
 * that carries it.
 */
class AreaOfEffect : public Object {
public:
    /** Struct type of an AreaEffectList record. */
    static constexpr uint32_t kSaveStructType = 13;

    enum class Shape {
        Circle = 0,
        Rectangle = 1
    };

    AreaOfEffect(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Object(
            id,
            ObjectType::AreaOfEffect,
            std::move(sceneName),
            game,
            services) {
    }

    static bool classof(const Object *from) {
        return from->type() == ObjectType::AreaOfEffect;
    }

    void deserialize(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);

    void update(float dt) override;
    void resolveSavedReferences(
        const std::function<std::shared_ptr<Object>(uint32_t)> &resolver) override;

    /** Shape, scripts and tag of a vfx_persistent row. */
    void loadAreaEffect(int areaEffectId);
    /** Scripts given with the effect replace the row's; an empty name keeps the row's. */
    void overrideScripts(const std::string &onEnter, const std::string &heartbeat, const std::string &onExit);
    /**
     * The creator, and, when it is a creature running a spell, the spell's
     * metamagic, save DC and caster level as they are now.
     */
    void setCreator(const std::shared_ptr<Object> &creator);
    /** The creature that carries this area of effect. */
    void setCarrier(const std::shared_ptr<Creature> &carrier);
    /** Only a temporary duration runs out. */
    void setDuration(DurationType type, float seconds);

    std::shared_ptr<Object> creator() const { return savedReference(kCreatorReference); }
    std::shared_ptr<Object> carrier() const { return _carrier.boundObject(); }
    std::shared_ptr<Object> lastEntered() const { return _lastEntered.boundObject(); }
    std::shared_ptr<Object> lastLeft() const { return _lastLeft.boundObject(); }
    int spellSaveDC() const { return _spellSaveDC; }
    int spellLevel() const { return _spellLevel; }

    /** The point the shape is centred on: a carried circle stands on its carrier. */
    glm::vec3 center() const;
    /** Radius of the circle, or of the circle around the rectangle. */
    float boundingRadius() const { return _radius; }
    bool contains(const glm::vec3 &point) const;
    /**
     * Fix a rectangle's corners where it is placed, its length along the
     * facing and centred on the position; they stay there when it later jumps.
     */
    void fixCorners();

    /** Test the creatures of the area against the shape, signalling each entry and exit when announce is set. */
    void scanOccupants(bool announce = true);
    /** Note where a creature now stands, signalling an entry or exit when announce is set. */
    void updateOccupancy(const std::shared_ptr<Creature> &creature, bool announce);
    /** Forget a creature leaving the world, without running OnExit. */
    void forgetOccupant(uint32_t objectId) { _occupants.erase(objectId); }
    /** The carrier jumped: occupants leave, the shape moves to it, and the creatures there enter. */
    void jumpToCarrier();
    /** The area of effect is going: OnExit runs at once for every occupant still inside. */
    void releaseOccupants();

    /** A creature entered: it is the last to enter and the enter script runs. */
    void receiveEnteredSignal(const std::shared_ptr<Object> &entering);
    /** A creature left: it is the last to leave and the exit script runs. */
    void receiveExitedSignal(const std::shared_ptr<Object> &exiting);

private:
    friend class ModuleSnapshotBuilder;

    static constexpr const char *kCreatorReference = "CreatorId";
    /** The save DC of an area of effect no spell set. */
    static constexpr int kDefaultSpellSaveDC = 14;

    /** Serializable */
    int _areaEffectId {0};
    Shape _shape {Shape::Circle};
    float _radius {0.0f};
    float _width {0.0f};
    float _length {0.0f};
    int _metaMagic {0};
    int _spellSaveDC {kDefaultSpellSaveDC};
    int _spellLevel {0};
    DurationType _durationType {DurationType::Permanent};
    /** Seconds, counted only when temporary. */
    float _remainingDuration {0.0f};
    std::string _onEnter;
    std::string _onExit;
    /** The creature that carries this area of effect; invalid for one standing at a point. */
    SavedObjectReference _carrier;
    SavedObjectReference _lastEntered;
    SavedObjectReference _lastLeft;
    // END Serializable

    std::set<uint32_t> _occupants;
    std::array<glm::vec2, 4> _corners {};

    void setShape(Shape shape, float radiusOrWidth, float length);
    /** A reference to a live object, or an invalid one for none. */
    SavedObjectReference referenceTo(const std::shared_ptr<Object> &object) const;
    bool isCarried() const { return !_carrier.isInvalid(); }
    /** Whether a creature noted inside still stands inside. */
    bool isOccupantInside(const Object &object) const;
    void followCarrier();
    void destroy();
};

} // namespace game

} // namespace reone
