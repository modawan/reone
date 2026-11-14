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

#include "reone/game/minigameview.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/graphics/modelnode.h"
#include "reone/scene/node/model.h"
#include "reone/scene/animproperties.h"
#include "reone/scene/types.h"

using namespace reone::graphics;
using namespace reone::scene;

namespace reone {

namespace game {

namespace {

void collectAnimatedNodes(const ModelNode &node, std::set<std::string> &out) {
    auto &mutableNode = const_cast<ModelNode &>(node);
    if (!mutableNode.vectorTracks().empty() ||
        !mutableNode.quaternionTracks().empty() ||
        !mutableNode.floatTracks().empty()) {
        out.insert(node.name());
    }
    for (const auto &child : node.children()) {
        collectAnimatedNodes(*child, out);
    }
}

} // namespace

namespace {

// The roll of BankL_01..10 and BankR_01..10 on each game's bike, in degrees:
// the angle of the bike's lean node (dummy04) from level, averaged over the
// keys of the clip. Read from the clips of the retail bikes (K1 v_superbike,
// TSL v_supertrike01); the left and right clips agree to 0.02 degrees, and so
// the left ones are averaged with the right ones. The steps are 0.5 to 1.5
// degrees and shrink toward the last clips.
constexpr std::array<float, kMinigameBankFrames> kKotorBankRoll {0.50f, 1.52f, 2.76f, 4.28f, 5.81f, 7.34f, 8.68f, 9.83f, 10.85f, 11.68f};
constexpr std::array<float, kMinigameBankFrames> kTslBankRoll {0.70f, 1.56f, 2.78f, 4.30f, 5.84f, 7.36f, 8.69f, 9.84f, 10.81f, 11.65f};

} // namespace

std::string minigameBankAnimation(float lean, bool tsl) {
    const auto &rolls = tsl ? kTslBankRoll : kKotorBankRoll;
    // A full lean is the roll of the last clip.
    float roll = std::clamp(std::fabs(lean), 0.0f, 1.0f) * rolls.back();
    // The nearest of level (roll 0) and the ten clips.
    int frame = 0;
    float previous = 0.0f;
    for (int i = 0; i < kMinigameBankFrames; ++i) {
        if (roll < (previous + rolls[i]) * 0.5f) {
            break;
        }
        frame = i + 1;
        previous = rolls[i];
    }
    if (frame == 0) {
        return kMinigameLevelAnimation;
    }
    return boost::str(boost::format("%s_%02d") % (lean < 0.0f ? "BankL" : "BankR") % frame);
}

const std::set<std::string> &MinigameAnimator::nodeSet(const Animation &anim) {
    auto it = _nodeSets.find(&anim);
    if (it != _nodeSets.end()) {
        return it->second;
    }
    std::set<std::string> nodes;
    if (anim.rootNode()) {
        collectAnimatedNodes(*anim.rootNode(), nodes);
    }
    return _nodeSets.emplace(&anim, std::move(nodes)).first->second;
}

void MinigameAnimator::start(ModelSceneNode &model, Animation &anim, bool looping) {
    const auto &nodes = nodeSet(anim);
    std::vector<std::string> replaced;
    for (const auto &channel : model.animationChannels()) {
        if (!channel.anim) {
            continue;
        }
        bool same = channel.anim == &anim;
        if ((same && !looping) || (!same && nodeSet(*channel.anim) == nodes)) {
            replaced.push_back(channel.anim->name());
        }
    }
    for (const auto &name : replaced) {
        model.removeAnimation(name);
    }
    int flags = AnimationFlags::overlay | AnimationFlags::layer;
    if (looping) {
        flags |= AnimationFlags::loop;
    }
    model.playAnimation(anim, nullptr, AnimationProperties::fromFlags(flags));
    if (!looping) {
        _oneShotRemaining[&model] = anim.length();
    }
}

namespace {

// Scripts name animations in any case ("SecOne6"); the models store them in lower case.
std::shared_ptr<graphics::Animation> findAnimation(const ModelSceneNode &model, const std::string &name) {
    auto anim = model.model().getAnimation(name);
    if (!anim) {
        anim = model.model().getAnimation(boost::to_lower_copy(name));
    }
    return anim;
}

} // namespace

bool MinigameAnimator::play(ModelSceneNode &model, const std::string &requested, bool looping, bool queue, bool overlay) {
    if (requested.empty()) {
        return false;
    }
    auto anim = findAnimation(model, requested);
    if (!anim) {
        return false;
    }
    const std::string &name = anim->name();
    if (looping && model.isAnimationPlaying(name)) {
        return true;
    }
    if (queue) {
        auto running = _oneShotRemaining.find(&model);
        if (running != _oneShotRemaining.end() && running->second > 0.0f) {
            _pending.push_back({&model, name, looping, running->second});
            return true;
        }
    }
    start(model, *anim, looping);
    return true;
}

void MinigameAnimator::remove(ModelSceneNode &model, const std::string &requested) {
    auto anim = findAnimation(model, requested);
    const std::string name = anim ? anim->name() : requested;
    model.removeAnimation(name);
    _pending.erase(std::remove_if(_pending.begin(), _pending.end(), [&](const Pending &p) {
                       return p.model == &model && p.name == name;
                   }),
                   _pending.end());
}

bool MinigameAnimator::pose(ModelSceneNode &model, const std::string &requested, const std::string &previous) {
    auto anim = findAnimation(model, requested);
    if (anim) {
        model.playAnimation(*anim, nullptr, AnimationProperties::fromFlags(AnimationFlags::loop | AnimationFlags::overlay));
    }
    if (!previous.empty()) {
        auto old = findAnimation(model, previous);
        if (old && old != anim) {
            model.removeAnimation(old->name());
        }
    }
    return anim != nullptr;
}

void MinigameAnimator::update(float dt) {
    for (auto &entry : _oneShotRemaining) {
        entry.second = std::max(0.0f, entry.second - dt);
    }
    std::vector<Pending> due;
    for (auto it = _pending.begin(); it != _pending.end();) {
        it->delay -= dt;
        if (it->delay <= 0.0f) {
            due.push_back(*it);
            it = _pending.erase(it);
        } else {
            ++it;
        }
    }
    for (auto &p : due) {
        if (auto anim = p.model->model().getAnimation(p.name)) {
            start(*p.model, *anim, p.looping);
        }
    }
}

void MinigameAnimator::forget(const ModelSceneNode &model) {
    _oneShotRemaining.erase(&model);
    _pending.erase(std::remove_if(_pending.begin(), _pending.end(), [&](const Pending &p) {
                       return p.model == &model;
                   }),
                   _pending.end());
}

void MinigameAnimator::clear() {
    _pending.clear();
    _oneShotRemaining.clear();
    _nodeSets.clear();
}

} // namespace game

} // namespace reone
