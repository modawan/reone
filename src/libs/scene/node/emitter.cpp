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

#include "reone/scene/node/emitter.h"

#include "reone/graphics/context.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/uniforms.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/textures.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/camera.h"
#include "reone/scene/node/particle.h"
#include "reone/scene/particleutil.h"
#include "reone/scene/render/pass.h"
#include "reone/system/randomutil.h"

using namespace reone::graphics;

namespace reone {

namespace scene {

bool EmitterSceneNode::AnimationState::empty() const {
    return !birthrate &&
           !lifeExpectancy &&
           !xSize &&
           !ySize &&
           !frameStart &&
           !frameEnd &&
           !fps &&
           !spread &&
           !velocity &&
           !randomVelocity &&
           !blurLength &&
           !mass &&
           !grav &&
           !lightningDelay &&
           !lightningRadius &&
           !lightningScale &&
           !lightningSubDiv &&
           !particleSizeStart &&
           !particleSizeMid &&
           !particleSizeEnd &&
           !colorStart &&
           !colorMid &&
           !colorEnd &&
           !alphaStart &&
           !alphaMid &&
           !alphaEnd;
}

EmitterSceneNode::AnimationState EmitterSceneNode::animationStateAt(
    const ModelNode &animationNode,
    float time) {

    AnimationState state;
    auto readFloat = [&animationNode, time](ControllerType type) -> std::optional<float> {
        float value;
        if (animationNode.floatValueAtTime(type, time, value)) {
            return value;
        }
        return std::nullopt;
    };
    auto readInt = [&readFloat](ControllerType type) -> std::optional<int> {
        auto value = readFloat(type);
        return value ? std::make_optional(static_cast<int>(*value)) : std::nullopt;
    };
    auto readVector = [&animationNode, time](ControllerType type) -> std::optional<glm::vec3> {
        glm::vec3 value;
        if (animationNode.vectorValueAtTime(type, time, value)) {
            return value;
        }
        return std::nullopt;
    };

    state.birthrate = readFloat(ControllerTypes::birthrate);
    state.lifeExpectancy = readFloat(ControllerTypes::lifeExp);
    state.xSize = readFloat(ControllerTypes::xSize);
    state.ySize = readFloat(ControllerTypes::ySize);
    state.frameStart = readInt(ControllerTypes::frameStart);
    state.frameEnd = readInt(ControllerTypes::frameEnd);
    state.fps = readFloat(ControllerTypes::fps);
    state.spread = readFloat(ControllerTypes::spread);
    state.velocity = readFloat(ControllerTypes::velocity);
    state.randomVelocity = readFloat(ControllerTypes::randVel);
    state.blurLength = readFloat(ControllerTypes::blurLength);
    state.mass = readFloat(ControllerTypes::mass);
    state.grav = readFloat(ControllerTypes::grav);
    state.lightningDelay = readFloat(ControllerTypes::lightingDelay);
    state.lightningRadius = readFloat(ControllerTypes::lightingRadius);
    state.lightningScale = readFloat(ControllerTypes::lightingScale);
    state.lightningSubDiv = readInt(ControllerTypes::lightingSubDiv);
    state.particleSizeStart = readFloat(ControllerTypes::sizeStart);
    state.particleSizeMid = readFloat(ControllerTypes::sizeMid);
    state.particleSizeEnd = readFloat(ControllerTypes::sizeEnd);
    state.colorStart = readVector(ControllerTypes::colorStart);
    state.colorMid = readVector(ControllerTypes::colorMid);
    state.colorEnd = readVector(ControllerTypes::colorEnd);
    state.alphaStart = readFloat(ControllerTypes::alphaStart);
    state.alphaMid = readFloat(ControllerTypes::alphaMid);
    state.alphaEnd = readFloat(ControllerTypes::alphaEnd);

    return state;
}

void EmitterSceneNode::applyAnimationState(const AnimationState &state) {
    if (state.birthrate) {
        _birthrate = glm::max(*state.birthrate, 0.0f);
        if (_birthrate == 0.0f) {
            _birthAccumulator = 0.0f;
        }
    }
    if (state.lifeExpectancy) {
        _lifeExpectancy = *state.lifeExpectancy;
    }
    if (state.xSize) {
        _size.x = *state.xSize;
    }
    if (state.ySize) {
        _size.y = *state.ySize;
    }
    if (state.frameStart) {
        _frameStart = *state.frameStart;
    }
    if (state.frameEnd) {
        _frameEnd = *state.frameEnd;
    }
    if (state.fps) {
        _fps = *state.fps;
    }
    if (state.spread) {
        _spread = *state.spread;
    }
    if (state.velocity) {
        _velocity = *state.velocity;
    }
    if (state.randomVelocity) {
        _randomVelocity = *state.randomVelocity;
    }
    if (state.blurLength) {
        _blurLength = *state.blurLength;
    }
    if (state.mass) {
        _mass = *state.mass;
    }
    if (state.grav) {
        _grav = *state.grav;
    }
    if (state.lightningDelay) {
        _lightningDelay = *state.lightningDelay;
    }
    if (state.lightningRadius) {
        _lightningRadius = *state.lightningRadius;
    }
    if (state.lightningScale) {
        _lightningScale = *state.lightningScale;
    }
    if (state.lightningSubDiv) {
        _lightningSubDiv = *state.lightningSubDiv;
    }
    if (state.particleSizeStart) {
        _particleSize.start = *state.particleSizeStart;
    }
    if (state.particleSizeMid) {
        _particleSize.mid = *state.particleSizeMid;
    }
    if (state.particleSizeEnd) {
        _particleSize.end = *state.particleSizeEnd;
    }
    if (state.colorStart) {
        _color.start = *state.colorStart;
    }
    if (state.colorMid) {
        _color.mid = *state.colorMid;
    }
    if (state.colorEnd) {
        _color.end = *state.colorEnd;
    }
    if (state.alphaStart) {
        _alpha.start = *state.alphaStart;
    }
    if (state.alphaMid) {
        _alpha.mid = *state.alphaMid;
    }
    if (state.alphaEnd) {
        _alpha.end = *state.alphaEnd;
    }
}

void EmitterSceneNode::init() {
    _modelNode.floatValueAtTime(ControllerTypes::birthrate, 0.0f, _birthrate);
    _modelNode.floatValueAtTime(ControllerTypes::lifeExp, 0.0f, _lifeExpectancy);
    _modelNode.floatValueAtTime(ControllerTypes::xSize, 0.0f, _size.x);
    _modelNode.floatValueAtTime(ControllerTypes::ySize, 0.0f, _size.y);

    float frameStart, frameEnd;
    if (_modelNode.floatValueAtTime(ControllerTypes::frameStart, 0.0f, frameStart)) {
        _frameStart = static_cast<int>(frameStart);
    }
    if (_modelNode.floatValueAtTime(ControllerTypes::frameEnd, 0.0f, frameEnd)) {
        _frameEnd = static_cast<int>(frameEnd);
    }

    _modelNode.floatValueAtTime(ControllerTypes::fps, 0.0f, _fps);
    _modelNode.floatValueAtTime(ControllerTypes::spread, 0.0f, _spread);
    _modelNode.floatValueAtTime(ControllerTypes::velocity, 0.0f, _velocity);
    _modelNode.floatValueAtTime(ControllerTypes::randVel, 0.0f, _randomVelocity);
    _modelNode.floatValueAtTime(ControllerTypes::blurLength, 0.0f, _blurLength);
    _modelNode.floatValueAtTime(ControllerTypes::mass, 0.0f, _mass);
    _modelNode.floatValueAtTime(ControllerTypes::grav, 0.0f, _grav);
    _modelNode.floatValueAtTime(ControllerTypes::lightingDelay, 0.0f, _lightningDelay);
    _modelNode.floatValueAtTime(ControllerTypes::lightingRadius, 0.0f, _lightningRadius);
    _modelNode.floatValueAtTime(ControllerTypes::lightingScale, 0.0f, _lightningScale);

    float lightingSubDiv;
    if (_modelNode.floatValueAtTime(ControllerTypes::lightingSubDiv, 0.0f, lightingSubDiv)) {
        _lightningSubDiv = static_cast<int>(lightingSubDiv);
    }

    _modelNode.floatValueAtTime(ControllerTypes::sizeStart, 0.0f, _particleSize.start);
    _modelNode.floatValueAtTime(ControllerTypes::sizeMid, 0.0f, _particleSize.mid);
    _modelNode.floatValueAtTime(ControllerTypes::sizeEnd, 0.0f, _particleSize.end);
    _modelNode.vectorValueAtTime(ControllerTypes::colorStart, 0.0f, _color.start);
    _modelNode.vectorValueAtTime(ControllerTypes::colorMid, 0.0f, _color.mid);
    _modelNode.vectorValueAtTime(ControllerTypes::colorEnd, 0.0f, _color.end);
    _modelNode.floatValueAtTime(ControllerTypes::alphaStart, 0.0f, _alpha.start);
    _modelNode.floatValueAtTime(ControllerTypes::alphaMid, 0.0f, _alpha.mid);
    _modelNode.floatValueAtTime(ControllerTypes::alphaEnd, 0.0f, _alpha.end);

}

void EmitterSceneNode::update(float dt) {
    removeExpiredParticles(dt);
    if (isSpawningSuppressed()) {
        discardSpawnTime(dt);
    } else {
        spawnParticles(dt);
    }

    for (auto &child : _children) {
        if (child->type() != SceneNodeType::Particle) {
            continue;
        }
        auto particle = static_cast<ParticleSceneNode *>(child);
        particle->update(dt);
    }
}

bool EmitterSceneNode::isSpawningSuppressed() const {
    for (auto ancestor = parent(); ancestor; ancestor = ancestor->parent()) {
        if (ancestor->type() == SceneNodeType::Model && ancestor->isCulled()) {
            return true;
        }
    }
    return false;
}

void EmitterSceneNode::discardSpawnTime(float dt) {
    _birthAccumulator = 0.0f;

    if (_modelNode.emitter()->updateMode != ModelNode::Emitter::UpdateMode::Lightning) {
        return;
    }
    _birthTimer.update(dt);
    if (_birthTimer.elapsed()) {
        _birthTimer.reset(_lightningDelay);
    }
}

void EmitterSceneNode::removeExpiredParticles(float dt) {
    if (_lifeExpectancy == -1.0f) {
        return;
    }
    std::vector<ParticleSceneNode *> expiredParticles;
    for (auto &child : _children) {
        if (child->type() != SceneNodeType::Particle) {
            continue;
        }
        auto particle = static_cast<ParticleSceneNode *>(child);
        if (particle->isExpired()) {
            expiredParticles.push_back(particle);
        }
    }
    for (auto &particle : expiredParticles) {
        removeChild(*particle);
        _particlePool.push_back(particle);
    }
}

void EmitterSceneNode::spawnParticles(float dt) {
    std::shared_ptr<ModelNode::Emitter> emitter(_modelNode.emitter());
    switch (emitter->updateMode) {
    case ModelNode::Emitter::UpdateMode::Fountain:
        for (int spawnCount = particleutil::advanceSpawnAccumulator(_birthrate, dt, _birthAccumulator);
             spawnCount > 0;
             --spawnCount) {
            doSpawnParticle();
        }
        break;
    case ModelNode::Emitter::UpdateMode::Single:
        if (!_spawned || (_children.empty() && emitter->loop)) {
            doSpawnParticle();
            _spawned = true;
        }
        break;
    case ModelNode::Emitter::UpdateMode::Lightning:
        _birthTimer.update(dt);
        if (_birthTimer.elapsed()) {
            spawnLightningParticles();
            _birthTimer.reset(_lightningDelay);
        }
        break;
    default:
        break;
    }
}

ParticleSceneNode *EmitterSceneNode::takeParticle() {
    if (!_particlePool.empty()) {
        auto particle = _particlePool.front();
        _particlePool.pop_front();
        return particle;
    }

    int maxParticles = _modelNode.emitter()->updateMode == ModelNode::Emitter::UpdateMode::Single
                           ? 1
                           : particleutil::kMaxEmitterParticles;
    if (_particleCount >= maxParticles) {
        return nullptr;
    }

    auto particle = _sceneGraph.newParticle(*this).get();
    ++_particleCount;
    return particle;
}

void EmitterSceneNode::doSpawnParticle() {
    auto particle = takeParticle();
    if (!particle) {
        return;
    }
    particle->setLifetime(0.0f);

    float halfW = 0.005f * _size.x;
    float halfH = 0.005f * _size.y;
    glm::vec3 position(randomFloat(-halfW, halfW), randomFloat(-halfH, halfH), 0.0f);
    particle->setLocalTransform(glm::translate(position));

    float halfSpread = 0.5f * _spread;
    float angle1 = randomFloat(-halfSpread, halfSpread);
    float angle2 = randomFloat(-halfSpread, halfSpread);
    glm::vec3 dir(glm::sin(angle1), glm::sin(angle2), glm::cos(angle1) * glm::cos(angle2));
    glm::vec3 velocity((_velocity + randomFloat(0.0f, _randomVelocity)) * dir);
    particle->setVelocity(std::move(velocity));

    particle->setFrame(_frameStart);
    if (_fps > 0.0f) {
        particle->setAnimLength((_frameEnd - _frameStart + 1) / _fps);
    }

    addChild(*particle);
}

void EmitterSceneNode::spawnLightningParticles() {
    // Ensure there is a reference node directly under this emitter
    auto ref = std::find_if(_children.begin(), _children.end(), [](auto &child) { return child->type() == SceneNodeType::Dummy; });
    if (ref == _children.end()) {
        return;
    }

    float halfW = 0.005f * _size.x;
    float halfH = 0.005f * _size.y;
    glm::vec3 origin(randomFloat(-halfW, halfW), randomFloat(-halfH, halfH), 0.0f);
    glm::vec3 emitterSpaceRefPos(_absTransformInv * glm::vec4((*ref)->origin(), 1.0f));
    glm::vec3 refToOrigin(emitterSpaceRefPos - origin);
    float distance = glm::abs(refToOrigin.z);
    float segmentLength = distance / static_cast<float>(_lightningSubDiv + 1);
    float halfRadius = 0.5f * _lightningRadius;

    std::vector<std::pair<glm::vec3, glm::vec3>> segments;
    segments.resize(_lightningSubDiv + 1);
    segments[0].first = origin;
    for (int i = 1; i < _lightningSubDiv + 1; ++i) {
        glm::vec3 dir(glm::normalize(emitterSpaceRefPos - segments[i - 1].first));
        glm::vec3 offset(
            randomFloat(-halfRadius, halfRadius),
            randomFloat(-halfRadius, halfRadius),
            0.0f);
        segments[i - 1].second = segments[i - 1].first + segmentLength * dir + offset;
        segments[i].first = segments[i - 1].second;
    }
    segments[_lightningSubDiv].second = emitterSpaceRefPos;

    // Return all particles to pool
    for (auto it = _children.begin(); it != _children.end();) {
        auto child = *it;
        if ((*it)->type() == SceneNodeType::Particle) {
            _particlePool.push_back(static_cast<ParticleSceneNode *>(child));
            it = _children.erase(it);
        } else {
            ++it;
        }
    }

    for (auto &segment : segments) {
        auto particle = takeParticle();
        if (!particle) {
            return;
        }
        particle->setLifetime(0.0f);

        glm::vec3 endToStart(segment.second - segment.first);
        glm::vec3 center(0.5f * (segment.first + segment.second));
        particle->setLocalTransform(glm::translate(center));
        particle->setDir(_absTransform * glm::vec4(glm::normalize(endToStart), 0.0f));
        particle->setSize(glm::vec2(_lightningScale, glm::length(endToStart)));

        addChild(*particle);
    }
}

void EmitterSceneNode::detonate() {
    if (isSpawningSuppressed()) {
        return;
    }
    doSpawnParticle();
}

void EmitterSceneNode::renderLeafs(IRenderPass &pass, const std::vector<SceneNode *> &leafs) {
    if (leafs.empty()) {
        return;
    }
    auto emitter = _modelNode.emitter();
    auto texture = _resourceSvc.textures.get(emitter->textureName, TextureUsage::MainTex);
    if (!texture) {
        return;
    }
    auto emitterRight = glm::vec3(_absTransform[0]);
    auto emitterUp = glm::vec3(_absTransform[1]);
    auto emitterForward = glm::vec3(_absTransform[2]);

    auto &cameraNode = _sceneGraph.camera()->get();
    auto view = cameraNode.camera()->view();
    auto cameraRight = glm::vec3(view[0][0], view[1][0], view[2][0]);
    auto cameraUp = glm::vec3(view[0][1], view[1][1], view[2][1]);
    auto cameraForward = glm::vec3(view[0][2], view[1][2], view[2][2]);

    auto particles = std::vector<ParticleInstance>(leafs.size());
    for (size_t i = 0; i < leafs.size(); ++i) {
        const auto particle = static_cast<ParticleSceneNode *>(leafs[i]);
        particles[i].frame = particle->frame();
        particles[i].position = particle->origin();
        particles[i].size = glm::vec2(particle->size());
        particles[i].color = glm::vec4(
            particle->color() * _renderProfile.colorTint * _renderProfile.colorIntensity,
            particle->alpha());
        particles[i].color.a *= _renderProfile.opacity;
        switch (emitter->renderMode) {
        case ModelNode::Emitter::RenderMode::BillboardToLocalZ:
            particles[i].right = glm::vec4(emitterUp, 0.0f);
            particles[i].up = glm::vec4(emitterRight, 0.0f);
            break;
        case ModelNode::Emitter::RenderMode::MotionBlur: {
            auto basis = particleutil::buildMotionBlurBasis(
                _absTransform,
                particle->velocity(),
                cameraNode.origin() - particle->origin(),
                cameraRight,
                cameraUp,
                particles[i].size.y,
                _blurLength);
            particles[i].size.x = glm::min(particles[i].size.x, _renderProfile.motionMaxWidth);
            particles[i].size.y = particleutil::clampMotionTrailLength(
                particles[i].size.y,
                basis.lengthScale,
                _renderProfile.motionLengthScale,
                _renderProfile.motionMaxLength);
            particles[i].color.a *= _renderProfile.motionOpacity;
            particles[i].right = glm::vec4(basis.right, 0.0f);
            particles[i].up = glm::vec4(basis.up, 0.0f);
            break;
        }
        case ModelNode::Emitter::RenderMode::BillboardToWorldZ:
            particles[i].size *= _renderProfile.worldZScale;
            particles[i].color.a *= _renderProfile.worldZOpacity;
            particles[i].right = glm::vec4(0.0f, 1.0f, 0.0, 0.0f);
            particles[i].up = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            break;
        case ModelNode::Emitter::RenderMode::AlignedToParticleDir:
            particles[i].right = glm::vec4(emitterRight, 0.0f);
            particles[i].up = glm::vec4(emitterForward, 0.0f);
            break;
        case ModelNode::Emitter::RenderMode::Linked: {
            auto particleUp = particle->dir();
            auto particleForward = glm::cross(particleUp, cameraRight);
            auto particleRight = glm::cross(particleForward, particleUp);
            particles[i].right = glm::vec4(particleRight, 0.0f);
            particles[i].up = glm::vec4(particleUp, 0.0f);
            break;
        }
        case ModelNode::Emitter::RenderMode::Normal:
        default:
            particles[i].right = glm::vec4(cameraRight, 0.0f);
            particles[i].up = glm::vec4(cameraUp, 0.0f);
            break;
        }
        if (emitter->renderMode != ModelNode::Emitter::RenderMode::MotionBlur &&
            emitter->renderMode != ModelNode::Emitter::RenderMode::BillboardToWorldZ) {
            float largestDimension = glm::max(particles[i].size.x, particles[i].size.y);
            float largeParticleFactor = glm::smoothstep(1.0f, 4.0f, largestDimension);
            particles[i].size *= glm::mix(1.0f, _renderProfile.largeParticleScale, largeParticleFactor);
        }
    }
    bool twosided = _modelNode.emitter()->twosided || _modelNode.emitter()->renderMode == ModelNode::Emitter::RenderMode::MotionBlur;
    auto faceCulling = twosided ? FaceCullMode::None : FaceCullMode::Back;
    bool premultipliedAlpha = emitter->blendMode == ModelNode::Emitter::BlendMode::Lighten;
    bool motionBlur = emitter->renderMode == ModelNode::Emitter::RenderMode::MotionBlur;
    pass.drawParticles(
        *texture,
        faceCulling,
        premultipliedAlpha,
        motionBlur,
        _renderProfile.policy,
        emitter->gridSize,
        particles);
}

} // namespace scene

} // namespace reone
