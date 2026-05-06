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

#include "reone/game/d20/feat.h"
#include "reone/game/d20/feats.h"
#include "reone/game/d20/skill.h"
#include "reone/game/d20/skills.h"
#include "reone/game/d20/spell.h"
#include "reone/game/d20/spells.h"
#include "reone/game/di/services.h"
#include "reone/game/object/item.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/strings.h"

#include <iterator>
#include <map>
#include <regex>
#include <set>

using namespace reone::resource;

namespace reone {

namespace game {

namespace {

struct Section {
    std::string title;
    std::vector<std::string> lines;
};

struct RangeBonus {
    int min {0};
    int max {0};

    bool empty() const {
        return min == 0 && max == 0;
    }

    void add(const RangeBonus &other) {
        min += other.min;
        max += other.max;
    }
};

struct DerivedProperties {
    int attackModifier {0};
    int defenseBonus {0};
    int blasterBoltDeflection {0};
    std::map<int, int> attributeBonuses;
    std::map<std::string, RangeBonus> damageBonuses;
    std::set<std::string> foldedDamageTypes;
    RangeBonus massiveCriticals;
    bool foldedDefenseBonus {false};
};

static std::string lowerASCII(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

static std::string trim(std::string value) {
    auto begin = std::find_if(value.begin(), value.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    });
    auto end = std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base();
    return begin < end ? std::string(begin, end) : std::string();
}

static std::string singleLine(std::string value) {
    for (char &ch : value) {
        if (ch == '\r' || ch == '\n' || ch == '\t') {
            ch = ' ';
        }
    }

    std::string result;
    bool previousSpace = false;
    for (char ch : value) {
        bool space = std::isspace(static_cast<unsigned char>(ch));
        if (space && previousSpace) {
            continue;
        }
        result += space ? ' ' : ch;
        previousSpace = space;
    }
    return trim(result);
}

static std::string titleFromRaw(std::string value) {
    value = trim(value);
    bool nextUpper = true;
    for (char &ch : value) {
        if (ch == '_' || ch == '-') {
            ch = ' ';
            nextUpper = true;
        } else if (nextUpper) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            nextUpper = false;
        } else {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
    }
    return value;
}

static bool isDeletedCell(const std::string &value) {
    return value.empty() || value == "****" || value == "*";
}

static std::optional<int> parseInt(const std::string &value) {
    try {
        size_t pos = 0;
        int result = std::stoi(value, &pos, 0);
        if (pos == value.size()) {
            return result;
        }
    } catch (const std::exception &) {
    }
    return std::nullopt;
}

static std::optional<RangeBonus> parseRangeBonus(std::string value) {
    value = trim(value);
    if (value.empty()) {
        return std::nullopt;
    }

    std::smatch match;
    static const std::regex kDiceRegex("^([+-]?\\d+)\\s*d\\s*(\\d+)$", std::regex::icase);
    if (std::regex_search(value, match, kDiceRegex)) {
        int count = std::stoi(match[1].str());
        int sides = std::stoi(match[2].str());
        return RangeBonus {count, count * sides};
    }

    static const std::regex kRangeRegex("^([+-]?\\d+)\\s*-\\s*([+-]?\\d+)$");
    if (std::regex_search(value, match, kRangeRegex)) {
        return RangeBonus {std::stoi(match[1].str()), std::stoi(match[2].str())};
    }

    if (auto maybeValue = parseInt(value)) {
        return RangeBonus {*maybeValue, *maybeValue};
    }

    static const std::regex kLooseIntRegex("([+-]?\\d+)");
    if (std::regex_search(value, match, kLooseIntRegex)) {
        int amount = std::stoi(match[1].str());
        return RangeBonus {amount, amount};
    }
    return std::nullopt;
}

static std::optional<std::string> cell(
    const TwoDA &twoDa,
    int row,
    const std::vector<std::string> &columns) {

    if (row < 0 || row >= twoDa.getRowCount()) {
        return std::nullopt;
    }

    std::unordered_map<std::string, int> columnIndices;
    for (int i = 0; i < static_cast<int>(twoDa.columns().size()); ++i) {
        columnIndices.insert(std::make_pair(lowerASCII(twoDa.columns()[i]), i));
    }

    for (auto &column : columns) {
        auto it = columnIndices.find(lowerASCII(column));
        if (it == columnIndices.end()) {
            continue;
        }
        const std::string &value = twoDa.rows()[row].values[it->second];
        if (!isDeletedCell(value)) {
            return value;
        }
    }
    return std::nullopt;
}

static std::string textFromCell(ServicesView &services, const std::string &raw) {
    auto value = trim(raw);
    if (auto maybeStrRef = parseInt(value)) {
        std::string text(services.resource.strings.getText(*maybeStrRef));
        if (!text.empty()) {
            return text;
        }
    }
    return titleFromRaw(value);
}

static std::shared_ptr<TwoDA> getTwoDA(ServicesView &services, const std::string &resRef) {
    if (resRef.empty()) {
        return nullptr;
    }
    return services.resource.twoDas.get(lowerASCII(resRef));
}

static std::string resolveRowText(ServicesView &services, const std::string &tableName, int row) {
    auto twoDa = getTwoDA(services, tableName);
    if (!twoDa) {
        return "";
    }

    static const std::vector<std::string> kTextColumns {
        "name",
        "stringref",
        "strref",
        "labelstrref",
        "gamestrref",
        "description",
        "desc"};

    auto maybeTextCell = cell(*twoDa, row, kTextColumns);
    if (maybeTextCell) {
        return textFromCell(services, *maybeTextCell);
    }

    static const std::vector<std::string> kRawColumns {
        "label",
        "value"};

    auto maybeRawCell = cell(*twoDa, row, kRawColumns);
    return maybeRawCell ? titleFromRaw(*maybeRawCell) : "";
}

static std::string resolvePropertyDefText(ServicesView &services, int property, const std::vector<std::string> &columns) {
    auto itemPropDef = services.resource.twoDas.get("itempropdef");
    if (!itemPropDef) {
        return "";
    }
    auto maybeCell = cell(*itemPropDef, property, columns);
    return maybeCell ? textFromCell(services, *maybeCell) : "";
}

static std::string resolvePropertyDefResRef(ServicesView &services, int property, const std::vector<std::string> &columns) {
    auto itemPropDef = services.resource.twoDas.get("itempropdef");
    if (!itemPropDef) {
        return "";
    }
    auto maybeCell = cell(*itemPropDef, property, columns);
    return maybeCell ? lowerASCII(trim(*maybeCell)) : "";
}

static std::string resolveIndexedTableName(ServicesView &services, const std::string &tableName, int row) {
    auto table = getTwoDA(services, tableName);
    if (!table) {
        return "";
    }

    static const std::vector<std::string> kResRefColumns {
        "resref",
        "tablename",
        "table",
        "name",
        "label"};

    auto maybeCell = cell(*table, row, kResRefColumns);
    if (!maybeCell) {
        return "";
    }
    std::string value(lowerASCII(trim(*maybeCell)));
    if (auto maybeStrRef = parseInt(value)) {
        std::string text(services.resource.strings.getText(*maybeStrRef));
        if (!text.empty()) {
            return lowerASCII(text);
        }
    }
    return value;
}

static std::string resolveSubtype(ServicesView &services, const Item::PropertyEntry &property) {
    std::string tableName(resolvePropertyDefResRef(
        services,
        property.propertyName,
        {"subtyperesref", "subtype", "subtypetable", "subtyperes"}));
    if (tableName.empty()) {
        tableName = resolveIndexedTableName(services, "iprp_subtypes", property.subtype);
    }
    return resolveRowText(services, tableName, property.subtype);
}

static std::string resolveCost(ServicesView &services, const Item::PropertyEntry &property) {
    std::string tableName(resolvePropertyDefResRef(
        services,
        property.propertyName,
        {"costtableresref", "costtable", "costtableres"}));
    if (tableName.empty()) {
        tableName = resolveIndexedTableName(services, "iprp_costtable", property.costTable);
    }
    return resolveRowText(services, tableName, property.costValue);
}

static std::string resolveParam(ServicesView &services, const Item::PropertyEntry &property) {
    std::string tableName(resolvePropertyDefResRef(
        services,
        property.propertyName,
        {"param1resref", "param1table", "paramtableresref", "param1"}));
    if (tableName.empty()) {
        tableName = resolveIndexedTableName(services, "iprp_paramtable", property.paramTable);
    }
    return resolveRowText(services, tableName, property.paramValue);
}

static std::string itemPropertyName(ItemProperty property) {
    switch (property) {
    case ItemProperty::AbilityBonus:
        return "Ability Bonus";
    case ItemProperty::AcBonus:
        return "Defense Bonus";
    case ItemProperty::AcBonusVsAlignmentGroup:
        return "Defense Bonus vs Alignment";
    case ItemProperty::AcBonusVsDamageType:
        return "Defense Bonus vs Damage";
    case ItemProperty::AcBonusVsRacialGroup:
        return "Defense Bonus vs Racial Group";
    case ItemProperty::EnhancementBonus:
        return "Enhancement Bonus";
    case ItemProperty::AttackPenalty:
        return "Attack Penalty";
    case ItemProperty::BonusFeat:
        return "Bonus Feat";
    case ItemProperty::ActivateItem:
        return "Activate Item";
    case ItemProperty::DamageBonus:
        return "Damage Bonus";
    case ItemProperty::ImmunityDamageType:
        return "Damage Immunity";
    case ItemProperty::DamageReduction:
        return "Damage Reduction";
    case ItemProperty::DamageResistance:
        return "Damage Resistance";
    case ItemProperty::Immunity:
        return "Immunity";
    case ItemProperty::ImprovedSavingThrow:
    case ItemProperty::ImprovedSavingThrowSpecific:
        return "Saving Throw Bonus";
    case ItemProperty::Keen:
        return "Keen";
    case ItemProperty::OnHitProperties:
        return "On Hit";
    case ItemProperty::Regeneration:
        return "Regeneration";
    case ItemProperty::SkillBonus:
        return "Skill Bonus";
    case ItemProperty::AttackBonus:
        return "Attack Bonus";
    case ItemProperty::UnlimitedAmmunition:
        return "Unlimited Ammunition";
    case ItemProperty::UseLimitationAlignmentGroup:
        return "Use Limitation Alignment";
    case ItemProperty::UseLimitationClass:
        return "Use Limitation Class";
    case ItemProperty::UseLimitationRacialType:
        return "Use Limitation Racial Type";
    case ItemProperty::TrueSeeing:
        return "True Seeing";
    case ItemProperty::MassiveCriticals:
        return "Massive Criticals";
    case ItemProperty::FreedomOfMovement:
        return "Freedom Of Movement";
    case ItemProperty::RegenerationForcePoints:
        return "Regeneration Force Points";
    case ItemProperty::BlasterBoltDeflectIncrease:
        return "Blaster Bolt Deflection";
    case ItemProperty::BlasterBoltDeflectDecrease:
        return "Blaster Bolt Deflection Penalty";
    case ItemProperty::UseLimitationFeat:
        return "Use Limitation Feat";
    default:
        return "";
    }
}

static std::string propertyDisplayName(ServicesView &services, int propertyName) {
    std::string name(resolvePropertyDefText(
        services,
        propertyName,
        {"name", "label", "stringref", "strref", "gamestrref"}));
    if (!name.empty()) {
        return name;
    }

    std::string enumName(itemPropertyName(static_cast<ItemProperty>(propertyName)));
    return !enumName.empty() ? enumName : "Property " + std::to_string(propertyName);
}

static std::string signedValue(std::string value, bool positive = true) {
    value = trim(value);
    if (value.empty()) {
        return "";
    }
    if (positive && value[0] != '+' && value[0] != '-' && parseInt(value)) {
        return "+" + value;
    }
    if (!positive && value[0] != '-') {
        return "-" + value;
    }
    return value;
}

static std::optional<RangeBonus> propertyCostRange(ServicesView &services, const Item::PropertyEntry &property) {
    std::string cost(resolveCost(services, property));
    if (!cost.empty()) {
        return parseRangeBonus(cost);
    }
    if (property.costValue != 0) {
        return RangeBonus {property.costValue, property.costValue};
    }
    return std::nullopt;
}

static std::optional<int> propertyCostInt(ServicesView &services, const Item::PropertyEntry &property) {
    auto maybeRange = propertyCostRange(services, property);
    if (!maybeRange || maybeRange->min != maybeRange->max) {
        return std::nullopt;
    }
    return maybeRange->min;
}

static std::string formatSignedInt(int value) {
    return signedValue(std::to_string(value), value >= 0);
}

static std::string formatRangeBonus(const RangeBonus &range) {
    if (range.min == range.max) {
        return formatSignedInt(range.min);
    }
    std::string sign = range.min >= 0 ? "+" : "";
    return sign + std::to_string(range.min) + "-" + std::to_string(range.max);
}

static std::string abilityName(int ability) {
    switch (static_cast<Ability>(ability)) {
    case Ability::Strength:
        return "Strength";
    case Ability::Dexterity:
        return "Dexterity";
    case Ability::Constitution:
        return "Constitution";
    case Ability::Intelligence:
        return "Intelligence";
    case Ability::Wisdom:
        return "Wisdom";
    case Ability::Charisma:
        return "Charisma";
    default:
        return "";
    }
}

static std::string propertyDetailLine(
    ServicesView &services,
    const Item::PropertyEntry &property,
    bool signedCost = false,
    bool positive = true) {

    std::vector<std::string> parts;

    std::string subtype(resolveSubtype(services, property));
    if (!subtype.empty()) {
        parts.push_back(subtype);
    }

    std::string cost(resolveCost(services, property));
    if (!cost.empty()) {
        parts.push_back(signedCost ? signedValue(cost, positive) : cost);
    } else if (property.costValue != 0) {
        parts.push_back(signedCost ? signedValue(std::to_string(property.costValue), positive) : std::to_string(property.costValue));
    }

    std::string param(resolveParam(services, property));
    if (!param.empty()) {
        parts.push_back(param);
    }

    if (parts.empty()) {
        return propertyDisplayName(services, property.propertyName);
    }

    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            result += " ";
        }
        result += parts[i];
    }
    return result;
}

static std::string rawPropertyDetails(const Item::PropertyEntry &property) {
    std::vector<std::string> parts {
        "subtype " + std::to_string(property.subtype),
        "cost table " + std::to_string(property.costTable),
        "cost value " + std::to_string(property.costValue),
        "param table " + std::to_string(property.paramTable),
        "param value " + std::to_string(property.paramValue)};

    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        result += parts[i];
    }
    return result;
}

