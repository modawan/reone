/*
 * Copyright (c) 2025 The reone project contributors
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

#include "reone/game/animations.h"
#include "reone/game/attack.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/system/logutil.h"

#include <cctype>
#include <string>

using namespace reone::resource;

namespace reone {

namespace game {

void Animations::parseAnims(TwoDA &animDa) {
    for (int row = 0; row < animDa.getRowCount(); ++row) {
        Anim anim;
        anim.name = animDa.getString(row, "name");
        anim.attack = animDa.getBool(row, "attack");
        _anims.push_back(anim);
    }
}

struct CombatAnimColumn {
    enum Kind {
        Parry,
        Dodge,
        Damage,
    } kind;

    std::string name;
    CreatureWieldType wield;
};

// Split a string followed by a number.
static std::pair<std::string, int> splitStrInt(std::string s) {
    size_t i;
    for (i = s.size(); i > 0; --i) {
        if (!std::isdigit(s[i - 1])) {
            break;
        }
    }

    if (i == 0 || i == s.size()) {
        return {"", 0};
    }

    std::string num = s.substr(i);
    return {s.substr(0, i), std::stoi(num)};
}

static std::vector<CombatAnimColumn> parseCombatAnimColumns(TwoDA &combatAnimDa) {
    std::vector<CombatAnimColumn> result;

    for (const std::string &columnName : combatAnimDa.columns()) {
        std::pair<std::string, int> pair = splitStrInt(columnName);
        if (pair.first.empty()) {
            continue;
        }

        CombatAnimColumn::Kind kind;
        if (pair.first == "parry") {
            kind = CombatAnimColumn::Parry;
        } else if (pair.first == "dodge") {
            kind = CombatAnimColumn::Dodge;
        } else if (pair.first == "damage") {
            kind = CombatAnimColumn::Damage;
        } else {
            continue;
        }

        CombatAnimColumn column;
        column.name = columnName;
        column.kind = kind;
        column.wield = static_cast<CreatureWieldType>(pair.second);
        result.push_back(column);
    }
    return result;
}

void Animations::parseCombatAnim(TwoDA &combatAnimDa) {
    std::vector<CombatAnimColumn> columns = parseCombatAnimColumns(combatAnimDa);

    // Rows of combatanimations.2da match the order of attack animations from
    // animations.2da.
    int row = 0;
    for (const Anim &attackAnim : _anims) {
        if (row == combatAnimDa.getRowCount()) {
            return;
        }

        if (!attackAnim.attack) {
            continue;
        }

        // Parse animations that follow an attack: parry, dodge, damage.
        for (const CombatAnimColumn &column : columns) {
            uint32_t animId = combatAnimDa.getInt(row, column.name, kNoAnim);
            if (animId == kNoAnim) {
                continue;
            }
            if (animId >= _anims.size()) {
                warn("combatanimations.2da: unknown anim " + std::to_string(animId));
                continue;
            }
            AttackResult &result = _attackResults[{attackAnim.name, column.wield}];
            switch (column.kind) {
            case CombatAnimColumn::Parry:
                result.parry = animId;
                break;
            case CombatAnimColumn::Dodge:
                result.dodge = animId;
                break;
            case CombatAnimColumn::Damage:
                result.damage = animId;
                break;
            }
        }

        ++row;
    }
}

void Animations::init() {
    std::shared_ptr<TwoDA> animDa(_twoDas.get("animations"));
    if (!animDa) {
        return;
    }

    parseAnims(*animDa);

    std::shared_ptr<TwoDA> combatAnimDa(_twoDas.get("combatanimations.2da"));
    if (!combatAnimDa) {
        return;
    }

    parseCombatAnim(*combatAnimDa);
}

void Animations::clear() {
    _anims.clear();
    _attackResults.clear();
}

std::string Animations::getNameById(uint32_t id) const {
    if (id >= _anims.size()) {
        return std::string();
    }
    return _anims[id].name;
}

std::string Animations::getAttackResult(std::string attackAnim,
                                        CreatureWieldType targetWield,
                                        AttackResultType result) const {
    auto it = _attackResults.find({attackAnim, targetWield});
    if (it == _attackResults.end()) {
        return std::string();
    }

    switch (result) {
    case AttackResultType::Invalid:
        return std::string();
    case AttackResultType::HitSuccessful:
    case AttackResultType::CriticalHit:
    case AttackResultType::AutomaticHit:
        return getNameById(it->second.damage);

    case AttackResultType::Miss:
    case AttackResultType::AttackResisted:
    case AttackResultType::AttackFailed:
    case AttackResultType::Parried:
    case AttackResultType::Deflected:
        if (isRangedWieldType(targetWield)) {
            return getNameById(it->second.dodge);
        }
        return getNameById(it->second.damage);
    }

    return std::string();
}

} // namespace game

} // namespace reone
