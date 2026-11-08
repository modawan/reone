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

#include "reone/game/itemdescription.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/format.hpp>

#include "reone/game/d20/feat.h"
#include "reone/game/d20/feats.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"
#include "reone/game/twodautil.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/strings.h"

using namespace reone::resource;

namespace reone {

namespace game {

namespace {

// Interface strings of the property lines.
constexpr int kNoDescriptionStrRef = 32172;
constexpr int kAttributeRequirementsStrRef = 117597;
constexpr int kFirstAbilityRequirementStrRef = 117591;
constexpr int kFeatRequirementsStrRef = 38536;
constexpr int kGenderRequirementsStrRef = 47995;
constexpr int kSubraceRequirementsStrRef = 48013;
constexpr int kCharacterRequirementsStrRef = 48026;
constexpr int kPartyMemberRequirementsStrRef = 130590;
constexpr int kBaoDurNameStrRef = 102045;
constexpr int kDamageStrRef = 31385;
constexpr int kVersusStrRef = 1395;
constexpr int kPhysicalDamageStrRef = 38552;
constexpr int kRangeStrRef = 38565;
constexpr int kCriticalThreatStrRef = 38571;
constexpr int kOnHitStrRef = 38583;
constexpr int kChanceStrRef = 38574;
constexpr int kDurationStrRef = 47878;
constexpr int kTSLDifficultyClassStrRef = 48756;
constexpr int kK1DifficultyClassStrRef = 49135;
constexpr int kOffHandStrRef = 42145;
constexpr int kAttackModifierStrRef = 38566;
constexpr int kUnarmedAttackSuffixStrRef = 114160;
constexpr int kDefenseStrRef = 38593;
constexpr int kMaxDexterityBonusStrRef = 42144;
constexpr int kSingleUseStrRef = 783;
constexpr int kUsesStrRef = 41899;
constexpr int kUnlimitedUsesStrRef = 41900;
constexpr int kSavesStrRef = 41935;
constexpr int kSkillsStrRef = 41936;
constexpr int kVulnerabilityStrRef = 41898;
constexpr int kForceResistanceStrRef = 41930;
constexpr int kUseLimitationStrRef = 41934;
/** The header of an upgrade row's keyed properties. */
constexpr int kKeyedHeaderStrRef = 1545;
/** The attack bonus an enhancement names. */
constexpr int kEnhancementAttackStrRef = 375;

// The damage kinds of a weapon, named in this order.
constexpr std::pair<int, int> kDamageTypeNames[] {
    {static_cast<int>(DamageType::Physical), 38552},
    {static_cast<int>(DamageType::Universal), 38553},
    {static_cast<int>(DamageType::Acid), 38554},
    {static_cast<int>(DamageType::Cold), 38555},
    {static_cast<int>(DamageType::LightSide), 38556},
    {static_cast<int>(DamageType::Electrical), 38557},
    {static_cast<int>(DamageType::Fire), 38558},
    {static_cast<int>(DamageType::DarkSide), 38559},
    {static_cast<int>(DamageType::Sonic), 38560},
    {static_cast<int>(DamageType::Ion), 38561},
    {static_cast<int>(DamageType::Blaster), 38562},
    {static_cast<int>(DamageType::Poison), 41903}};

// The damage bonus subtypes (iprp_damagetype.2da) up to slashing are the
// physical kinds, which are listed as one.
constexpr int kLastPhysicalDamageSubtype = 2;

// Lightsaber crystals list no properties, nor do grenades and (in TSL) rockets.
constexpr int kCrystalItemType = 46;
// Droid plating shows no Dexterity limit.
constexpr int kDroidPlatingItemType = 15;
// TSL gauntlets list damage like a weapon.
constexpr int kGauntletsBaseItem = 45;
// Items whose description names Bao-Dur among the party member requirements.
// Bao-Dur also cannot equip base item 100, which is not on this list.
constexpr int kBaoDurItems[] {35, 36, 37, 97, 98, 99, 102};
constexpr int kBaoDurNpc = 1;

// Cost tables of the attribute limits and of the on-hit difficulty class.
constexpr int kAttributeCostTable = 26;
constexpr int kOnHitDifficultyCostTable = 25;

// The highest ability (iprp_abilities.2da) an attribute limit names; its
// interface strings follow the first ability's in order.
constexpr int kLastAbilitySubtype = 5;

// On-hit effects (iprp_onhit.2da) with lines of their own. TSL adds knockdown
// to the effects an item lists; KotOR lists them up to instant death.
constexpr int kOnHitAbilityDrain = 6;
constexpr int kOnHitPoison = 7;
constexpr int kOnHitSlayRacialGroup = 8;
constexpr int kOnHitSlayAlignmentGroup = 9;
constexpr int kOnHitInstantDeath = 10;
constexpr int kOnHitKnockdown = 11;

// On-hit and vulnerability durations are in rounds, which last three seconds.
constexpr int kSecondsPerRound = 3;

// A Dexterity limit of this value means none.
constexpr int kNoDexterityLimit = -1;

// The short property names a held upgrade's lines use: a property name
// replaced by a shorter one, or by none.
constexpr std::pair<int, int> kShortPropertyNames[] {
    {659, -1},     // Attribute Bonus -> none
    {662, 129935}, // Defense Bonus vs. Damage Type -> Defense vs. Damage
    {672, 129931}, // Damage Bonus -> Damage +
    {673, 129933}, // Damage Bonus vs. Alignment -> Damage vs. Align
    {675, 686},    // Damage Immunity -> Immunity
    {678, -1},     // Damage Resistance -> none
    {680, -1},     // Attribute Penalty -> none
    {700, 129930}, // Attack Bonus -> Attack +
    {705, -1},     // Alignment Limitation -> none
    {48014, -1}};  // PC Restriction -> none
// The subtype names the short names replace, by none or by physical damage.
constexpr std::pair<int, int> kShortSubtypeNames[] {
    {0, -1},                       // Bad StrRef -> none
    {727, -1},                     // Light Side -> none
    {728, -1},                     // Dark Side -> none
    {731, kPhysicalDamageStrRef},  // Slashing -> Physical
    {798, kPhysicalDamageStrRef}}; // Slashing -> Physical
// Subtype names that leave a property no line under the short names:
// Bludgeoning and Piercing, each twice.
constexpr int kLinelessSubtypeNames[] {729, 730, 796, 797};
// A held upgrade lists its properties up to this many lines.
constexpr int kMaxPropertyLines = 5;

// The name a table of short names puts in place of an interface string, or
// the string itself.
template <size_t N>
int shortName(const std::pair<int, int> (&names)[N], int strRef) {
    const auto it = std::find_if(std::begin(names), std::end(names), [strRef](const auto &name) {
        return name.first == strRef;
    });
    return it != std::end(names) ? it->second : strRef;
}

// Which names a property's strings use: its full names, or the short names
// of a held upgrade's lines.
enum class PropertyNaming {
    Full,
    Short
};

struct PropertyStrings {
    std::string name;
    std::string subtype;
    std::string cost;
    std::string param;
};

// The chance, duration and difficulty class of an on-hit effect.
struct OnHitOdds {
    int chance {0};
    int durationRounds {0};
    int difficulty {0};
};

class ItemDescriptionBuilder {
public:
    ItemDescriptionBuilder(const Item &item, const Game &game, ServicesView &services) :
        _item(item),
        _game(game),
        _services(services),
        _tsl(game.isTSL()) {
        for (const auto &property : item.properties()) {
            if (item.isPropertyActive(property)) {
                _active.push_back(&property);
            }
        }
    }