static std::string fallbackPropertyLine(ServicesView &services, const Item::PropertyEntry &property) {
    std::string detail(propertyDetailLine(services, property));
    std::string name(propertyDisplayName(services, property.propertyName));
    if (detail.empty() || detail == name) {
        detail = rawPropertyDetails(property);
    }
    return name + ": " + detail;
}

static std::string featName(ServicesView &services, int featIdx) {
    auto feat = services.game.feats.get(static_cast<FeatType>(featIdx));
    if (feat && !feat->name.empty()) {
        return feat->name;
    }
    return resolveRowText(services, "feat", featIdx);
}

static std::string skillName(ServicesView &services, int skillIdx) {
    auto skill = services.game.skills.get(static_cast<SkillType>(skillIdx));
    if (skill && !skill->name.empty()) {
        return skill->name;
    }
    return resolveRowText(services, "skills", skillIdx);
}

static std::string damageTypeName(int flags) {
    if (flags == 0) {
        return "";
    }
    if (flags == static_cast<int>(DamageType::Physical)) {
        return "Physical";
    }

    std::vector<std::pair<int, std::string>> names {
        {static_cast<int>(DamageType::Bludgeoning), "Bludgeoning"},
        {static_cast<int>(DamageType::Piercing), "Piercing"},
        {static_cast<int>(DamageType::Slashing), "Slashing"},
        {static_cast<int>(DamageType::Universal), "Universal"},
        {static_cast<int>(DamageType::Acid), "Acid"},
        {static_cast<int>(DamageType::Cold), "Cold"},
        {static_cast<int>(DamageType::LightSide), "Light Side"},
        {static_cast<int>(DamageType::Electrical), "Electrical"},
        {static_cast<int>(DamageType::Fire), "Fire"},
        {static_cast<int>(DamageType::DarkSide), "Dark Side"},
        {static_cast<int>(DamageType::Sonic), "Sonic"},
        {static_cast<int>(DamageType::Ion), "Ion"},
        {static_cast<int>(DamageType::Blaster), "Energy"}};

    std::vector<std::string> parts;
    for (auto &name : names) {
        if ((flags & name.first) == name.first) {
            parts.push_back(name.second);
        }
    }

    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            result += "/";
        }
        result += parts[i];
    }
    return result;
}

