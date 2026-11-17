/*
 * Copyright (c) 2020-2026 The reone project contributors
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

#include <map>
#include <set>
#include <string>
#include <vector>

namespace reone {

namespace graphics {

class Animation;

}

namespace scene {

class ModelSceneNode;

}

namespace game {

/** The number of clips of the bike's lean on each side, BankL_01..10 and BankR_01..10. */
constexpr int kMinigameBankFrames = 10;

/** The clip of a level bike. */
constexpr const char *kMinigameLevelAnimation = "Ready_01";

/**
 * The bike's animation for \p lean (-1 = full left .. 1 = full right): the clip
 * whose roll is nearest to the lean, BankL_01..10 or BankR_01..10, or
 * "Ready_01" when the bike is nearer to level than to the first clip. A lean of
 * 1 is the roll of clip 10. The clips' rolls are not evenly spaced, and differ
 * a little between the KotOR and TSL bikes: \p tsl picks the bike.
 */
std::string minigameBankAnimation(float lean, bool tsl);

/**
 * Plays the animations minigame scripts ask for (SWMG_PlayAnimation) on the
 * models of a minigame actor.
 *
 * Every request becomes a layered overlay channel, and a new animation
 * replaces any running one that drives the same set of nodes: the K1 dashboard
 * swaps "SecOne3" for "SecOne4" and the radar "HudRot_010" for "HudRot_011"
 * every update, while the six radar contact loops, which drive different
 * nodes, run side by side. A looping animation that is already running is not
 * restarted. A queued request waits for the model's current one-shot
 * animation to end.
 */
class MinigameAnimator {
public:
    /** Returns false when the model has no such animation. */
    bool play(scene::ModelSceneNode &model, const std::string &name, bool looping, bool queue, bool overlay);

    void remove(scene::ModelSceneNode &model, const std::string &name);

    /**
     * Holds the model in the pose of the looping animation \p name, in place
     * of \p previous (which may be empty or the same). Unlike play(), the
     * animation is not a layer: it is a base animation, which shows in full
     * from the first frame and sits under every layer, so a script's
     * animation of the same nodes still plays over it and a new frame does
     * not restart the blend that a layer makes over the animation's
     * transition time. This is how the bike's lean follows the steering.
     * Returns false when the model has no such animation; \p previous is
     * taken off all the same.
     */
    bool pose(scene::ModelSceneNode &model, const std::string &name, const std::string &previous);

    void update(float dt);

    /** Forget a model (it is being removed from the scene). */
    void forget(const scene::ModelSceneNode &model);

    void clear();

private:
    struct Pending {
        scene::ModelSceneNode *model {nullptr};
        std::string name;
        bool looping {false};
        float delay {0.0f};
    };

    std::vector<Pending> _pending;
    std::map<const scene::ModelSceneNode *, float> _oneShotRemaining;
    std::map<const graphics::Animation *, std::set<std::string>> _nodeSets;

    const std::set<std::string> &nodeSet(const graphics::Animation &anim);
    void start(scene::ModelSceneNode &model, graphics::Animation &anim, bool looping);
};

} // namespace game

} // namespace reone
