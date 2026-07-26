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

#include "reone/system/timer.h"

#include "../render/pass.h"
#include "modelnode.h"

namespace reone {

namespace scene {

class ModelSceneNode;
class ParticleSceneNode;

/**
 * Optional presentation tuning applied to a complete particle model.
 *
 * The default values preserve authored Odyssey emitter behavior. Game-facing
 * effects can opt into a profile without teaching the renderer asset names.
 */
struct ParticleRenderProfile {
    float largeParticleScale {1.0f};
    float worldZScale {1.0f};
    float opacity {1.0f};
    float worldZOpacity {1.0f};
    float motionLengthScale {1.0f};
    float motionMaxWidth {std::numeric_limits<float>::max()};
    float motionOpacity {1.0f};
    glm::vec3 colorTint {1.0f};
    float colorIntensity {1.0f};
    ParticleRenderPolicy policy;
    float motionMaxLength {std::numeric_limits<float>::max()};
};

class EmitterSceneNode : public ModelNodeSceneNode {
public:
    struct AnimationState {
        std::optional<float> birthrate;
        std::optional<float> lifeExpectancy;
        std::optional<float> xSize;
        std::optional<float> ySize;
        std::optional<int> frameStart;
        std::optional<int> frameEnd;
        std::optional<float> fps;
        std::optional<float> spread;
        std::optional<float> velocity;
        std::optional<float> randomVelocity;
        std::optional<float> blurLength;
        std::optional<float> mass;
        std::optional<float> grav;
        std::optional<float> lightningDelay;
        std::optional<float> lightningRadius;
        std::optional<float> lightningScale;
        std::optional<int> lightningSubDiv;
        std::optional<float> particleSizeStart;
        std::optional<float> particleSizeMid;
        std::optional<float> particleSizeEnd;
        std::optional<glm::vec3> colorStart;
        std::optional<glm::vec3> colorMid;
        std::optional<glm::vec3> colorEnd;
        std::optional<float> alphaStart;
        std::optional<float> alphaMid;
        std::optional<float> alphaEnd;

        bool empty() const;
    };

    EmitterSceneNode(
        graphics::ModelNode &modelNode,
        ISceneGraph &sceneGraph,
        graphics::GraphicsServices &graphicsSvc,
        audio::AudioServices &audioSvc,
        resource::ResourceServices &resourceSvc) :
        ModelNodeSceneNode(
            modelNode,
            SceneNodeType::Emitter,
            sceneGraph,
            graphicsSvc,
            audioSvc,
            resourceSvc) {
    }

    void init();

    void update(float dt) override;

    void renderLeafs(IRenderPass &pass, const std::vector<SceneNode *> &leafs) override;

    void detonate();

    static AnimationState animationStateAt(const graphics::ModelNode &animationNode, float time);
    void applyAnimationState(const AnimationState &state);
    void setRenderProfile(const ParticleRenderProfile &profile) { _renderProfile = profile; }
    const ParticleRenderProfile &renderProfile() const { return _renderProfile; }

    float getParticleSize(float time) const { return _particleSize.get(time); };
    glm::vec3 getColor(float time) const { return _color.get(time); };
    float getAlpha(float time) const { return _alpha.get(time); };

    float lifeExpectancy() const { return _lifeExpectancy; }
    int frameStart() const { return _frameStart; }
    int frameEnd() const { return _frameEnd; }
    float grav() const { return _grav; }

private:
    template <class T>
    struct StartMidEnd {
        T start;
        T mid;
        T end;

        T get(float factor) const {
            if (factor < 0.5f) {
                return glm::mix(start, mid, 2.0f * factor);
            } else {
                return glm::mix(mid, end, 2.0f * factor - 1.0f);
            }
        }
    };

    StartMidEnd<float> _particleSize;
    StartMidEnd<glm::vec3> _color;
    StartMidEnd<float> _alpha;

    float _birthrate {0.0f};      /**< rate of particle birth per second */
    float _lifeExpectancy {0.0f}; /**< life of each particle in seconds */
    glm::vec2 _size {0.0f};
    int _frameStart {0};
    int _frameEnd {0};
    float _fps {0.0f};
    float _spread {0.0f};
    float _velocity {0.0f};
    float _randomVelocity {0.0f};
    float _blurLength {0.0f};
    float _mass {0.0f};
    float _grav {0.0f};
    float _lightningDelay {0.0f};
    float _lightningRadius {0.0f};
    float _lightningScale {0.0f};
    int _lightningSubDiv {0};
    ParticleRenderProfile _renderProfile;

    float _birthAccumulator {0.0f};
    Timer _birthTimer;
    bool _spawned {false};
    int _particleCount {0};

    std::deque<ParticleSceneNode *> _particlePool;

    void spawnParticles(float dt);
    void removeExpiredParticles(float dt);
    void doSpawnParticle();
    void spawnLightningParticles();
    ParticleSceneNode *takeParticle();
    bool isSpawningSuppressed() const;
    void discardSpawnTime(float dt);
};

} // namespace scene

} // namespace reone
