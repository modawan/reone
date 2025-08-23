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
#include "reone/audio/clip.h"
#include "reone/audio/mixer.h"
#include "reone/audio/types.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/script/runner.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/system/casting.h"

namespace reone {

namespace game {

static void playAudioClip(std::string key, ServicesView &services) {
    std::shared_ptr<audio::AudioClip> clip = services.resource.audioClips.get(key);
    services.audio.mixer.play(std::move(clip), audio::AudioType::Sound);
}

void CastSpellAtObjectAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (!isa<Creature>(actor)) {
        complete();
        return;
    }

    Creature &caster = (Creature &) actor;
    caster.face(*_target);

    _timer.update(dt);

    switch (_state) {
    case SpellState::Reset: {
        _state = SpellState::Conjure;
        _timer.reset(_spell->conjtime);
        if (_spell->conjtime > 0.0f) {
            caster.playAnimation(_spell->conjanim);
            return;
        }
        // fallthrough
    }
    case SpellState::Conjure: {
        if (!_timer.elapsed()) {
            return;
        }
        _state = SpellState::Cast;
        _timer.reset(_spell->casttime);
        if (!_spell->castsound.empty()) {
            playAudioClip(_spell->castsound, _services);
        }
        if (!_spell->impactscript.empty()) {
            _game.scriptRunner().run(_spell->impactscript, _target->id(), actor.id());
        }
        if (_spell->casttime > 0.0f) {
            caster.playAnimation(_spell->castanim);
            return;
        }
        // fallthrough
    }
    case SpellState::Cast: {
        if (!_timer.elapsed()) {
            return;
        }
        _state = SpellState::Catch;
        _timer.reset(_spell->catchtime);
        if (_spell->catchtime > 0.0f) {
            caster.playAnimation(_spell->catchanim);
            return;
        }
        // fallthrough
    }
    case SpellState::Catch: {
        if (!_timer.elapsed()) {
            return;
        }
        _state = SpellState::Complete;
        complete();
    }
    }
}

} // namespace game

} // namespace reone