    std::string build() {
        const int itemType = _item.itemType();
        if (itemType != kCrystalItemType && itemType != Item::kGrenadeItemType &&
            !(_tsl && itemType == Item::kRocketItemType)) {
            if (_tsl) {
                addAttributeRequirements();
            }
            addFeatRequirements();
            if (_tsl) {
                addNamedRequirements(ItemProperty::LimitUseByGender, kGenderRequirementsStrRef, "gender");
                addNamedRequirements(ItemProperty::LimitUseBySubrace, kSubraceRequirementsStrRef, "subrace");
                addCharacterRequirements();
                addPartyMemberRequirements();
            }
            const bool gauntlets = _tsl && _item.baseItemType() == kGauntletsBaseItem;
            if (_item.weaponType() != WeaponType::None || gauntlets) {
                addDamage();
                addRange();
                if (!gauntlets) {
                    addCriticalThreat();
                }
                addOnHit();
                addWeaponSize();
            }
            addAttackModifier();
            addDefense();
            addUses();
            addImmunities();
            addSaves();
            addSkills();
            if (_tsl) {
                addDeflection();
            }
            addOtherProperties();
        }
        std::string description(_item.descIdentified());
        if (description.empty()) {
            description = gui(kNoDescriptionStrRef);
        }
        return _text + description;
    }

    /**
     * A line for each property chosen, in the short names, up to five lines:
     * its name, then its cost, straight after a name ending in a plus and
     * after a colon otherwise, then its subtype, after a comma when both
     * come before it, a colon after a name alone and a space after a cost
     * alone. A property with none of the three has no line.
     */
    std::string buildPropertyLines(const std::function<bool(const Item::PropertyEntry &)> &include) const {
        std::string text;
        int lineCount = 0;
        for (const auto &property : _item.properties()) {
            if (lineCount == kMaxPropertyLines) {
                break;
            }
            if (!include(property)) {
                continue;
            }
            const auto strings = propertyStrings(type(property), property.subtype, property.costValue, std::nullopt, PropertyNaming::Short);
            std::string line = strings.name;
            if (!strings.cost.empty()) {
                if (!strings.name.empty() && strings.name.back() != '+') {
                    line += ": ";
                }
                line += strings.cost;
            }
            if (!strings.subtype.empty()) {
                if (!strings.name.empty()) {
                    line += strings.cost.empty() ? ": " : ", ";
                } else if (!strings.cost.empty()) {
                    line += " ";
                }
                line += strings.subtype;
            }
            if (!line.empty()) {
                text += line + "\n";
                ++lineCount;
            }
        }
        return text;
    }

    /**
     * The properties keyed to an upgrade row, active or not: a header, then a
     * line for each property in ascending order, every line ending in a line
     * break; nothing when no property is keyed to it. An enhancement names its
     * attack and damage bonus, and an enhancement of no bonus an empty line.
     */
    std::string buildKeyedText(int row) const {
        std::vector<std::string> lines;
        for (const auto &property : _item.properties()) {
            if (property.upgradeType != row) {
                continue;
            }
            if (type(property) == static_cast<int>(ItemProperty::EnhancementBonus)) {
                std::string line;
                if (property.costValue > 0) {
                    const auto bonus = " +" + std::to_string(property.costValue);
                    line = gui(kEnhancementAttackStrRef) + bonus + ", " + gui(kDamageStrRef) + bonus;
                }
                lines.push_back(std::move(line));
                continue;
            }
            if (auto line = keyedPropertyLine(property)) {
                lines.push_back(std::move(*line));
            }
        }
        if (lines.empty()) {
            return "";
        }
        std::sort(lines.begin(), lines.end());
        std::string text = gui(kKeyedHeaderStrRef) + ":\n";
        for (const auto &line : lines) {
            text += line + "\n";
        }
        return text;
    }

private:
    const Item &_item;
    const Game &_game;
    ServicesView &_services;
    bool _tsl;
    // Active properties, in list order.
    std::vector<const Item::PropertyEntry *> _active;
    std::string _text;

    std::string gui(int strRef) const {
        return _game.getInterfaceText(strRef);
    }

    std::shared_ptr<TwoDA> requiredTable(const std::string &resRef) const {
        return getRequiredTwoDA(_services.resource.twoDas, boost::to_lower_copy(resRef));
    }

    std::shared_ptr<TwoDA> costTable(int index) const {
        return requiredTable(requiredTable("iprp_costtable")->getString(index, "name"));
    }

    std::shared_ptr<TwoDA> paramTable(int index) const {
        return requiredTable(requiredTable("iprp_paramtable")->getString(index, "tableresref"));
    }

    static int type(const Item::PropertyEntry &property) {
        return static_cast<int>(property.propertyName);
    }

