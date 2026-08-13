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

#include <array>
#include <limits>
#include <optional>
#include <set>
#include <vector>

#include "reone/script/enginetype.h"

#include "types.h"

namespace reone {

namespace resource {
class Gff;
}

namespace game {

class Object;
class Game;

using EffectId = uint64_t;

constexpr EffectId kUnassignedEffectId = 0;
constexpr uint32_t kSavedEffectInvalidObjectId = 0x7f000000;

enum class EffectIdImportResult {
    Unassigned,
    Imported,
    Existing,
};

/** Runtime-session owner for the global exposed-effect identity namespace. */
class EffectIdNamespace {
public:
    static constexpr EffectId kFirstId = 1;

    EffectId allocate();
    EffectIdImportResult importId(EffectId id);
    bool setNextId(EffectId id);
    void reset();

    EffectId nextId() const { return _nextId; }
    bool contains(EffectId id) const { return _ids.count(id) != 0; }
    size_t size() const { return _ids.size(); }

private:
    EffectId _nextId {kFirstId};
    std::set<EffectId> _ids;
};

struct EffectInstance;

class Effect : public script::EngineType {
public:
    Effect(EffectType type) :
        _type(type) {
    }

    virtual void applyTo(Object &object);
    virtual bool onApply(Object &object);
    virtual void onRemove(Object &object);

    EffectType type() const { return _type; }

protected:
    EffectType _type;
};

/**
 * Semantic state of one applied effect.
 *
 * The executable Effect is optional: retail records whose behavior Reone does
 * not implement still remain typed, queryable and removable without losing
 * their saved identity or payload.
 */
struct EffectInstance {
    std::shared_ptr<Effect> effect;
    EffectId id {kUnassignedEffectId};
    uint16_t retailType {0};
    uint16_t subType {0};
    float duration {0.0f};
    /**
     * Derived live countdown. Saved Duration remains the semantic authored
     * duration; saved expiry day/time must be converted by the load coordinator.
     */
    std::optional<float> remainingDuration;
    uint32_t expiryDay {0};
    uint32_t expiryTime {0};
    uint32_t creatorId {0};
    uint32_t spellId {std::numeric_limits<uint32_t>::max()};
    int32_t exposed {0};
    bool skipOnLoad {false};
    std::vector<int32_t> integerParameters;
    std::array<float, 4> floatParameters {};
    std::array<std::string, 6> stringParameters {};
    std::array<uint32_t, 4> objectParameters {
        kSavedEffectInvalidObjectId,
        kSavedEffectInvalidObjectId,
        kSavedEffectInvalidObjectId,
        kSavedEffectInvalidObjectId,
    };
    std::weak_ptr<Object> creator;

    static EffectInstance fromGff(const resource::Gff &gff);

    DurationType durationType() const;
    uint16_t semanticSubType() const { return subType & 0x18; }
    bool hasStableId() const { return id != kUnassignedEffectId; }
    bool shouldRestoreOnLoad() const {
        return !skipOnLoad && durationType() != DurationType::Equipped;
    }

    std::shared_ptr<Object> boundCreator() const { return creator.lock(); }

private:
    friend class Game;
    bool bindCreator(const std::shared_ptr<Object> &object);
};

/**
 * A serialized EffectInstance used as an NWScript engine value.
 *
 * This deliberately reuses the save-facing effect model. Applying it through
 * Object::applyEffect preserves that semantic payload instead of inventing a
 * second, save-only effect representation.
 */
class SavedEffectValue : public Effect {
public:
    explicit SavedEffectValue(EffectInstance instance);
    const EffectInstance &instance() const { return _instance; }

private:
    EffectInstance _instance;
};

} // namespace game

} // namespace reone