static std::optional<int> baseItemInt(ServicesView &services, const Item &item, const std::vector<std::string> &columns) {
    auto baseItems = services.resource.twoDas.get("baseitems");
    if (!baseItems) {
        return std::nullopt;
    }
    auto maybeCell = cell(*baseItems, item.baseItemType(), columns);
    if (!maybeCell) {
        return std::nullopt;
    }
    return parseInt(*maybeCell);
}

static std::vector<int> baseItemInts(ServicesView &services, const Item &item, const std::vector<std::string> &columns) {
    auto baseItems = services.resource.twoDas.get("baseitems");
    if (!baseItems) {
        return {};
    }

    std::vector<int> values;
    for (auto &column : columns) {
        auto maybeCell = cell(*baseItems, item.baseItemType(), {column});
        if (!maybeCell) {
            continue;
        }
        auto maybeValue = parseInt(*maybeCell);
        if (maybeValue && *maybeValue > 0) {
            values.push_back(*maybeValue);
        }
    }
    return values;
}

static std::optional<bool> baseItemBool(ServicesView &services, const Item &item, const std::vector<std::string> &columns) {
    auto baseItems = services.resource.twoDas.get("baseitems");
    if (!baseItems) {
        return std::nullopt;
    }

    auto maybeCell = cell(*baseItems, item.baseItemType(), columns);
    if (!maybeCell) {
        return std::nullopt;
    }

    std::string value(lowerASCII(trim(*maybeCell)));
    if (value == "true" || value == "yes") {
        return true;
    }
    if (value == "false" || value == "no") {
        return false;
    }
    if (auto maybeValue = parseInt(value)) {
        return *maybeValue != 0;
    }
    return std::nullopt;
}

