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

#include "reone/game/d20/spells.h"

#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/strings.h"

using namespace reone::graphics;
using namespace reone::audio;
using namespace reone::resource;

namespace reone {

namespace game {

static const std::vector<std::pair<ClassType, std::string>> kClassLevelColumns = {
    {ClassType::JediGuardian, "guardian"},
    {ClassType::JediConsular, "consular"},
    {ClassType::JediSentinel, "sentinel"},
    {ClassType::JediWeaponMaster, "weapmstr"},
    {ClassType::JediMaster, "jedimaster"},
    {ClassType::JediWatchman, "watchman"},
    {ClassType::SithMarauder, "marauder"},
    {ClassType::SithLord, "sithlord"},
    {ClassType::SithAssassin, "assassin"}};

static std::optional<SpellType> parseSpellType(const std::string &value) {
    if (value.empty()) {
        return std::nullopt;
    }

    try {
        size_t parsedLength = 0;
        int parsed = std::stoi(value, &parsedLength);
        if (parsedLength == value.size() && parsed >= 0) {
            return static_cast<SpellType>(parsed);
        }
    } catch (const std::exception &) {
    }

    return std::nullopt;
}

static std::vector<SpellType> parsePrerequisites(const std::string &value) {
    std::vector<SpellType> result;
    std::vector<std::string> tokens;
    boost::split(tokens, value, boost::is_any_of("_"));
    for (const auto &token : tokens) {
        auto maybeSpell = parseSpellType(token);
        if (maybeSpell) {
            result.push_back(*maybeSpell);
        }
    }
    return result;
}

void Spells::init() {
    std::shared_ptr<TwoDA> spells(_twoDas.get("spells"));
    if (!spells)
        return;

    for (int row = 0; row < spells->getRowCount(); ++row) {
        SpellType type = static_cast<SpellType>(row);
        std::string name(_strings.getText(spells->getInt(row, "name", -1)));
        std::string description(_strings.getText(spells->getInt(row, "spelldesc", -1)));
        std::shared_ptr<Texture> icon(_textures.get(spells->getString(row, "iconresref"), TextureUsage::GUI));
        uint32_t pips = spells->getHexInt(row, "pips");
        std::vector<SpellType> prerequisites(parsePrerequisites(spells->getString(row, "prerequisites")));
        std::optional<SpellType> masterSpell(parseSpellType(spells->getString(row, "masterspell")));
        int userType = spells->getInt(row, "usertype", -1);
        uint32_t category = spells->getHexInt(row, "category");
        std::string impactScript = spells->getString(row, "impactscript");
        std::string castAnim = spells->getString(row, "castanim");
        std::string castSound = spells->getString(row, "castsound");
        float conjTime = spells->getInt(row, "conjtime") / 1000.0f;
        float castTime = spells->getInt(row, "casttime") / 1000.0f;
        uint32_t itemTargeting = spells->getInt(row, "itemtargeting");
        bool hostile = spells->getBool(row, "hostilesetting");
        std::string projModel = spells->getString(row, "projmodel");

        auto spell = std::make_shared<Spell>();
        spell->type = type;
        spell->name = std::move(name);
        spell->description = std::move(description);
        spell->icon = std::move(icon);
        spell->pips = pips;
        spell->prerequisites = std::move(prerequisites);
        spell->masterSpell = masterSpell;
        spell->userType = userType;
        for (const auto &[clazz, column] : kClassLevelColumns) {
            auto maybeRequirement = spells->getIntOpt(row, column);
            if (maybeRequirement) {
                spell->classLevelRequirements.emplace(clazz, *maybeRequirement);
            }
        }
        spell->category = category;
        spell->impactScript = impactScript;
        spell->castAnim = castAnim;
        spell->castSound = castSound.empty() ? nullptr : _audioClips.get(castSound);
        spell->conjTime = conjTime;
        spell->castTime = castTime;
        spell->itemTargeting = itemTargeting;
        spell->hostile = hostile;
        spell->projModel = projModel.empty() ? nullptr : _models.get(projModel);
        _spells.insert(std::make_pair(type, std::move(spell)));
    }
}

std::shared_ptr<Spell> Spells::get(SpellType type) const {
    auto it = _spells.find(type);
    return it != _spells.end() ? it->second : nullptr;
}

} // namespace game

} // namespace reone
