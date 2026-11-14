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

#include "reone/scene/graph.h"

#include "reone/audio/context.h"
#include "reone/audio/di/services.h"
#include "reone/audio/source.h"
#include "reone/graphics/camera/perspective.h"
#include "reone/graphics/context.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/uniforms.h"
#include "reone/graphics/walkmesh.h"
#include "reone/scene/collision.h"
#include "reone/scene/drawdebug.h"
#include "reone/scene/node/camera.h"
#include "reone/scene/node/emitter.h"
#include "reone/scene/node/grass.h"
#include "reone/scene/node/grasscluster.h"
#include "reone/scene/node/light.h"
#include "reone/scene/node/mesh.h"
#include "reone/scene/node/model.h"
#include "reone/scene/node/particle.h"
#include "reone/scene/node/sound.h"
#include "reone/scene/node/trigger.h"
#include "reone/scene/node/walkmesh.h"
#include "reone/scene/render/pipeline.h"
#include "reone/system/logutil.h"

using namespace reone::graphics;

namespace reone {

namespace scene {

static constexpr int kMaxFlareLights = 4;

static constexpr float kShadowFadeSpeed = 2.0f;

static constexpr float kLightRadiusBias = 64.0f;

static constexpr float kMaxCollisionDistanceWalk = 8.0f;

static constexpr float kMaxCollisionDistanceLineOfSight = 16.0f;
static constexpr float kMaxCollisionDistanceLineOfSight2 = kMaxCollisionDistanceLineOfSight * kMaxCollisionDistanceLineOfSight;

static constexpr float kPointLightShadowsFOV = glm::radians(90.0f);
static constexpr float kPointLightShadowsNearPlane = 0.25f;
static constexpr float kPointLightShadowsFarPlane = 2500.0f;

static const std::vector<float> g_shadowCascadeDivisors {
    0.005f,
    0.015f,
    0.045f,
    0.135f};

void SceneGraph::clear() {
    _modelRoots.clear();
    _walkmeshRoots.clear();
    _triggerRoots.clear();
    // A sound leaves the scene silent.
    for (auto &root : _soundRoots) {
        root->stopSound();
    }
    _soundRoots.clear();
    _grassRoots.clear();
    _roomWalkmeshes.clear();
    _floorStacking.reset();
    _roomTest.reset();
    _activeLights.clear();
}

void SceneGraph::addRoot(std::shared_ptr<ModelSceneNode> node) {
    _modelRoots.push_back(std::move(node));
}

void SceneGraph::addRoot(std::shared_ptr<WalkmeshSceneNode> node) {
    _walkmeshRoots.push_back(std::move(node));
}

void SceneGraph::addRoot(std::shared_ptr<TriggerSceneNode> node) {
    _triggerRoots.push_back(std::move(node));
}

void SceneGraph::addRoot(std::shared_ptr<GrassSceneNode> node) {
    _grassRoots.push_back(std::move(node));
}

void SceneGraph::addRoot(std::shared_ptr<SoundSceneNode> node) {
    _soundRoots.push_back(std::move(node));
}

void SceneGraph::removeRoot(ModelSceneNode &node) {
    for (auto it = _activeLights.begin(); it != _activeLights.end();) {
        if (&(*it)->model() == &node) {
            it = _activeLights.erase(it);
        } else {
            ++it;
        }
    }
    auto it = std::remove_if(
        _modelRoots.begin(),
        _modelRoots.end(),
        [&node](auto &root) { return root.get() == &node; });
    _modelRoots.erase(it, _modelRoots.end());
}

void SceneGraph::removeRoot(WalkmeshSceneNode &node) {
    auto it = std::remove_if(
        _walkmeshRoots.begin(),
        _walkmeshRoots.end(),
        [&node](auto &root) { return root.get() == &node; });
    _walkmeshRoots.erase(it, _walkmeshRoots.end());
}

void SceneGraph::removeRoot(TriggerSceneNode &node) {
    auto it = std::remove_if(
        _triggerRoots.begin(),
        _triggerRoots.end(),
        [&node](auto &root) { return root.get() == &node; });
    _triggerRoots.erase(it, _triggerRoots.end());
}

void SceneGraph::removeRoot(GrassSceneNode &node) {
    auto it = std::remove_if(
        _grassRoots.begin(),
        _grassRoots.end(),
        [&node](auto &root) { return root.get() == &node; });
    _grassRoots.erase(it, _grassRoots.end());
}

void SceneGraph::removeRoot(SoundSceneNode &node) {
    node.stopSound();
    auto it = std::remove_if(
        _soundRoots.begin(),
        _soundRoots.end(),
        [&node](auto &root) { return root.get() == &node; });
    _soundRoots.erase(it, _soundRoots.end());
}

static constexpr float kForceSightPulsePeriod = 2.048f;

void SceneGraph::update(float dt) {
    _forceSightPhase = std::fmod(_forceSightPhase + dt, kForceSightPulsePeriod);
    _textureTime += dt;
    if (_updateRoots) {
        for (auto &root : _modelRoots) {
            root->update(dt);
        }
        // Grass is kept round the camera only while the option shows it; it
        // grows back where the camera is once the option is turned on again.
        if (_graphicsOpt.grass) {
            for (auto &root : _grassRoots) {
                root->update(dt);
            }
        }
        for (auto &root : _soundRoots) {
            root->update(dt);
        }
    }
    if (!_activeCamera) {
        return;
    }
    cullRoots();
    refresh();
    updateLighting();
    updateShadowLight(dt);
    updateFlareLights();
    updateSounds();
    prepareOpaqueLeafs();
    prepareTransparentLeafs();
}

// The glow pulses between half and full strength.
float SceneGraph::forceSightPulse() const {
    return 0.75f + 0.25f * std::sin(glm::two_pi<float>() * _forceSightPhase / kForceSightPulsePeriod);
}

void SceneGraph::cullRoots() {
    const auto roomTest = _roomTest.lock();
    for (auto &root : _modelRoots) {
        if (!root->isEnabled() || !root->isRendered() || (roomTest && root->usage() != ModelUsage::Room && !roomTest->model(*root))) {
            root->setCulled(true);
            continue;
        }

        if (!root->isCullingEnabled()) {
            root->setCulled(false);
            continue; // disable distance and frustum culling
        }

        float distanceToCamera = root->getSquareDistanceTo(*_activeCamera);
        float drawDistance = root->drawDistance() * root->drawDistance();

        bool culled = (distanceToCamera > drawDistance) || (!_activeCamera->isInFrustum(*root));
        root->setCulled(culled);
    }
}

void SceneGraph::updateLighting() {
    // Find closest lights and create a lookup
    auto closestLights = computeClosestLights(kMaxLights, [](auto &light, float distance2) {
        float radius = light.radius() + kLightRadiusBias;
        return distance2 < radius * radius;
    });
    std::set<LightSceneNode *> lookup;
    for (auto &light : closestLights) {
        lookup.insert(light);
    }
    // De-activate active lights, unless found in a lookup. Active lights are removed from the lookup
    for (auto &light : _activeLights) {
        if (lookup.count(light) == 0) {
            light->setActive(false);
        } else {
            lookup.erase(light);
        }
    }
    // Remove active lights that are inactive and completely faded
    for (auto it = _activeLights.begin(); it != _activeLights.end();) {
        auto light = *it;
        if ((!light->isActive() && light->strength() == 0.0f) || (!light->model().isEnabled())) {
            it = _activeLights.erase(it);
        } else {
            ++it;
        }
    }
    // Add closest lights to active lights
    for (auto &light : lookup) {
        if (_activeLights.size() >= kMaxLights) {
            return;
        }
        light->setActive(true);
        _activeLights.push_back(light);
    }
}

void SceneGraph::updateShadowLight(float dt) {
    auto closestLights = computeClosestLights(1, [this](auto &light, float distance2) {
        if (!light.modelNode().light()->shadow) {
            return false;
        }
        float radius = light.radius();
        return distance2 < radius * radius;
    });
    if (_shadowLight) {
        if (closestLights.empty() || _shadowLight != closestLights.front()) {
            _shadowActive = false;
        }
        if (_shadowActive) {
            _shadowStrength = glm::min(1.0f, _shadowStrength + kShadowFadeSpeed * dt);
        } else {
            _shadowStrength = glm::max(0.0f, _shadowStrength - kShadowFadeSpeed * dt);
            if (_shadowStrength == 0.0f) {
                _shadowLight = nullptr;
            }
        }
    }
    if (!_shadowLight && !closestLights.empty()) {
        _shadowLight = closestLights.front();
        _shadowActive = true;
    }
}

void SceneGraph::updateFlareLights() {
    _flareLights = computeClosestLights(kMaxFlareLights, [](auto &light, float distance2) {
        if (light.modelNode().light()->flares.empty()) {
            return false;
        }
        float radius = light.modelNode().light()->flareRadius;
        return distance2 < radius * radius;
    });
}

// A sound is audible while it is enabled and, when positional, within its
// maximum distance of the listener, as when it plays. Which audible sounds
// play is the mixer's to share out, by their groups and the voices.
void SceneGraph::updateSounds() {
    const glm::vec3 listener {_audioSvc.context.listenerPosition()};
    for (auto &root : _soundRoots) {
        const bool audible = root->isEnabled() &&
                             (!root->isPositional() || audio::AudioSource::isWithinRange(root->origin(), root->maxDistance(), listener));
        root->setAudible(audible);
    }
}

void SceneGraph::refresh() {
    _opaqueMeshes.clear();
    _transparentMeshes.clear();
    _blendedDepthMeshes.clear();
    _shellMeshes.clear();
    _shadowMeshes.clear();
    _lights.clear();
    _emitters.clear();

    // A room's lights go with the room, which is disabled while hidden; an
    // object's light goes with the room it stands in itself.
    const auto roomTest = _roomTest.lock();
    for (auto &root : _modelRoots) {
        const bool room = root->usage() == ModelUsage::Room;
        refreshFromNode(*root, room ? nullptr : roomTest.get(), false);
    }
}

void SceneGraph::refreshFromNode(SceneNode &node, const RoomTest *roomTest, bool lightsOnly) {
    // A disabled node (and its subtree) is skipped from the render lists, so
    // callers can suppress a specific mesh or emitter. The renderer otherwise
    // ignores isEnabled on child nodes.
    if (!node.isEnabled()) {
        return;
    }

    bool propagate = true;

    switch (node.type()) {
    case SceneNodeType::Model: {
        // A culled model is not drawn, but its lights shine whether it is
        // drawn or not; one that is not rendered gives nothing.
        auto &model = static_cast<ModelSceneNode &>(node);
        if (model.isCulled()) {
            propagate = model.isRendered();
            lightsOnly = true;
        }
        break;
    }
    case SceneNodeType::Mesh: {
        if (lightsOnly) {
            break;
        }
        // For model nodes, determine whether they should be rendered and cast shadows
        auto &modelNode = static_cast<MeshSceneNode &>(node);
        bool shouldRender = modelNode.shouldRender();
        if (shouldRender) {
            // Sort model nodes into transparent and opaque
            if (modelNode.isTransparent()) {
                _transparentMeshes.push_back(&modelNode);
                if (modelNode.writesBlendedDepth()) {
                    _blendedDepthMeshes.push_back(&modelNode);
                }
            } else {
                _opaqueMeshes.push_back(&modelNode);
            }
        }
        if (shouldRender && modelNode.model().hasBumpedOutShell()) {
            _shellMeshes.push_back(&modelNode);
        }
        if (modelNode.shouldCastShadows()) {
            _shadowMeshes.push_back(&modelNode);
        }
        break;
    }
    case SceneNodeType::Light:
        if (!roomTest || roomTest->point(node.origin())) {
            _lights.push_back(static_cast<LightSceneNode *>(&node));
        }
        break;
    case SceneNodeType::Emitter:
        if (!lightsOnly) {
            _emitters.push_back(static_cast<EmitterSceneNode *>(&node));
        }
        break;
    default:
        break;
    }

    if (propagate) {
        for (auto &child : node.children()) {
            refreshFromNode(*child, roomTest, lightsOnly);
        }
    }
}

void SceneGraph::prepareOpaqueLeafs() {
    _opaqueLeafs.clear();

    std::vector<SceneNode *> bucket;
    auto camera = _activeCamera->camera();

    // Group grass clusters into buckets without sorting. The grass option
    // decides each frame whether grass is drawn.
    for (auto &grass : _grassRoots) {
        if (!_graphicsOpt.grass || !grass->isEnabled()) {
            continue;
        }
        for (auto &child : grass->children()) {
            if (child->type() != SceneNodeType::GrassCluster) {
                continue;
            }
            auto cluster = static_cast<GrassClusterSceneNode *>(child);
            if (!camera->isInFrustum(cluster->origin())) {
                continue;
            }
            if (bucket.size() >= kMaxGrassClusters) {
                _opaqueLeafs.push_back(std::make_pair(grass.get(), bucket));
                bucket.clear();
            }
            bucket.push_back(cluster);
        }
        if (!bucket.empty()) {
            _opaqueLeafs.push_back(std::make_pair(grass.get(), bucket));
            bucket.clear();
        }
    }
}

bool SceneGraph::hasShadowLight() const {
    return _graphicsOpt.shadows && _shadowLight;
}

void SceneGraph::prepareTransparentLeafs() {
    _transparentLeafs.clear();

    auto camera = _activeCamera->camera();

    // Add meshes and emitters to transparent leafs
    std::vector<SceneNode *> leafs;
    for (auto &mesh : _transparentMeshes) {
        leafs.push_back(mesh);
    }
    // Particles are dropped here rather than at the emitter, so the emitters
    // still simulate and still advance the shared random sequence. A switch
    // that also stopped the simulation would move every later draw's noise and
    // make two builds disagree for a reason that is not what is being compared.
    if (_graphicsOpt.particles) {
        for (auto &emitter : _emitters) {
            for (auto &child : emitter->children()) {
                if (child->type() != SceneNodeType::Particle) {
                    continue;
                }
                auto particle = static_cast<ParticleSceneNode *>(child);
                if (!camera->isInFrustum(particle->origin())) {
                    continue;
                }
                leafs.push_back(particle);
            }
        }
    }

    // Group transparent leafs into buckets
    SceneNode *bucketParent = nullptr;
    std::vector<SceneNode *> bucket;
    for (auto leaf : leafs) {
        SceneNode *parent = leaf->parent();
        if (leaf->type() == SceneNodeType::Mesh) {
            parent = &static_cast<MeshSceneNode *>(leaf)->model();
        }
        if (!bucket.empty()) {
            int maxCount = 1;
            if (parent->type() == SceneNodeType::Emitter) {
                maxCount = kMaxParticles;
            } else if (parent->type() == SceneNodeType::Grass) {
                maxCount = kMaxGrassClusters;
            }
            if (bucketParent != parent || bucket.size() >= maxCount) {
                _transparentLeafs.push_back(std::make_pair(bucketParent, bucket));
                bucket.clear();
            }
        }
        bucketParent = parent;
        bucket.push_back(leaf);
    }
    if (bucketParent && !bucket.empty()) {
        _transparentLeafs.push_back(std::make_pair(bucketParent, bucket));
    }
}

static const glm::vec3 kForceSightFogLuminance {0.30f, 0.59f, 0.11f};
static constexpr float kForceSightFogGrey = 0.65f;

Texture &SceneGraph::render(const glm::ivec2 &dim) {
    if (!_renderPipeline) {
        auto rendererType = _graphicsOpt.pbr ? RendererType::PBR : RendererType::Retro;
        _renderPipeline = _renderPipelineFactory.create(rendererType, dim);
        _renderPipeline->init();
    }
    auto &pipeline = *_renderPipeline;
    pipeline.reset();

    auto cameraNode = this->camera();
    if (cameraNode) {
        auto camera = cameraNode->get().camera();
        _graphicsSvc.uniforms.setGlobals([this, &camera](auto &globals) {
            globals.projection = camera->projection();
            globals.projectionInv = camera->projectionInv();
            globals.view = camera->view();
            globals.viewInv = camera->viewInv();
            globals.cameraPosition = glm::vec4(camera->position(), 1.0f);
            globals.worldAmbientColor = glm::vec4(ambientLightColor(), 1.0f);
            globals.clipNear = camera->zNear();
            globals.clipFar = camera->zFar();
            globals.numLights = static_cast<int>(_activeLights.size());
            for (size_t i = 0; i < _activeLights.size(); ++i) {
                auto &light = globals.lights[i];
                light.position = glm::vec4(_activeLights[i]->origin(), _activeLights[i]->isDirectional() ? 0.0f : 1.0f);
                light.color = glm::vec4(_activeLights[i]->color(), 1.0f);
                light.multiplier = _activeLights[i]->multiplier() * _activeLights[i]->strength();
                light.radius = _activeLights[i]->radius();
                light.ambientOnly = static_cast<int>(_activeLights[i]->modelNode().light()->ambientOnly);
                light.dynamicType = _activeLights[i]->modelNode().light()->dynamicType;
            }
            if (hasShadowLight()) {
                computeLightSpaceMatrices();
                for (int i = 0; i < kNumShadowLightSpace; ++i) {
                    globals.shadowLightSpace[i] = _shadowLightSpace[i];
                }
                globals.shadowLightPosition = glm::vec4(shadowLightPosition(), isShadowLightDirectional() ? 0.0 : 1.0);
                globals.shadowCascadeFarPlanes = _shadowCascadeFarPlanes;
                globals.shadowStrength = shadowStrength();
                globals.shadowRadius = shadowRadius();
                globals.shadowSoft = _graphicsOpt.softShadows ? 1 : 0;
            }
            globals.forceSight = isForceSightEnabled() ? 1 : 0;
            if (isFogEnabled()) {
                globals.fogNear = fogNear();
                globals.fogFar = fogFar();
                // Under Force Sight the fog turns to the darkened grey of its
                // colour.
                globals.fogColor = isForceSightEnabled()
                                       ? glm::vec4(glm::vec3(glm::dot(fogColor(), kForceSightFogLuminance) * kForceSightFogGrey), 1.0f)
                                       : glm::vec4(fogColor(), 1.0f);
            }
        });
        auto halfDim = dim / 2;
        auto screenProjection = glm::mat4(1.0f);
        screenProjection *= glm::scale(glm::vec3(halfDim.x, halfDim.y, 1.0f));
        screenProjection *= glm::translate(glm::vec3(0.5f, 0.5f, 0.0f));
        screenProjection *= glm::scale(glm::vec3(0.5f, 0.5f, 1.0f));
        screenProjection *= camera->projection();
        _graphicsSvc.uniforms.setScreenEffect([&camera, &screenProjection](auto &screenEffect) {
            screenEffect.projection = camera->projection();
            screenEffect.projectionInv = glm::inverse(screenEffect.projection);
            screenEffect.screenProjection = screenProjection;
            screenEffect.clipNear = camera->zNear();
            screenEffect.clipFar = camera->zFar();
        });
        if (hasShadowLight()) {
            auto passName = isShadowLightDirectional()
                                ? RenderPassName::DirLightShadowsPass
                                : RenderPassName::PointLightShadows;
            pipeline.inRenderPass(passName, [this](auto &pass) {
                renderShadows(pass);
            });
        }
        pipeline.inRenderPass(RenderPassName::OpaqueGeometry, [this](auto &pass) {
            renderOpaque(pass);
        });
        pipeline.inRenderPass(RenderPassName::BlendedGeometryDepth, [this](auto &pass) {
            for (auto &mesh : _blendedDepthMeshes) {
                mesh->renderBlendedDepth(pass);
            }
        });
        pipeline.inRenderPass(RenderPassName::TransparentGeometry, [this](auto &pass) {
            renderTransparent(pass);
        });
        pipeline.inRenderPass(RenderPassName::PostProcessing, [this, &camera](auto &pass) {
            if (!_flareLights.empty()) {
                renderLensFlares(pass);
            }
        });
        pipeline.inRenderPass(RenderPassName::Debug, [this, &camera](auto &pass) {
            renderDrawDebug(pass, _graphicsSvc, _resourceSvc, name());
        });
    }

    return pipeline.render(_frameEffects);
}

void SceneGraph::renderShadows(IRenderPass &pass) {
    if (!_activeCamera) {
        return;
    }
    _graphicsSvc.context.withFaceCullMode(FaceCullMode::Front, [this, &pass]() {
        for (auto &mesh : _shadowMeshes) {
            mesh->renderShadow(pass);
        }
    });
}

void SceneGraph::renderOpaque(IRenderPass &pass) {
    if (!_activeCamera) {
        return;
    }
    if (_renderWalkmeshes || _renderTriggers) {
        _graphicsSvc.uniforms.setWalkmesh([this](auto &walkmesh) {
            for (int i = 0; i < kMaxWalkmeshMaterials - 1; ++i) {
                walkmesh.materials[i] = _walkableSurfaces.count(i) > 0 ? glm::vec4(0.0f, 1.0f, 0.0f, 1.0f) : glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            }
            walkmesh.materials[kMaxWalkmeshMaterials - 1] = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f); // triggers
        });
    }