    bool hasProperty(ItemProperty kind) const {
        return std::any_of(_active.begin(), _active.end(), [kind](const Item::PropertyEntry *property) {
            return type(*property) == static_cast<int>(kind);
        });
    }

    // Another active property of the same kind and subtype has a higher value.
    bool isOutvalued(const Item::PropertyEntry &property) const {
        return std::any_of(_active.begin(), _active.end(), [&property](const Item::PropertyEntry *other) {
            return other->propertyName == property.propertyName && other->subtype == property.subtype &&
                   other->costValue > property.costValue;
        });
    }

    /**
     * The name of a property and the names of its subtype, cost and
     * parameter. A property the table does not name shows no text; a subtype
     * without a name of its own is shown with its property's name. No cost
     * or parameter value asks for no cost or parameter text. Short naming replaces
     * some property and subtype names, and some subtypes then leave the
     * property no text at all.
     */
    PropertyStrings propertyStrings(int kind, std::optional<int> subtype, std::optional<int> costValue, std::optional<int> paramValue, PropertyNaming naming) const {
        PropertyStrings strings;
        auto definitions = requiredTable("itempropdef");
        auto nameStrRef = definitions->getIntOpt(kind, "name");
        if (!nameStrRef) {
            return strings;
        }
        if (naming == PropertyNaming::Short) {
            nameStrRef = shortName(kShortPropertyNames, *nameStrRef);
        }
        if (*nameStrRef != -1) {
            strings.name = gui(*nameStrRef);
        }
        std::shared_ptr<TwoDA> subtypes;
        const std::string subtypeResRef(definitions->getString(kind, "subtyperesref"));
        if (!subtypeResRef.empty() && subtype) {
            subtypes = requiredTable(subtypeResRef);
            int subtypeStrRef = subtypes->getIntOpt(*subtype, "name").value_or(*nameStrRef);
            if (naming == PropertyNaming::Short) {
                if (std::find(std::begin(kLinelessSubtypeNames), std::end(kLinelessSubtypeNames), subtypeStrRef) != std::end(kLinelessSubtypeNames)) {
                    return {};
                }
                subtypeStrRef = shortName(kShortSubtypeNames, subtypeStrRef);
            }
            strings.subtype = gui(subtypeStrRef);
        }
        const auto costTableIndex = definitions->getIntOpt(kind, "costtableresref");
        if (costValue && costTableIndex) {
            const auto costStrRef = costTable(*costTableIndex)->getIntOpt(*costValue, "name");
            if (costStrRef && *costStrRef > 0) {
                strings.cost = gui(*costStrRef);
            }
        }
        // The parameter table is the property's, or else its subtype's.
        auto paramTableIndex = definitions->getIntOpt(kind, "param1resref");
        if (!paramTableIndex && subtypes) {
            paramTableIndex = subtypes->getIntOpt(*subtype, "param1resref");
        }
        if (paramValue && paramTableIndex) {
            const auto paramStrRef = paramTable(*paramTableIndex)->getIntOpt(*paramValue, "name");
            if (paramStrRef) {
                strings.param = gui(*paramStrRef);
            }
        }
        return strings;
    }

    /**
     * A keyed property's line: its name, a colon when it has a subtype,
     * cost or parameter table, then the names of its subtype, cost and
     * parameter, each after a space and joined by commas. A subtype's own
     * cost or parameter table replaces the property's; the physical damage
     * kinds are named as one. None when the property has no name or its
     * subtype table does not load.
     */
    std::optional<std::string> keyedPropertyLine(const Item::PropertyEntry &property) const {
        auto definitions = requiredTable("itempropdef");
        const int kind = type(property);
        const auto nameStrRef = definitions->getIntOpt(kind, "name");
        if (!nameStrRef) {
            return std::nullopt;
        }
        const auto subtypeResRef = definitions->getString(kind, "subtyperesref");
        int costTableIndex = definitions->getIntOpt(kind, "costtableresref").value_or(0);
        auto paramTableIndex = definitions->getIntOpt(kind, "param1resref");
        std::string line = gui(*nameStrRef);
        if (!subtypeResRef.empty() || costTableIndex != 0 || paramTableIndex) {
            line += ":";
        }
        if (!subtypeResRef.empty()) {
            const auto subtypes = _services.resource.twoDas.get(boost::to_lower_copy(subtypeResRef));
            if (!subtypes) {
                return std::nullopt;
            }
            const int subtype = property.subtype;
            if (const int subtypeCostTable = subtypes->getIntOpt(subtype, "costtableresref").value_or(0); subtypeCostTable != 0) {
                costTableIndex = subtypeCostTable;
            }
            if (const auto subtypeParamTable = subtypes->getIntOpt(subtype, "param1resref")) {
                paramTableIndex = subtypeParamTable;
            }
            const bool physical = kind == static_cast<int>(ItemProperty::DamageBonus) && subtype <= kLastPhysicalDamageSubtype;
            line += " " + gui(physical ? kPhysicalDamageStrRef : subtypes->getIntOpt(subtype, "name").value_or(-1));
            if (costTableIndex != 0 || paramTableIndex) {
                line += ",";
            }
        }
        if (costTableIndex != 0) {
            line += " " + gui(costTable(costTableIndex)->getIntOpt(property.costValue, "name").value_or(-1));
            if (paramTableIndex) {
                line += ",";
            }
        }
        if (paramTableIndex) {
            line += " " + gui(paramTable(*paramTableIndex)->getIntOpt(property.paramValue, "name").value_or(-1));
        }
        return line;
    }

    // The damage a damage cost value adds, as {minimum, maximum}. A row
    // without dice adds a fixed amount, its cost value.
    std::pair<int, int> damageRange(int costValue) const {
        auto costs = requiredTable("iprp_damagecost");
        const auto dice = costs->getIntOpt(costValue, "numdice");
        if (!dice) {
            return {costValue, costValue};
        }
        if (*dice == 0) {
            return {1, costValue};
        }
        return {*dice, *dice * costs->getInt(costValue, "die")};
    }

    OnHitOdds onHitOdds(int costValue, int paramValue) const {
        auto durations = requiredTable("iprp_onhitdur");
        OnHitOdds odds;
        odds.chance = durations->getInt(paramValue, "effectchance");
        odds.durationRounds = durations->getInt(paramValue, "durationrounds");
        odds.difficulty = costTable(kOnHitDifficultyCostTable)->getInt(costValue, "value");
        return odds;
    }