static bool hasUpgradeHostMetadata(const Item &item) {
    if (!item.isEquippable()) {
        return false;
    }
    return std::any_of(item.properties().begin(), item.properties().end(), [](const Item::PropertyEntry &property) {
        return property.upgradeType != 0;
    });
}

static Section &sectionByTitle(std::vector<Section> &sections, const std::string &title) {
    auto it = std::find_if(sections.begin(), sections.end(), [&title](const Section &section) {
        return section.title == title;
    });
    if (it != sections.end()) {
        return *it;
    }
    sections.push_back({title, {}});
    return sections.back();
}

static std::string damageRange(int min, int max) {
    return std::to_string(min) + "-" + std::to_string(max);
}

static std::string criticalThreatRange(const Item &item) {
    int threatChances = std::clamp(item.criticalThreat(), 1, 20);
    int startThreat = 21 - threatChances;
    return std::to_string(startThreat) + "-20,x" + std::to_string(item.criticalHitMultiplier());
}

static std::string itemDescriptionText(const Item &item) {
    std::string description(item.isIdentified() ? item.descIdentified() : item.description());
    if (description.empty() && !item.descIdentified().empty()) {
        description = item.descIdentified();
    }
    return description;
}

static bool isBalancedWeapon(ServicesView &services, const Item &item) {
    if (auto maybeBalanced = baseItemBool(services, item, {"balanced", "isbalanced", "offhandbalanced"})) {
        return *maybeBalanced;
    }
    if (auto maybeWeaponSize = baseItemInt(services, item, {"weaponsize", "weapon_size", "weapsize"})) {
        return item.weaponType() != WeaponType::None && *maybeWeaponSize <= 1;
    }
    return item.weaponWield() == WeaponWield::BlasterPistol;
}