    // Draw opaque meshes
    for (auto &mesh : _opaqueMeshes) {
        mesh->render(pass);
    }
    // Draw opaque leafs
    for (auto &[node, leafs] : _opaqueLeafs) {
        node->renderLeafs(pass, leafs);
    }

    if (_renderAABB) {
        for (auto &model : _modelRoots) {
            if (model->isEnabled() && !model->isCulled()) {
                model->renderAABB(pass);
            }
        }
    }
    if (_renderWalkmeshes) {
        for (auto &walkmesh : _walkmeshRoots) {
            if (walkmesh->isEnabled() && !walkmesh->isCulled()) {
                walkmesh->render(pass);
            }
        }
    }
    if (_renderTriggers) {
        for (auto &trigger : _triggerRoots) {
            if (trigger->isEnabled() && !trigger->isCulled()) {
                trigger->render(pass);
            }
        }
    }
}

void SceneGraph::renderTransparent(IRenderPass &pass) {
    if (!_activeCamera || _renderWalkmeshes) {
        return;
    }
    // Draw transparent leafs (incl. meshes)
    for (auto &[node, leafs] : _transparentLeafs) {
        node->renderLeafs(pass, leafs);
    }
    for (auto &mesh : _shellMeshes) {
        auto &model = mesh->model();
        mesh->renderBumpedOutShell(pass, *model.bumpedOutShellTexture(), model.bumpedOutShellOffset());
    }
}

void SceneGraph::renderLensFlares(IRenderPass &pass) {
    // Draw lens flares
    if (_flareLights.empty() || _renderWalkmeshes) {
        return;
    }
    _graphicsSvc.context.withDepthTestMode(DepthTestMode::None, [this, &pass]() {
        for (auto &light : _flareLights) {
            Collision collision;
            if (testLineOfSight(_activeCamera->origin(), light->origin(), collision)) {
                continue;
            }
            light->renderLensFlare(pass, light->modelNode().light()->flares.front());
        }
    });
}

static std::vector<glm::vec4> computeFrustumCornersWorldSpace(const glm::mat4 &projection, const glm::mat4 &view) {
    auto inv = glm::inverse(projection * view);

    std::vector<glm::vec4> corners;
    for (auto x = 0; x < 2; ++x) {
        for (auto y = 0; y < 2; ++y) {
            for (auto z = 0; z < 2; ++z) {
                auto pt = inv * glm::vec4(
                                    2.0f * x - 1.0f,
                                    2.0f * y - 1.0f,
                                    2.0f * z - 1.0f,
                                    1.0f);
                corners.push_back(pt / pt.w);
            }
        }
    }

    return corners;
}

static glm::mat4 computeDirectionalLightSpaceMatrix(
    float fov,
    float aspect,
    float near, float far,
    const glm::vec3 &lightDir,
    const glm::mat4 &cameraView) {

    auto projection = glm::perspective(fov, aspect, near, far);

    glm::vec3 center(0.0f);
    auto corners = computeFrustumCornersWorldSpace(projection, cameraView);
    for (auto &v : corners) {
        center += glm::vec3(v);
    }
    center /= corners.size();

    auto lightView = glm::lookAt(center - lightDir, center, glm::vec3(0.0f, 1.0f, 0.0f));

    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();
    for (auto &v : corners) {
        auto trf = lightView * v;
        minX = std::min(minX, trf.x);
        maxX = std::max(maxX, trf.x);
        minY = std::min(minY, trf.y);
        maxY = std::max(maxY, trf.y);
        minZ = std::min(minZ, trf.z);
        maxZ = std::max(maxZ, trf.z);
    }
    float zMult = 10.0f;
    if (minZ < 0.0f) {
        minZ *= zMult;
    } else {
        minZ /= zMult;
    }
    if (maxZ < 0.0f) {
        maxZ /= zMult;
    } else {
        maxZ *= zMult;
    }

    auto lightProjection = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);
    return lightProjection * lightView;
}

