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

#include <boost/algorithm/string.hpp>

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