static void addBaseFacts(ServicesView &services, const Item &item, DerivedProperties &derived, std::vector<Section> &sections) {
    Section featsRequired {"Feats Required"};
    std::set<int> featIds;
    for (int feat : baseItemInts(
             services,
             item,
             {"reqfeat0", "reqfeat1", "reqfeat2", "reqfeat3", "reqfeat4", "reqfeat", "requiredfeat", "featrequired", "basefeat"})) {
        featIds.insert(feat);
    }
    for (int feat : featIds) {
        std::string name(featName(services, feat));
        featsRequired.lines.push_back(!name.empty() ? singleLine(name) : std::to_string(feat));
    }
    sections.push_back(std::move(featsRequired));

    Section baseFacts {""};
    if (item.weaponType() != WeaponType::None && item.numDice() > 0 && item.dieToRoll() > 0) {
        std::string type(damageTypeName(item.damageFlags()));
        int minDamage = item.numDice();
        int maxDamage = item.numDice() * item.dieToRoll();
        if (!type.empty()) {
            auto maybeDamageBonus = derived.damageBonuses.find(type);
            if (maybeDamageBonus != derived.damageBonuses.end()) {
                minDamage += maybeDamageBonus->second.min;
                maxDamage += maybeDamageBonus->second.max;
                derived.foldedDamageTypes.insert(type);
            }
        }

        std::string line(damageRange(minDamage, maxDamage));
        if (!type.empty()) {
            line = type + ", " + line;
        }
        baseFacts.lines.push_back("Damage: " + line);
    }

    if (item.weaponType() != WeaponType::None && item.criticalThreat() > 0 && item.criticalHitMultiplier() > 0) {
        std::string line("Critical Threat: " + criticalThreatRange(item));
        if (!derived.massiveCriticals.empty()) {
            line += " " + formatRangeBonus(derived.massiveCriticals);
        }
        baseFacts.lines.push_back(line);
    }

    if (item.weaponType() == WeaponType::Ranged && item.attackRange() > 0.0f) {
        baseFacts.lines.push_back("Range: " + std::to_string(static_cast<int>(item.attackRange())) + "m");
    }

    if (auto maybeDefense = baseItemInt(services, item, {"baseac", "acbonus", "defbonus", "armorbonus", "armorclass"})) {
        int defense = *maybeDefense + derived.defenseBonus;
        derived.foldedDefenseBonus = true;
        if (defense != 0) {
            baseFacts.lines.push_back("Defense Bonus: " + formatSignedInt(defense));
        }
    }

    if (isBalancedWeapon(services, item)) {
        baseFacts.lines.push_back("Balanced: +2/+0 vs. two-weapon penalty if used in the off hand");
    }

    sections.push_back(std::move(baseFacts));
}