static glm::mat4 getPointLightView(const glm::vec3 &lightPos, CubeMapFace face) {
    switch (face) {
    case CubeMapFace::PositiveX:
        return glm::lookAt(lightPos, lightPos + glm::vec3(1.0, 0.0, 0.0), glm::vec3(0.0, -1.0, 0.0));
    case CubeMapFace::NegativeX:
        return glm::lookAt(lightPos, lightPos + glm::vec3(-1.0, 0.0, 0.0), glm::vec3(0.0, -1.0, 0.0));
    case CubeMapFace::PositiveY:
        return glm::lookAt(lightPos, lightPos + glm::vec3(0.0, 1.0, 0.0), glm::vec3(0.0, 0.0, 1.0));
    case CubeMapFace::NegativeY:
        return glm::lookAt(lightPos, lightPos + glm::vec3(0.0, -1.0, 0.0), glm::vec3(0.0, 0.0, -1.0));
    case CubeMapFace::PositiveZ:
        return glm::lookAt(lightPos, lightPos + glm::vec3(0.0, 0.0, 1.0), glm::vec3(0.0, -1.0, 0.0));
    case CubeMapFace::NegativeZ:
        return glm::lookAt(lightPos, lightPos + glm::vec3(0.0, 0.0, -1.0), glm::vec3(0.0, -1.0, 0.0));
    default:
        throw std::invalid_argument("Invalid cube map face: " + std::to_string(static_cast<int>(face)));
    }
}

