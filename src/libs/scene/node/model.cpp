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

#include "reone/scene/node/model.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "glm/gtx/quaternion.hpp"

#include "reone/graphics/animation.h"
#include "reone/graphics/context.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/material.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/uniforms.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/models.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/emitter.h"
#include "reone/scene/node/light.h"
#include "reone/scene/node/mesh.h"
#include "reone/scene/render/pass.h"
#include "reone/scene/types.h"
#include "reone/system/logutil.h"

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace scene {

static constexpr float kTransitionLength = 0.25f;

// The length of a channel: that of its lip animation, if any.
static float channelLength(const ModelSceneNode::AnimationChannel &channel) {
    return channel.lipAnim ? channel.lipAnim->length() : channel.anim->length();
}

// How long a channel has played, whichever way it runs.
static float playedTime(const ModelSceneNode::AnimationChannel &channel) {
    return channel.properties.speed < 0.0f ? channelLength(channel) - channel.time : channel.time;
}

void ModelSceneNode::init() {
    if (!_nodeByNumber.empty()) {
        return;
    }
    if (_model->rootNode()) {
        buildNodeTree(*_model->rootNode(), *this);
    }
    computeAABB();
    _point = _aabb.isDegenerate();
}

void ModelSceneNode::buildNodeTree(ModelNode &node, SceneNode &parent) {
    // Convert model node to scene node
    std::shared_ptr<ModelNodeSceneNode> sceneNode;
    if (node.isMesh()) {
        sceneNode = _sceneGraph.newMesh(*this, node);
    } else if (node.isLight()) {
        sceneNode = _sceneGraph.newLight(*this, node);
    } else if (node.isEmitter()) {
        sceneNode = _sceneGraph.newEmitter(node);
    } else {
        sceneNode = _sceneGraph.newDummy(node);
    }

    if (node.isSkinMesh()) {
        // Reparent skin meshes to prevent animation being applied twice
        glm::mat4 transform(node.parent()->absoluteTransform() * node.localTransform());
        sceneNode->setLocalTransform(std::move(transform));
        addChild(*sceneNode);
    } else {
        sceneNode->setLocalTransform(node.localTransform());
        parent.addChild(*sceneNode);
    }
    _nodeByNumber[node.number()] = sceneNode.get();
    _nodeByName[node.name()] = sceneNode.get();

    if (node.isReference()) {
        auto reference = node.reference();
        if (!reference->modelName.empty()) {
            auto model = _resourceSvc.models.get(reference->modelName);
            if (model) {
                auto refModelNode = _sceneGraph.newModel(*model, _usage);
                refModelNode->init();
                attach(node.name(), *refModelNode);
            }
        }
    }
    for (auto &child : node.children()) {
        buildNodeTree(*child, *sceneNode);
    }
}

void ModelSceneNode::update(float dt) {
    // Optimization: skip invisible models
    if (!_enabled) {
        return;
    }
    SceneNode::update(dt);
    updateAnimations(dt);
}

void ModelSceneNode::renderLeafs(IRenderPass &pass, const std::vector<SceneNode *> &leafs) {
    for (auto &leaf : leafs) {
        static_cast<MeshSceneNode *>(leaf)->render(pass);
    }
}

void ModelSceneNode::renderAABB(IRenderPass &pass) {
    auto aabbWorld = _aabb * _absTransform;
    std::vector<glm::vec4> corners;
    corners.reserve(8);
    for (const auto &corner : aabbWorld.corners()) {
        corners.emplace_back(corner, 1.0f);
    }
    pass.drawAABB(corners);
}

void ModelSceneNode::computeAABB() {
    _aabb = _model->aabb();
    for (auto &attachment : _attachments) {
        if (attachment.second->type() == SceneNodeType::Model) {
            AABB modelSpaceAABB(attachment.second->aabb() * attachment.second->absoluteTransform() * _absTransformInv);
            _aabb.expand(modelSpaceAABB);
        }
    }
}

void ModelSceneNode::signalEvent(const std::string &name) {
    if (name == "detonate") {
        for (auto &node : _nodeByNumber) {
            if (node.second->type() == SceneNodeType::Emitter) {
                static_cast<EmitterSceneNode *>(node.second)->detonate();
            }
        }
    } else if (_animEventListener) {
        _animEventListener->onEventSignalled(name);
    }
}

void ModelSceneNode::prewarmEmitters() {
    // Collect first: prewarming gives an emitter particle children, and the
    // walk should not be reading a list it has just grown.
    std::vector<EmitterSceneNode *> emitters;
    std::function<void(SceneNode &)> collect = [&collect, &emitters](SceneNode &node) {
        if (node.type() == SceneNodeType::Emitter) {
            emitters.push_back(static_cast<EmitterSceneNode *>(&node));
        }
        for (auto &child : node.children()) {
            collect(*child);
        }
    };
    collect(*this);

    for (auto *emitter : emitters) {
        emitter->prewarmContinuousParticles();
    }
}

void ModelSceneNode::attach(const std::string &parentName, SceneNode &node) {
    auto maybeParent = _nodeByName.find(parentName);
    if (maybeParent == _nodeByName.end()) {
        return;
    }
    auto parent = maybeParent->second;
    parent->addChild(node);

    _attachments.insert(std::make_pair(parentName, &node));

    computeAABB();
}

ModelNodeSceneNode *ModelSceneNode::getNodeByNumber(uint16_t number) {
    auto it = _nodeByNumber.find(number);
    return it != _nodeByNumber.end() ? it->second : nullptr;
}

ModelNodeSceneNode *ModelSceneNode::getNodeByName(const std::string &name) {
    auto it = _nodeByName.find(name);
    return it != _nodeByName.end() ? it->second : nullptr;
}

void ModelSceneNode::setFadeAlpha(float alpha) {
    _fadeAlpha = alpha;
    for (auto &[name, attachment] : _attachments) {
        if (attachment->type() == SceneNodeType::Model) {
            static_cast<ModelSceneNode *>(attachment)->setFadeAlpha(alpha);
        }
    }
}

SceneNode *ModelSceneNode::getAttachment(const std::string &parentName) {
    auto parent = _model->getNodeByName(parentName);
    if (!parent) {
        return nullptr;
    }
    auto it = _attachments.find(parent->name());
    return it != _attachments.end() ? it->second : nullptr;
}

void ModelSceneNode::setMainTexture(Texture *texture) {
    for (auto &child : _children) {
        if (child->type() == SceneNodeType::Dummy || child->type() == SceneNodeType::Mesh) {
            static_cast<ModelNodeSceneNode *>(child)->setMainTexture(texture);
        }
    }
}

void ModelSceneNode::setEnvironmentMap(Texture *texture) {
    for (auto &child : _children) {
        if (child->type() == SceneNodeType::Dummy || child->type() == SceneNodeType::Mesh) {
            static_cast<ModelNodeSceneNode *>(child)->setEnvironmentMap(texture);
        }
    }
}
static bool animationIntersectsModel(
    const Animation &anim,
    const std::shared_ptr<ModelNode> &node) {
    if (!node) {
        return false;
    }
    if (anim.getNodeByName(node->name())) {
        return true;
    }
    return std::any_of(node->children().begin(), node->children().end(), [&](const auto &child) {
        return animationIntersectsModel(anim, child);
    });
}

static bool shouldReuseExternalAnimationForAttachment(
    const Animation &anim,
    const ModelSceneNode &attachedModel,
    const AnimationProperties &properties) {
    if (attachedModel.usage() != ModelUsage::Creature) {
        return false;
    }
    // Upstream drives ordinary body animations into every composite creature
    // attachment. External stunt clips are different: only matching authored
    // tracks may replace an attachment's local animation or overlay channels.
    return !(properties.flags & AnimationFlags::retargetRoot) ||
           animationIntersectsModel(anim, attachedModel.model().rootNode());
}

void ModelSceneNode::setBumpedOutShell(std::shared_ptr<Texture> texture, float offset) {
    _bumpedOutShellTexture = std::move(texture);
    _bumpedOutShellOffset = offset;
}

void ModelSceneNode::clearBumpedOutShell() {
    _bumpedOutShellTexture.reset();
    _bumpedOutShellOffset = 0.0f;
}

void ModelSceneNode::playAnimation(const std::string &name, std::shared_ptr<LipAnimation> lipAnim, AnimationProperties properties) {
    auto anim = _model->getAnimation(name);
    if (anim) {
        playAnimation(*anim, std::move(lipAnim), std::move(properties));
    }
}

void ModelSceneNode::playAnimation(Animation &anim, std::shared_ptr<LipAnimation> lipAnim, AnimationProperties properties) {
    if (properties.scale == 0.0f) {
        properties.scale = _model->animationScale();
    }

    if (properties.flags & AnimationFlags::layer) {
        playLayer(anim, lipAnim, properties);
    } else {
        // Return if same animation is already playing
        auto base = baseAnimationChannel();
        if (base && base->get().anim == &anim && base->get().lipAnim == lipAnim && base->get().properties == properties) {
            return;
        }
        playBase(anim, lipAnim, properties);
    }

    // Optionally propagate animation to attachments
    if (properties.flags & AnimationFlags::propagate) {
        for (auto &attachment : _attachments) {
            if (attachment.second->type() != SceneNodeType::Model) {
                continue;
            }
            auto &attachedModel = *static_cast<ModelSceneNode *>(attachment.second);
            if (shouldReuseExternalAnimationForAttachment(
                    anim, attachedModel, properties)) {
                // External stunt models include facial tracks for the live
                // appearance head, but that head has no local stunt clip.
                // Reuse the proxy animation where node names intersect;
                // do not map its placement root onto the attachment.
                auto attachedProperties = properties;
                attachedProperties.flags &= ~AnimationFlags::retargetRoot;
                attachedModel.playAnimation(
                    anim, lipAnim, std::move(attachedProperties));
                continue;
            }
            // Attachments have their own animation sets. Resolve by name so
            // an unrelated body animation cannot replace a weapon's local
            // state animation (for example, a lightsaber's "off" pose).
            attachedModel.playAnimation(anim.name(), lipAnim, properties);
        }
    }
}

// A layer goes on top of the other layers and removes nothing. A layer already
// running the same animation is moved on top and goes on from where it is; one
// that has finished and is blending out starts over and blends back in from
// its current weight.
void ModelSceneNode::playLayer(Animation &anim, std::shared_ptr<LipAnimation> lipAnim, AnimationProperties properties) {
    const size_t layers = layerCount();
    for (size_t i = 0; i < layers; ++i) {
        if (_animChannels[i].anim != &anim) {
            continue;
        }
        AnimationChannel running = std::move(_animChannels[i]);
        _animChannels.erase(_animChannels.begin() + static_cast<std::ptrdiff_t>(i));
        if (running.finished) {
            running.time = 0.0f;
            running.stateByNodeNumber.clear();
            running.finished = false;
        }
        running.lipAnim = std::move(lipAnim);
        running.properties = std::move(properties);
        _animChannels.push_front(std::move(running));
        return;
    }
    AnimationChannel channel {anim, std::move(lipAnim), std::move(properties)};
    channel.weight = 0.0f;
    _animChannels.push_front(std::move(channel));
}

// Anything but a layer replaces or blends into the base channels only; the
// layers stay on top of it.
void ModelSceneNode::playBase(Animation &anim, std::shared_ptr<LipAnimation> lipAnim, AnimationProperties properties) {
    const size_t layers = layerCount();
    std::deque<AnimationChannel> layerChannels(
        std::make_move_iterator(_animChannels.begin()),
        std::make_move_iterator(_animChannels.begin() + static_cast<std::ptrdiff_t>(layers)));
    _animChannels.erase(_animChannels.begin(), _animChannels.begin() + static_cast<std::ptrdiff_t>(layers));

    AnimationBlendMode blendMode = getAnimationBlendMode(properties.flags);

    switch (blendMode) {
    case AnimationBlendMode::Single:
        // In Single mode, clear channels and add animation on top
        _animChannels.clear();
        _animChannels.push_front(AnimationChannel(anim, lipAnim, properties));
        break;

    case AnimationBlendMode::Blend: {
        // In Blend mode, if there is an animation on top, initiate
        // transition between old and new animations
        bool transition = false;
        if (!_animChannels.empty()) {
            _animChannels[0].freeze = true;
            _animChannels[0].transition = false;
            transition = true;
        }
        // Add animation on top
        _animChannels.push_front(AnimationChannel(anim, lipAnim, properties));
        if (transition) {
            AnimationChannel &channel = _animChannels[0];
            channel.transition = true;
            float start = glm::max(0.0f, channel.anim->transitionTime() - kTransitionLength);
            channel.time = channel.properties.speed < 0.0f ? channelLength(channel) - start : start;
        }
        while (_animChannels.size() > 2ll) {
            _animChannels.pop_back();
        }
        break;
    }

    case AnimationBlendMode::Overlay:
        // In Overlay mode, clear channels only if previous mode is not
        // Overlay and add animation on top
        if (_animBlendMode != AnimationBlendMode::Overlay) {
            _animChannels.clear();
        }
        _animChannels.push_front(AnimationChannel(anim, lipAnim, properties));
        break;

    default:
        break;
    }

    _animBlendMode = blendMode;

    _animChannels.insert(_animChannels.begin(),
                         std::make_move_iterator(layerChannels.begin()),
                         std::make_move_iterator(layerChannels.end()));
}

size_t ModelSceneNode::layerCount() const {
    size_t count = 0;
    while (count < _animChannels.size() && (_animChannels[count].properties.flags & AnimationFlags::layer)) {
        ++count;
    }
    return count;
}

std::optional<std::reference_wrapper<const ModelSceneNode::AnimationChannel>> ModelSceneNode::baseAnimationChannel() const {
    const size_t layers = layerCount();
    if (layers == _animChannels.size()) {
        return std::nullopt;
    }
    return _animChannels[layers];
}

bool ModelSceneNode::removeAnimation(const std::string &name) {
    std::string lower(boost::to_lower_copy(name));
    bool removed = false;
    for (auto it = _animChannels.begin(); it != _animChannels.end();) {
        if (it->anim && it->anim->name() == lower) {
            it = _animChannels.erase(it);
            removed = true;
            continue;
        }
        ++it;
    }
    if (removed && !baseAnimationChannel()) {
        _animBlendMode = AnimationBlendMode::Single;
    }
    return removed;
}

bool ModelSceneNode::fadeOutLayer(const std::string &name) {
    auto lower = boost::to_lower_copy(name);
    bool found = false;
    const size_t layers = layerCount();
    for (size_t i = 0; i < layers; ++i) {
        if (_animChannels[i].anim->name() != lower) {
            continue;
        }
        _animChannels[i].finished = true;
        found = true;
    }
    return found;
}

void ModelSceneNode::removeLayers() {
    _animChannels.erase(_animChannels.begin(), _animChannels.begin() + static_cast<std::ptrdiff_t>(layerCount()));
}

bool ModelSceneNode::isAnimationPlaying(const std::string &name) const {
    std::string lower(boost::to_lower_copy(name));
    for (const auto &channel : _animChannels) {
        if (channel.anim && channel.anim->name() == lower) {
            return true;
        }
    }
    return false;
}

bool ModelSceneNode::restartAnimation(const std::string &name) {
    auto anim = _model->getAnimation(name);
    if (!anim) {
        return false;
    }
    auto channel = std::find_if(_animChannels.begin(), _animChannels.end(), [&](const auto &channel) {
        return channel.anim == anim.get();
    });
    if (channel == _animChannels.end()) {
        return false;
    }

    channel->time = 0.0f;
    channel->stateByNodeNumber.clear();
    channel->finished = false;
    return true;
}

ModelSceneNode::AnimationBlendMode ModelSceneNode::getAnimationBlendMode(int flags) {
    return (flags & AnimationFlags::blend) ? AnimationBlendMode::Blend : ((flags & AnimationFlags::overlay) ? AnimationBlendMode::Overlay : AnimationBlendMode::Single);
}

void ModelSceneNode::updateAnimations(float dt) {
    // Erase finished layers once they have blended out, whether they finished
    // or were faded out: a layer never holds its last pose.
    const auto layersEnd = _animChannels.begin() + static_cast<std::ptrdiff_t>(layerCount());
    auto layersToErase = std::remove_if(_animChannels.begin(), layersEnd, [](auto &channel) {
        return channel.finished && (channel.weight <= 0.0f || channel.anim->transitionTime() <= 0.0f);
    });
    _animChannels.erase(layersToErase, layersEnd);
    const size_t layers = layerCount();

    // Erase finished base channels
    switch (_animBlendMode) {
    case AnimationBlendMode::Single:
    case AnimationBlendMode::Overlay: {
        auto firstBase = _animChannels.begin() + static_cast<std::ptrdiff_t>(layers);
        auto channelsToErase = std::remove_if(firstBase, _animChannels.end(), [](auto &channel) { return channel.finished && (channel.properties.flags & AnimationFlags::fireForget); });
        _animChannels.erase(channelsToErase, _animChannels.end());
        break;
    }
    case AnimationBlendMode::Blend:
        if (_animChannels.size() > layers + 1 && !_animChannels[layers].transition) {
            _animChannels.pop_back();
        }
        if (_animChannels.size() > layers && _animChannels[layers].finished) {
            _animChannels.erase(_animChannels.begin() + static_cast<std::ptrdiff_t>(layers));
        }
        break;
    default:
        break;
    }

    if (_animChannels.empty()) {
        playAnimation("default", nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
        return;
    }

    for (auto &channel : _animChannels) {
        if (!channel.anim) {
            continue;
        }
        if (!channel.freeze) {
            updateAnimationChannel(channel, dt);
        }
    }

    // Apply states and compute bone transforms only when this model is not culled
    if (!_culled) {
        applyAnimationStates(*_model->rootNode(), layerCount());
        applyLookAt(dt);
    }
}

// Head look-at

static constexpr float kLookAtBlendTime = 0.25f;
static constexpr float kMaxLookAtArc = 89.0f;

bool ModelSceneNode::beginLookAt(const std::string &bone, float arcH, float arcV) {
    auto node = _nodeByName.find(bone);
    if (node == _nodeByName.end()) {
        endLookAt();
        return false;
    }
    LookAt look;
    look.bone = bone;
    look.arcH = std::min(kMaxLookAtArc, std::abs(arcH));
    look.arcV = std::min(kMaxLookAtArc, std::abs(arcV));
    // The turn starts from wherever the bone is now.
    look.rotation = glm::quat_cast(node->second->localTransform());
    look.point = glm::vec3(node->second->absoluteTransform()[3]);
    _lookAt = std::move(look);
    return true;
}

void ModelSceneNode::setLookAtPoint(const glm::vec3 &point) {
    if (_lookAt && !_lookAt->returning) {
        _lookAt->point = point;
    }
}

void ModelSceneNode::endLookAt() {
    if (!_lookAt || _lookAt->returning) {
        return;
    }
    _lookAt->returning = true;
    _lookAt->returnTime = 0.0f;
    _lookAt->returnFrom = _lookAt->rotation;
}

// The bone's rotation becomes a yaw toward the point, then a pitch toward it,
// each held within its arc; every frame the bone closes dt / 0.25 of the gap.
void ModelSceneNode::applyLookAt(float dt) {
    if (!_lookAt) {
        return;
    }
    auto &bone = *_nodeByName.at(_lookAt->bone);
    auto &look = *_lookAt;
    const glm::vec3 translation {bone.localTransform()[3]};
    if (look.returning) {
        look.returnTime += dt;
        const float factor = std::min(1.0f, look.returnTime / kLookAtBlendTime);
        look.rotation = glm::slerp(look.returnFrom, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), factor);
        bone.setLocalTransform(glm::translate(translation) * glm::mat4_cast(look.rotation));
        if (factor >= 1.0f) {
            _lookAt.reset();
        }
        return;
    }
    // Measure from the pose the look held last frame.
    bone.setLocalTransform(glm::translate(translation) * glm::mat4_cast(look.rotation));
    const glm::mat4 &world = bone.absoluteTransform();
    const glm::vec3 toPoint {look.point - glm::vec3 {world[3]}};
    const glm::vec3 forward {world * glm::vec4 {0.0f, 1.0f, 0.0f, 0.0f}};
    const glm::vec3 direction {glm::normalize(toPoint)};
    const glm::quat arc = glm::rotation(glm::normalize(forward), direction);
    const glm::vec3 turned {(arc * look.rotation) * glm::vec3 {0.0f, 1.0f, 0.0f}};
    const float yaw = glm::degrees(std::atan2(-turned.x, turned.y));
    const float pitch = glm::degrees(std::asin(glm::clamp(direction.z, -1.0f, 1.0f)));
    const float clampedYaw = glm::clamp(yaw, -look.arcH, look.arcH);
    const float clampedPitch = glm::clamp(pitch, -look.arcV, look.arcV);
    const glm::quat target =
        glm::angleAxis(glm::radians(clampedYaw), glm::vec3(0.0f, 0.0f, 1.0f)) *
        glm::angleAxis(glm::radians(clampedPitch), glm::vec3(1.0f, 0.0f, 0.0f));
    look.rotation = glm::slerp(look.rotation, target, std::min(1.0f, dt / kLookAtBlendTime));
    bone.setLocalTransform(glm::translate(translation) * glm::mat4_cast(look.rotation));
}

// END Head look-at

void ModelSceneNode::setHiliteColor(std::optional<glm::vec3> color) {
    _hiliteColor = color;
    for (auto &[_, attachment] : _attachments) {
        if (attachment->type() == SceneNodeType::Model) {
            static_cast<ModelSceneNode *>(attachment)->setHiliteColor(color);
        }
    }
}

void ModelSceneNode::updateAnimationChannel(AnimationChannel &channel, float dt) {
    // Take length from the lip animation, if any
    float length = channelLength(channel);
    bool backwards = channel.properties.speed < 0.0f;

    // Advance time; a channel played backwards runs from its end to its start
    float oldTime = channel.time;
    channel.time = glm::clamp(channel.time + channel.properties.speed * dt, 0.0f, length);

    // Clear transition flag if past transition time
    if (channel.transition && playedTime(channel) >= channel.anim->transitionTime()) {
        channel.transition = false;
    }

    // Signal events between previous and current time
    for (auto &event : channel.anim->events()) {
        bool passed = backwards ? (event.time < oldTime && event.time >= channel.time)
                                : (event.time > oldTime && event.time <= channel.time);
        if (passed) {
            signalEvent(event.name);
        }
    }

    // Compute animation states only when this model is not culled
    if (!_culled) {
        float transitionTime = backwards ? length - channel.anim->transitionTime() : channel.anim->transitionTime();
        float time = channel.transition ? transitionTime : channel.time;
        channel.stateByNodeNumber.clear();
        computeAnimationStates(channel, time, *_model->rootNode());
    }

    bool lastFrame = channel.time == (backwards ? 0.0f : length);
    if (lastFrame) {
        bool loop = channel.properties.flags & AnimationFlags::loop;
        if (loop) {
            channel.time = backwards ? length : 0.0f;
            rearmSingleEmitters(channel.anim->root());
        } else {
            channel.finished = true;
        }
    }

    // A layer blends in over its transition time and, once finished, blends
    // out over it again; without a transition time it is shown in full.
    if (channel.properties.flags & AnimationFlags::layer) {
        float transitionTime = channel.anim->transitionTime();
        if (transitionTime <= 0.0f) {
            channel.weight = 1.0f;
        } else if (channel.finished) {
            channel.weight = glm::max(0.0f, channel.weight - dt / transitionTime);
        } else {
            channel.weight = glm::min(1.0f, channel.weight + dt / transitionTime);
        }
    }
}

static bool doesNodeHaveAncestor(const ModelNode &node, const std::string &name) {
    if (name.empty()) {
        return true;
    }
    if (node.name() == name) {
        return true;
    }
    auto parent = node.parent();
    if (!parent) {
        return false;
    }
    return doesNodeHaveAncestor(*parent, name);
}

void ModelSceneNode::rearmSingleEmitters(const std::string &animationRoot) {
    for (auto &[number, node] : _nodeByNumber) {
        if (node->type() != SceneNodeType::Emitter ||
            !doesNodeHaveAncestor(node->modelNode(), animationRoot)) {
            continue;
        }
        static_cast<EmitterSceneNode *>(node)->rearmSingle();
    }
}

void ModelSceneNode::computeAnimationStates(AnimationChannel &channel, float time, const ModelNode &modelNode) {
    std::shared_ptr<ModelNode> animNode(channel.anim->getNodeByName(modelNode.name()));
    if (!animNode && !modelNode.parent() && (channel.properties.flags & AnimationFlags::retargetRoot)) {
        // External stunt animations are authored on proxy models. Retarget
        // their root placement track to the live creature model's root.
        animNode = channel.anim->rootNode();
    }
    if (animNode && modelNode.isAnimated() && doesNodeHaveAncestor(modelNode, channel.anim->root())) {
        AnimationState state;
        state.flags = 0;

        glm::vec3 position(modelNode.restPosition());
        glm::quat orientation(modelNode.restOrientation());
        float scale = 1.0f;

        if (channel.lipAnim) {
            uint8_t leftShape, rightShape;
            float factor;
            if (channel.lipAnim->getKeyframes(time, leftShape, rightShape, factor)) {
                float oneOverNumShapes = 1.0f / static_cast<float>(kNumLipShapes);
                float leftShapeTime = leftShape * oneOverNumShapes * channel.anim->length();
                float rightShapeTime = rightShape * oneOverNumShapes * channel.anim->length();
                glm::vec3 leftShapePos, rightShapePos;
                glm::quat leftShapeRot, rightShapeRot;
                if (animNode->positionAtTime(leftShapeTime, leftShapePos) &&
                    animNode->positionAtTime(rightShapeTime, rightShapePos)) {
                    position += channel.properties.scale * glm::mix(leftShapePos, rightShapePos, factor);
                    state.flags |= AnimationStateFlags::transform;
                }
                if (animNode->orientationAtTime(leftShapeTime, leftShapeRot) &&
                    animNode->orientationAtTime(rightShapeTime, rightShapeRot)) {
                    orientation = glm::slerp(leftShapeRot, rightShapeRot, factor);
                    state.flags |= AnimationStateFlags::transform;
                }
            }
        } else {
            glm::vec3 animPosition;
            if (animNode->positionAtTime(time, animPosition)) {
                position += channel.properties.scale * animPosition;
                state.flags |= AnimationStateFlags::transform;
            }
            if (animNode->orientationAtTime(time, orientation)) {
                state.flags |= AnimationStateFlags::transform;
            }
            if (animNode->scaleAtTime(time, scale)) {
                state.flags |= AnimationStateFlags::transform;
            }
        }
        if (state.flags & AnimationStateFlags::transform) {
            state.transform *= glm::scale(glm::vec3(scale));
            state.transform *= glm::translate(position);
            state.transform *= glm::mat4_cast(orientation);
        }
        if (animNode->floatValueAtTime(ControllerTypes::alpha, time, state.alpha)) {
            state.flags |= AnimationStateFlags::alpha;
        }
        if (animNode->vectorValueAtTime(ControllerTypes::selfIllumColor, time, state.selfIllumColor)) {
            state.flags |= AnimationStateFlags::selfIllumColor;
        }
        if (animNode->vectorValueAtTime(ControllerTypes::color, time, state.color)) {
            state.flags |= AnimationStateFlags::color;
        }
        if (animNode->floatValueAtTime(ControllerTypes::birthrate, time, state.birthrate)) {
            state.flags |= AnimationStateFlags::birthrate;
        }
        channel.stateByNodeNumber[modelNode.number()] = std::move(state);
    }

    for (auto &child : modelNode.children()) {
        computeAnimationStates(channel, time, *child);
    }
}

// The transform a factor of the way from one transform to another.
static glm::mat4 blendTransforms(const glm::mat4 &from, const glm::mat4 &to, float factor) {
    glm::vec3 scale1, scale2, translation1, translation2, skew;
    glm::quat orientation1, orientation2;
    glm::vec4 perspective;
    glm::decompose(to, scale1, orientation1, translation1, skew, perspective);
    glm::decompose(from, scale2, orientation2, translation2, skew, perspective);
    glm::mat4 transform {1.0f};
    transform *= glm::scale(glm::mix(scale2, scale1, factor));
    transform *= glm::translate(glm::mix(translation2, translation1, factor));
    transform *= glm::mat4_cast(glm::slerp(orientation2, orientation1, factor));
    return transform;
}

void ModelSceneNode::applyAnimationStates(const ModelNode &modelNode, size_t layers) {
    auto maybeSceneNode = _nodeByNumber.find(modelNode.number());
    if (maybeSceneNode != _nodeByNumber.end()) {
        auto sceneNode = maybeSceneNode->second;
        AnimationState combined;

        switch (_animBlendMode) {
        case AnimationBlendMode::Single:
        case AnimationBlendMode::Blend: {
            if (_animChannels.size() <= layers) {
                break;
            }
            const AnimationChannel &channel1 = _animChannels[layers];
            AnimationState state1;
            auto state1Iter = channel1.stateByNodeNumber.find(modelNode.number());
            if (state1Iter != channel1.stateByNodeNumber.end()) {
                state1 = state1Iter->second;
            }
            bool blend = _animBlendMode == AnimationBlendMode::Blend && channel1.transition && _animChannels.size() > layers + 1;
            if (blend) {
                const AnimationChannel &channel2 = _animChannels[layers + 1];
                AnimationState state2;
                auto state2Iter = channel2.stateByNodeNumber.find(modelNode.number());
                if (state2Iter != channel2.stateByNodeNumber.end()) {
                    state2 = state2Iter->second;
                }
                if (state1.flags & AnimationStateFlags::transform && state2.flags & AnimationStateFlags::transform) {
                    float factor = glm::min(1.0f, playedTime(channel1) / channel1.anim->transitionTime());
                    combined.flags |= AnimationStateFlags::transform;
                    combined.transform = blendTransforms(state2.transform, state1.transform, factor);
                } else if (state1.flags & AnimationStateFlags::transform) {
                    combined.flags |= AnimationStateFlags::transform;
                    combined.transform = state1.transform;
                } else if (state2.flags & AnimationStateFlags::transform) {
                    combined.flags |= AnimationStateFlags::transform;
                    combined.transform = state2.transform;
                }
            } else if (state1.flags & AnimationStateFlags::transform) {
                combined.flags |= AnimationStateFlags::transform;
                combined.transform = state1.transform;
            }
            if (state1.flags & AnimationStateFlags::alpha) {
                combined.flags |= AnimationStateFlags::alpha;
                combined.alpha = state1.alpha;
            }
            if (state1.flags & AnimationStateFlags::selfIllumColor) {
                combined.flags |= AnimationStateFlags::selfIllumColor;
                combined.selfIllumColor = state1.selfIllumColor;
            }
            if (state1.flags & AnimationStateFlags::color) {
                combined.flags |= AnimationStateFlags::color;
                combined.color = state1.color;
            }
            if (state1.flags & AnimationStateFlags::birthrate) {
                combined.flags |= AnimationStateFlags::birthrate;
                combined.birthrate = state1.birthrate;
            }
            break;
        }
        case AnimationBlendMode::Overlay:
            for (size_t i = layers; i < _animChannels.size(); ++i) {
                const AnimationChannel &channel = _animChannels[i];
                auto maybeState = channel.stateByNodeNumber.find(modelNode.number());
                if (maybeState == channel.stateByNodeNumber.end()) {
                    continue;
                }
                const AnimationState &state = maybeState->second;
                if ((state.flags & AnimationStateFlags::transform) && !(combined.flags & AnimationStateFlags::transform)) {
                    combined.flags |= AnimationStateFlags::transform;
                    combined.transform = state.transform;
                }
                if ((state.flags & AnimationStateFlags::alpha) && !(combined.flags & AnimationStateFlags::alpha)) {
                    combined.flags |= AnimationStateFlags::alpha;
                    combined.alpha = state.alpha;
                }
                if ((state.flags & AnimationStateFlags::selfIllumColor) && !(combined.flags & AnimationStateFlags::selfIllumColor)) {
                    combined.flags |= AnimationStateFlags::selfIllumColor;
                    combined.selfIllumColor = state.selfIllumColor;
                }
                if ((state.flags & AnimationStateFlags::color) && !(combined.flags & AnimationStateFlags::color)) {
                    combined.flags |= AnimationStateFlags::color;
                    combined.color = state.color;
                }
                if ((state.flags & AnimationStateFlags::birthrate) && !(combined.flags & AnimationStateFlags::birthrate)) {
                    combined.flags |= AnimationStateFlags::birthrate;
                    combined.birthrate = state.birthrate;
                }
            }
            break;
        default:
            break;
        }

        // Layers go over the base from the oldest to the newest. Each pulls
        // the transform of every node it animates toward its own by its
        // weight, from what lies beneath or, where nothing beneath animates
        // the node, from where the node is; its other states replace those
        // beneath.
        for (size_t i = layers; i-- > 0;) {
            const AnimationChannel &channel = _animChannels[i];
            auto maybeState = channel.stateByNodeNumber.find(modelNode.number());
            if (maybeState == channel.stateByNodeNumber.end()) {
                continue;
            }
            const AnimationState &state = maybeState->second;
            if (state.flags & AnimationStateFlags::transform) {
                const glm::mat4 &beneath = (combined.flags & AnimationStateFlags::transform) ? combined.transform : sceneNode->localTransform();
                combined.transform = channel.weight < 1.0f ? blendTransforms(beneath, state.transform, channel.weight) : state.transform;
                combined.flags |= AnimationStateFlags::transform;
            }
            if (state.flags & AnimationStateFlags::alpha) {
                combined.flags |= AnimationStateFlags::alpha;
                combined.alpha = state.alpha;
            }
            if (state.flags & AnimationStateFlags::selfIllumColor) {
                combined.flags |= AnimationStateFlags::selfIllumColor;
                combined.selfIllumColor = state.selfIllumColor;
            }
            if (state.flags & AnimationStateFlags::color) {
                combined.flags |= AnimationStateFlags::color;
                combined.color = state.color;
            }
            if (state.flags & AnimationStateFlags::birthrate) {
                combined.flags |= AnimationStateFlags::birthrate;
                combined.birthrate = state.birthrate;
            }
        }

        if (combined.flags & AnimationStateFlags::transform) {
            sceneNode->setLocalTransform(combined.transform);
        }
        if (combined.flags & AnimationStateFlags::alpha) {
            static_cast<MeshSceneNode *>(sceneNode)->setAlpha(combined.alpha);
        }
        if (combined.flags & AnimationStateFlags::selfIllumColor) {
            static_cast<MeshSceneNode *>(sceneNode)->setSelfIllumColor(combined.selfIllumColor);
        }
        if (combined.flags & AnimationStateFlags::color) {
            static_cast<LightSceneNode *>(sceneNode)->setColor(combined.color);
        }
        if ((combined.flags & AnimationStateFlags::birthrate) && sceneNode->type() == SceneNodeType::Emitter) {
            static_cast<EmitterSceneNode *>(sceneNode)->setBirthrate(combined.birthrate);
        }
    }

    for (auto &child : modelNode.children()) {
        applyAnimationStates(*child, layers);
    }
}

void ModelSceneNode::pauseAnimation() {
    if (_animChannels.empty()) {
        return;
    }
    _animChannels.front().freeze = true;
}

void ModelSceneNode::resumeAnimation() {
    if (_animChannels.empty()) {
        return;
    }
    _animChannels.front().freeze = false;
}

void ModelSceneNode::setAnimationTime(float time) {
    if (_animChannels.empty()) {
        return;
    }
    auto &channel = _animChannels.front();
    channel.time = time;
    bool looped = (channel.properties.flags & AnimationFlags::loop) != 0;
    bool frozen = channel.freeze;
    if (looped) {
        channel.properties.flags ^= AnimationFlags::loop;
    }
    channel.freeze = false;
    updateAnimations(0.0f);
    channel.freeze = frozen;
    if (looped) {
        channel.properties.flags |= AnimationFlags::loop;
    }
}

bool ModelSceneNode::isAnimationFinished() const {
    return _animChannels.empty() || _animChannels.front().finished;
}

std::string ModelSceneNode::activeAnimationName() const {
    if (_animChannels.empty()) {
        return "";
    }
    const AnimationChannel &channel = _animChannels.front();
    return channel.anim->name();
}

void ModelSceneNode::setModel(Model &model) {
    _children.clear();

    _model = &model;

    _nodeByName.clear();
    _nodeByNumber.clear();
    _attachments.clear();

    _animChannels.clear();
    _animBlendMode = AnimationBlendMode::Single;

    buildNodeTree(*_model->rootNode(), *this);
    computeAABB();
}

} // namespace scene

} // namespace reone
