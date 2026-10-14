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

#include "reone/game/action/castspell.h"

#include <algorithm>

#include "reone/game/action/castspellatlocation.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/combat.h"
#include "reone/game/d20/spells.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"
#include "reone/game/projectiles.h"
#include "reone/system/logutil.h"

namespace reone {

namespace game {

CastSpellAction::CastSpellAction(
    Game &game,
    ServicesView &services,
    ActionType type,
    std::shared_ptr<Spell> spell,
    std::shared_ptr<Object> target,
    const glm::vec3 &location,
    std::optional<std::shared_ptr<Item>> item,
    bool cheat,
    ProjectilePathType projectilePathType,
    bool instantSpell,
    std::optional<size_t> itemProperty,
    std::optional<int> itemCasterLevel,
    std::optional<SpellSelection> selection,
    std::optional<FeatType> associatedFeat,
    bool fake) :
    Action(game, services, type),
    _spell(std::move(spell)),
    _target(std::move(target)),
    _location(location),
    _item(std::move(item)),
    _itemProperty(itemProperty),
    _itemCasterLevel(itemCasterLevel),
    _selection(selection),
    _associatedFeat(associatedFeat),
    _cheat(cheat),
    _projectilePathType(normalizeProjectilePath(projectilePathType)),
    _instantSpell(instantSpell),
    _fake(fake),
    _schedule(_spell->conjTime, _spell->castTime, _spell->catchTime) {
    requireRuntimeObject(_target);
    if (_item && *_item) {
        requireRuntimeObject(*_item);
        if (!_itemProperty) {
            _itemProperty = (*_item)->spellProperty(_spell->type);
        }
        _itemType = (*_item)->itemType();
    }
}

CastSpellAtObjectAction::CastSpellAtObjectAction(
    Game &game,
    ServicesView &services,
    std::shared_ptr<Spell> spell,
    std::shared_ptr<Object> target,
    std::optional<std::shared_ptr<Item>> item,
    bool cheat,
    ProjectilePathType projectilePathType,
    bool instantSpell,
    std::optional<size_t> itemProperty,
    std::optional<int> itemCasterLevel,
    std::optional<SpellSelection> selection,
    std::optional<FeatType> associatedFeat,
    bool fake) :
    CastSpellAction(game, services, ActionType::CastSpellAtObject, std::move(spell), std::move(target),
                    glm::vec3 {0.0f}, std::move(item), cheat, projectilePathType, instantSpell, itemProperty,
                    itemCasterLevel, selection, associatedFeat, fake) {
}

CastSpellAtLocationAction::CastSpellAtLocationAction(
    Game &game,
    ServicesView &services,
    std::shared_ptr<Spell> spell,
    const std::shared_ptr<Location> &targetLocation,
    bool cheat,
    ProjectilePathType projectilePathType,
    bool instantSpell,
    std::optional<std::shared_ptr<Item>> item,
    std::optional<size_t> itemProperty,
    std::optional<int> itemCasterLevel,
    std::optional<SpellSelection> selection,
    std::optional<FeatType> associatedFeat,
    bool fake) :
    CastSpellAction(game, services, ActionType::CastSpellAtLocation, std::move(spell), nullptr,
                    targetLocation->position(), std::move(item), cheat, projectilePathType, instantSpell,
                    itemProperty, itemCasterLevel, selection, associatedFeat, fake) {
}

std::shared_ptr<Action> CastSpellAtObjectAction::toLocationUse() const {
    auto use = _game.newAction<CastSpellAtLocationAction>(
        _spell, std::make_shared<Location>(_location, 0.0f), _cheat, _projectilePathType, _instantSpell, _item,
        _itemProperty, _itemCasterLevel, _selection, _associatedFeat, _fake);
    use->setUserAction(isUserAction());
    return use;
}

glm::vec3 CastSpellAction::aimPosition() const {
    return _target ? _target->position() : _location;
}

std::optional<std::reference_wrapper<const Object>> CastSpellAction::targetObject() const {
    if (!_target) {
        return std::nullopt;
    }
    return std::cref(*_target);
}

std::shared_ptr<Object> CastSpellAction::activeSpellTarget(const Object &actor) const {
    return _target ? _target : _game.getObjectById(actor.spatialArea()->get().id());
}

void CastSpellAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto *creature = dyn_cast<Creature>(&actor);
    if (isCompleted() || isCancelled()) {
        return;
    }
    if (creature) {
        _game.party().setCombatMessage(*creature, _game.party().idleCombatMessage());
    }
    // An item use fails, leaving its user posed, once its item or its spell is
    // gone, or once the item has no use left before it takes effect.
    if (_item && !_dispatched) {
        if (!itemAvailable()) {
            const bool noUseLeft = *_item && (*_item)->isRuntimeLive() && _itemProperty &&
                                   *_itemProperty < (*_item)->properties().size() && !(*_item)->hasSpellUse(*_itemProperty);
            finish(actor);
            presentFailedItemUse(_game, actor, noUseLeft);
            return;
        }
        const auto propertySpell = static_cast<SpellType>((*_item)->properties()[*_itemProperty].subtype);
        const auto effectiveSpell = itemSpellForActor(actor, propertySpell);
        if (effectiveSpell != _spell->type) {
            const auto definition = _services.game.spells.get(effectiveSpell);
            if (!definition) {
                finish(actor);
                presentFailedItemUse(_game, actor);
                return;
            }
            _spell = definition;
            // A started item use keeps its item type's times.
            if (!_started) {
                _schedule.setDurations(_spell->conjTime, _spell->castTime, _spell->catchTime);
            }
            if (_committed) {
                _castContext.spellId = static_cast<int>(effectiveSpell);
            }
        }
    }
    // After its release too, as soon as its item is gone, unless the item's
    // use plays on without it.
    if (_item && _dispatched && !(*_item && (*_item)->isRuntimeLive()) && itemUseEndsWithItem(*_itemType)) {
        finish(actor);
        presentFailedItemUse(_game, actor);
        return;
    }
    if (_restorePresentation) {
        _restorePresentation = false;
        if (_started || _committed) {
            auto &context = actor.spellScriptContext();
            // The spell context is published again unless the target object
            // or the item is gone.
            if (!context.restored() && (!_target || runtimeDependenciesLive())) {
                publishSpellContext(actor);
            }
            _ownsSpellTarget = context.activeTarget() != nullptr;
        }
        if (_started && creature) {
            if (_ownsMovementRestriction) {
                creature->setMovementRestricted(true);
            }
            _presenter.restore(*creature, *_spell, _itemType, Creature::asCreature(_target), aimPosition(), _schedule);
        }
    }
    // A party member at zero vitality stops casting; a cast that can no longer
    // be cleared runs on through the other states that stop actions. A cast
    // also fails once its target creature is lost, and a power once its
    // caster's equipment no longer allows it. A failed item use leaves its
    // user posed. A released use that plays on without its item no longer
    // depends on it, nor on its target.
    const bool playsOn = _item && _dispatched && !itemUseEndsWithItem(*_itemType);
    if (actor.isDead() ||
        (!playsOn && (!runtimeDependenciesLive() || (_target && spellTargetLost(actor, *_target)))) ||
        (creature && (creature->isTemporarilyDead() ||
                      ((!_started || isClearable()) && !creature->canCastSpells()) ||
                      (!_item && !equipmentAllowsSpell(*creature, *_spell))))) {
        finish(actor);
        if (_item) {
            presentFailedItemUse(_game, actor);
        }
        return;
    }
    // A caster that already acted in a round that has not ended waits for its end.
    if (!_started && creature && _game.combat().awaitsRoundEnd(*creature, *self)) {
        return;
    }
    // A caster keeps turning toward the object its spell is aimed at; using an
    // item does not.
    if (creature && _target && !_item) {
        creature->setOrientationLock(_target->id());
    }
    if (creature && _associatedFeat && creature->featRemainingUses(*_associatedFeat) == 0) {
        finish(actor);
        return;
    }
    if (!_started) {
        if (!admitSpellCast(actor, *_spell, _cheat || _fake || _item.has_value(), _item.has_value(), _selection) ||
            !itemAvailable()) {
            finish(actor);
            return;
        }
        if (creature) {
            actor.spellScriptContext().setActiveTarget(activeSpellTarget(actor));
            _ownsSpellTarget = true;
        }
        if (!_cheat && !withinSpellRange(actor, *_spell, aimPosition(), targetObject())) {
            if (!creature || !creature->canMove()) {
                finish(actor);
                return;
            }
            creature->navigateTo(aimPosition(), true, spellRange(actor, *_spell, targetObject()), dt);
            // A target no path reaches fails the cast.
            if (creature->navigationFailed()) {
                finish(actor);
            }
            return;
        }
    }
    // Approach and casting share this action. Refresh the decision counter only after
    // approach completes.
    if (creature) {
        creature->refreshCombatDecisionTimer();
    }
    // A caster turns toward an object as it stands, and at once toward a location.
    if (creature) {
        if (_target) {
            creature->setDesiredFacingToward(_target->position());
        } else {
            creature->turnToward(_location);
        }
    }
    if (_instantSpell) {
        if (creature) {
            creature->beginSpellActivity(static_cast<int>(_spell->type), _item.has_value(), _spell->hostile);
            _game.combat().beginInstantCast(self, *creature, _target);
        }
        // An instant cast shows its release as any cast does.
        _presentationId = _services.game.projectiles.beginSpell(actor, targetObject(), aimPosition(), *_spell,
                                                                _projectilePathType, _game, _services);
        if (commit(actor)) {
            release(actor);
        }
        finish(actor);
        return;
    }
    std::optional<std::reference_wrapper<const CombatRound>> round;
    if (creature) {
        round = _game.combat().addAction(self, actor);
    }
    if (!_started && round && round->get().suspends(*self)) {
        return;
    }
    const auto state = round ? _schedule.update(round->get(), *self, dt) : _schedule.update(true, dt);
    std::optional<std::reference_wrapper<Creature>> targetCreature;
    if (_target && _target->type() == ObjectType::Creature) {
        targetCreature = static_cast<Creature &>(*_target);
    }
    // What the cast shows this frame; the conjure waits until the caster stands.
    if (creature && state != SpellSchedule::Conjure) {
        _presenter.update(*creature, *_spell, _itemType, targetCreature, aimPosition(), _schedule, state);
    }
    if (creature && !_item && _committed && isClearable() && state > SpellSchedule::Cast &&
        !continuePaidCast(*creature, *self)) {
        finish(actor);
        return;
    }