void SceneGraph::computeLightSpaceMatrices() {
    if (isShadowLightDirectional()) {
        auto camera = std::static_pointer_cast<PerspectiveCamera>(this->camera()->get().camera());
        auto lightDir = glm::normalize(camera->position() - shadowLightPosition());
        float fovy = camera->fovy();
        float aspect = camera->aspect();
        float cameraNear = camera->zNear();
        float cameraFar = camera->zFar();
        for (int i = 0; i < kNumShadowCascades; ++i) {
            float far = cameraFar * g_shadowCascadeDivisors[i];
            float near = cameraNear;
            if (i > 0) {
                near = cameraFar * g_shadowCascadeDivisors[i - 1];
            }
            _shadowLightSpace[i] = computeDirectionalLightSpaceMatrix(fovy, aspect, near, far, lightDir, camera->view());
            _shadowCascadeFarPlanes[i] = far;
        }
    } else {
        glm::mat4 projection(glm::perspective(kPointLightShadowsFOV, 1.0f, kPointLightShadowsNearPlane, kPointLightShadowsFarPlane));
        for (int i = 0; i < kNumCubeFaces; ++i) {
            glm::mat4 lightView(getPointLightView(shadowLightPosition(), static_cast<CubeMapFace>(i)));
            _shadowLightSpace[i] = projection * lightView;
        }
    }
}

std::vector<LightSceneNode *> SceneGraph::computeClosestLights(int count, const std::function<bool(const LightSceneNode &, float)> &pred) const {
    // Compute distance from each light to the camera
    std::vector<std::pair<LightSceneNode *, float>> distances;
    for (auto &light : _lights) {
        float distance2 = light->getSquareDistanceTo(*_activeCamera);
        if (!pred(*light, distance2)) {
            continue;
        }
        distances.push_back(std::make_pair(light, distance2));
    }

    // Sort lights by distance to the camera. Directional lights are prioritizied
    sort(distances.begin(), distances.end(), [](auto &a, auto &b) {
        auto aLight = a.first;
        auto bLight = b.first;
        if (aLight->isDirectional() && !bLight->isDirectional()) {
            return true;
        }
        if (!aLight->isDirectional() && bLight->isDirectional()) {
            return false;
        }
        float aDistance = a.second;
        float bDistance = b.second;
        return aDistance < bDistance;
    });

    // Keep up to maximum number of lights
    if (distances.size() > count) {
        distances.erase(distances.begin() + count, distances.end());
    }

    std::vector<LightSceneNode *> lights;
    for (auto &light : distances) {
        lights.push_back(light.first);
    }
    return lights;
}

bool SceneGraph::testLineOfSight(const glm::vec3 &origin, const glm::vec3 &dest, Collision &outCollision) const {
    const auto originToDest = dest - origin;
    const float maxDistance = glm::length(originToDest);
    // A line of no length meets nothing.
    if (maxDistance == 0.0f) {
        return false;
    }
    const auto dir = originToDest / maxDistance;
    float minDistance = std::numeric_limits<float>::max();

    // Every room and every object walkmesh is tested, however far the line
    // runs from its ends.
    for (auto &root : _walkmeshRoots) {
        if (!root->isEnabled()) {
            continue;
        }
        glm::vec3 originLocal;
        glm::vec3 dirLocal;
        if (root->walkmesh().isAreaWalkmesh()) {
            originLocal = origin;
            dirLocal = dir;
        } else {
            originLocal = root->absoluteTransformInverse() * glm::vec4 {origin, 1.0f};
            dirLocal = root->absoluteTransformInverse() * glm::vec4 {dir, 0.0f};
        }
        auto raycast = root->walkmesh().raycast(_lineOfSightSurfaces, originLocal, dirLocal, maxDistance, /*ignoreBackface=*/false);
        if (raycast.fail || raycast.distance > minDistance) {
            continue;
        }
        uint32_t material = root->walkmesh().materials[raycast.face];
        glm::vec3 normal = root->walkmesh().normals[raycast.face];
        outCollision.user = root->user();
        outCollision.intersection = origin + raycast.distance * dir;
        outCollision.normal = root->absoluteTransform() * glm::vec4(normal, 0.0f);
        outCollision.material = material;
        minDistance = raycast.distance;
    }

    return minDistance != std::numeric_limits<float>::max();
}