    std::string damageTypes(int flags) const {
        std::string result;
        for (const auto &[mask, strRef] : kDamageTypeNames) {
            if ((flags & mask) == 0) {
                continue;
            }
            if (!result.empty()) {
                result += ", ";
            }
            result += gui(strRef);
        }
        return result;
    }

    // Damage bonus subtype n is the damage kind of flag 1 << n.
    static bool hasDamageType(int flags, int subtype) {
        return (flags & (1 << subtype)) != 0;
    }

    int difficultyClassStrRef() const {
        return _tsl ? kTSLDifficultyClassStrRef : kK1DifficultyClassStrRef;
    }

    // Requirements

    void addAttributeRequirements() {
        if (!hasProperty(ItemProperty::LimitUseByAttribute)) {
            return;
        }
        auto values = costTable(kAttributeCostTable);
        bool listed = false;
        for (const auto *property : _active) {
            if (type(*property) != static_cast<int>(ItemProperty::LimitUseByAttribute)) {
                continue;
            }
            if (!listed) {
                _text += gui(kAttributeRequirementsStrRef) + ":\n";
                listed = true;
            }
            if (property->subtype <= kLastAbilitySubtype) {
                _text += gui(kFirstAbilityRequirementStrRef + property->subtype) + ": ";
            }
            _text += std::to_string(values->getInt(property->costValue, "value")) + "\n";
        }
        if (listed) {
            _text += "\n";
        }
    }

    // A feat name's colon reads " - ". A feat.2da row without a name is no
    // feat and names nothing.
    std::string featName(FeatType type) const {
        const auto feat = _services.game.feats.get(type);
        std::string name(feat ? gui(feat->nameStrRef) : std::string());
        const size_t colon = name.find(':');
        if (colon != std::string::npos) {
            name = name.substr(0, colon) + " - " + name.substr(colon + 1);
        }
        return name;
    }

    // Of the armour proficiencies an item asks for, only the highest is listed.
    static bool isArmourProficiency(FeatType feat) {
        return feat == FeatType::ArmourProfLight || feat == FeatType::ArmourProfMedium || feat == FeatType::ArmourProfHeavy;
    }

    void addFeatRequirements() {
        const auto &feats = _item.requiredFeats();
        auto armour = FeatType::ArmourProfLight;
        for (auto feat : feats) {
            if (feat == FeatType::ArmourProfMedium && armour == FeatType::ArmourProfLight) {
                armour = FeatType::ArmourProfMedium;
            }
            if (feat == FeatType::ArmourProfHeavy) {
                armour = FeatType::ArmourProfHeavy;
            }
        }
        bool listed = false;
        const auto listFeat = [this, &listed](FeatType feat) {
            if (!listed) {
                _text += gui(kFeatRequirementsStrRef) + ":\n";
                listed = true;
            }
            _text += featName(feat) + "\n";
        };
        for (auto feat : feats) {
            if (feat == FeatType::Invalid) {
                continue;
            }
            if (isArmourProficiency(feat) && feat != armour) {
                continue;
            }
            listFeat(feat);
        }
        for (const auto *property : _active) {
            if (type(*property) == static_cast<int>(ItemProperty::UseLimitationFeat) && property->subtype != 0) {
                listFeat(static_cast<FeatType>(property->subtype));
            }
        }
        if (listed) {
            _text += "\n";
        }
    }

    void addNamedRequirements(ItemProperty kind, int headerStrRef, const std::string &tableName) {
        if (!hasProperty(kind)) {
            return;
        }
        auto names = requiredTable(tableName);
        bool listed = false;
        for (const auto *property : _active) {
            if (type(*property) != static_cast<int>(kind)) {
                continue;
            }
            if (!listed) {
                _text += gui(headerStrRef) + ":\n";
                listed = true;
            }
            _text += gui(names->getInt(property->subtype, "name", -1)) + "\n";
        }
        if (listed) {
            _text += "\n";
        }
    }

    // Subtype 0 names the player character.
    void addCharacterRequirements() {
        if (!hasProperty(ItemProperty::LimitUseByPc)) {
            return;
        }
        auto characters = requiredTable("iprp_pc");
        bool listed = false;
        for (const auto *property : _active) {
            if (type(*property) != static_cast<int>(ItemProperty::LimitUseByPc)) {
                continue;
            }
            if (!listed) {
                _text += gui(kCharacterRequirementsStrRef) + ":\n";
                listed = true;
            }
            _text += (property->subtype != 0 ? gui(characters->getInt(property->subtype, "name", -1))
                                             : _game.party().playerCharacterName()) +
                     "\n";
        }
        if (listed) {
            _text += "\n";
        }
    }

    // Once Bao-Dur has joined, the listed items name him.
    void addPartyMemberRequirements() {
        if (!_game.party().influence(kBaoDurNpc)) {
            return;
        }
        const int baseItem = _item.baseItemType();
        if (std::find(std::begin(kBaoDurItems), std::end(kBaoDurItems), baseItem) == std::end(kBaoDurItems)) {
            return;
        }
        _text += gui(kPartyMemberRequirementsStrRef) + ":\n" + gui(kBaoDurNameStrRef) + "\n\n";
    }

    // Weapon lines