static void addPropertyLine(Section &section, const std::string &line) {
    if (!line.empty()) {
        section.lines.push_back(line);
    }
}

static bool addSignedCost(ServicesView &services, const Item::PropertyEntry &property, int &target, bool positive = true) {
    auto maybeValue = propertyCostInt(services, property);
    if (!maybeValue) {
        return false;
    }
    target += positive ? *maybeValue : -*maybeValue;
    return true;
}

static void addDamageBonus(DerivedProperties &derived, const std::string &damageType, const RangeBonus &bonus) {
    if (damageType.empty()) {
        return;
    }
    derived.damageBonuses[damageType].add(bonus);
}

static void addAggregatedPropertyLines(const DerivedProperties &derived, std::vector<Section> &sections) {
    Section scalar {""};

    if (derived.attackModifier != 0) {
        scalar.lines.push_back("Attack Modifier: " + formatSignedInt(derived.attackModifier));
    }

    if (!derived.foldedDefenseBonus && derived.defenseBonus != 0) {
        scalar.lines.push_back("Defense Bonus: " + formatSignedInt(derived.defenseBonus));
    }

    if (derived.blasterBoltDeflection != 0) {
        scalar.lines.push_back("Blaster Bolt Deflection: " + formatSignedInt(derived.blasterBoltDeflection));
    }

    for (auto &bonus : derived.damageBonuses) {
        if (derived.foldedDamageTypes.count(bonus.first) > 0 || bonus.second.empty()) {
            continue;
        }
        scalar.lines.push_back("Damage Bonus: " + bonus.first + " " + formatRangeBonus(bonus.second));
    }

    for (auto &bonus : derived.attributeBonuses) {
        std::string name(abilityName(bonus.first));
        if (!name.empty() && bonus.second != 0) {
            scalar.lines.push_back(name + ": " + formatSignedInt(bonus.second));
        }
    }

    sections.push_back(std::move(scalar));
}

