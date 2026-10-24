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

#include <boost/algorithm/string.hpp>

#include "reone/game/d20/attributes.h"
#include "reone/game/d20/class.h"
#include "reone/game/twodautil.h"
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

static bool isPickerVisible(const Spell &spell, const CreatureClass &clazz) {
    auto maybeRequirement = spell.getClassLevelRequirement(clazz.type());
    return spell.userType == kSpellUserTypeForcePower && maybeRequirement && *maybeRequirement >= 0;
}

static bool prerequisitesSatisfied(
    const Spell &spell,
    const CreatureAttributes &attributes,
    const std::set<SpellType> &chosen) {
    return std::all_of(spell.prerequisites.begin(), spell.prerequisites.end(), [&](SpellType prerequisite) {
        return attributes.hasSpell(prerequisite) || chosen.count(prerequisite) > 0;
    });
}

static int getVisualDepth(
    SpellType type,
    const std::unordered_map<SpellType, std::shared_ptr<Spell>> &spells,
    const std::set<SpellType> &visible,
    std::unordered_map<SpellType, int> &depths,
    std::set<SpellType> &visited) {
    auto maybeDepth = depths.find(type);
    if (maybeDepth != depths.end()) {
        return maybeDepth->second;
    }
    if (!visited.insert(type).second) {
        return 0;
    }

    int result = 0;
    auto spell = spells.at(type);
    for (auto prerequisite : spell->prerequisites) {
        if (visible.count(prerequisite) > 0) {
            result = std::max(result, getVisualDepth(prerequisite, spells, visible, depths, visited) + 1);
        }
    }

    visited.erase(type);
    depths.emplace(type, result);
    return result;
}

static std::optional<SpellType> getDisplayParent(
    const Spell &spell,
    const std::set<SpellType> &visible,
    const std::unordered_map<SpellType, int> &depths) {
    std::optional<SpellType> result;
    int deepest = -1;
    for (auto prerequisite : spell.prerequisites) {
        auto maybeDepth = depths.find(prerequisite);
        if (visible.count(prerequisite) > 0 && maybeDepth != depths.end() && maybeDepth->second > deepest) {
            result = prerequisite;
            deepest = maybeDepth->second;
        }
    }
    return result;
}

static SpellType getChainRoot(
    SpellType type,
    const std::unordered_map<SpellType, std::optional<SpellType>> &parents) {
    std::set<SpellType> visited;
    SpellType result = type;
    while (visited.insert(result).second) {
        auto maybeParent = parents.find(result);
        if (maybeParent == parents.end() || !maybeParent->second) {
            break;
        }
        result = *maybeParent->second;
    }
    return result;
}

// With merged tiers, a power of the higher slot of a pair takes the place of
// an entry in the lower one.
static constexpr int kLowerTierSlot1 = 16;
static constexpr int kHigherTierSlot1 = 17;
static constexpr int kLowerTierSlot2 = 20;
static constexpr int kHigherTierSlot2 = 21;

std::vector<std::shared_ptr<Spell>> ISpells::getActionSpells(
    const std::vector<SpellType> &candidates, bool hostileMenu,
    const std::function<bool(const Spell &)> &offered, bool mergeTiers) const {
    struct Entry {
        int slot;
        std::shared_ptr<Spell> spell;
    };
    std::vector<Entry> entries;
    for (SpellType type : candidates) {
        auto spell = get(type);
        if (!spell || !offered(*spell)) {
            continue;
        }
        const int slot = hostileMenu ? spell->hostileSlot : spell->friendlySlot;
        if (slot < 0) {
            continue;
        }
        auto it = entries.begin();
        for (; it != entries.end(); ++it) {
            const bool higherTier = mergeTiers && ((slot == kHigherTierSlot2 && it->slot == kLowerTierSlot2) ||
                                                   (slot == kHigherTierSlot1 && it->slot == kLowerTierSlot1));
            if (higherTier || it->slot == slot || it->slot > slot) {
                break;
            }
        }
        if (it == entries.end() || it->slot > slot) {
            entries.insert(it, Entry {slot, std::move(spell)});
        } else if (it->slot != slot || spell->menuPriority > it->spell->menuPriority) {
            *it = Entry {slot, std::move(spell)};
        }
    }
    std::vector<std::shared_ptr<Spell>> result;
    for (auto &entry : entries) {
        result.push_back(std::move(entry.spell));
    }
    return result;
}

