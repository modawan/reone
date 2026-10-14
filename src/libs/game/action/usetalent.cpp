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

#include "reone/game/action/castspellatlocation.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/action/useskill.h"
#include "reone/game/action/usetalentatlocation.h"
#include "reone/game/action/usetalentonobject.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"

namespace reone {

namespace game {

void UseTalentOnObjectAction::dispatchToAction(Object &actor) {
    assert(!_action && "dispatchToAction is called twice");
    const auto *creature = dyn_cast<Creature>(&actor);
    if (!creature || !_target || !isa<Creature>(_target.get()) || !actor.isCommandable()) {
        return;
    }
    switch (_chosenTalent->type()) {
    case TalentType::Feat: {
        _action = _game.newAction<UseFeatAction>(
            static_cast<FeatType>(_chosenTalent->value()),
            _target);
        break;
    }
    case TalentType::Spell: {
        auto spell = _services.game.spells.get(static_cast<SpellType>(_chosenTalent->value()));
        if (!spell) {
            return;
        }
        std::optional<std::shared_ptr<Item>> item;
        std::optional<size_t> property;
        std::optional<int> casterLevel;
        if (_chosenTalent->item() != script::kObjectInvalid && _chosenTalent->itemPropertyIndex() != -1) {
            auto source = _game.getObjectById<Item>(_chosenTalent->item());
            if (!source) {
                return;
            }
            item = std::move(source);
            property = static_cast<size_t>(_chosenTalent->itemPropertyIndex());
            if (_chosenTalent->casterLevel() != kUnspecifiedCasterLevel) {
                casterLevel = _chosenTalent->casterLevel();
            }
        } else {
            // Look for an item power source before submitting a class-based or
            // spell-like-ability cast, including for a plain spell talent.
            if (const auto source = creature->itemForPower(_chosenTalent->value())) {
                item = source->first;
                property = source->second;
            }
        }
        const std::optional<SpellSelection> selection = item ? std::nullopt
                                                             : std::optional<SpellSelection> {SpellSelection {_chosenTalent->castingClass(), _chosenTalent->casterLevel()}};
        auto cast = _game.newAction<CastSpellAtObjectAction>(
            std::move(spell), _target, item, /*cheat=*/false,
            ProjectilePathType::Default, /*instantSpell=*/false, property, casterLevel, selection);
        // The command carries where its target stands now.
        cast->setCommandLocation(_target->position());
        _action = std::move(cast);
        break;
    }
    case TalentType::Skill:
        _action = _game.newAction<UseSkillAction>(static_cast<SkillType>(_chosenTalent->value()), _target);
        break;
    default:
        break;
    }
}

void UseTalentOnObjectAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (isCompleted() || isCancelled()) {
        return;
    }
    if (!_action) {
        complete();
        return;
    }

    _action->execute(self, actor, dt);
    if (_action->locked()) {
        lock();
    }
    setClearable(_action->isClearable());

    if (_action->isCompleted()) {
        complete();
    }
}

bool UseTalentOnObjectAction::cancel(std::shared_ptr<Action>, Object &actor) {
    if (_action) {
        if (!_action->cancel(_action, actor)) {
            return false;
        }
        _action->markCancelled();
        _action->complete();
    }
    complete();
    return true;
}

std::optional<SavedActionRecord> UseTalentOnObjectAction::saveFacingState() const {
    // The talent is saved as the action it stands for: a physical feat as an
    // attack, a spell as a cast.
    return _action ? _action->saveFacingState() : std::nullopt;
}

void UseTalentAtLocationAction::dispatchToAction(Object &actor) {
    assert(!_action && "dispatchToAction is called twice");
    const auto *creature = dyn_cast<Creature>(&actor);
    if (!creature || !actor.isCommandable()) {
        return;
    }
    // The location dispatcher only emits spell/item actions. Feat and skill
    // talent kinds return without inserting work in both titles.
    if (_chosenTalent->type() != TalentType::Spell) {
        return;
    }

    std::optional<std::shared_ptr<Item>> item;
    std::optional<size_t> property;
    std::optional<int> itemLevel;
    if (_chosenTalent->item() != script::kObjectInvalid && _chosenTalent->itemPropertyIndex() != -1) {
        item = _game.getObjectById<Item>(_chosenTalent->item());
        property = static_cast<size_t>(_chosenTalent->itemPropertyIndex());
        if (_chosenTalent->casterLevel() != kUnspecifiedCasterLevel) {
            itemLevel = _chosenTalent->casterLevel();
        }
    } else if (const auto source = creature->itemForPower(_chosenTalent->value())) {
        item = source->first;
        property = source->second;
    }
    if (item) {
        // Both explicit and discovered item routes clear the existing commands
        // before UseItem. A normal class/SLA cast does not take this branch.
        actor.clearAllActions(true);
        if (!*item || *property >= (*item)->properties().size()) {
            return;
        }
    }
    const auto spellId = item ? (*item)->properties()[*property].subtype : _chosenTalent->value();
    auto spell = _services.game.spells.get(static_cast<SpellType>(spellId));
    if (!spell) {
        return;
    }
    const auto selection = item ? std::nullopt : std::optional<SpellSelection> {SpellSelection {_chosenTalent->castingClass(), _chosenTalent->casterLevel()}};
    _action = _game.newAction<CastSpellAtLocationAction>(std::move(spell), _targetLocation,
                                                         /*cheat=*/false, ProjectilePathType::Default, /*instantSpell=*/false, item, property, itemLevel, selection);
}

void UseTalentAtLocationAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (isCompleted() || isCancelled()) {
        return;
    }
    if (!_action) {
        complete();
        return;
    }

    _action->execute(self, actor, dt);
    if (_action->locked()) {
        lock();
    }
    setClearable(_action->isClearable());

    if (_action->isCompleted()) {
        complete();
    }
}

bool UseTalentAtLocationAction::cancel(std::shared_ptr<Action>, Object &actor) {
    if (_action) {
        if (!_action->cancel(_action, actor)) {
            return false;
        }
        _action->markCancelled();
        _action->complete();
    }
    complete();
    return true;
}

std::optional<SavedActionRecord> UseTalentAtLocationAction::saveFacingState() const {
    // Save the emitted command, not an additional talent-dispatch layer.
    return _action ? _action->saveFacingState() : std::nullopt;
}

} // namespace game

} // namespace reone