bool SceneGraph::testWalk(const glm::vec3 &origin, const glm::vec3 &dest, const IUser *excludeUser, Collision &outCollision) const {
    glm::vec3 originToDest(dest - origin);
    glm::vec3 dir(glm::normalize(originToDest));
    float maxDistance = glm::length(originToDest);
    float minDistance = std::numeric_limits<float>::max();
    // The whole segment is tested, however long it is.
    float reach = std::max(kMaxCollisionDistanceWalk, maxDistance);

    for (auto &root : _walkmeshRoots) {
        if (!root->isEnabled() || root->user() == excludeUser) {
            continue;
        }
        if (!root->walkmesh().isAreaWalkmesh()) {
            float distance2 = root->getSquareDistanceTo(origin);
            if (distance2 > reach * reach) {
                continue;
            }
        }
        glm::vec3 objSpaceOrigin(root->absoluteTransformInverse() * glm::vec4(origin, 1.0f));
        glm::vec3 objSpaceDir(root->absoluteTransformInverse() * glm::vec4(dir, 0.0f));
        auto raycast = root->walkmesh().raycast(_walkcheckSurfaces, objSpaceOrigin, objSpaceDir, reach, /*ignoreBackface=*/false);
        if (raycast.fail || raycast.distance > maxDistance || raycast.distance > minDistance) {
            continue;
        }
        uint32_t material = root->walkmesh().materials[raycast.face];
        glm::vec3 normal = root->walkmesh().normals[raycast.face];
        outCollision.user = root->user();
        outCollision.intersection = origin + raycast.distance * dir;
        outCollision.normal = root->absoluteTransform() * glm::vec4(normal, 0.0f);
        outCollision.material = material;
        minDistance = raycast.distance;
    }

    return minDistance != std::numeric_limits<float>::max();
}

// The inverse of a unit direction for a box test, a zero component taken as
// the largest float, so that a line starting on a box face still has a
// number for its distance to it.
static glm::vec3 inverseDirection(const glm::vec3 &dir) {
    glm::vec3 inverse;
    for (int i = 0; i < 3; ++i) {
        inverse[i] = dir[i] != 0.0f ? 1.0f / dir[i] : std::numeric_limits<float>::max();
    }
    return inverse;
}

// How far along a line, in an AABB mesh's space and along a unit direction,
// it first meets a face of one of the surfaces, from either side, within its
// length; and which face. The tree's boxes pass over what the line misses and
// what lies beyond the nearest face found.
static std::optional<float> raycastAABBMesh(const ModelNode::TriangleMesh &mesh, const std::set<uint32_t> &surfaces,
                                            const glm::vec3 &origin, const glm::vec3 &dir, float maxDistance, uint32_t &outFace) {
    const auto &faces = mesh.mesh->faces();
    const auto &vertices = mesh.mesh->vertices();
    const glm::vec3 invDir {inverseDirection(dir)};
    std::optional<float> nearest;
    std::stack<const ModelNode::AABBTree *> nodes;
    nodes.push(mesh.aabbTree.get());
    while (!nodes.empty()) {
        const auto *node = nodes.top();
        nodes.pop();
        const float reach = nearest.value_or(maxDistance);
        float boxDistance = 0.0f;
        if (!node->aabb.raycast(origin, invDir, reach, boxDistance)) {
            continue;
        }
        if (node->faceIndex < 0) {
            if (node->left) {
                nodes.push(node->left.get());
            }
            if (node->right) {
                nodes.push(node->right.get());
            }
            continue;
        }
        const auto faceIndex = static_cast<size_t>(node->faceIndex);
        if (faceIndex >= faces.size()) {
            continue;
        }
        const auto &face = faces[faceIndex];
        if (surfaces.count(face.material) == 0 ||
            std::any_of(face.vertices.begin(), face.vertices.end(), [&vertices](uint16_t vertex) {
                return vertex >= vertices.size();
            })) {
            continue;
        }
        glm::vec2 baryPosition {0.0f};
        float distance = 0.0f;
        if (glm::intersectRayTriangle(origin, dir,
                                      vertices[face.vertices[0]].position,
                                      vertices[face.vertices[1]].position,
                                      vertices[face.vertices[2]].position,
                                      baryPosition, distance) &&
            distance > 0.0f && distance < reach) {
            nearest = distance;
            outFace = static_cast<uint32_t>(faceIndex);
        }
    }
    return nearest;
}

// A door's or placeable's model stops a camera unless the object lets it
// through, it takes no scene hits, or the model is classified as a
// character or a lightsaber, or as a placeable without the hit filter
// override of its subclassification.
static bool modelStopsCamera(const ModelSceneNode &root) {
    static constexpr int kPassedClassifications = MdlClassification::character | MdlClassification::lightsaber;
    const auto usage = root.usage();
    if ((usage != ModelUsage::Door && usage != ModelUsage::Placeable) || !root.isEnabled() || root.cameraPassesThrough() ||
        !root.takesSceneHits()) {
        return false;
    }
    const int classification = root.model().classification();
    if ((classification & kPassedClassifications) != 0) {
        return false;
    }
    return (classification & MdlClassification::placeable) == 0 ||
           (root.model().subclassification() & MdlSubclassification::hitFilterOverride) != 0;
}

// A room stops a camera by the AABB mesh of its model, every room whether it
// is shown or not. A door or placeable that does stops it by the triangles of
// its model's meshes as they are posed.
bool SceneGraph::testCameraLine(const glm::vec3 &origin, const glm::vec3 &dest, Collision &outCollision) const {
    const glm::vec3 originToDest {dest - origin};
    const float maxDistance = glm::length(originToDest);
    // A line of no length meets nothing.
    if (maxDistance == 0.0f) {
        return false;
    }
    const glm::vec3 dir {originToDest / maxDistance};
    float nearest = std::numeric_limits<float>::max();
    for (auto &root : _modelRoots) {
        if (!modelStopsCamera(*root)) {
            continue;
        }
        const auto share = root->intersectSegment(origin, dest);
        if (!share || *share * maxDistance >= nearest) {
            continue;
        }
        outCollision.user = root->user();
        outCollision.intersection = origin + *share * originToDest;
        outCollision.normal = -dir;
        outCollision.material = -1;
        nearest = *share * maxDistance;
    }
    testRoomModels(origin, dest, nearest, outCollision);
    return nearest != std::numeric_limits<float>::max();
}

