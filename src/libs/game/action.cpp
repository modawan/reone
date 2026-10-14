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

#include "reone/game/action.h"

#include <algorithm>

#include "reone/game/action/castspell.h"
#include "reone/game/action/movetoobject.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/attack.h"
#include "reone/game/d20/spell.h"
#include "reone/game/object.h"
#include "reone/system/logutil.h"

namespace reone {

namespace game {

void Action::requireRuntimeObject(const std::shared_ptr<Object> &object) {
    if (object) {
        _runtimeDependencies.emplace_back(object);
    }
}

bool Action::runtimeDependenciesLive() const {
    return std::all_of(
        _runtimeDependencies.begin(),
        _runtimeDependencies.end(),
        [](const auto &reference) { return reference.resolve() != nullptr; });
}

void Action::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    warn("Action execution not implemented: " + std::to_string(static_cast<int>(_type)));
    complete();
}

uint32_t Action::serializedActionId() const {
    if (_savedAction) {
        return _savedAction->actionId;
    }
    auto type = SavedActionType::Invalid;
    switch (_type) {
    case ActionType::MoveToPoint:
    case ActionType::MoveToLocation:
        type = SavedActionType::MoveToPoint;
        break;
    case ActionType::PlayAnimation:
        type = SavedActionType::PlayAnimation;
        break;
    case ActionType::EquipItem:
        type = SavedActionType::EquipItem;
        break;
    case ActionType::UnequipItem:
        type = SavedActionType::UnequipItem;
        break;
    case ActionType::AttackObject:
        type = SavedActionType::AttackObject;
        break;
    case ActionType::UseFeat:
        // A physical attack feat is an attack; any other feat use is no
        // command a save represents.
        if (isPhysicalAttackFeat(static_cast<const UseFeatAction &>(*this).feat())) {
            type = SavedActionType::AttackObject;
        }
        break;
    case ActionType::CastSpellAtObject:
    case ActionType::CastSpellAtLocation:
        type = SavedActionType::CastSpell;
        break;
    case ActionType::MoveToObject:
        type = static_cast<const MoveToObjectAction &>(*this).usesPointPath() ? SavedActionType::MoveToPoint : SavedActionType::MoveToObject;
        break;
    case ActionType::PickUpItem:
        type = SavedActionType::PickUpItem;
        break;
    case ActionType::PutDownItem:
        type = SavedActionType::PutDownItem;
        break;
    case ActionType::OpenDoor:
        type = SavedActionType::OpenDoor;
        break;
    case ActionType::CloseDoor:
        type = SavedActionType::CloseDoor;
        break;
    case ActionType::StartConversation:
        type = SavedActionType::StartConversation;
        break;
    case ActionType::Wait:
        type = SavedActionType::Wait;
        break;
    case ActionType::DoCommand:
        type = SavedActionType::DoCommand;
        break;
    case ActionType::OpenLock:
        type = SavedActionType::OpenLock;
        break;
    case ActionType::Lock:
        type = SavedActionType::Lock;
        break;
    case ActionType::OpenContainer:
        type = SavedActionType::OpenContainer;
        break;
    case ActionType::Follow:
    case ActionType::ForceFollowObject:
        type = SavedActionType::Follow;
        break;
    case ActionType::FollowLeader:
        type = SavedActionType::FollowLeader;
        break;
    case ActionType::FollowOwner:
        type = SavedActionType::FollowOwner;
        break;
    default:
        break;
    }
    return static_cast<uint32_t>(type);
}

std::optional<int> scriptActionNumber(uint32_t actionId, bool tsl) {
    switch (static_cast<SavedActionType>(actionId)) {
    case SavedActionType::MoveToPoint:
        return 0;
    case SavedActionType::PickUpItem:
        return 1;
    case SavedActionType::PutDownItem:
        return 2;
    case SavedActionType::AttackObject:
        return 3;
    case SavedActionType::CastSpell:
        return 4;
    case SavedActionType::OpenDoor:
        return 5;
    case SavedActionType::CloseDoor:
        return 6;
    case SavedActionType::StartConversation:
        return 7;
    case SavedActionType::DisarmMine:
        return 8;
    case SavedActionType::RecoverMine:
        return 9;
    case SavedActionType::FlagMine:
        return 10;
    case SavedActionType::ExamineMine:
        return 11;
    case SavedActionType::SetMine:
        return 12;
    case SavedActionType::Wait:
        return 36;
    case SavedActionType::OpenLock:
        return 13;
    case SavedActionType::Lock:
        return 14;
    case SavedActionType::OpenContainer:
        return 15;
    case SavedActionType::AnimalEmpathy:
        return 16;
    case SavedActionType::Rest:
        return 17;
    case SavedActionType::Taunt:
        return 18;
    case SavedActionType::ItemCastSpell:
        return 19;
    case SavedActionType::Counterspell:
        return 31;
    case SavedActionType::PickPocket:
        return 34;
    case SavedActionType::Follow:
        return 35;
    case SavedActionType::Heal:
        return 33;
    case SavedActionType::FollowLeader:
        return 38;
    case SavedActionType::CombatDispatch:
        return 39;
    default:
        break;
    }
    if (!tsl) {
        return std::nullopt;
    }
    // The commands TSL adds.
    static constexpr uint32_t kFirstTSLAction = 67;
    if (actionId < kFirstTSLAction || actionId > static_cast<uint32_t>(SavedActionType::SwitchWeapons)) {
        return std::nullopt;
    }
    static constexpr int kFirstTSLScriptAction = 40;
    return kFirstTSLScriptAction + static_cast<int>(actionId - kFirstTSLAction);
}

bool isHostileAction(Action &action) {
    switch (action.type()) {
    case ActionType::AttackObject:
        return true;
    case ActionType::UseFeat:
        return isPhysicalAttackFeat(static_cast<UseFeatAction &>(action).feat());
    case ActionType::CastSpellAtObject:
    case ActionType::CastSpellAtLocation:
        return static_cast<CastSpellAction &>(action).spell()->hostile;
    default:
        break;
    }
    return false;
}

} // namespace game

} // namespace reone