    void addDamage() {
        if (hasProperty(ItemProperty::NoDamage)) {
            _text += gui(kDamageStrRef) + (_tsl ? ": 1\n" : ": 0\n");
        } else {
            // Enhancement and damage of the weapon's own kinds raise its
            // range, damage penalties lower it. In TSL the minimum is at
            // least 1 and a weapon without damage shows no line.
            const int flags = _item.damageFlags();
            int minimum = _item.numDice();
            int maximum = _item.numDice() * _item.dieToRoll();
            const std::string kinds(damageTypes(flags));
            for (const auto *property : _active) {
                switch (static_cast<ItemProperty>(type(*property))) {
                case ItemProperty::DecreasedDamage:
                    minimum -= property->costValue;
                    maximum -= property->costValue;
                    break;
                case ItemProperty::DamageBonus:
                    if (hasDamageType(flags, property->subtype)) {
                        const auto [low, high] = damageRange(property->costValue);
                        minimum += low;
                        maximum += high;
                    }
                    break;
                case ItemProperty::EnhancementBonus:
                    minimum += property->costValue;
                    maximum += property->costValue;
                    break;
                default:
                    break;
                }
            }
            if (_tsl && minimum <= 0) {
                minimum = 1;
            }
            if (!_tsl || maximum != 0) {
                _text += gui(kDamageStrRef) + ": ";
                if (_tsl && _item.baseItemType() == kGauntletsBaseItem) {
                    if (!kinds.empty()) {
                        _text += kinds + " ";
                    }
                    _text += "+" + std::to_string(maximum) + "\n\n";
                } else {
                    if (!kinds.empty()) {
                        _text += kinds + ", ";
                    }
                    _text += str(boost::format("%d-%d\n\n") % minimum % maximum);
                }
            }
        }
        addGroupDamage(ItemProperty::DamageBonusVsAlignmentGroup, _tsl ? "\n" : "\n\n");
        addGroupDamage(ItemProperty::DamageBonusVsRacialGroup, "\n\n");
        addDamageReduction();
        addExtraDamage();
    }

    static std::string bonusRange(int minimum, int maximum) {
        return minimum == maximum ? str(boost::format("+%d") % maximum) : str(boost::format("+%d-%d") % minimum % maximum);
    }

    // The bonuses against groups are summed into one line, named for the
    // last of them.
    void addGroupDamage(ItemProperty kind, const std::string &end) {
        const Item::PropertyEntry *last = nullptr;
        int minimum = 0;
        int maximum = 0;
        for (const auto *property : _active) {
            if (type(*property) != static_cast<int>(kind)) {
                continue;
            }
            const auto [low, high] = damageRange(property->costValue);
            minimum += low;
            maximum += high;
            last = property;
        }
        if (!last) {
            return;
        }
        const auto strings = propertyStrings(static_cast<int>(kind), last->subtype, std::nullopt, last->paramValue, PropertyNaming::Full);
        _text += strings.param + ": " + bonusRange(minimum, maximum) + " " + gui(kVersusStrRef) + " " + strings.subtype + end;
    }

    // The amount a name ends with, as in "Penalty -2" or "Bonus +3", read
    // with the pattern given; a name that holds none gives 0.
    static int namedAmount(const std::string &name, const std::string &pattern) {
        int amount = 0;
        std::sscanf(name.c_str(), pattern.c_str(), &amount);
        return amount;
    }

    void addDamageReduction() {
        for (const auto *property : _active) {
            if (type(*property) != static_cast<int>(ItemProperty::DamageReduction) || isOutvalued(*property)) {
                continue;
            }
            const auto strings = propertyStrings(type(*property), property->subtype, property->costValue, std::nullopt, PropertyNaming::Full);
            const int amount = namedAmount(strings.subtype, "%*s +%d");
            _text += strings.name + ": +" + std::to_string(amount) + " (" + strings.cost + ")\n\n";
        }
    }

    // Damage of kinds the weapon does not deal, one entry per kind; the
    // physical kinds count as one.
    void addExtraDamage() {
        const int flags = _item.damageFlags();
        const int damageBonus = static_cast<int>(ItemProperty::DamageBonus);
        bool listed = false;
        for (size_t i = 0; i < _active.size(); ++i) {
            const auto &property = *_active[i];
            if (type(property) != damageBonus || hasDamageType(flags, property.subtype)) {
                continue;
            }
            const auto strings = propertyStrings(damageBonus, property.subtype, property.costValue, std::nullopt, PropertyNaming::Full);
            if (!listed) {
                _text += strings.name + ": ";
            }
            bool listedBefore = false;
            int minimum = 0;
            int maximum = 0;
            for (size_t j = 0; j < _active.size(); ++j) {
                const auto &other = *_active[j];
                if (type(other) != damageBonus) {
                    continue;
                }
                if (other.subtype != property.subtype &&
                    (property.subtype > kLastPhysicalDamageSubtype || other.subtype > kLastPhysicalDamageSubtype))
                    continue;
                if (j < i) {
                    listedBefore = true;
                    break;
                }
                const auto [low, high] = damageRange(other.costValue);
                minimum += low;
                maximum += high;
            }
            if (listedBefore) {
                continue;
            }
            const std::string kind(property.subtype <= kLastPhysicalDamageSubtype ? gui(kPhysicalDamageStrRef) : strings.subtype);
            if (listed) {
                _text += ", ";
            }
            _text += bonusRange(minimum, maximum) + " " + kind;
            listed = true;
        }
        if (listed) {
            _text += "\n\n";
        }
    }

    void addRange() {
        if (!_item.isRanged()) {
            return;
        }
        _text += str(boost::format("%s: %dm\n") % gui(kRangeStrRef) % static_cast<int>(_item.attackRange())) + "\n";
    }

    // Keen doubles the threat range; massive criticals add their damage. In
    // KotOR only the highest of a subtype counts.
    void addCriticalThreat() {
        _text += gui(kCriticalThreatStrRef) + ":\n";
        int threat = _item.criticalThreat();
        if (hasProperty(ItemProperty::Keen)) {
            threat *= 2;
        }
        int minimum = 0;
        int maximum = 0;
        for (const auto *property : _active) {
            if (type(*property) != static_cast<int>(ItemProperty::MassiveCriticals) || (!_tsl && isOutvalued(*property))) {
                continue;
            }
            const auto [low, high] = damageRange(property->costValue);
            minimum += low;
            maximum += high;
        }
        if (minimum != 0 || maximum != 0) {
            _text += str(boost::format("%d-20,x%d +%d-%d\n") % (21 - threat) % _item.criticalHitMultiplier() % minimum % maximum);
        } else {
            _text += str(boost::format("%d-20,x%d\n") % (21 - threat) % _item.criticalHitMultiplier());
        }
        _text += "\n";
    }

