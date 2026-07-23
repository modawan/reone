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

#include "reone/game/action/playanimation.h"

#include "reone/game/animationutil.h"
#include "reone/game/object.h"
#include "reone/scene/animproperties.h"
#include "reone/graphics/animation.h"

using namespace reone::scene;

namespace reone {

namespace game {

void PlayAnimationAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (_playing) {
        _timer.update(dt);
        if (_timer.elapsed()) {
            complete();
        }
        return;
    }

    if (isAnimationLooping(_animation)) {
        // Looping animations never finish. Complete the action immediately to
        // avoid stalling the action queue.
        if (_durationSeconds < 0.0f) {
            complete();
        } else {
            _timer.reset(_durationSeconds);
        }
    } else {
        // Set the timer to match duration of the animation.
        auto node = actor.sceneNode();
        if (node->type() != SceneNodeType::Model) {
            complete();
            return;
        }

        const graphics::Model &model = std::static_pointer_cast<ModelSceneNode>(node)->model();
        std::shared_ptr<graphics::Animation> anim = model.getAnimation(actor.getAnimationName(_animation));
        if (!anim) {
            complete();
            return;
        }

        _timer.reset(anim->length());
    }

    AnimationProperties properties;
    properties.speed = _speed;
    properties.duration = _durationSeconds;
    actor.playAnimation(_animation, std::move(properties));
    _playing = true;
}

} // namespace game

} // namespace reone
