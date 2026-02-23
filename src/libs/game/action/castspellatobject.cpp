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

#include "reone/game/action/castspellatobject.h"
#include "reone/audio/mixer.h"
#include "reone/game/d20/spells.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"

namespace reone {

namespace game {

CastSpellAtObjectAction::CastSpellAtObjectAction(
    Game &game,
    ServicesView &services,
    std::shared_ptr<Spell> spell,
    std::shared_ptr<Object> target,
    std::optional<std::shared_ptr<Item>> item,
    bool cheat) :
    Action(game, services, ActionType::CastSpellAtObject),
    _spell(std::move(spell)),
    _target(std::move(target)),
    _item(std::move(item)),
    _schedule(_spell->conjTime, _spell->castTime),
    _cheat(cheat) {}

static void runScript(Game &game, const Spell &spell, const Object &target) {
    std::vector<script::Argument> args = {
        {script::ArgKind::Caller, script::Variable::ofObject(target.id())},
        {script::ArgKind::SpellId, script::Variable::ofInt(static_cast<int>(spell.type))},
    };
    game.scriptRunner().run(spell.impactScript, args);
}

void CastSpellAtObjectAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    Creature &caster = cast<Creature>(actor);

    if (_target->isDead()) {
        finish(caster);
        return;
    }

    // TODO: navigate to the target
    caster.face(*_target);

    const CombatRound &round = _game.combat().addAction(self, actor);
    SpellSchedule::State state = _schedule.update(round, *self, dt);
    if (_grenade) {
        _grenade->update(state, *_target, dt);
    }

    // Gameplay updates
    switch (state) {
    case SpellSchedule::Conjure: {
        lock();

        bool lastItem = false;
        if (!_cheat && _item && !caster.removeItem(_item.value(), lastItem)) {
            // Ran out of items since this action was enqueued. This may happen
            // in case of shared inventory (not implemented yet).
            //
            // Cheats ignore all requirements and do not remove the item.
            finish(caster);
            return;
        }

        caster.setMovementType(Creature::MovementType::None);
        caster.setMovementRestricted(true);

        scene::AnimationProperties animProp =
            scene::AnimationProperties::fromFlags(scene::AnimationFlags::blend);

        if (_spell->projModel) {
            // Throw a grenade.
            _grenade = Grenade();
            _grenade->fire(caster, *_spell->projModel, /*swingTime=*/0.8f, /*throwTime=*/0.5f);
            caster.playAnimation("throwgren");
        } else {
            // Activate an item.
            caster.playAnimation((_spell->itemTargeting ? "activate" : "inject"), animProp);
        }
        return;
    }
    case SpellSchedule::Cast: {
        if (_spell->castSound) {
            _services.audio.mixer.play(
                _spell->castSound,
                audio::AudioType::Sound,
                /*gain=*/1.0f,
                /*loop=*/false,
                caster.position());
        }
        return;
    }
    case SpellSchedule::Effect: {
        runScript(_game, *_spell, *_target);
        break;
    }
    case SpellSchedule::Finish: {
        finish(caster);
        return;
    }
    default:
        break;
    }
}

void CastSpellAtObjectAction::finish(Creature &caster) {
    caster.setMovementRestricted(false);
    complete();
}

} // namespace game

} // namespace reone
