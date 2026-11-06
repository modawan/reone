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
#include <cassert>

#include "reone/game/action/castspellatlocation.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/changefacing.h"
#include "reone/game/action/movetoobject.h"
#include "reone/game/combat.h"
#include "reone/game/combatfeedback.h"
#include "reone/game/d20/class.h"
#include "reone/game/d20/spells.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/game/projectiles.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"

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
    _skipsApproach(instantSpell),
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

bool CastSpellAction::thrownAtPoint() const {
    return _game.isTSL() && _itemType == Item::kGrenadeItemType && isHandThrownGrenadeSpell(_spell->type);
}

uint64_t CastSpellAction::beginProjectile(Object &actor) {
    if (thrownAtPoint()) {
        return _services.game.projectiles.beginSpell(actor, std::nullopt, _location, *_spell,
                                                     _projectilePathType, _game, _services);
    }
    return _services.game.projectiles.beginSpell(actor, targetObject(), aimPosition(), *_spell,
                                                 _projectilePathType, _game, _services);
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
            _presenter.restore(*creature, *_spell, _schedule);
        }
    }
    // A cast fails, as every action does, once its caster is dead or down;
    // it also fails once its target creature is lost, and a power
    // once its caster's equipment no longer allows it; an item use that has
    // not started tests its target only once its user has walked up and
    // turned to it. A failed item use leaves its user posed. A released use
    // that plays on without its item no longer depends on it, nor on its
    // target.
    const bool playsOn = _item && _dispatched && !itemUseEndsWithItem(*_itemType);
    const bool testsTarget = !_item || _started;
    if (actor.isDead() ||
        (!playsOn && (!runtimeDependenciesLive() || (testsTarget && _target && spellTargetLost(actor, *_target)))) ||
        (creature && !_item && !equipmentAllowsSpell(*creature, *_spell))) {
        finish(actor);
        if (_item) {
            presentFailedItemUse(_game, actor);
        }
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
    // A placeable casts in full on its first update, whatever the distance:
    // it neither approaches, conjures nor waits for its cast time.
    if (!creature) {
        _presentationId = beginProjectile(actor);
        if (commit(actor)) {
            release(actor);
        }
        finish(actor);
        return;
    }
    // A cast is admitted, and its range tested, once, as it is dispatched:
    // its caster's uses and points are not tested again on the way to its
    // target or as it pays, and a caster out of range walks up to it and
    // casts wherever its walk ends. A cast that skips its approach has no
    // range; a cheat cast skips only the admission.
    if (!_admitted) {
        if (!admitSpellCast(actor, *_spell, _cheat || _fake || _item.has_value(), _item.has_value(), _selection)) {
            finish(actor);
            return;
        }
        _admitted = true;
        _dispatchedOutOfRange = !_skipsApproach && !withinSpellRange(actor, *_spell, aimPosition(), targetObject());
        _approaching = _dispatchedOutOfRange;
        // A cast at an object that does not skip its approach checks its line
        // to the object before it starts, wherever its caster stands and
        // whoever ordered it.
        _checksUseLine = !_skipsApproach && _target;
    }
    if (!_started) {
        if (!itemAvailable()) {
            finish(actor);
            return;
        }
        actor.spellScriptContext().setActiveTarget(activeSpellTarget(actor));
        _ownsSpellTarget = true;
        // A cast at a location walks on toward it, however often its walk
        // fails or its caster cannot move, until it is within range; a walk
        // toward an object also ends within use range of it, or within range
        // of its use point in a clear line.
        if (_approaching) {
            if (creature->walkToward(targetObject(), aimPosition(), true, spellRange(actor, *_spell, targetObject()), dt, targetObject(), true)) {
                _approaching = false;
            } else if (_target && (creature->navigationFailed() || !creature->canMove())) {
                // A cast at an object whose walk fails checks its use line
                // from where its caster stands.
                _approaching = false;
            }
            return;
        }
        // Such a check passes once the caster is within the spell's range of
        // the object's use point, or its use range, with a clear line to it.
        // It is taken before the caster walks toward that point and again once
        // the walk has ended, however often its walk fails or its caster cannot
        // move; a walk under way runs to its own end, within use range of the
        // object or, in a clear line, within range of the point.
        if (_checksUseLine) {
            const float range = spellRange(actor, *_spell, targetObject());
            const float useRange = creature->useRange(*_target).range;
            if (!creature->navigateToUse(*_target, std::max(0.0f, range - useRange), dt, true, false, true, true,
                                         range - useRange, true)) {
                return;
            }
            _checksUseLine = false;
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
    if (!testsTarget && _target && spellTargetLost(actor, *_target)) {
        finish(actor);
        presentFailedItemUse(_game, actor);
        return;
    }
    // A cast breaks off the caster's activities on every frame it runs once
    // its approach is over, its waits for the round included; a fake cast
    // interrupts nothing.
    if (creature && !_fake) {
        creature->interruptForSpell(static_cast<int>(_spell->type), _item.has_value(), _spell->hostile);
    }
    // A caster that already acted in a round that has not ended waits for its
    // end once it has walked up.
    if (!_started && creature && _game.combat().awaitsRoundEnd(*creature, *self)) {
        return;
    }
    if (_instantSpell) {
        if (creature) {
            creature->beginSpellActivity(static_cast<int>(_spell->type), _item.has_value());
            _game.combat().beginInstantCast(self, *creature, _target);
        }
        // An instant cast shows its release as any cast does.
        _presentationId = beginProjectile(actor);
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
    // An item use is shown as used at the location its command carries, not
    // where its target now stands.
    if (creature && state != SpellSchedule::Conjure) {
        _presenter.update(*creature, *_spell, _itemType, targetCreature, _location, _schedule, state);
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
            creature->beginSpellActivity(static_cast<int>(_spell->type), _item.has_value());
            float pause = _spell->conjTime + _spell->castTime + _spell->catchTime;
            if (_item) {
                // An item use takes effect and ends at its item type's times.
                const auto &use = _presenter.itemUse(*creature, *_spell, *_itemType, Creature::asCreature(_target), _location);
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
        // An item use started over lets go of the projectile it was holding.
        _services.game.projectiles.cancelSpell(_presentationId);
        _presentationId = beginProjectile(actor);
        if (creature) {
            if (!_item) {
                creature->spellCastVisuals().showConjure(*creature, *_spell);
            }
            _presenter.update(*creature, *_spell, _itemType, targetCreature, _location, _schedule, state);
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
        int castingClass = creature && !_cheat && !_item ? creature->spellCastingClass() : kUnselectedCastingClass;
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
    // The range was tested once, when the cast was dispatched.
    if (_commitAttempted || !itemAvailable())
        return false;
    _commitAttempted = true;
    // A fake cast spends nothing and announces nothing.
    if (_fake) {
        _committed = true;
        return !isCompleted() && !isCancelled() && !actor.isDead();
    }
    _committed = commitSpellCast(actor, *_spell, _cheat || _item.has_value(), _instantSpell, _castContext, _selection);
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
            *_spell, actor.position(), aimPosition(), _projectilePathType));
    const uint32_t projectileMilliseconds = actor.lastSpellProjectileMilliseconds();
    _projectileTime = projectileMilliseconds / 1000.0f;
    // An instant cast lands at the origin, which its target object's position
    // replaces when the impact is delivered. An item use, instant or not,
    // lands at the location its command carries.
    const glm::vec3 impactPosition = _instantSpell && !_item ? glm::vec3(0.0f) : _location;
    // A fake cast shows the projectile and has no impact.
    // A grenade thrown by hand lands at its point, with no target object.
    _dispatched = _fake || queueSpellImpact(_game, *_spell, actor, thrownAtPoint() ? nullptr : _target, impactPosition,
                                            _castContext, _item ? *_item : nullptr, projectileMilliseconds);
    if (!_dispatched) {
        finish(actor);
        return;
    }
    _services.game.projectiles.releaseSpell(_presentationId, _projectileTime, _game, _services);
    presentSpellRelease(actor, *_spell);
    // A creature's cast or item use is announced; a fake cast is not.
    if (auto *creature = dyn_cast<Creature>(&actor); creature && !_fake) {
        addPowerUseFeedback(_game, *creature, *_spell, _item ? *_item : nullptr);
    }
    if (_item && !_cheat && !_itemConsumed) {
        _itemConsumed = true;
        auto item = *_item;
        if (item->consumeSpellUse(*_itemProperty)) {
            // The event owns the released impact; retiring its item must not
            // retract that event. The use itself ends once the item is gone.
            releaseItemDependency();
            _game.postObjectDestruction(*item, actor.id(), 0.0f);
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
    // A cleared cast or item use leaves its caster attempting no spell.
    if (creature) {
        _game.combat().transferEquipment(*creature);
        creature->setAttemptedSpellTarget(script::kObjectInvalid);
    }
    _services.game.projectiles.cancelSpell(_presentationId);
    finish(actor);
    // A cleared cast leaves its caster in the pause or ready pose.
    if (creature && !creature->isDead() && !creature->isTemporarilyDead()) {
        creature->showPauseReadyAnimation(false);
    }
    return true;
}

// A power's level is the one it has for its casting class, and its innate level
// when it is cast from no class or as a special ability.
static int castPowerLevel(const Creature &caster, const Spell &spell, int castingClass) {
    const auto &classes = caster.attributes().classLevels();
    const auto index = static_cast<size_t>(castingClass);
    if (index < classes.size()) {
        return spell.getLevelForClass(classes[index].first->type());
    }
    return spell.innateLevel;
}

bool CastSpellAction::breaksConcentration(const Creature &caster, int damage) const {
    if (_game.isTSL() || !conjuring() || _spell->castAnimation == SpellCastAnimation::Throw) {
        return false;
    }
    const int level = castPowerLevel(caster, *_spell, caster.spellCastContext().castingClass);
    return randomInt(1, 20) < damage + level + 10;
}

void CastSpellAction::drop(Object &caster) {
    _services.game.projectiles.cancelSpell(_presentationId);
    releaseMovementRestriction(caster);
    if (_ownsSpellTarget) {
        caster.spellScriptContext().clearActiveTarget();
        _ownsSpellTarget = false;
    }
    // Only a cast that has begun holds its caster's pose and visuals.
    auto *creature = dyn_cast<Creature>(&caster);
    if (creature && _started) {
        creature->releaseCastAnimation();
        if (!_dispatched && !_item) {
            creature->spellCastVisuals().clear();
        }
    }
}

void CastSpellAction::releaseMovementRestriction(Object &caster) {
    if (!_ownsMovementRestriction) {
        return;
    }
    if (auto *creature = dyn_cast<Creature>(&caster)) {
        creature->setMovementRestricted(false);
    }
    _ownsMovementRestriction = false;
}

void CastSpellAction::finish(Object &caster, bool keepCastHold) {
    if (!isCompleted()) {
        debug(str(boost::format("Spell finished: actor=%u spell=%d committed=%d released=%d") % caster.id() % static_cast<int>(_spell->type) % _committed % _dispatched), LogChannel::Combat);
    }
    auto *creature = dyn_cast<Creature>(&caster);
    // The cast lets go of the spell target it set; a finished item use also
    // leaves its user attempting no spell and aiming at nothing.
    if (_ownsSpellTarget || (creature && _item)) {
        caster.spellScriptContext().clearActiveTarget();
        _ownsSpellTarget = false;
    }
    if (creature && _item) {
        creature->setAttemptedSpellTarget(script::kObjectInvalid);
    }
    releaseMovementRestriction(caster);
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

void CastSpellAction::onQueued(Object &actor) {
    _placeableCaster = isa<Placeable>(actor);
}

std::optional<SavedActionRecord> CastSpellAction::saveFacingState() const {
    // The order is the command's parameters; the progress of the cast goes
    // beside them. An item use names its item and the property it is used
    // through, and the spell is the property's.
    SavedActionRecord record;
    if (_item) {
        // An item that is gone is saved as no item, and a property not found
        // as a place past the item's use properties; either use fails again
        // on its first frame.
        const auto &item = *_item;
        SavedItemUseCommand command;
        command.item = SavedObjectReference::fromRuntimeId(item ? item->id() : script::kObjectInvalid);
        _game.bindSavedObjectReference(command.item);
        if (item) {
            command.useProperty = static_cast<int>(item->usePropertyPlace(_itemProperty.value_or(item->properties().size())));
        }
        if (_target) {
            command.target = SavedObjectReference::fromRuntimeId(_target->id());
            _game.bindSavedObjectReference(command.target);
        }
        command.position = _location;
        command.toAction(record);
    } else {
        SavedSpellCommand command;
        command.placeable = _placeableCaster;
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
        command.domainLevel = _domainLevel;
        command.metaType = _metaType;
        command.spontaneousCast = _spontaneousCast;
        command.toAction(record);
    }

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
    state.admitted = _admitted;
    // A range check saved in front of the cast leaves the cast saved as
    // having passed it.
    const bool rangeCheckSaved = savesRangeCheck();
    state.approaching = _approaching && !rangeCheckSaved;
    state.skipsApproach = _skipsApproach;
    state.checksUseLine = _checksUseLine && !rangeCheckSaved;
    state.started = _started;
    state.commitAttempted = _commitAttempted;
    state.committed = _committed;
    state.released = _dispatched;
    state.movementOwned = _ownsMovementRestriction;
    state.itemCasterLevel = _itemCasterLevel;
    record.cast = std::move(state);
    return record;
}

// The walk runs to the object's position, ending at the spell's range in a
// clear line; the range check runs to the spell's range and is checked
// against it, and its walk may end in a clear line. A turn toward the
// object, or the location, follows them, and stands alone in front of a cast
// not yet dispatched.
std::vector<SavedActionRecord> CastSpellAction::rangeCheckCommands(const Object &actor) const {
    std::vector<SavedActionRecord> result;
    if (_placeableCaster || _started) {
        return result;
    }
    if (savesRangeCheck()) {
        const float range = spellRange(actor, *_spell, targetObject());
        if (_approaching) {
            SavedMoveCommand walk;
            walk.destination = aimPosition();
            walk.area = SavedObjectReference::fromRuntimeId(actor.spatialArea()->get().id());
            walk.target = SavedObjectReference::fromRuntimeId(_target->id());
            walk.run = true;
            walk.carried.clearLine = true;
            walk.range = range;
            walk.toAction(result.emplace_back());
        }
        result.push_back(MoveToObjectAction::savedCommand(*_target, /*run=*/true, range, range,
                                                          MoveToObjectAction::ApproachFlags::forCast(queuedBehind())));
    } else if (_admitted) {
        return result;
    }
    result.push_back(_target ? ChangeFacingAction::savedCommand(*_target) : ChangeFacingAction::savedCommand(aimPosition()));
    return result;
}

bool CastSpellAction::savesRangeCheck() const {
    return _target && !_placeableCaster && !_skipsApproach && !_started && (_approaching || _checksUseLine) &&
           (!queuedBehind() || _dispatchedOutOfRange);
}

bool CastSpellAction::queuedBehind() const {
    return !_item && !isScheduledCommand();
}

void CastSpellAction::restartItemUse(const SavedCastAction &state) {
    assert(_item);
    _presentationId = state.presentationId;
}

void CastSpellAction::restoreCastState(const SavedCastAction &state) {
    // An item use starts over after a load instead.
    assert(!_item);
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
    _admitted = state.admitted;
    _approaching = state.approaching;
    _skipsApproach = state.skipsApproach;
    _checksUseLine = state.checksUseLine;
    _started = state.started;
    _commitAttempted = state.commitAttempted;
    _committed = state.committed;
    _dispatched = state.released;
    _ownsMovementRestriction = state.movementOwned;
    _restorePresentation = true;
    // A paid spell derives its clearability again on its next frame.
}

// The spell an item use carries once its item, property or spell is gone: it
// names no spell row.
static constexpr SpellType kNoItemSpell = static_cast<SpellType>(-1);

std::shared_ptr<Action> CastSpellAction::restore(const SavedActionRecord &record, Game &game) {
    const auto &cast = record.cast;
    const bool cheat = cast && cast->cheat;
    if (record.type() == SavedActionType::ItemCastSpell) {
        // An item use whose item, property, spell or target object is gone
        // still comes back; it fails on its first frame. It is then held as
        // a use at its location with no item and no spell of its own.
        const auto use = SavedItemUseCommand::fromAction(record);
        auto target = use->target.boundObject();
        const bool targetGone = !use->target.isInvalid() && !target;
        auto item = targetGone ? nullptr : std::dynamic_pointer_cast<Item>(use->item.boundObject());
        const auto property = item ? item->usePropertyAt(static_cast<size_t>(use->useProperty)) : std::nullopt;
        auto spell = property ? game.getSpell(static_cast<SpellType>(item->properties()[*property].subtype)) : nullptr;
        if (!spell) {
            spell = std::make_shared<Spell>();
            spell->type = kNoItemSpell;
        }
        const std::optional<int> casterLevel = cast ? cast->itemCasterLevel : std::nullopt;
        std::shared_ptr<CastSpellAction> action;
        if (!target) {
            action = game.newAction<CastSpellAtLocationAction>(spell, std::make_shared<Location>(use->position, 0.0f),
                                                               cheat, ProjectilePathType::Default, false,
                                                               std::optional(item), property, casterLevel);
        } else {
            auto atObject = game.newAction<CastSpellAtObjectAction>(spell, std::move(target), std::optional(item), cheat,
                                                                    ProjectilePathType::Default, false, property,
                                                                    casterLevel);
            atObject->setCommandLocation(use->position);
            action = std::move(atObject);
        }
        if (cast) {
            action->restartItemUse(*cast);
        }
        return action;
    }
    const auto command = SavedSpellCommand::fromAction(record);
    auto spell = game.getSpell(static_cast<SpellType>(command->spellId));
    if (!spell) {
        return nullptr;
    }
    // A placeable's cast is ordered without a casting class.
    const std::optional<SpellSelection> selection = command->placeable ? std::nullopt : std::optional(command->selection);
    std::shared_ptr<CastSpellAction> action;
    if (command->target.isInvalid()) {
        auto location = std::make_shared<Location>(command->position, 0.0f);
        action = game.newAction<CastSpellAtLocationAction>(spell, location, cheat, command->path, command->instant,
                                                           std::nullopt, std::nullopt, std::nullopt, selection,
                                                           command->associatedFeat, command->fake);
    } else {
        auto target = command->target.boundObject();
        if (!target) {
            return nullptr;
        }
        auto atObject = game.newAction<CastSpellAtObjectAction>(spell, std::move(target), std::nullopt, cheat, command->path,
                                                                command->instant, std::nullopt, std::nullopt,
                                                                selection, command->associatedFeat, command->fake);
        atObject->setCommandLocation(command->position);
        action = std::move(atObject);
    }
    action->setCommandParameters(command->domainLevel, command->metaType, command->spontaneousCast);
    if (cast) {
        action->restoreCastState(*cast);
    }
    return action;
}

} // namespace game

} // namespace reone
