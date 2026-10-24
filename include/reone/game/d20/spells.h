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

#include <functional>
#include <optional>
#include <vector>

#include "reone/resource/types.h"

#include "../types.h"

#include "spell.h"

namespace reone {

namespace resource {

class IStrings;
class ITwoDAs;
class ITextures;
class IAudioClips;
class IModels;

} // namespace resource

namespace game {

class CreatureAttributes;
class CreatureClass;

enum class SpellAvailability {
    Hidden,
    Known,
    Chosen,
    Selectable,
    LockedClassLevel,
    LockedMissingPrerequisite
};

struct SpellDisplayEntry {
    SpellType type;
    std::string name;
    std::string description;
    std::shared_ptr<graphics::Texture> icon;
    std::vector<SpellType> prerequisites;
    SpellAvailability availability {SpellAvailability::Hidden};
    bool visible {false};
    bool known {false};
    bool chosen {false};
    bool selectable {false};
    std::optional<SpellType> displayParent;
    SpellType chainRoot {SpellType::All};
    int visualDepth {0};
    int sourceOrder {0};
};

/**
 * A row of ranges.2da: the distances spells, perception, party following and
 * traps use; none where a cell is blank. Each use of a range says what a
 * blank one counts as.
 */
struct RangeRow {
    std::optional<float> primary;
    std::optional<float> secondary;
};

class ISpells {
public:
    virtual ~ISpells() = default;

    virtual std::shared_ptr<Spell> get(SpellType type) const = 0;

    /** A row of ranges.2da; fails outside the table. */
    virtual const RangeRow &range(int row) const = 0;

    /** A row of ranges.2da, or none outside the table. */
    virtual std::optional<std::reference_wrapper<const RangeRow>> findRange(int row) const = 0;
    /**
     * The powers a menu offers from \p candidates, one per slot in slot
     * order: of those \p offered admits, the highest priority of each slot,
     * the earlier candidate on equal priority. With \p mergeTiers a slot-21
     * power takes the place of a slot-20 entry and a slot-17 power that of a
     * slot-16 entry.
     */
    std::vector<std::shared_ptr<Spell>> getActionSpells(
        const std::vector<SpellType> &candidates, bool hostileMenu,
        const std::function<bool(const Spell &)> &offered, bool mergeTiers) const;
    virtual bool isLevelUpCandidate(
        SpellType type,
        const CreatureAttributes &attributes,
        const CreatureClass &clazz,
        const std::set<SpellType> &chosen) const = 0;
    virtual std::vector<SpellType> getLevelUpCandidates(
        const CreatureAttributes &attributes,
        const CreatureClass &clazz,
        const std::set<SpellType> &chosen) const = 0;
    virtual std::vector<SpellDisplayEntry> getLevelUpDisplayEntries(
        const CreatureAttributes &attributes,
        const CreatureClass &clazz,
        const std::set<SpellType> &chosen) const = 0;

    /**
     * Adds the powers a TSL character is granted with its newest level, which
     * is in the last of its classes, to that class's powers. These come on
     * top of the powers the player picks. A power without a row in the spell
     * table is not granted.
     *
     * @param tsl false in K1, which grants no powers this way
     */
    virtual void addGrantedPowers(CreatureAttributes &attributes, bool tsl) const = 0;
};

class Spells : public ISpells, boost::noncopyable {
public:
    Spells(
        resource::ITextures &textures,
        resource::IAudioClips &audioClips,
        resource::IModels &models,
        resource::IStrings &strings,
        resource::ITwoDAs &twoDas) :
        _textures(textures),
        _audioClips(audioClips),
        _models(models),
        _strings(strings),
        _twoDas(twoDas) {
    }

    /**
     * Loads every spells.2da row.
     *
     * @param gameId the title, which decides the CastAnim codes it knows
     */
    void init(resource::GameID gameId);

    std::shared_ptr<Spell> get(SpellType type) const override;
    const RangeRow &range(int row) const override;
    std::optional<std::reference_wrapper<const RangeRow>> findRange(int row) const override;
    bool isLevelUpCandidate(
        SpellType type,
        const CreatureAttributes &attributes,
        const CreatureClass &clazz,
        const std::set<SpellType> &chosen) const override;
    std::vector<SpellType> getLevelUpCandidates(
        const CreatureAttributes &attributes,
        const CreatureClass &clazz,
        const std::set<SpellType> &chosen) const override;
    std::vector<SpellDisplayEntry> getLevelUpDisplayEntries(
        const CreatureAttributes &attributes,
        const CreatureClass &clazz,
        const std::set<SpellType> &chosen) const override;
    void addGrantedPowers(CreatureAttributes &attributes, bool tsl) const override;

private:
    /** The spells.2da rows, by spell. */
    std::unordered_map<SpellType, std::shared_ptr<Spell>> _spells;
    /** The ranges.2da rows, by row. */
    std::vector<RangeRow> _ranges;

    // Services

    resource::ITextures &_textures;
    resource::IAudioClips &_audioClips;
    resource::IModels &_models;
    resource::IStrings &_strings;
    resource::ITwoDAs &_twoDas;

    // END Services
};

} // namespace game

} // namespace reone
