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

#include "../action.h"
#include "../object/item.h"

namespace reone {

namespace game {

enum class SkillResult;
class Creature;
class Trigger;

/**
 * Use of a skill on a target: Demolitions sets a mine from a kit, or
 * disarms, recovers, flags or examines one, and Treat Injury heals a creature
 * with a medical item. Security is an unlock (UnlockObjectAction).
 */
class UseSkillAction : public Action {
public:
    UseSkillAction(Game &game,
                   ServicesView &services,
                   SkillType skill,
                   std::shared_ptr<Object> target,
                   int subSkill = 0,
                   std::shared_ptr<Item> itemUsed = nullptr) :
        Action(game, services, ActionType::UseSkill),
        _skill(skill),
        _target(std::move(target)),
        _subSkill(subSkill),
        _itemUsed(std::move(itemUsed)) {
        requireRuntimeObject(_target);
        requireRuntimeObject(_itemUsed);
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::UseSkill;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    /** Clearing mine work or a heal part-way returns the actor to its ordinary animation. */
    bool cancel(std::shared_ptr<Action> self, Object &actor) override;

    SkillType skill() const { return _skill; }
    /**
     * Demolitions sets, disarms, recovers, flags or examines a mine; Treat
     * Injury heals.
     */
    uint32_t serializedActionId() const override;
    /**
     * Only what the skill is used on and with is kept: an action taken up
     * again does all of its work afresh, and walks up again unless it is a
     * heal that already had.
     */
    std::optional<SavedActionRecord> saveFacingState() const override;
    /**
     * A saved skill command: a mine worked on, a mine set or a heal.
     * None for a mine set whose kit is gone.
     */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);
    /** A heal taken up again after its approach works where the healer stands. */
    void skipApproach();

private:
    SkillType _skill;
    std::shared_ptr<Object> _target;
    int _subSkill;
    std::shared_ptr<Item> _itemUsed;

    /**
     * Demolitions: approach, then the queued work clip, then resolve.
     * Treat Injury: approach, then work for a while, then resolve.
     */
    int _phase {0};
    float _workTime {0.0f};

    void executeDemolitions(Creature &actor, float dt);
    bool setsMine() const;
    void resolveMine(Creature &actor);
    /** The trap is told the actor disarmed it (script event 24). */
    void signalDisarmed(Object &trap, Creature &actor);
    /** The target door or placeable leaves the area's traps. */
    void leaveAreaTraps();
    /** A disarmed or recovered mine goes with its disarm, once anything queued for it before has run. */
    void removeDisarmedMine(Trigger &mine, Creature &actor);
    void resolveSetMine(Creature &actor);
    void executeTreatInjury(Creature &actor, float dt);
    void resolveHeal(Creature &actor, Creature &target);
    /** Tells the controlling player the skill's roll against \p dc and its result, over \p target too for some. */
    void reportSkill(Creature &actor, const std::shared_ptr<Object> &target, int actionStrRef, int roll, int rank, int dc,
                     SkillResult result) const;
};

} // namespace game

} // namespace reone