static void addProperties(ServicesView &services, const Item &item, DerivedProperties &derived, std::vector<Section> &sections) {
    Section requirements {"Requirements / Restrictions"};
    Section attack {"Attack Modifier"};
    Section damageBonus {"Damage Bonus"};
    Section damageResistance {"Damage Resistance"};
    Section damageImmunity {"Damage Immunity"};
    Section immunity {"Immunity"};
    Section saves {"Saves"};
    Section skills {"Skills"};
    Section attributes {"Attributes"};
    Section regeneration {"Regeneration"};
    Section featsGranted {"Feats Granted"};
    Section activateItem {"Activate Item"};
    Section onHit {""};
    Section special {"Special / Properties"};

    for (auto &property : item.properties()) {
        ItemProperty type = static_cast<ItemProperty>(property.propertyName);
        switch (type) {
        case ItemProperty::AbilityBonus:
        case ItemProperty::DecreasedAbilityScore: {
            auto maybeValue = propertyCostInt(services, property);
            if (maybeValue) {
                derived.attributeBonuses[property.subtype] += type == ItemProperty::AbilityBonus ? *maybeValue : -*maybeValue;
            } else {
                addPropertyLine(attributes, propertyDetailLine(services, property, true, type == ItemProperty::AbilityBonus));
            }
            break;
        }
        case ItemProperty::AcBonus:
        case ItemProperty::DecreasedAc:
            if (!addSignedCost(services, property, derived.defenseBonus, type != ItemProperty::DecreasedAc)) {
                addPropertyLine(sectionByTitle(sections, "Defense Bonus"), propertyDetailLine(services, property, true, type != ItemProperty::DecreasedAc));
            }
            break;
        case ItemProperty::AcBonusVsAlignmentGroup:
        case ItemProperty::AcBonusVsDamageType:
        case ItemProperty::AcBonusVsRacialGroup:
            addPropertyLine(sectionByTitle(sections, "Defense Bonus"), propertyDetailLine(services, property, true));
            break;
        case ItemProperty::EnhancementBonus:
        case ItemProperty::AttackBonus:
        case ItemProperty::AttackPenalty:
        case ItemProperty::DecreasedAttackModifier:
            if (!addSignedCost(
                    services,
                    property,
                    derived.attackModifier,
                    type != ItemProperty::AttackPenalty && type != ItemProperty::DecreasedAttackModifier)) {
                addPropertyLine(attack, propertyDetailLine(
                                            services,
                                            property,
                                            true,
                                            type != ItemProperty::AttackPenalty && type != ItemProperty::DecreasedAttackModifier));
            }
            break;
        case ItemProperty::EnhancementBonusVsAlignmentGroup:
        case ItemProperty::EnhancementBonusVsRacialGroup:
        case ItemProperty::AttackBonusVsAlignmentGroup:
        case ItemProperty::AttackBonusVsRacialGroup:
            addPropertyLine(attack, propertyDetailLine(services, property, true));
            break;
        case ItemProperty::DamageBonus:
        case ItemProperty::ExtraMeleeDamageType:
        case ItemProperty::ExtraRangedDamageType: {
            std::string damageType(resolveSubtype(services, property));
            auto maybeBonus = propertyCostRange(services, property);
            if (!damageType.empty() && maybeBonus) {
                addDamageBonus(derived, damageType, *maybeBonus);
            } else {
                addPropertyLine(damageBonus, propertyDetailLine(services, property, true));
            }
            break;
        }
        case ItemProperty::MassiveCriticals: {
            auto maybeBonus = propertyCostRange(services, property);
            if (maybeBonus) {
                derived.massiveCriticals.add(*maybeBonus);
            } else {
                addPropertyLine(damageBonus, propertyDetailLine(services, property, true));
            }
            break;
        }
        case ItemProperty::DamageBonusVsAlignmentGroup:
        case ItemProperty::DamageBonusVsRacialGroup:
        case ItemProperty::MonsterDamage:
            addPropertyLine(damageBonus, propertyDetailLine(services, property, true));
            break;
        case ItemProperty::ImmunityDamageType:
            addPropertyLine(damageImmunity, propertyDetailLine(services, property));
            break;
        case ItemProperty::DamageReduction:
        case ItemProperty::DamageResistance:
            addPropertyLine(damageResistance, propertyDetailLine(services, property));
            break;
        case ItemProperty::Immunity:
            addPropertyLine(immunity, propertyDetailLine(services, property));
            break;
        case ItemProperty::ImprovedSavingThrow:
        case ItemProperty::ImprovedSavingThrowSpecific:
        case ItemProperty::DecreasedSavingThrows:
        case ItemProperty::DecreasedSavingThrowsSpecific:
            addPropertyLine(saves, propertyDetailLine(
                                       services,
                                       property,
                                       true,
                                       type != ItemProperty::DecreasedSavingThrows && type != ItemProperty::DecreasedSavingThrowsSpecific));
            break;
        case ItemProperty::SkillBonus: {
            std::string line(skillName(services, property.subtype));
            std::string value(resolveCost(services, property));
            if (!line.empty() && !value.empty()) {
                line += ": " + signedValue(value);
            }
            addPropertyLine(skills, !line.empty() ? line : propertyDetailLine(services, property, true));
            break;
        }
        case ItemProperty::DecreasedSkillModifier:
            addPropertyLine(skills, propertyDetailLine(services, property, true, false));
            break;
        case ItemProperty::Regeneration:
        case ItemProperty::RegenerationForcePoints:
            addPropertyLine(regeneration, propertyDetailLine(services, property, true));
            break;
        case ItemProperty::BonusFeat: {
            std::string name(featName(services, property.subtype));
            addPropertyLine(featsGranted, !name.empty() ? name : propertyDetailLine(services, property));
            break;
        }
        case ItemProperty::ActivateItem: {
            auto spell = services.game.spells.get(static_cast<SpellType>(property.subtype));
            if (spell && !spell->name.empty()) {
                addPropertyLine(activateItem, spell->name);
            } else {
                addPropertyLine(activateItem, propertyDetailLine(services, property));
            }
            break;
        }
        case ItemProperty::OnHitProperties:
        case ItemProperty::OnMonsterHit:
            addPropertyLine(onHit, "On Hit: " + propertyDetailLine(services, property));
            break;
        case ItemProperty::BlasterBoltDeflectIncrease:
        case ItemProperty::BlasterBoltDeflectDecrease:
            if (!addSignedCost(services, property, derived.blasterBoltDeflection, type == ItemProperty::BlasterBoltDeflectIncrease)) {
                addPropertyLine(special, fallbackPropertyLine(services, property));
            }
            break;
        case ItemProperty::UseLimitationAlignmentGroup:
        case ItemProperty::UseLimitationClass:
        case ItemProperty::UseLimitationRacialType:
        case ItemProperty::UseLimitationFeat:
        case ItemProperty::LimitUseByGender:
        case ItemProperty::LimitUseBySubrace:
        case ItemProperty::LimitUseByPc:
            addPropertyLine(requirements, fallbackPropertyLine(services, property));
            break;
        default:
            addPropertyLine(special, fallbackPropertyLine(services, property));
            break;
        }
    }

    sections.insert(sections.begin(), std::move(requirements));
    sections.push_back(std::move(attack));
    sections.push_back(std::move(damageBonus));
    sections.push_back(std::move(damageResistance));
    sections.push_back(std::move(damageImmunity));
    sections.push_back(std::move(immunity));
    sections.push_back(std::move(saves));
    sections.push_back(std::move(skills));
    sections.push_back(std::move(attributes));
    sections.push_back(std::move(regeneration));
    sections.push_back(std::move(featsGranted));
    sections.push_back(std::move(activateItem));
    sections.push_back(std::move(onHit));
    sections.push_back(std::move(special));
}

