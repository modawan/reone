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
#include "../castspell.h"
#include "../types.h"

namespace reone {

namespace game {

class Item;
struct Spell;

/**
 * A spell cast or an item use, aimed at an object or at a location. The cast
 * approaches its target until it is within the spell's range, then conjures,
 * pays, releases its impact and finishes on its creature's combat round. A
 * fake cast shows all of this and has no impact.
 */
class CastSpellAction : public Action {
public:
    static bool classof(Action *from) {
        return from->type() == ActionType::CastSpellAtObject || from->type() == ActionType::CastSpellAtLocation;
    }

    /**
     * A saved CastSpell or ItemCastSpell command. A cast takes up its
     * progress; an item use starts over, and one whose item is gone fails on
     * its first frame, leaving its user posed, and the queue goes on.
     */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    bool cancel(std::shared_ptr<Action> self, Object &actor) override;
    std::optional<SavedActionRecord> saveFacingState() const override;
    /** An instant cast starts an ordinary round, which runs its end-of-round script. */
    bool suppressesEndRoundScript() const override { return (_fake && !_instantSpell) || _cutsceneAttack; }
    bool holdsCombatRound() const override { return _schedule.holdsRound() && !isCompleted() && !isCancelled(); }
    /** An instant cast takes no action of its round. */
    bool tookRoundAction() const override { return _dispatched && !_fake && !_instantSpell; }
    bool joinsRunningRound() const override { return !_instantSpell; }

    /** Removes the unreleased projectile shown while conjuring. */
    void dropPresentation();
    /** Takes up the progress of a cast loaded from a save. */
    void restoreCastState(const SavedCastAction &state);
    /**
     * Sets the command's domain level, meta type and spontaneous-cast
     * parameter, which a saved command carries.
     */
    void setCommandParameters(int domainLevel, int metaType, int spontaneousCast) {
        _domainLevel = domainLevel;
        _metaType = metaType;
        _spontaneousCast = spontaneousCast;
    }
    /**
     * Starts an item use over from its saved command. It keeps the projectile
     * it was holding only to let go of it when it starts again.
     */
    void restartItemUse(const SavedCastAction &state);

    /** The cast has reached its end and no longer occupies its creature. */
    bool castEnded() const { return !_schedule.awaitingRelease(); }
    bool instantSpell() const { return _instantSpell; }
    /** The cast shows itself and has no impact. */
    bool fake() const { return _fake; }
    /** The object the cast is aimed at; none for a cast at a location. */
    const std::shared_ptr<Object> &target() const { return _target; }
    const std::shared_ptr<Spell> &spell() const { return _spell; }
    const std::optional<std::shared_ptr<Item>> &item() const { return _item; }
    const std::optional<size_t> &itemProperty() const { return _itemProperty; }

protected:
    std::shared_ptr<Spell> _spell;
    std::shared_ptr<Object> _target;
    /**
     * The spell's target location: the location a cast at a location is aimed
     * at, or the location the command of a cast at an object carries.
     */
    glm::vec3 _location {0.0f};
    std::optional<std::shared_ptr<Item>> _item;
    std::optional<size_t> _itemProperty;
    /**
     * The item's base item type, which decides how its use is shown and
     * whether it plays on without the item.
     */
    std::optional<int> _itemType;
    std::optional<int> _itemCasterLevel;
    std::optional<SpellSelection> _selection;
    /** The feat whose use a completed cast spends. */
    std::optional<FeatType> _associatedFeat;
    bool _cheat;
    ProjectilePathType _projectilePathType;
    bool _instantSpell;
    bool _fake;
    /** The command's domain level, meta type and spontaneous-cast parameter. */
    int _domainLevel {0};
    int _metaType {0};
    int _spontaneousCast {0};

    SpellSchedule _schedule;
    CastPresenter _presenter;
    SpellCastContext _castContext;
    uint64_t _presentationId {0};
    float _projectileTime {0.0f};
    bool _restorePresentation {false};
    bool _itemConsumed {false};
    bool _ownsMovementRestriction {false};
    bool _ownsSpellTarget {false};
    /** The cast passed its admission when it was first dispatched. */
    bool _admitted {false};
    /** The caster was out of range when the cast was dispatched and is walking up to it. */
    bool _approaching {false};
    bool _started {false};
    bool _commitAttempted {false};
    bool _committed {false};
    bool _dispatched {false};

    /**
     * @param target the object the cast is aimed at, or none for a cast at
     *        \p location
     * @param itemProperty the item's property that casts the spell; by
     *        default the item's first property that casts it
     */
    CastSpellAction(
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
        bool fake);

private:
    void finish(Object &caster, bool keepCastHold = false);
    bool itemAvailable() const;
    bool commit(Object &actor);
    bool continuePaidCast(Creature &caster, const Action &queued);
    void publishSpellContext(Object &actor);
    void release(Object &actor);
    /** Stops depending on the item, whose retirement must not end the use. */
    void releaseItemDependency();

    /** Where the caster goes, turns and sends its projectile. */
    glm::vec3 aimPosition() const;
    /** The target object, for the range and the presentation. */
    std::optional<std::reference_wrapper<const Object>> targetObject() const;
    /** What the cast's caster has as its spell target: the target object, or its area. */
    std::shared_ptr<Object> activeSpellTarget(const Object &actor) const;
};

} // namespace game

} // namespace reone