    switch (state) {
    case SpellSchedule::Conjure: {
        publishSpellContext(actor);
        _started = true;
        // An item use cannot be cleared once it has started.
        if (_item) {
            setClearable(false);
        }
        debug(str(boost::format("Spell started: actor=%u spell=%d") % actor.id() % static_cast<int>(_spell->type)), LogChannel::Combat);
        if (creature) {
            creature->beginSpellActivity(static_cast<int>(_spell->type), _item.has_value(), _spell->hostile, !_fake);
            float pause = _spell->conjTime + _spell->castTime + _spell->catchTime;
            if (_item) {
                // An item use takes effect and ends at its item type's times.
                const auto &use = _presenter.itemUse(*creature, *_spell, *_itemType, Creature::asCreature(_target), aimPosition());
                _schedule.setDurations(use.impact, std::max(0.0f, use.end - use.impact), 0.0f);
                pause = use.end;
            }
            // An item use holds a round it starts for as long as it lasts, without
            // holding the partner; a cast holds both for all three times.
            _game.combat().beginCast(self, *creature, pause, _item.has_value(), _fake);
            _ownsMovementRestriction = !creature->movementLockedByAction();
            creature->setMovementType(Creature::MovementType::None);
            creature->setMovementRestricted(true);
        }
        _presentationId = _services.game.projectiles.beginSpell(actor, targetObject(), aimPosition(), *_spell,
                                                                _projectilePathType, _game, _services);
        if (creature) {
            if (!_item) {
                creature->spellCastVisuals().showConjure(*creature, *_spell);
            }
            _presenter.update(*creature, *_spell, _itemType, targetCreature, aimPosition(), _schedule, state);
        }
        return;
    }
    case SpellSchedule::Cast: {
        if (!commit(actor)) {
            finish(actor);
            return;
        }
        if (creature && !_item && !continuePaidCast(*creature, *self)) {
            finish(actor);
            return;
        }
        // The impact goes out with the payment; only its projectile delays it.
        release(actor);
        return;
    }
    case SpellSchedule::Finish: {
        // A caught cast ends as a failure: it spends no feat use and keeps its
        // orientation lock and cast clip until the round pause poses its caster.
        const bool caught = !_item && _spell->catchTime > 0.0f;
        if (!caught && creature) {
            // Only normal cast completion owns the associated-feat debit. Neither
            // talent selection, release nor cancellation charges this parameter.
            if (_associatedFeat) {
                creature->spendFeatUse(*_associatedFeat);
            }
            // A completed cast releases the caster's orientation lock.
            if (!_item) {
                creature->setOrientationLock(script::kObjectInvalid);
            }
        }
        finish(actor, caught);
        return;
    }
    default:
        break;
    }
}

// An item use needs its item, wherever it now is, and its property with a
// use left.
bool CastSpellAction::itemAvailable() const {
    if (!_item) {
        return true;
    }
    return *_item && (*_item)->isRuntimeLive() && _itemProperty &&
           *_itemProperty < (*_item)->properties().size() && (*_item)->hasSpellUse(*_itemProperty);
}

void CastSpellAction::publishSpellContext(Object &actor) {
    const auto *creature = dyn_cast<Creature>(&actor);
    auto context = _castContext;
    if (!_committed) {
        int level = _itemCasterLevel.value_or(creature ? creature->spellCasterLevel(*_spell, _item.has_value()) : 0);
        int castingClass = creature && !_cheat && !_item ? creature->spellCastingClass(*_spell) : kUnselectedCastingClass;
        if (_selection && !_item) {
            castingClass = _selection->classIndex;
            if (_selection->hasExplicitLevel()) {
                level = _selection->casterLevel;
            } else if (creature && _selection->sourceKind() == CastingSourceKind::Class) {
                level = creature->adjustedClassLevel(castingClass);
            } else if (creature && _selection->sourceKind() == CastingSourceKind::SpellLikeAbility) {
                level = creature->spellLikeAbilityCasterLevel(static_cast<int>(_spell->type));
            }
        }
        context = SpellCastContext {};
        context.spellId = static_cast<int>(_spell->type);
        context.casterLevel = std::max(0, level);
        context.castingClass = castingClass;
    }
    // The spell's target location faces as the caster does.
    const Location location {_location, scriptFacingFromObject(actor.getFacing())};
    actor.spellScriptContext().setImpact(context, _target, _item ? *_item : nullptr, location);
    if (creature) {
        actor.spellScriptContext().setActiveTarget(activeSpellTarget(actor));
    }
}

bool CastSpellAction::commit(Object &actor) {
    if (_committed) {
        return !isCompleted() && !isCancelled();
    }
    // Once the cast has started, a target object may have stepped away: the
    // cast goes on. A cast at a location stays bound to its range.
    const bool rangeChecked = !_cheat && (!_started || !_target);
    if (_commitAttempted || !itemAvailable() ||
        (rangeChecked && !withinSpellRange(actor, *_spell, aimPosition(), targetObject())))
        return false;
    _commitAttempted = true;
    // A fake cast spends nothing and announces nothing.
    if (_fake) {
        _committed = true;
        return !isCompleted() && !isCancelled() && !actor.isDead();
    }
    _committed = commitSpellCast(actor, *_spell, _cheat || _item.has_value(), _castContext, _item.has_value(), _selection);
    if (_committed && _item && _itemCasterLevel) {
        _castContext.casterLevel = *_itemCasterLevel;
        actor.setSpellCastContext(_castContext);
    }
    if (_committed) {
        publishSpellContext(actor);
    }
    return _committed && !isCompleted() && !isCancelled() && !actor.isDead() && runtimeDependenciesLive();
}

void CastSpellAction::release(Object &actor) {
    if (_dispatched || !_committed || !itemAvailable() || !runtimeDependenciesLive()) {
        return;
    }
    if (_item) {
        if (auto *creature = dyn_cast<Creature>(&actor)) {
            publishItemCastLevel(*creature);
        }
    }
    // An instant cast's projectile flies, and its impact waits, for as long as
    // the caster's last measured flight.
    if (!_instantSpell)
        actor.setLastSpellProjectileMilliseconds(spellProjectileTimeMilliseconds(
            *_spell, actor.position(), aimPosition(), _projectilePathType, _game.isTSL()));
    const uint32_t projectileMilliseconds = actor.lastSpellProjectileMilliseconds();
    _projectileTime = projectileMilliseconds / 1000.0f;
    const Location impactLocation {_location, scriptFacingFromObject(actor.getFacing())};
    // A fake cast shows the projectile and has no impact.
    _dispatched = _fake || queueSpellImpact(_game, *_spell, actor, _target, impactLocation, _castContext,
                                            _item ? *_item : nullptr, projectileMilliseconds);
    if (!_dispatched) {
        finish(actor);
        return;
    }
    _services.game.projectiles.releaseSpell(_presentationId, _projectileTime, _game, _services);
    presentSpellRelease(actor, *_spell);
    if (_item && !_cheat && !_itemConsumed) {
        _itemConsumed = true;
        auto item = *_item;
        if (item->consumeSpellUse(*_itemProperty)) {
            // The event owns the released impact; retiring its item must not
            // retract that event. The use itself ends once the item is gone.
            releaseItemDependency();
            _game.queueObjectDestruction(*item, 0.0f);
        }
    }
}

void CastSpellAction::releaseItemDependency() {
    const auto &item = *_item;
    _runtimeDependencies.erase(
        std::remove_if(_runtimeDependencies.begin(), _runtimeDependencies.end(),
                       [&](const auto &ref) { return ref.resolve().get() == item.get(); }),
        _runtimeDependencies.end());
}

bool CastSpellAction::cancel(std::shared_ptr<Action>, Object &actor) {
    auto *creature = dyn_cast<Creature>(&actor);
    if (creature) {
        _game.combat().transferEquipment(*creature);
    }
    _services.game.projectiles.cancelSpell(_presentationId);
    finish(actor);
    // A cleared cast leaves its caster in the pause or ready pose.
    if (creature && !creature->isDead() && !creature->isTemporarilyDead()) {
        creature->showPauseReadyAnimation(false);
    }
    return true;
}

void CastSpellAction::dropPresentation() {
    _services.game.projectiles.cancelSpell(_presentationId);
}

void CastSpellAction::finish(Object &caster, bool keepCastHold) {
    if (!isCompleted()) {
        debug(str(boost::format("Spell finished: actor=%u spell=%d committed=%d released=%d") % caster.id() % static_cast<int>(_spell->type) % _committed % _dispatched), LogChannel::Combat);
    }
    if (_ownsSpellTarget) {
        caster.spellScriptContext().clearActiveTarget();
        _ownsSpellTarget = false;
    }
    auto *creature = dyn_cast<Creature>(&caster);
    if (_ownsMovementRestriction) {
        if (creature) {
            creature->setMovementRestricted(false);
        }
        _ownsMovementRestriction = false;
    }
    _services.game.projectiles.cancelSpell(_presentationId);
    if (creature && !keepCastHold) {
        creature->releaseCastAnimation();
    }
    // A spell that ends before its release takes its conjure visuals with it.
    if (creature && _started && !_dispatched && !_item && !isCompleted()) {
        creature->spellCastVisuals().clear();
    }
    complete();
}

// A paid spell stays clearable only while its round is engaged, and a real
// cast fails once its caster is entangled.
bool CastSpellAction::continuePaidCast(Creature &caster, const Action &queued) {
    setClearable(_game.combat().isRoundEngaged(queued));
    return _fake || !interruptEntangledCast(caster);
}

std::optional<SavedActionRecord> CastSpellAction::saveFacingState() const {
    // The order is the command's parameters; the progress of the cast goes
    // beside them.
    SavedActionRecord record;
    record.actionId = static_cast<uint32_t>(SavedActionType::CastSpell);
    SavedSpellCommand command;
    command.spellId = static_cast<int>(_spell->type);
    command.selection = _selection.value_or(SpellSelection {});
    if (_target) {
        command.target = SavedObjectReference::fromRuntimeId(_target->id());
        _game.bindSavedObjectReference(command.target);
    }
    command.position = _location;
    command.path = _projectilePathType;
    command.associatedFeat = _associatedFeat;
    command.fake = _fake;
    command.instant = _instantSpell;
    command.toAction(record);

    SavedCastAction state;
    _schedule.save(state);
    state.projectileTime = _projectileTime;
    state.presentationId = _presentationId;
    state.casterLevel = _castContext.casterLevel;
    state.metaMagic = _castContext.metaMagic;
    state.castingClass = _castContext.castingClass;
    state.forceCost = _castContext.forcePointCost;
    state.cheat = _cheat;
    state.hasSelection = _selection.has_value();
    state.started = _started;
    state.commitAttempted = _commitAttempted;
    state.committed = _committed;
    state.released = _dispatched;
    state.movementOwned = _ownsMovementRestriction;
    state.itemConsumed = _itemConsumed;
    if (_item) {
        SavedCastAction::ItemUse use;
        use.item = SavedObjectReference::fromRuntimeId(*_item ? (*_item)->id() : script::kObjectInvalid);
        _game.bindSavedObjectReference(use.item);
        if (_itemProperty) {
            use.property = static_cast<int>(*_itemProperty);
        }
        use.casterLevel = _itemCasterLevel;
        use.baseItemType = _itemType;
        state.itemUse = std::move(use);
    }
    record.cast = std::move(state);
    return record;
}

void CastSpellAction::restoreCastState(const SavedCastAction &state) {
    _schedule.restore(state);
    _projectileTime = state.projectileTime;
    _presentationId = state.presentationId;
    _castContext.spellId = static_cast<int>(_spell->type);
    _castContext.casterLevel = state.casterLevel;
    _castContext.metaMagic = state.metaMagic;
    _castContext.castingClass = state.castingClass;
    _castContext.forcePointCost = state.forceCost;
    // A cast ordered without a casting class takes its caster's own; the
    // command's class and level do not stand for one.
    if (!state.hasSelection) {
        _selection.reset();
    }
    _started = state.started;
    _commitAttempted = state.commitAttempted;
    _committed = state.committed;
    _dispatched = state.released;
    _ownsMovementRestriction = state.movementOwned;
    _itemConsumed = state.itemConsumed;
    if (_item && state.itemUse) {
        _itemType = state.itemUse->baseItemType;
    }
    _restorePresentation = true;
    // A started item use stays unclearable; a paid spell derives its
    // clearability again on its next frame.
    if (_started && _item) {
        setClearable(false);
    }
    if (_itemConsumed && _item) {
        releaseItemDependency();
    }
}

} // namespace game

} // namespace reone