/**
 * The cast animation of a spells.2da CastAnim cell, compared in lower case.
 * Only CastAnim picks a spell's clips; CatchAnim picks none. Fury, crush and
 * monsterfury are TSL codes; in KotOR they, like any other cell, are Other.
 */
static SpellCastAnimation parseCastAnimation(const std::string &value, resource::GameID gameId) {
    static const std::map<std::string, SpellCastAnimation> kCodes {
        {"self", SpellCastAnimation::Self}, {"dark", SpellCastAnimation::Dark}, {"up", SpellCastAnimation::Up}, {"area", SpellCastAnimation::Area}, {"point", SpellCastAnimation::Touch}, {"touch", SpellCastAnimation::Touch}, {"throw", SpellCastAnimation::Throw}, {"jump", SpellCastAnimation::Jump}, {"monster", SpellCastAnimation::Monster}};
    static const std::map<std::string, SpellCastAnimation> kTslCodes {
        {"fury", SpellCastAnimation::Fury}, {"crush", SpellCastAnimation::Crush}, {"monsterfury", SpellCastAnimation::MonsterFury}};
    const auto code = boost::algorithm::to_lower_copy(value);
    const auto found = kCodes.find(code);
    if (found != kCodes.end()) {
        return found->second;
    }
    if (gameId == resource::GameID::TSL) {
        const auto tslFound = kTslCodes.find(code);
        if (tslFound != kTslCodes.end()) {
            return tslFound->second;
        }
    }
    return SpellCastAnimation::Other;
}

// The ranges.2da row of a spells.2da range tag, by its first letter; a
// personal range reads the touch row.
static std::optional<int> getSpellRangeRow(const std::string &range) {
    static constexpr int kTouchRangeRow = 1;
    static constexpr int kShortRangeRow = 2;
    static constexpr int kMediumRangeRow = 3;
    static constexpr int kLongRangeRow = 4;
    static constexpr int kThrowRangeRow = 19;
    if (range.empty()) {
        return std::nullopt;
    }
    switch (range.front()) {
    case 'P':
    case 'T':
        return kTouchRangeRow;
    case 'S':
        return kShortRangeRow;
    case 'M':
        return kMediumRangeRow;
    case 'L':
        return kLongRangeRow;
    case 'W':
        return kThrowRangeRow;
    default:
        return std::nullopt;
    }
}