    void addOnHit() {
        const int onHitKind = static_cast<int>(ItemProperty::OnHitProperties);
        const int lastEffect = _tsl ? kOnHitKnockdown : kOnHitInstantDeath;
        for (const auto *property : _active) {
            if (type(*property) != onHitKind || property->subtype > lastEffect) {
                continue;
            }
            const auto effect = propertyStrings(onHitKind, property->subtype, std::nullopt, std::nullopt, PropertyNaming::Full);
            const std::string prefix(gui(kOnHitStrRef) + ": ");
            switch (property->subtype) {
            case kOnHitAbilityDrain:
            case kOnHitSlayRacialGroup:
            case kOnHitSlayAlignmentGroup: {
                _text += prefix + effect.subtype + " ";
                const auto strings = propertyStrings(onHitKind, property->subtype, property->costValue, property->paramValue, PropertyNaming::Full);
                _text += strings.param + " " + strings.cost + "\n\n";
                break;
            }
            case kOnHitPoison: {
                _text += prefix;
                const auto strings = propertyStrings(onHitKind, property->subtype, property->costValue, property->paramValue, PropertyNaming::Full);
                const int difficulty = requiredTable("poison")->getInt(property->paramValue, "dc_save");
                _text += strings.param + " " + gui(difficultyClassStrRef()) + " " + std::to_string(difficulty) + "\n\n";
                break;
            }
            case kOnHitInstantDeath: {
                _text += prefix + effect.subtype + " ";
                const auto strings = propertyStrings(onHitKind, property->subtype, property->costValue, property->paramValue, PropertyNaming::Full);
                _text += ", " + strings.cost + "\n\n";
                break;
            }
            case kOnHitKnockdown: {
                _text += prefix + effect.subtype + " ";
                const auto odds = onHitOdds(property->costValue, property->paramValue);
                _text += str(boost::format("%s %d\n\n") % gui(difficultyClassStrRef()) % odds.difficulty);
                break;
            }
            default: {
                _text += prefix + effect.subtype + " ";
                const auto odds = onHitOdds(property->costValue, property->paramValue);
                _text += str(boost::format("%d%% %s, %d %s, %s %d\n\n") % odds.chance % gui(kChanceStrRef) %
                             (odds.durationRounds * kSecondsPerRound) % gui(kDurationStrRef) %
                             gui(difficultyClassStrRef()) % odds.difficulty);
                break;
            }
            }
        }
    }

    void addWeaponSize() {
        if (_item.weaponSize() == CreatureSize::Small) {
            _text += gui(kOffHandStrRef) + "\n\n";
        }
    }

    // Attack and defence

    void addAttackModifier() {
        int total = 0;
        for (const auto *property : _active) {
            switch (static_cast<ItemProperty>(type(*property))) {
            case ItemProperty::EnhancementBonus:
            case ItemProperty::AttackBonus:
                total += property->costValue;
                break;
            case ItemProperty::AttackPenalty:
            case ItemProperty::DecreasedAttackModifier:
                total -= property->costValue;
                break;
            default:
                break;
            }
        }
        // TSL gauntlets say the modifier is for unarmed attacks.
        const std::string suffix(_tsl && _item.baseItemType() == kGauntletsBaseItem
                                     ? _services.resource.strings.getText(kUnarmedAttackSuffixStrRef)
                                     : std::string());
        // A nonzero total is written with its sign.
        if (total != 0) {
            _text += str(boost::format("%s: %+d%s\n") % gui(kAttackModifierStrRef) % total % suffix);
        }
        static constexpr ItemProperty kVersusKinds[] {
            ItemProperty::EnhancementBonusVsAlignmentGroup,
            ItemProperty::EnhancementBonusVsRacialGroup,
            ItemProperty::AttackBonusVsAlignmentGroup,
            ItemProperty::AttackBonusVsRacialGroup};
        if (std::any_of(std::begin(kVersusKinds), std::end(kVersusKinds), [this](ItemProperty kind) { return hasProperty(kind); })) {
            if (total == 0) {
                _text += gui(kAttackModifierStrRef) + ": ";
            }
            for (auto kind : kVersusKinds) {
                addAttackBonuses(static_cast<int>(kind));
            }
        }
        _text += "\n";
    }

    // One line per subtype; the running sum carries over from one subtype
    // to the next.
    void addAttackBonuses(int kind) {
        int sum = 0;
        for (size_t i = 0; i < _active.size(); ++i) {
            const auto &property = *_active[i];
            if (type(property) != kind) {
                continue;
            }
            bool listedBefore = false;
            for (size_t j = 0; j < _active.size(); ++j) {
                const auto &other = *_active[j];
                if (type(other) != kind || other.subtype != property.subtype) {
                    continue;
                }
                if (j < i) {
                    listedBefore = true;
                    break;
                }
                sum += other.costValue;
            }
            if (listedBefore) {
                continue;
            }
            const auto strings = propertyStrings(kind, property.subtype, property.costValue, std::nullopt, PropertyNaming::Full);
            _text += "+" + std::to_string(sum) + " " + gui(kVersusStrRef) + " " + strings.subtype + "\n";
        }
    }

