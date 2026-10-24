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

#include <algorithm>
#include <cctype>
#include <string>

#include <boost/algorithm/string.hpp>

#include "reone/game/animationutil.h"
#include "reone/game/attack.h"
#include "reone/game/twodautil.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/system/exception/validation.h"
#include "reone/system/logutil.h"

using namespace reone::resource;

namespace reone {

namespace game {

// Animation IDs from 10000 name dialoganimations.2da rows.
static constexpr int kDialogAnimationBase = 10000;

void Animations::parseAnims(TwoDA &animDa) {
    for (int row = 0; row < animDa.getRowCount(); ++row) {
        Anim anim;
        anim.name = animDa.getString(row, "name");
        anim.attack = animDa.getBool(row, "attack");
        anim.overlay = animDa.getBool(row, "overlay");
        anim.looping = animDa.getBool(row, "looping");
        anim.fireForget = animDa.getBool(row, "fireforget");
        anim.parry = animDa.getBool(row, "parry");
        anim.hidesEquippedItems = animDa.getBool(row, "hideequippeditems");
        anim.dialog = isDialogAnimationRow(animDa, row);
        _animIndexByName.emplace(boost::to_lower_copy(anim.name), _anims.size());
        _anims.push_back(anim);
    }
}

std::optional<std::reference_wrapper<const Animations::Anim>> Animations::findByName(const std::string &name) const {
    const auto it = _animIndexByName.find(boost::to_lower_copy(name));
    if (it == _animIndexByName.end()) {
        return std::nullopt;
    }
    return _anims[it->second];
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
    const std::vector<std::string> &columnNames = combatAnimDa.columns();
    if (columnNames.size() < 2 || columnNames.front() != "hits") {
        throw ValidationException(
            "combatanimations.2da: invalid attack columns");
    }

    std::vector<CombatAnimColumn> columns = parseCombatAnimColumns(combatAnimDa);
    const std::vector<TwoDA::Row> &rows = combatAnimDa.rows();

    for (int row = 0; row < combatAnimDa.getRowCount(); ++row) {
        const std::string &rowLabel = rows[row].label;
        size_t parsedLength = 0;
        unsigned long animationId;
        try {
            animationId = std::stoul(rowLabel, &parsedLength, 10);
        } catch (const std::exception &) {
            throw ValidationException(
                "combatanimations.2da: invalid row label " + rowLabel);
        }
        if (parsedLength != rowLabel.size() || animationId >= _anims.size()) {
            throw ValidationException(
                "combatanimations.2da: invalid animation row " + rowLabel);
        }

        const Anim &attackAnim = _anims[animationId];
        // Store every column after `hits` in source order. getMeleeImpactTime()
        // indexes this sequence by the attack's zero-based position in the round.
        std::vector<int> impactTimes;
        impactTimes.reserve(columnNames.size() - 1);
        for (size_t column = 1; column < columnNames.size(); ++column) {
            impactTimes.push_back(combatAnimDa.getInt(
                row,
                columnNames[column],
                0));
        }
        if (!_meleeImpactTimes.emplace(
                                  attackAnim.name,
                                  std::move(impactTimes))
                 .second) {
            throw ValidationException(
                "combatanimations.2da: duplicate attack animation " +
                attackAnim.name);
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
    }
}

void Animations::parseDialogAnims(TwoDA &dialogAnimDa) {
    for (int row = 0; row < dialogAnimDa.getRowCount(); ++row) {
        DialogAnimation anim;
        anim.name = boost::to_lower_copy(dialogAnimDa.getString(row, "name"));
        anim.dialog = isDialogAnimationRow(dialogAnimDa, row);
        _dialogAnims.push_back(std::move(anim));
    }
}

void Animations::init() {
    parseAnims(*getRequiredTwoDA(_twoDas, "animations"));
    parseCombatAnim(*getRequiredTwoDA(_twoDas, "combatanimations"));
    parseDialogAnims(*getRequiredTwoDA(_twoDas, "dialoganimations"));
}

void Animations::clear() {
    _anims.clear();
    _animIndexByName.clear();
    _attackResults.clear();
    _meleeImpactTimes.clear();
    _dialogAnims.clear();
}

std::string Animations::getNameById(uint32_t id) const {
    if (id >= _anims.size()) {
        return std::string();
    }
    return _anims[id].name;
}

std::string Animations::getReactionAnimation(const std::string &attackAnim,
                                             CreatureWieldType targetWield,
                                             AttackReaction reaction) const {
    // A swing without a row, or a row with no clip for the reaction and the
    // target's wield, makes the target play the first row (walk).
    static constexpr uint32_t kWalkAnimation = 0;
    auto it = _attackResults.find({attackAnim, targetWield});
    if (it == _attackResults.end()) {
        return getNameById(kWalkAnimation);
    }
    uint32_t id = kNoAnim;
    switch (reaction) {
    case AttackReaction::Dodge:
        id = it->second.dodge;
        break;
    case AttackReaction::Parry:
        id = it->second.parry;
        break;
    case AttackReaction::Damage:
        id = it->second.damage;
        break;
    }
    return getNameById(id == kNoAnim ? kWalkAnimation : id);
}

int Animations::getMeleeImpactTime(
    const std::string &attackAnim,
    size_t attackIndex) const {
    // A swing with no timing row or cell lands at once.
    auto it = _meleeImpactTimes.find(attackAnim);
    if (it == _meleeImpactTimes.end()) {
        return 0;
    }
    return attackIndex < it->second.size() ? it->second[attackIndex] : 0;
}

bool Animations::isLoopingById(uint32_t id) const {
    return id < _anims.size() && _anims[id].looping;
}

bool Animations::isFireForgetById(uint32_t id) const {
    return id < _anims.size() && _anims[id].fireForget;
}

bool Animations::isOverlay(const std::string &name) const {
    const auto anim = findByName(name);
    return anim && anim->get().overlay;
}

bool Animations::hidesEquippedItems(const std::string &name) const {
    const auto anim = findByName(name);
    return anim && anim->get().hidesEquippedItems;
}

bool Animations::isParry(const std::string &name) const {
    const auto anim = findByName(name);
    return anim && anim->get().parry;
}

bool Animations::isDialogAnimation(int id) const {
    if (id >= kDialogAnimationBase && id < kDialogAnimationBase + static_cast<int>(_dialogAnims.size())) {
        return _dialogAnims[id - kDialogAnimationBase].dialog;
    }
    if (id >= 0 && id < std::min(kDialogAnimationBase, static_cast<int>(_anims.size()))) {
        return _anims[id].dialog;
    }
    return isDialogCutClip(id);
}

std::optional<std::reference_wrapper<const DialogAnimation>> Animations::findDialogAnimation(int row) const {
    if (row < 0 || row >= static_cast<int>(_dialogAnims.size())) {
        return std::nullopt;
    }
    return _dialogAnims[row];
}

} // namespace game

} // namespace reone