static bool shouldUseHeader(const Section &section) {
    if (section.title.empty()) {
        return false;
    }
    if (section.title == "Description") {
        return true;
    }
    if (section.title == "Feats Required" ||
        section.title == "Requirements / Restrictions" ||
        section.title == "Skills" ||
        section.title == "Attributes" ||
        section.title == "Feats Granted" ||
        section.title == "On Hit" ||
        section.title == "Special / Properties") {
        return true;
    }
    return section.lines.size() > 1;
}

static void appendSection(std::vector<std::string> &result, const Section &section) {
    if (section.lines.empty()) {
        return;
    }
    if (!result.empty()) {
        result.push_back("");
    }

    if (section.title.empty()) {
        for (auto &line : section.lines) {
            if (!result.empty() && !result.back().empty()) {
                result.push_back("");
            }
            result.push_back(line);
        }
        return;
    }

    if (shouldUseHeader(section)) {
        result.push_back(section.title == "Description" ? section.title : section.title + ":");
        result.insert(result.end(), section.lines.begin(), section.lines.end());
        return;
    }

    if (section.lines.size() == 1) {
        result.push_back(section.title + ": " + section.lines.front());
        return;
    }

    result.push_back(section.title + ":");
    result.insert(result.end(), section.lines.begin(), section.lines.end());
}

} // namespace

std::vector<std::string> buildItemDescriptionLines(const Item &item, ServicesView &services) {
    std::vector<std::string> result;
    if (!item.localizedName().empty()) {
        result.push_back(item.localizedName());
    }

    std::vector<Section> sections;
    DerivedProperties derived;
    std::vector<Section> propertySections;

    // Upgradeable host templates can carry candidate/component properties in PropertiesList.
    // Installed-upgrade-derived display needs active upgrade state, which Item does not expose yet.
    bool renderProperties = !hasUpgradeHostMetadata(item);
    if (renderProperties) {
        addProperties(services, item, derived, propertySections);
    }

    addBaseFacts(services, item, derived, sections);

    if (!propertySections.empty() && propertySections.front().title == "Requirements / Restrictions") {
        auto insertPos = sections.size() > 1 ? sections.begin() + 1 : sections.end();
        sections.insert(insertPos, std::move(propertySections.front()));
        propertySections.erase(propertySections.begin());
    }
    if (renderProperties) {
        addAggregatedPropertyLines(derived, sections);
        sections.insert(
            sections.end(),
            std::make_move_iterator(propertySections.begin()),
            std::make_move_iterator(propertySections.end()));
    }

    for (auto &section : sections) {
        appendSection(result, section);
    }

    std::string description(itemDescriptionText(item));
    if (!description.empty()) {
        appendSection(result, {"Description", {description}});
    }

    return result;
}

std::string joinItemDescriptionLines(const std::vector<std::string> &lines) {
    std::string result;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            result += "\n";
        }
        result += lines[i];
    }
    return result;
}

} // namespace game

} // namespace reone