    void addDefense() {
        // Armour bonuses stack unless the base item's defence is enchantment
        // of a kind, where only the highest of a subtype counts; the first
        // defence penalty counts when it is the highest of its subtype.
        int defense = _item.baseDefense();
        for (const auto *property : _active) {
            if (type(*property) != static_cast<int>(ItemProperty::AcBonus)) {
                continue;
            }
            if (_item.acBonusType() != ACBonus::Dodge && isOutvalued(*property)) {
                continue;
            }
            defense += property->costValue;
        }
        if (hasProperty(ItemProperty::DecreasedAc)) {
            const auto &properties = _item.properties();
            auto penalty = std::find_if(properties.begin(), properties.end(), [](const Item::PropertyEntry &property) {
                return type(property) == static_cast<int>(ItemProperty::DecreasedAc);
            });
            if (_item.isPropertyActive(*penalty) && !isOutvalued(*penalty)) {
                defense -= penalty->costValue;
            }
        }
        if (defense != 0) {
            _text += gui(kDefenseStrRef) + ": " + str(boost::format("%d\n") % defense) + "\n";
        }

        // The Dexterity limit, with TSL's adjustments; an item without one
        // shows its adjustment alone, if any.
        if (_item.itemType() != kDroidPlatingItemType) {
            const int limit = _item.maxDexterityBonus();
            const int adjustment = _item.maxDexterityBonusAdjustment();
            std::optional<int> shown;
            if (limit != kNoDexterityLimit) {
                shown = limit + adjustment;
            } else if (adjustment != 0) {
                shown = adjustment;
            }
            if (shown) {
                _text += gui(kMaxDexterityBonusStrRef) + ": +" + std::to_string(*shown) + "\n\n";
            }
        }

        for (const auto *property : _active) {
            const auto kind = static_cast<ItemProperty>(type(*property));
            const bool versus = kind == ItemProperty::AcBonusVsAlignmentGroup ||
                                kind == ItemProperty::AcBonusVsDamageType ||
                                kind == ItemProperty::AcBonusVsRacialGroup;
            const bool protection = kind == ItemProperty::ImmunityDamageType || kind == ItemProperty::DamageResistance;
            if ((!versus && !protection) || isOutvalued(*property)) {
                continue;
            }
            const auto strings = propertyStrings(type(*property), property->subtype, property->costValue, std::nullopt, PropertyNaming::Full);
            if (versus) {
                _text += "+" + strings.cost + " " + gui(kVersusStrRef) + " " + strings.subtype + "\n\n";
            } else {
                _text += strings.name + ": " + strings.cost + " " + gui(kVersusStrRef) + " " + strings.subtype + "\n\n";
            }
        }
    }

    // Miscellaneous lines

    // The uses a charged or daily cast-spell property has left.
    int usesLeft(const Item::PropertyEntry &property) const {
        const int charges = _item.charges();
        const int mode = property.costValue;
        if (!property.usable) {
            return 0;
        }
        if (mode == Item::kSingleUse) {
            return _item.stackSize();
        }
        if (Item::isChargedUse(mode)) {
            return charges > 0 ? charges / Item::chargesPerUse(mode) : 0;
        }
        // A daily property always has its uses left recorded.
        if (Item::isDailyUse(mode)) {
            return *property.usesPerDay;
        }
        if (Item::isTimedUse(mode)) {
            return 1;
        }
        return 0;
    }

    // The whole an Activate Item property's uses left are shown out of: the
    // full charges divided by the charges a use costs, except that two or
    // one charges a use show the full charges; the uses a day of a daily
    // property; one use of a timed one; none, shown as unlimited, otherwise.
    void addUses() {
        const int maximum = _item.maxCharges();
        for (const auto *property : _active) {
            if (type(*property) != static_cast<int>(ItemProperty::ActivateItem)) {
                continue;
            }
            const int mode = property->costValue;
            if (mode == Item::kSingleUse) {
                _text += gui(kSingleUseStrRef) + "\n\n";
                continue;
            }
            int uses = 0;
            if (Item::isChargedUse(mode)) {
                uses = Item::chargesPerUse(mode) > 2 ? maximum / Item::chargesPerUse(mode) : maximum;
            } else if (Item::isDailyUse(mode)) {
                uses = mode - Item::kOneUsePerDay + 1;
            } else if (Item::isTimedUse(mode)) {
                uses = 1;
            }
            _text += gui(kUsesStrRef) + ": ";
            if (uses != 0) {
                _text += str(boost::format("%d/%d\n") % usesLeft(*property) % uses) + "\n";
            } else {
                _text += gui(kUnlimitedUsesStrRef) + "\n\n";
            }
        }
    }

    void addImmunities() {
        if (!hasProperty(ItemProperty::Immunity)) {
            return;
        }
        int count = 0;
        for (const auto *property : _active) {
            if (type(*property) != static_cast<int>(ItemProperty::Immunity)) {
                continue;
            }
            const auto strings = propertyStrings(type(*property), property->subtype, std::nullopt, std::nullopt, PropertyNaming::Full);
            if (count == 0) {
                _text += strings.name + ": ";
            }
            if (count > 0) {
                _text += ", ";
            }
            _text += strings.subtype;
            ++count;
        }
        _text += "\n\n";
    }

    void addSaves() {
        if (!hasProperty(ItemProperty::ImprovedSavingThrow) && !hasProperty(ItemProperty::DecreasedSavingThrows) &&
            !hasProperty(ItemProperty::ImprovedSavingThrowSpecific) && !hasProperty(ItemProperty::DecreasedSavingThrowsSpecific)) {
            return;
        }
        _text += gui(kSavesStrRef) + ": ";
        int count = 0;
        for (const auto *property : _active) {
            if (isOutvalued(*property)) {
                continue;
            }
            const auto kind = static_cast<ItemProperty>(type(*property));
            const bool bonus = kind == ItemProperty::ImprovedSavingThrow || kind == ItemProperty::ImprovedSavingThrowSpecific;
            const bool penalty = kind == ItemProperty::DecreasedSavingThrows || kind == ItemProperty::DecreasedSavingThrowsSpecific;
            if (!bonus && !penalty) {
                continue;
            }
            const auto strings = propertyStrings(type(*property), property->subtype, property->costValue, std::nullopt, PropertyNaming::Full);
            if (count > 0) {
                _text += ", ";
            }
            if (bonus) {
                _text += strings.subtype + " +" + strings.cost;
            } else {
                _text += strings.subtype + " -" + std::to_string(namedAmount(strings.cost, "%*s -%d"));
            }
            ++count;
        }
        _text += "\n\n";
    }

    void addSkills() {
        if (!hasProperty(ItemProperty::SkillBonus) && !hasProperty(ItemProperty::DecreasedSkillModifier)) {
            return;
        }
        _text += gui(kSkillsStrRef) + ": ";
        int count = 0;
        for (const auto *property : _active) {
            const auto kind = static_cast<ItemProperty>(type(*property));
            if ((kind != ItemProperty::SkillBonus && kind != ItemProperty::DecreasedSkillModifier) || isOutvalued(*property)) {
                continue;
            }
            const auto strings = propertyStrings(type(*property), property->subtype, property->costValue, std::nullopt, PropertyNaming::Full);
            if (count > 0) {
                _text += ", ";
            }
            _text += strings.subtype + " " + penaltyOrCost(kind == ItemProperty::DecreasedSkillModifier, strings.cost);
            ++count;
        }
        _text += "\n\n";
    }