void Spells::init(resource::GameID gameId) {
    // A blank range stays blank; each use says what it counts as.
    const auto ranges = getRequiredTwoDA(_twoDas, "ranges");
    for (int row = 0; row < ranges->getRowCount(); ++row) {
        _ranges.push_back({ranges->getFloatOpt(row, "primaryrange"), ranges->getFloatOpt(row, "secondaryrange")});
    }

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
        uint32_t formMask = static_cast<uint32_t>(
            spells->getInt(row, "formmask"));
        bool hostile = spells->getBool(row, "hostilesetting");
        std::string projModel = spells->getString(row, "projmodel");

        auto spell = std::make_shared<Spell>();
        spell->catchTime = spells->getInt(row, "catchtime") / 1000.0f;
        spell->projectile = spells->getBool(row, "proj");
        const auto path = boost::algorithm::to_lower_copy(spells->getString(row, "projtype"));
        static const std::map<std::string, ProjectilePathType> paths {
            {"homing", ProjectilePathType::Homing}, {"ballistic", ProjectilePathType::Ballistic}, {"highballistic", ProjectilePathType::HighBallistic}, {"accelerating", ProjectilePathType::Accelerating}, {"spiral", ProjectilePathType::Spiral}, {"linked", ProjectilePathType::Linked}, {"bounce", ProjectilePathType::Bounce}, {"burst", ProjectilePathType::Burst}, {"grenade", ProjectilePathType::Grenade}};
        auto parsedPath = paths.find(path);
        if (parsedPath != paths.end()) {
            spell->projectilePath = parsedPath->second;
        }
        spell->projectileSpawn = boost::algorithm::to_lower_copy(spells->getString(row, "projspwnpoint"));
        spell->projectileOrientation = boost::algorithm::to_lower_copy(spells->getString(row, "projorientation"));
        spell->conjureVisuals = {spells->getString(row, "conjheadvisual"), spells->getString(row, "conjhandvisual"),
                                 spells->getString(row, "conjgrndvisual")};
        spell->castVisuals = {spells->getString(row, "castheadvisual"), spells->getString(row, "casthandvisual"),
                              spells->getString(row, "castgrndvisual")};
        spell->exclusion = spells->getHexInt(row, "exclusion");
        spell->type = type;
        spell->name = std::move(name);
        spell->description = std::move(description);
        spell->icon = std::move(icon);
        spell->pips = pips;
        spell->prerequisites = std::move(prerequisites);
        spell->masterSpell = masterSpell;
        spell->userType = userType;
        spell->innateLevel = spells->getInt(row, "inate", Spell::kBlankInnateLevel);
        spell->forcePointCost = spells->getInt(row, "forcepoints", 0);
        const std::string alignment = spells->getString(row, "goodevil");
        spell->alignment = alignment.empty() ? 'N' : alignment.front();
        for (const auto &[clazz, column] : kClassLevelColumns) {
            auto maybeRequirement = spells->getIntOpt(row, column);
            if (maybeRequirement) {
                spell->classLevelRequirements.emplace(clazz, *maybeRequirement);
            }
        }
        spell->category = category;
        spell->maxCR = spells->getInt(row, "maxcr");
        spell->impactScript = impactScript;
        spell->castAnimation = parseCastAnimation(castAnim, gameId);
        spell->castSound = castSound.empty() ? nullptr : _audioClips.get(castSound);
        spell->conjTime = conjTime;
        spell->castTime = castTime;
        spell->itemTargeting = itemTargeting;
        spell->requireItemMask = static_cast<uint32_t>(spells->getInt(row, "requireitemmask", 0));
        spell->forbidItemMask = static_cast<uint32_t>(spells->getInt(row, "forbiditemmask", 0));
        const auto rangeTag = spells->getString(row, "range");
        const auto rangeRow = getSpellRangeRow(rangeTag);
        if (rangeRow) {
            spell->range = this->range(*rangeRow).primary.value_or(0.0f);
        }
        spell->rangeTag = rangeTag;
        // Casting uses the primary range and the menu's minimum the secondary
        // one, both from the row of the tag's first letter.
        if (rangeRow) {
            spell->minimumRange = this->range(*rangeRow).secondary.value_or(0.0f);
        }
        spell->formMask = formMask;
        spell->hostile = hostile;
        spell->hostileSlot = spells->getInt(row, "forcehostile", -1);
        spell->friendlySlot = spells->getInt(row, "forcefriendly", -1);
        spell->menuPriority = spells->getInt(row, "forcepriority");
        spell->projModel = projModel.empty() ? nullptr : _models.get(projModel);
        _spells.insert(std::make_pair(type, std::move(spell)));
    }
}

const RangeRow &Spells::range(int row) const {
    validateTwoDARowIndex("ranges", row, static_cast<int>(_ranges.size()));
    return _ranges[row];
}

std::optional<std::reference_wrapper<const RangeRow>> Spells::findRange(int row) const {
    if (row < 0 || row >= static_cast<int>(_ranges.size())) {
        return std::nullopt;
    }
    return std::cref(_ranges[row]);
}

std::shared_ptr<Spell> Spells::get(SpellType type) const {
    auto it = _spells.find(type);
    return it != _spells.end() ? it->second : nullptr;
}