void SceneGraph::testRoomModels(const glm::vec3 &origin, const glm::vec3 &dest, float &nearest, Collision &outCollision) const {
    const glm::vec3 dir {glm::normalize(dest - origin)};
    for (auto &root : _modelRoots) {
        if (root->usage() != ModelUsage::Room) {
            continue;
        }
        const auto aabbNode = root->model().getAABBNode();
        if (!aabbNode || !aabbNode->mesh()->mesh) {
            continue;
        }
        // A room stands still: a line that misses its model's box misses its
        // mesh, and is let go before its mesh's space is worked out.
        const glm::vec3 roomOrigin {root->absoluteTransformInverse() * glm::vec4 {origin, 1.0f}};
        const glm::vec3 roomDest {root->absoluteTransformInverse() * glm::vec4 {dest, 1.0f}};
        const float roomLength = glm::length(roomDest - roomOrigin);
        float boxDistance = 0.0f;
        if (roomLength == 0.0f ||
            !root->aabb().raycast(roomOrigin, inverseDirection((roomDest - roomOrigin) / roomLength), roomLength, boxDistance)) {
            continue;
        }
        // The line is taken into the mesh's space and its hit brought back.
        const glm::mat4 toWorld {root->absoluteTransform() * aabbNode->absoluteTransform()};
        const glm::vec3 meshOrigin {aabbNode->absoluteTransformInverse() * glm::vec4 {roomOrigin, 1.0f}};
        const glm::vec3 meshDest {aabbNode->absoluteTransformInverse() * glm::vec4 {roomDest, 1.0f}};
        const float meshLength = glm::length(meshDest - meshOrigin);
        if (meshLength == 0.0f) {
            continue;
        }
        const glm::vec3 meshDir {(meshDest - meshOrigin) / meshLength};
        uint32_t faceIndex = 0;
        const auto hit = raycastAABBMesh(*aabbNode->mesh(), _cameraSurfaces, meshOrigin, meshDir, meshLength, faceIndex);
        if (!hit) {
            continue;
        }
        const glm::vec3 intersection {toWorld * glm::vec4 {meshOrigin + *hit * meshDir, 1.0f}};
        const float distance = glm::length(intersection - origin);
        if (distance >= nearest) {
            continue;
        }
        const auto &face = aabbNode->mesh()->mesh->faces()[faceIndex];
        // A face met from behind still turns its normal back along the line.
        glm::vec3 normal {glm::normalize(glm::vec3 {toWorld * glm::vec4 {face.normal, 0.0f}})};
        if (glm::dot(normal, dir) > 0.0f) {
            normal = -normal;
        }
        outCollision.user = root->user();
        outCollision.intersection = intersection;
        outCollision.normal = normal;
        outCollision.material = static_cast<int>(face.material);
        nearest = distance;
    }
}

// The rooms, and the posed models of creatures, placeables and doors, and of
// what is attached to them, that take scene hits. Of the excluded objects
// only the models attached to them count, except the one at the excluded hook.
bool SceneGraph::testSceneHit(const glm::vec3 &origin, const glm::vec3 &dest, const std::vector<const IUser *> &excluded, const std::string &excludedHook, Collision &outCollision) const {
    const glm::vec3 originToDest {dest - origin};
    const float maxDistance = glm::length(originToDest);
    if (maxDistance == 0.0f) {
        return false;
    }
    float nearest = std::numeric_limits<float>::max();
    testRoomModels(origin, dest, nearest, outCollision);
    for (auto &root : _modelRoots) {
        const auto usage = root->usage();
        if (!root->isEnabled() || (usage != ModelUsage::Creature && usage != ModelUsage::Placeable && usage != ModelUsage::Door) ||
            !root->takesSceneHits()) {
            continue;
        }
        std::optional<float> share;
        if (std::find(excluded.begin(), excluded.end(), root->user()) != excluded.end()) {
            share = root->intersectAttachments(origin, dest, excludedHook);
        } else {
            share = root->intersectSegment(origin, dest);
            if (auto attached = root->intersectAttachments(origin, dest); attached && (!share || *attached < *share)) {
                share = attached;
            }
        }
        if (!share || *share * maxDistance >= nearest) {
            continue;
        }
        outCollision.user = root->user();
        outCollision.intersection = origin + *share * originToDest;
        outCollision.normal = -originToDest / maxDistance;
        outCollision.material = -1;
        nearest = *share * maxDistance;
    }
    return nearest != std::numeric_limits<float>::max();
}

void SceneGraph::setWalkableSurfaces(std::set<uint32_t> surfaces) {
    _walkableSurfaces = std::move(surfaces);
    updateBlockingSurfaces();
    _floorStacking.reset();
}

void SceneGraph::setWalkcheckSurfaces(std::set<uint32_t> surfaces) {
    _walkcheckSurfaces = std::move(surfaces);
    updateBlockingSurfaces();
}

void SceneGraph::updateBlockingSurfaces() {
    _blockingSurfaces.clear();
    for (uint32_t material : _walkcheckSurfaces) {
        if (_walkableSurfaces.count(material) == 0) {
            _blockingSurfaces.insert(material);
        }
    }
}

void SceneGraph::setRoomWalkmeshes(std::vector<std::shared_ptr<WalkmeshSceneNode>> rooms) {
    // An area made active again sets the same rooms, whose floors are known.
    if (rooms == _roomWalkmeshes) {
        return;
    }
    _roomWalkmeshes = std::move(rooms);
    _floorStacking.reset();
}

const SceneGraph::FloorStacking &SceneGraph::floorStacking() const {
    if (_floorStacking) {
        return *_floorStacking;
    }
    FloorStacking stacking;
    // The rooms with walkable faces, each with the level box round them and
    // its floor normals, worked out once for every pair it is in.
    struct Floors {
        const WalkmeshSceneNode *room {nullptr};
        glm::vec2 low {std::numeric_limits<float>::max()};
        glm::vec2 high {-std::numeric_limits<float>::max()};
        Walkmesh::FloorNormals normals;
    };
    std::vector<Floors> floors;
    for (const auto &room : _roomWalkmeshes) {
        if (!room) {
            continue;
        }
        const Walkmesh &walkmesh = room->walkmesh();
        Floors box {room.get()};
        for (size_t index = 0; index < walkmesh.faces.size(); ++index) {
            if (!Walkmesh::hasSurfaceMaterial(_walkableSurfaces, walkmesh.materials[index])) {
                continue;
            }
            for (uint32_t vertex : walkmesh.faces[index].indices) {
                const glm::vec2 point {room->absoluteTransform() * glm::vec4(walkmesh.vertices[vertex], 1.0f)};
                box.low = glm::min(box.low, point);
                box.high = glm::max(box.high, point);
            }
        }
        if (box.low.x > box.high.x) {
            continue;
        }
        box.normals = walkmesh.floorNormals(_walkableSurfaces);
        if (walkmesh.hasFloorOverFloor(box.normals)) {
            stacking.rooms.insert(room.get());
        }
        floors.push_back(std::move(box));
    }
    stacking.area = !stacking.rooms.empty();
    for (size_t i = 0; i < floors.size() && !stacking.area; ++i) {
        for (size_t j = i + 1; j < floors.size() && !stacking.area; ++j) {
            const Floors &a = floors[i];
            const Floors &b = floors[j];
            if (a.high.x < b.low.x || b.high.x < a.low.x || a.high.y < b.low.y || b.high.y < a.low.y) {
                continue;
            }
            stacking.area = a.room->walkmesh().hasFloorOver(a.normals, b.room->walkmesh(), b.normals,
                                                            a.room->absoluteTransformInverse() * b.room->absoluteTransform());
        }
    }
    if (stacking.area) {
        debug("Scene '" + _name + "' lays floors over floors: " +
              (stacking.rooms.empty() ? std::string("rooms lay floors over one another")
                                      : std::to_string(stacking.rooms.size()) + " rooms lay floors over their own"));
    }
    _floorStacking = std::move(stacking);
    return *_floorStacking;
}