    /**
     * The cost text of a bonus or penalty. KotOR shows a penalty as "-N",
     * with N read from its cost name; TSL shows the cost name.
     */
    std::string penaltyOrCost(bool penalty, const std::string &cost) const {
        if (penalty && !_tsl) {
            return "-" + std::to_string(namedAmount(cost, "%*s -%d"));
        }
        return cost;
    }

    /**
     * The title of a deflection line: the part of the property name before
     * its colon, followed by ": ". A name without a colon is used whole.
     */
    static std::string deflectionTitle(const std::string &name) {
        const size_t colon = name.find(':');
        return colon != std::string::npos ? name.substr(0, colon) + ": " : name;
    }

    // TSL sums the deflection bonuses and penalties; the line is titled with
    // the part of the first property's name before its colon.
    void addDeflection() {
        if (!hasProperty(ItemProperty::BlasterBoltDeflectIncrease) && !hasProperty(ItemProperty::BlasterBoltDeflectDecrease)) {
            return;
        }
        bool titled = false;
        int total = 0;
        for (const auto *property : _active) {
            const auto kind = static_cast<ItemProperty>(type(*property));
            if (kind != ItemProperty::BlasterBoltDeflectIncrease && kind != ItemProperty::BlasterBoltDeflectDecrease) {
                continue;
            }
            if (!titled) {
                const auto strings = propertyStrings(type(*property), property->subtype, property->costValue, std::nullopt, PropertyNaming::Full);
                const size_t colon = strings.name.find(':');
                if (colon != std::string::npos) {
                    _text += strings.name.substr(0, colon) + ": " + " ";
                }
                titled = true;
            }
            total += kind == ItemProperty::BlasterBoltDeflectIncrease ? property->costValue : -property->costValue;
        }
        if (total > 0) {
            _text += "+";
        }
        _text += std::to_string(total) + "\n\n";
    }

    void addOtherProperties() {
        for (const auto *property : _active) {
            const int kind = type(*property);
            switch (static_cast<ItemProperty>(kind)) {
            case ItemProperty::AbilityBonus:
            case ItemProperty::DecreasedAbilityScore: {
                if (isOutvalued(*property)) {
                    break;
                }
                const auto strings = propertyStrings(kind, property->subtype, property->costValue, std::nullopt, PropertyNaming::Full);
                _text += strings.subtype + ": " + penaltyOrCost(kind == static_cast<int>(ItemProperty::DecreasedAbilityScore), strings.cost) + "\n\n";
                break;
            }
            // KotOR lists each deflection property among the others.
            case ItemProperty::BlasterBoltDeflectIncrease:
            case ItemProperty::BlasterBoltDeflectDecrease: {
                if (_tsl || isOutvalued(*property)) {
                    break;
                }
                const auto strings = propertyStrings(kind, std::nullopt, property->costValue, std::nullopt, PropertyNaming::Full);
                if (kind == static_cast<int>(ItemProperty::BlasterBoltDeflectIncrease)) {
                    _text += deflectionTitle(strings.name) + "+" + std::to_string(property->costValue) + "\n\n";
                } else {
                    _text += deflectionTitle(strings.name) + " -" + std::to_string(namedAmount(strings.cost, "%*s -%d")) + "\n\n";
                }
                break;
            }
            case ItemProperty::BonusFeat: {
                const auto strings = propertyStrings(kind, property->subtype, std::nullopt, std::nullopt, PropertyNaming::Full);
                _text += strings.name + ": " + strings.subtype + "\n\n";
                break;
            }
            case ItemProperty::DamageVulnerability: {
                if (isOutvalued(*property)) {
                    break;
                }
                const auto strings = propertyStrings(kind, property->subtype, std::nullopt, std::nullopt, PropertyNaming::Full);
                const auto odds = onHitOdds(property->costValue, property->paramValue);
                _text += gui(kVulnerabilityStrRef) + ": " +
                         str(boost::format("%d%% %s, %d%s ") % odds.chance % gui(kChanceStrRef) %
                             (odds.durationRounds * kSecondsPerRound) % gui(kDurationStrRef)) +
                         gui(kVersusStrRef) + " " + strings.subtype + "\n\n";
                break;
            }
            case ItemProperty::ImprovedForceResistance: {
                if (isOutvalued(*property)) {
                    break;
                }
                const auto strings = propertyStrings(kind, property->subtype, property->costValue, std::nullopt, PropertyNaming::Full);
                _text += gui(kForceResistanceStrRef) + ": +" + std::to_string(namedAmount(strings.cost, "%*s %d")) + "\n\n";
                break;
            }
            case ItemProperty::Light:
            case ItemProperty::TrueSeeing:
            case ItemProperty::FreedomOfMovement:
                _text += propertyStrings(kind, std::nullopt, std::nullopt, std::nullopt, PropertyNaming::Full).name + "\n\n";
                break;
            case ItemProperty::UseLimitationAlignmentGroup:
            case ItemProperty::UseLimitationClass:
                _text += gui(kUseLimitationStrRef) + ": " +
                         propertyStrings(kind, property->subtype, std::nullopt, std::nullopt, PropertyNaming::Full).subtype + "\n\n";
                break;
            case ItemProperty::Regeneration:
            case ItemProperty::RegenerationForcePoints: {
                if (isOutvalued(*property)) {
                    break;
                }
                const auto strings = propertyStrings(kind, property->subtype, property->costValue, std::nullopt, PropertyNaming::Full);
                _text += strings.name + ": " + strings.cost + "\n\n";
                break;
            }
            default:
                break;
            }
        }
    }
};

} // namespace

std::string buildItemDescription(const Item &item, const Game &game, ServicesView &services) {
    return ItemDescriptionBuilder(item, game, services).build();
}

std::string buildItemPropertyLines(
    const Item &item,
    const Game &game,
    ServicesView &services,
    const std::function<bool(const Item::PropertyEntry &)> &include) {
    return ItemDescriptionBuilder(item, game, services).buildPropertyLines(include);
}

std::string buildKeyedPropertyText(const Item &item, int row, const Game &game, ServicesView &services) {
    return ItemDescriptionBuilder(item, game, services).buildKeyedText(row);
}

} // namespace game

} // namespace reone