bool Spells::isLevelUpCandidate(
    SpellType type,
    const CreatureAttributes &attributes,
    const CreatureClass &clazz,
    const std::set<SpellType> &chosen) const {
    auto maybeSpell = _spells.find(type);
    if (maybeSpell == _spells.end() ||
        !isPickerVisible(*maybeSpell->second, clazz) ||
        attributes.hasSpell(type) ||
        chosen.count(type) > 0) {
        return false;
    }

    int targetClassLevel = attributes.getClassLevel(clazz.type()) + 1;
    auto maybeRequirement = maybeSpell->second->getClassLevelRequirement(clazz.type());
    return maybeRequirement && targetClassLevel >= *maybeRequirement &&
           prerequisitesSatisfied(*maybeSpell->second, attributes, chosen);
}

std::vector<SpellType> Spells::getLevelUpCandidates(
    const CreatureAttributes &attributes,
    const CreatureClass &clazz,
    const std::set<SpellType> &chosen) const {
    std::vector<SpellType> result;
    for (const auto &[type, spell] : _spells) {
        if (isLevelUpCandidate(type, attributes, clazz, chosen)) {
            result.push_back(type);
        }
    }
    std::sort(result.begin(), result.end(), [](SpellType left, SpellType right) {
        return static_cast<int>(left) < static_cast<int>(right);
    });
    return result;
}

std::vector<SpellDisplayEntry> Spells::getLevelUpDisplayEntries(
    const CreatureAttributes &attributes,
    const CreatureClass &clazz,
    const std::set<SpellType> &chosen) const {
    std::set<SpellType> visible;
    for (const auto &[type, spell] : _spells) {
        if (isPickerVisible(*spell, clazz)) {
            visible.insert(type);
        }
    }

    std::unordered_map<SpellType, int> depths;
    for (auto type : visible) {
        std::set<SpellType> visited;
        getVisualDepth(type, _spells, visible, depths, visited);
    }

    std::unordered_map<SpellType, std::optional<SpellType>> parents;
    for (auto type : visible) {
        parents.emplace(type, getDisplayParent(*_spells.at(type), visible, depths));
    }

    int targetClassLevel = attributes.getClassLevel(clazz.type()) + 1;
    std::vector<SpellDisplayEntry> result;
    for (const auto &[type, spell] : _spells) {
        SpellDisplayEntry entry;
        entry.type = type;
        entry.name = spell->name;
        entry.description = spell->description;
        entry.icon = spell->icon;
        entry.prerequisites = spell->prerequisites;
        entry.sourceOrder = static_cast<int>(type);
        entry.known = attributes.hasSpell(type);
        entry.chosen = chosen.count(type) > 0;
        entry.visible = visible.count(type) > 0;
        if (!entry.visible) {
            result.push_back(std::move(entry));
            continue;
        }

        entry.displayParent = parents.at(type);
        entry.chainRoot = getChainRoot(type, parents);
        entry.visualDepth = depths.at(type);
        auto maybeRequirement = spell->getClassLevelRequirement(clazz.type());
        if (entry.known) {
            entry.availability = SpellAvailability::Known;
        } else if (entry.chosen) {
            entry.availability = SpellAvailability::Chosen;
        } else if (maybeRequirement && targetClassLevel < *maybeRequirement) {
            entry.availability = SpellAvailability::LockedClassLevel;
        } else if (!prerequisitesSatisfied(*spell, attributes, chosen)) {
            entry.availability = SpellAvailability::LockedMissingPrerequisite;
        } else {
            entry.availability = SpellAvailability::Selectable;
            entry.selectable = true;
        }
        result.push_back(std::move(entry));
    }

    std::sort(result.begin(), result.end(), [](const SpellDisplayEntry &left, const SpellDisplayEntry &right) {
        if (left.visible != right.visible) {
            return left.visible;
        }
        if (!left.visible) {
            return left.sourceOrder < right.sourceOrder;
        }
        if (left.chainRoot != right.chainRoot) {
            return static_cast<int>(left.chainRoot) < static_cast<int>(right.chainRoot);
        }
        if (left.visualDepth != right.visualDepth) {
            return left.visualDepth < right.visualDepth;
        }
        return left.sourceOrder < right.sourceOrder;
    });
    return result;
}

} // namespace game

} // namespace reone
