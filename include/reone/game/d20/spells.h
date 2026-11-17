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
    /**
     * Whether a level-up can choose the power, the number of picks aside: a
     * named Force power (user type 1 or 5) the level-up screen offers, with a
     * level of 0 or more for \p clazz that the character's total level after
     * the level-up reaches, neither known through any class nor in
     * \p chosen, whose master powers and prerequisites are each known or
     * chosen.
     */
    virtual bool isLevelUpCandidate(
        SpellType type,
        const CreatureAttributes &attributes,
        const CreatureClass &clazz,
        const std::set<SpellType> &chosen) const = 0;
    virtual std::vector<SpellType> getLevelUpCandidates(
        const CreatureAttributes &attributes,
        const CreatureClass &clazz,
        const std::set<SpellType> &chosen) const = 0;
    /**
     * The entries of the powers screens: every spell, those the level-up
     * screen offers to \p clazz visible.
     *
     * @param ownedForms true to show also the forms a TSL character owns, as
     *        known, as the abilities screen does; the level-up screen leaves
     *        them out, having no level for any class
     */
    virtual std::vector<SpellDisplayEntry> getLevelUpDisplayEntries(
        const CreatureAttributes &attributes,
        const CreatureClass &clazz,
        const std::set<SpellType> &chosen,
        bool ownedForms = false) const = 0;
    /**
     * The powers an automatic level-up chooses, at most \p count of them, as
     * recommended for the character's side (recommendPowers), the lowest row
     * filling a pick no ranked power can take. A power the level-up screen
     * offers to \p clazz is a candidate when the character's total level
     * after the level-up reaches its level, it is neither known through any
     * class nor chosen or granted at this level, and each of its master
     * powers and prerequisites is chosen or granted at this level or passes
     * \p knownAndAffordable. A character other than the player character
     * never takes a power it cannot pick (isPickDenied).
     *
     * @param attributes the character before the level, with the powers
     *        granted at this level
     * @param clazz the class the level is gained in, the character's last
     * @param alignment the character's alignment, which picks the side
     * @param count the number of powers the level gains
     * @param playerCharacter whether the character levels as the player
     *        character
     * @param granted the powers granted at this level
     * @param knownAndAffordable whether the character, as it is before the
     *        level, knows the power and can pay its Force point cost now
     */
    virtual std::vector<SpellType> getAutoLevelUpPowers(
        const CreatureAttributes &attributes,
        const CreatureClass &clazz,
        Alignment alignment,
        int count,
        bool playerCharacter,
        const std::set<SpellType> &granted,
        const std::function<bool(SpellType)> &knownAndAffordable) const = 0;
    /**
     * Whether the power is kept from a creature: Affect Mind and Dominate are
     * kept from one that may not take them.
     *
     * @param mayPickRestricted whether the creature may take them: on the
     *        level-up screen one flagged as the player character, for a
     *        recommendation or an automatic level-up one levelling as the
     *        player character
     */
    static bool isPickDenied(SpellType power, bool mayPickRestricted) {
        return !mayPickRestricted && (power == SpellType::AffectMind || power == SpellType::Dominate);
    }

    /**
     * Adds the powers a TSL character is granted with its newest level, which
     * is in the last of its classes, to that class's powers: the forms of a
     * Jedi base class, one a level from the Jedi level they start at, and the
     * powers of a prestige class at the levels of the newest class. These
     * come on top of the powers the player picks. A power without a row in
     * the spell table is not granted. KotOR grants no powers this way.
     *
     * @return the powers granted, in the order granted, once for each grant:
     *         two Jedi base classes can grant the same form
     */
    virtual std::vector<SpellType> addGrantedPowers(CreatureAttributes &attributes) const = 0;
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
     * @param gameId the title, which decides the CastAnim codes it knows and
     *        whether powers are granted at a level
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
        const std::set<SpellType> &chosen,
        bool ownedForms = false) const override;
    std::vector<SpellType> getAutoLevelUpPowers(
        const CreatureAttributes &attributes,
        const CreatureClass &clazz,
        Alignment alignment,
        int count,
        bool playerCharacter,
        const std::set<SpellType> &granted,
        const std::function<bool(SpellType)> &knownAndAffordable) const override;
    std::vector<SpellType> addGrantedPowers(CreatureAttributes &attributes) const override;

private:
    /** The spells.2da rows, by spell. */
    std::unordered_map<SpellType, std::shared_ptr<Spell>> _spells;
    /** The ranges.2da rows, by row. */
    std::vector<RangeRow> _ranges;
    /** Whether the table is TSL's, which grants powers at a level. */
    bool _tsl {false};

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