// Room surfaces are looked for from this high above a point down to as far
// below it.
static constexpr float kRoomSurfaceReach = 1000.0f;

// The face of the given materials of a room walkmesh that a point, in the
// walkmesh's own coordinates, stands on (see Walkmesh::standingFace), going
// down from a height over twice kRoomSurfaceReach; a vertical that meets none
// is tried once more, shifted 0.001 along the diagonal of x and y.
static std::optional<Walkmesh::Standing> standingRoomFace(const WalkmeshSceneNode &room, const std::set<uint32_t> &surfaces,
                                                          const std::set<uint32_t> &walkable, const glm::vec3 &point, float top,
                                                          bool stacked) {
    static const glm::vec3 kRetryShift {0.001f * glm::normalize(glm::vec3 {1.0f, 1.0f, 0.0f})};
    auto found = room.walkmesh().standingFace(surfaces, walkable, point, top, 2.0f * kRoomSurfaceReach, stacked);
    if (!found) {
        found = room.walkmesh().standingFace(surfaces, walkable, point + kRetryShift, top, 2.0f * kRoomSurfaceReach, stacked);
    }
    return found;
}

std::shared_ptr<WalkmeshSceneNode> SceneGraph::findRoomWalkmesh(const glm::vec3 &position) const {
    // The first room met; where floors lie over others, the first whose floor
    // under the point is at its level comes before it.
    const bool stacked = hasStackedFloors();
    std::shared_ptr<WalkmeshSceneNode> first;
    for (const auto &room : _roomWalkmeshes) {
        if (!room) {
            continue;
        }
        const glm::mat4 &toLocal = room->absoluteTransformInverse();
        const glm::vec3 local {toLocal * glm::vec4(position, 1.0f)};
        const float top = (toLocal * glm::vec4(position + glm::vec3 {0.0f, 0.0f, kRoomSurfaceReach}, 1.0f)).z;
        const auto face = standingRoomFace(*room, _walkcheckSurfaces, _walkableSurfaces, local, top, isStacked(*room));
        if (!face) {
            continue;
        }
        if (!stacked) {
            return room;
        }
        if (std::abs(face->height - local.z) <= Walkmesh::kFloorHeadroom) {
            return room;
        }
        if (!first) {
            first = room;
        }
    }
    return first;
}

std::optional<RoomFace> SceneGraph::findRoomFace(const WalkmeshSceneNode &room, const glm::vec3 &position, RoomFaceProbe probe) const {
    static const std::set<uint32_t> kAllSurfaces = [] {
        std::set<uint32_t> all;
        for (uint32_t material = 0; material < Walkmesh::kSurfaceMaterialCount; ++material) {
            all.insert(material);
        }
        return all;
    }();
    const glm::vec3 local {room.absoluteTransformInverse() * glm::vec4(position, 1.0f)};
    const float top = probe == RoomFaceProbe::Locating
                          ? (room.absoluteTransformInverse() * glm::vec4(position + glm::vec3 {0.0f, 0.0f, kRoomSurfaceReach}, 1.0f)).z
                          : kRoomSurfaceReach;
    const auto standing = standingRoomFace(room, probe == RoomFaceProbe::Any ? kAllSurfaces : _walkcheckSurfaces, _walkableSurfaces, local, top,
                                           isStacked(room));
    if (!standing) {
        return std::nullopt;
    }
    const Walkmesh::Face face {room.walkmesh().getFace(standing->face)};
    // The face's plane is taken at the point itself; an upright plane gives
    // height 0.
    float localHeight = 0.0f;
    if (face.normal.z != 0.0f) {
        const float distance = -glm::dot(face.normal, face.vertices[0]);
        localHeight = -(face.normal.x * local.x + face.normal.y * local.y + distance) / face.normal.z;
    }
    const glm::vec3 point {room.absoluteTransform() * glm::vec4(local.x, local.y, localHeight, 1.0f)};
    return RoomFace {face.material, point.z, glm::vec3 {room.absoluteTransform() * glm::vec4(face.normal, 0.0f)}};
}

RoomWalkEnd SceneGraph::walkRooms(const glm::vec3 &origin, const glm::vec3 &dest, std::optional<int> maxRooms, bool withBands,
                                  std::vector<RoomStretch> &outStretches, Collision &outCollision) const {
    // A perimeter crossing this close to where the walk entered a room, or to
    // where it ends, is that point itself.
    static constexpr float kPerimeterTolerance = 1e-4f;

    const glm::vec3 line {dest - origin};
    const glm::vec2 level {line};
    const float length = glm::length(level);
    const glm::vec3 back {length > 0.0f ? glm::vec3(-level / length, 0.0f) : glm::vec3(0.0f)};
    auto room = findRoomWalkmesh(origin);
    if (!room) {
        outCollision.user = nullptr;
        outCollision.intersection = origin;
        outCollision.normal = back;
        return RoomWalkEnd::OffRooms;
    }
    const float tolerance = length > kPerimeterTolerance ? kPerimeterTolerance / length : 1.0f;
    float entered = 0.0f;
    // The height of the floor at the perimeter edge the walk last crossed,
    // where the faces it walked across give it.
    std::optional<float> edgeHeight;
    for (int rooms = 1;; ++rooms) {
        glm::vec3 entry {origin + line * entered};
        if (edgeHeight) {
            entry.z = *edgeHeight;
        }
        if (maxRooms && rooms > *maxRooms) {
            outCollision.user = room->user();
            outCollision.intersection = entry;
            outCollision.normal = back;
            return RoomWalkEnd::RoomLimit;
        }
        const glm::mat4 &toLocal = room->absoluteTransformInverse();
        const glm::vec3 localEntry {toLocal * glm::vec4(entry, 1.0f)};
        const glm::vec3 localDest {toLocal * glm::vec4(dest, 1.0f)};
        const float shift = localEntry.z - entry.z;
        // In a room that lays floors over floors, the walk leaves it where it
        // leaves the faces it crosses, not where its level line first meets
        // the perimeter of any.
        const bool stacked = isStacked(*room);
        std::optional<graphics::Walkmesh::FaceWalk> faceWalk;
        if (withBands || stacked) {
            faceWalk = room->walkmesh().walkFaces(_walkcheckSurfaces, _walkableSurfaces, localEntry, localDest, stacked);
        }
        std::optional<graphics::Walkmesh::HeightBand> band;
        if (withBands && faceWalk) {
            band = graphics::Walkmesh::HeightBand {faceWalk->band.low - shift, faceWalk->band.high - shift};
        }
        outStretches.push_back(RoomStretch {room, entered, band});
        if (length <= kPerimeterTolerance) {
            return RoomWalkEnd::Dest;
        }
        // A face walk that leaves the faces no further on than where it
        // entered the room gives way to the level line.
        std::optional<graphics::Walkmesh::PerimeterCrossing> crossing;
        bool walked = stacked && faceWalk;
        edgeHeight.reset();
        if (walked && faceWalk->exit) {
            crossing = faceWalk->exit;
            crossing->fraction = entered + (1.0f - entered) * crossing->fraction;
            if (crossing->fraction > entered + tolerance) {
                edgeHeight = faceWalk->exitHeight - shift;
            } else {
                crossing.reset();
                walked = false;
            }
        }
        if (!walked) {
            crossing = room->walkmesh().findPerimeterCrossing(
                glm::vec2(toLocal * glm::vec4(origin, 1.0f)),
                glm::vec2(localDest),
                entered + tolerance);
        }
        if (!crossing || crossing->fraction >= 1.0f - tolerance) {
            return RoomWalkEnd::Dest;
        }
        const int next = crossing->transition;
        if (next >= 0 && next < static_cast<int>(_roomWalkmeshes.size()) && _roomWalkmeshes[next]) {
            room = _roomWalkmeshes[next];
            entered = crossing->fraction;
            continue;
        }
        outCollision.user = room->user();
        outCollision.intersection = origin + line * crossing->fraction;
        if (edgeHeight) {
            outCollision.intersection.z = *edgeHeight;
        }
        outCollision.normal = room->absoluteTransform() * glm::vec4(crossing->normal, 0.0f, 0.0f);
        return RoomWalkEnd::Edge;
    }
}

