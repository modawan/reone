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

#include "reone/game/object/trap.h"

#include <algorithm>

#include <boost/algorithm/string.hpp>

#include "reone/game/game.h"
#include "reone/game/messagelog.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/trigger.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"
#include "reone/game/script/runner.h"
#include "reone/game/twodautil.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/resource/provider/2das.h"

using namespace reone::resource;

namespace reone {

namespace game {

static constexpr int kTrapTriggeredStrRef = 1461;
// An armed trap goes off on an attacker that strikes it from within this range.
static constexpr float kStrikeRange = 4.0f;
// A DC is a byte: it takes one of this many values.
static constexpr int kByteDCValues = 256;

// Mine blasts
static constexpr float kMineBlastDelay = 3.0f;
static constexpr int kMineBlastRoll = 20;
static constexpr int kMineBlastSuccessStrRef = 128534;
static constexpr int kMineBlastFailureStrRef = 128533;
static constexpr int kMineBlastReportStrRef = 128535;
// The report's custom tokens.
static constexpr int kMineBlastResultToken = 1;
static constexpr int kMineBlastTotalToken = 2;
static constexpr int kMineBlastRollToken = 3;
static constexpr int kMineBlastRankToken = 4;
static constexpr int kMineBlastBonusToken = 5;
static constexpr int kMineBlastDCToken = 6;

// A door's detect DC takes the difficulty shift in full; its disarm DC and
// both of a placeable's are bytes. A trap trigger's DCs come from its type and
// are whole numbers. A door's trap can be found and disarmed unless its record
// says otherwise; a trap trigger's only when it says so. A placeable's trap is
// neither detectable, disarmable nor one-shot unless its record says so.
Trap::Trap(Carrier carrier) :
    _detectRule(carrier == Carrier::Door        ? DCRule::ClampedByte
                : carrier == Carrier::Placeable ? DCRule::Byte
                                                : DCRule::Whole),
    _disarmRule(carrier == Carrier::Trigger ? DCRule::Whole : DCRule::Byte),
    _detectable(carrier == Carrier::Door),
    _disarmable(carrier == Carrier::Door),
    _oneShot(carrier != Carrier::Placeable) {
}

std::vector<TrapTypeRow> Trap::readTypes(ITwoDAs &twoDas) {
    auto traps = getRequiredTwoDA(twoDas, "traps");
    std::vector<TrapTypeRow> types;
    for (int type = 0; type < traps->getRowCount(); ++type) {
        TrapTypeRow row;
        row.script = boost::to_lower_copy(traps->getString(type, "trapscript"));
        row.model = boost::to_lower_copy(traps->getString(type, "model"));
        row.kitResRef = boost::to_lower_copy(traps->getString(type, "resref"));
        row.nameStrRef = traps->getIntOpt(type, "trapname");
        row.setDC = traps->getIntOpt(type, "setdc");
        row.detectDCMod = traps->getIntOpt(type, "detectdcmod");
        row.disarmDCMod = traps->getIntOpt(type, "disarmdcmod");
        types.push_back(std::move(row));
    }
    return types;
}

bool Trap::isHostileTo(const Object &trapped, Faction faction, const Creature &creature) {
    return getObjectReputation(trapped, creature, trapped.game()) <= kHostileStanding &&
           faction != creature.faction();
}

void Trap::reportTriggered(Game &game) {
    game.messageLog().add(
        MessageLog::kFeedbackMessageType,
        MessageLog::Style::Normal,
        game.getFeedbackText(kTrapTriggeredStrRef));
}

void Trap::deserialize(const Gff &gff) {
    uint8_t type = 0;
    if (gff.readByte(type, "TrapType")) {
        _type = type;
    }
    gff.readBool(_detectable, "TrapDetectable");
    gff.readBool(_disarmable, "TrapDisarmable");
    gff.readBool(_oneShot, "TrapOneShot");
    gff.readInt(_ownerDemolitionsSkill, "OwnerDemolitions");
    gff.readResRef(_triggeredScript, "OnTrapTriggered");
    gff.readResRef(_disarmScript, "OnDisarm");
}

void Trap::deserializeArming(const Gff &gff, const Game &game, bool keepShiftedDCs) {
    uint8_t armed = 0;
    if (gff.readByte(armed, "TrapFlag")) {
        _armed = armed != 0;
    }
    uint8_t dc = 0;
    if (gff.readByte(dc, "DisarmDC")) {
        _disarmDC = dc;
    } else if (keepShiftedDCs) {
        _disarmDC = disarmDC(game);
    }
    if (gff.readByte(dc, "TrapDetectDC")) {
        _detectDC = dc;
    } else if (keepShiftedDCs) {
        _detectDC = detectDC(game);
    }
}

void Trap::loadTypeDCs(const Game &game) {
    const auto &row = game.trapType(_type);
    _disarmDC = row.disarmDCMod.value_or(_disarmDC);
    _detectDC = row.detectDCMod.value_or(_detectDC);
}

int Trap::shiftDC(int dc, int shift, DCRule rule) {
    switch (rule) {
    case DCRule::Byte: {
        const int wrapped = ((dc + shift) % kByteDCValues + kByteDCValues) % kByteDCValues;
        return wrapped == 0 ? 1 : wrapped;
    }
    case DCRule::ClampedByte: {
        const int shifted = dc + shift;
        return shifted <= 0 ? 1 : shifted % kByteDCValues;
    }
    case DCRule::Whole:
    default:
        return std::max(dc + shift, 1);
    }
}

int Trap::shiftedDC(int dc, DCRule rule, const Game &game) const {
    return game.isTSL() ? shiftDC(dc, game.trapDifficultyModifier(), rule) : dc;
}

int Trap::detectDC(const Game &game) const {
    return shiftedDC(_detectDC, _detectRule, game);
}

int Trap::disarmDC(const Game &game) const {
    return shiftedDC(_disarmDC, _disarmRule, game);
}

void Trap::spring(Object &owner, Faction faction, const Creature &caller, bool force) {
    if (caller.hasEffectImmunity(ImmunityType::Trap)) {
        return;
    }
    if (!force && !isHostileTo(owner, faction, caller)) {
        return;
    }
    auto &game = owner.game();
    if (_armed) {
        auto leader = game.party().getLeader();
        if (leader.get() == &caller) {
            reportTriggered(game);
        }
        // The door or placeable keeps the creature that set its trap off.
        if (auto *door = dyn_cast<Door>(&owner)) {
            door->setLastEnteredId(caller.id());
        } else if (auto *placeable = dyn_cast<Placeable>(&owner)) {
            placeable->setLastEnteredId(caller.id());
        }
        game.scriptRunner().run(_triggeredScript, owner.id());
        if (_oneShot) {
            _armed = false;
        }
    }
    _detection.detectedBy.clear();
    removeLinkedMine(game);
}

void Trap::springOnStrike(Object &owner, Faction faction, uint32_t attackerId) {
    if (!_armed) {
        return;
    }
    auto attacker = owner.game().getObjectById<Creature>(attackerId);
    if (attacker && owner.getSquareDistanceTo(*attacker) <= kStrikeRange * kStrikeRange) {
        spring(owner, faction, *attacker, false);
    }
}

void Trap::disarm(Object &owner, const Object &caller) {
    if (_armed) {
        _armed = false;
        runDisarmScript(owner, caller);
    }
    _detection.detectedBy.clear();
}

void Trap::runDisarmScript(Object &owner, const Object &caller) {
    owner.game().scriptRunner().run(_disarmScript, {{script::ArgKind::Caller, script::Variable::ofObject(owner.id())},
                                                    {script::ArgKind::LastDisarmed, script::Variable::ofObject(caller.id())}});
}

void Trap::armMine(
    const Game &game,
    int trapType,
    int detectDC,
    int disarmDC,
    int ownerDemolitionsSkill,
    const std::shared_ptr<Trigger> &linkedMine,
    const std::shared_ptr<Creature> &setter,
    int blastBonus,
    bool blast) {
    _armed = true;
    _type = trapType;
    _triggeredScript = game.trapType(trapType).script;
    _detectDC = detectDC;
    _disarmDC = disarmDC;
    _detectable = true;
    _oneShot = true;
    _ownerDemolitionsSkill = ownerDemolitionsSkill;
    _disarmable = !game.isTSL();
    _linkedMine = linkedMine;
    _mineSetter = setter;
    _mineBlastBonus = blastBonus;
    _mineBlastDelay = blast ? std::optional<float>(kMineBlastDelay) : std::nullopt;
}

void Trap::setUpMine(const Game &game, int trapType, int detectDC, int disarmDC, int ownerDemolitionsSkill) {
    _type = trapType;
    _detectable = true;
    _disarmable = true;
    _detectDC = detectDC;
    _disarmDC = disarmDC;
    _ownerDemolitionsSkill = ownerDemolitionsSkill;
    _triggeredScript = game.trapType(trapType).script;
}

void Trap::hide() {
    _detectable = false;
    _disarmable = false;
}

std::optional<MineBlast> Trap::updateMineBlast(float dt, int rankDivisor, int lockDC) {
    if (!_mineBlastDelay) {
        return std::nullopt;
    }
    *_mineBlastDelay -= dt;
    if (*_mineBlastDelay >= 0.0f) {
        return std::nullopt;
    }
    _mineBlastDelay.reset();
    if (!_armed) {
        return std::nullopt;
    }
    auto setter = _mineSetter.resolve();
    MineBlast blast;
    blast.rank = setter ? setter->getUnopposedSkillRank(SkillType::Demolitions) : 0;
    blast.total = blast.rank / rankDivisor + _mineBlastBonus + kMineBlastRoll;
    blast.lockDC = lockDC;
    blast.opens = blast.total >= lockDC;
    return blast;
}

void Trap::reportMineBlast(Game &game, const MineBlast &blast) {
    game.setCustomToken(kMineBlastResultToken,
                        game.getFeedbackText(blast.opens ? kMineBlastSuccessStrRef : kMineBlastFailureStrRef));
    game.setCustomToken(kMineBlastTotalToken, std::to_string(blast.total));
    game.setCustomToken(kMineBlastRollToken, std::to_string(kMineBlastRoll));
    game.setCustomToken(kMineBlastRankToken, std::to_string(blast.rank));
    game.setCustomToken(kMineBlastBonusToken, std::to_string(_mineBlastBonus));
    game.setCustomToken(kMineBlastDCToken, std::to_string(blast.lockDC));
    game.messageLog().add(
        MessageLog::kFeedbackMessageType,
        MessageLog::Style::Normal,
        game.getFeedbackText(kMineBlastReportStrRef));
    _armed = false;
    _detection.detectedBy.clear();
    removeLinkedMine(game);
}

// A linked mine still standing is destroyed with the area's other objects.
void Trap::removeLinkedMine(Game &game) {
    if (auto mine = _linkedMine.resolve()) {
        game.module()->area()->destroyObject(*mine);
    }
    _linkedMine.reset();
}

} // namespace game

} // namespace reone
