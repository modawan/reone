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

class ISpells {
public:
    virtual ~ISpells() = default;

    virtual std::shared_ptr<Spell> get(SpellType type) const = 0;
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

    void init();

    std::shared_ptr<Spell> get(SpellType type) const override;
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

private:
    std::unordered_map<SpellType, std::shared_ptr<Spell>> _spells;

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