bool SceneGraph::testRoomWalk(const glm::vec3 &origin, const glm::vec3 &dest, float radius, float height,
                              std::optional<int> maxRooms, std::vector<float> &outCrossings, Collision &outCollision) const {
    // Blocking faces are looked for this far beyond the heights the walk spans.
    static constexpr float kBandMargin = 0.1f;

    std::vector<RoomStretch> stretches;
    const RoomWalkEnd end = walkRooms(origin, dest, maxRooms, /*withBands=*/false, stretches, outCollision);
    for (size_t i = 1; i < stretches.size(); ++i) {
        outCrossings.push_back(stretches[i].from);
    }
    if (end != RoomWalkEnd::Edge) {
        return end != RoomWalkEnd::Dest;
    }
    const auto &room = stretches.back().room;
    const glm::vec3 entry {origin + (dest - origin) * stretches.back().from};
    const glm::vec3 exit {outCollision.intersection};
    const glm::vec3 localExit {room->absoluteTransformInverse() * glm::vec4(exit, 1.0f)};
    const float shift = localExit.z - exit.z;
    const float minZ = std::min(entry.z, exit.z) - kBandMargin + shift;
    const float maxZ = std::max(entry.z, exit.z) + height + kBandMargin + shift;
    return !room->walkmesh().hasFaceWithin(_blockingSurfaces, localExit, localExit, radius, minZ, maxZ);
}

ModelSceneNode *SceneGraph::pickModelAt(int x, int y, IUser *except) const {
    if (!_activeCamera) {
        return nullptr;
    }

    auto camera = _activeCamera->camera();
    glm::vec4 viewport(0.0f, 0.0f, _graphicsOpt.width, _graphicsOpt.height);
    glm::vec3 start(glm::unProject(glm::vec3(x, _graphicsOpt.height - y, 0.0f), camera->view(), camera->projection(), viewport));
    glm::vec3 end(glm::unProject(glm::vec3(x, _graphicsOpt.height - y, 1.0f), camera->view(), camera->projection(), viewport));
    glm::vec3 dir(glm::normalize(end - start));

    std::vector<std::pair<ModelSceneNode *, float>> distances;
    for (auto &model : _modelRoots) {
        if (!model->isPickable() || (except == model->user())) {
            continue;
        }
        if (model->getSquareDistanceTo(start) > kMaxCollisionDistanceLineOfSight2) {
            continue;
        }
        auto objSpaceStart = model->absoluteTransformInverse() * glm::vec4(start, 1.0f);
        auto objSpaceInvDir = 1.0f / (model->absoluteTransformInverse() * glm::vec4(dir, 0.0f));
        float distance;
        if (model->aabb().raycast(objSpaceStart, objSpaceInvDir, kMaxCollisionDistanceLineOfSight, distance) && distance > 0.0f) {
            Collision collision;
            if (testLineOfSight(start, start + distance * dir, collision) && collision.user != model->user()) {
                continue;
            }
            distances.push_back(std::make_pair(model.get(), distance));
        }
    }
    if (distances.empty()) {
        return nullptr;
    }
    sort(distances.begin(), distances.end(), [](auto &left, auto &right) { return left.second < right.second; });

    return distances[0].first;
}

std::optional<std::reference_wrapper<ModelSceneNode>> SceneGraph::pickModelRay(const glm::vec3 &origin, const glm::vec3 &dir) const {
    ModelSceneNode *model {nullptr};
    float minDistance = std::numeric_limits<float>::max();
    for (auto &root : _modelRoots) {
        if (!root->isEnabled() || root->isCulled() || !root->isPickable()) {
            continue;
        }
        auto aabbWorld = root->aabb() * root->absoluteTransform();
        float distance;
        if (aabbWorld.raycast(origin, 1.0f / dir, std::numeric_limits<float>::max(), distance) &&
            distance < minDistance) {
            model = root.get();
            minDistance = distance;
        }
    }
    if (!model) {
        return std::nullopt;
    }
    return *model;
}

std::shared_ptr<CameraSceneNode> SceneGraph::newCamera() {
    auto node = newSceneNode<CameraSceneNode>();
    return std::move(node);
}

std::shared_ptr<DummySceneNode> SceneGraph::newDummy(ModelNode &modelNode) {
    auto node = newSceneNode<DummySceneNode, ModelNode &>(modelNode);
    return std::move(node);
}

std::shared_ptr<ModelSceneNode> SceneGraph::newModel(Model &model, ModelUsage usage) {
    auto node = newSceneNode<ModelSceneNode, Model &, ModelUsage>(model, usage);
    node->init();
    return std::move(node);
}

std::shared_ptr<WalkmeshSceneNode> SceneGraph::newWalkmesh(Walkmesh &walkmesh) {
    auto node = newSceneNode<WalkmeshSceneNode, Walkmesh &>(walkmesh);
    node->init();
    return std::move(node);
}

std::shared_ptr<SoundSceneNode> SceneGraph::newSound() {
    auto node = newSceneNode<SoundSceneNode>();
    return std::move(node);
}

std::shared_ptr<MeshSceneNode> SceneGraph::newMesh(ModelSceneNode &model, ModelNode &modelNode) {
    auto node = newSceneNode<MeshSceneNode, ModelSceneNode &, ModelNode &>(model, modelNode);
    node->init();
    return std::move(node);
}

std::shared_ptr<LightSceneNode> SceneGraph::newLight(ModelSceneNode &model, ModelNode &modelNode) {
    auto node = newSceneNode<LightSceneNode, ModelSceneNode &, ModelNode &>(model, modelNode);
    node->init();
    return std::move(node);
}

std::shared_ptr<TriggerSceneNode> SceneGraph::newTrigger(std::vector<glm::vec3> geometry) {
    auto node = newSceneNode<TriggerSceneNode, std::vector<glm::vec3>>(std::move(geometry));
    node->init();
    return std::move(node);
}

std::shared_ptr<EmitterSceneNode> SceneGraph::newEmitter(ModelNode &modelNode) {
    auto node = newSceneNode<EmitterSceneNode, ModelNode &>(modelNode);
    node->init();
    return std::move(node);
}

std::shared_ptr<ParticleSceneNode> SceneGraph::newParticle(EmitterSceneNode &emitter) {
    auto node = newSceneNode<ParticleSceneNode, EmitterSceneNode &>(emitter);
    return std::move(node);
}

std::shared_ptr<GrassSceneNode> SceneGraph::newGrass(GrassProperties properties, ModelNode &aabbNode) {
    auto node = newSceneNode<GrassSceneNode, GrassProperties, ModelNode &>(properties, aabbNode);
    node->init();
    return std::move(node);
}

std::shared_ptr<GrassClusterSceneNode> SceneGraph::newGrassCluster(GrassSceneNode &grass) {
    auto node = newSceneNode<GrassClusterSceneNode>();
    return std::move(node);
}

} // namespace scene

} // namespace reone
