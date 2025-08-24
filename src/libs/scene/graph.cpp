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

#include "reone/audio/di/services.h"
#include "reone/graphics/camera/perspective.h"
#include "reone/graphics/context.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/uniforms.h"
#include "reone/graphics/walkmesh.h"
#include "reone/scene/collision.h"
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
static constexpr int kMaxSoundCount = 4;

static constexpr float kShadowFadeSpeed = 2.0f;
static constexpr float kElevationTestZ = 1024.0f;

static constexpr float kLightRadiusBias = 64.0f;

static constexpr float kMaxCollisionDistanceWalk = 8.0f;
static constexpr float kMaxCollisionDistanceWalk2 = kMaxCollisionDistanceWalk * kMaxCollisionDistanceWalk;

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
    _soundRoots.clear();
    _grassRoots.clear();
    _activeLights.clear();
}

void SceneGraph::addRoot(std::shared_ptr<ModelSceneNode> node) {
    _modelRoots.push_back(node);
}

void SceneGraph::addRoot(std::shared_ptr<WalkmeshSceneNode> node) {
    _walkmeshRoots.push_back(node);
}

void SceneGraph::addRoot(std::shared_ptr<TriggerSceneNode> node) {
    _triggerRoots.push_back(node);
}

void SceneGraph::addRoot(std::shared_ptr<GrassSceneNode> node) {
    _grassRoots.push_back(node);
}

void SceneGraph::addRoot(std::shared_ptr<SoundSceneNode> node) {
    _soundRoots.push_back(node);
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
    auto it = std::remove_if(
        _soundRoots.begin(),
        _soundRoots.end(),
        [&node](auto &root) { return root.get() == &node; });
    _soundRoots.erase(it, _soundRoots.end());
}

void SceneGraph::update(float dt) {
    if (_updateRoots) {
        for (auto &root : _modelRoots) {
            root->update(dt);
        }
        for (auto &root : _grassRoots) {
            root->update(dt);
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

void SceneGraph::cullRoots() {
    for (auto &root : _modelRoots) {
        bool culled =
            !root->isEnabled() ||
            root->getSquareDistanceTo(*_activeCamera) > root->drawDistance() * root->drawDistance() ||
            !_activeCamera->isInFrustum(*root);

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

void SceneGraph::updateSounds() {
    std::vector<std::pair<SoundSceneNode *, float>> distances;
    glm::vec3 cameraPos(_activeCamera->localTransform()[3]);

    // For each sound, calculate its distance to the camera
    for (auto &root : _soundRoots) {
        root->setAudible(false);
        if (!root->isEnabled()) {
            continue;
        }
        float dist2 = root->getSquareDistanceTo(cameraPos);
        float maxDist2 = root->maxDistance() * root->maxDistance();
        if (dist2 > maxDist2) {
            continue;
        }
        distances.push_back(std::make_pair(root.get(), dist2));
    }

    // Take up to N most closest sounds to the camera
    sort(distances.begin(), distances.end(), [](auto &left, auto &right) {
        int leftPriority = left.first->priority();
        int rightPriority = right.first->priority();
        if (leftPriority < rightPriority) {
            return true;
        }
        if (leftPriority > rightPriority) {
            return false;
        }
        return left.second < right.second;
    });
    if (distances.size() > kMaxSoundCount) {
        distances.erase(distances.begin() + kMaxSoundCount, distances.end());
    }

    // Mark closest sounds as audible
    for (auto &pair : distances) {
        pair.first->setAudible(true);
    }
}

void SceneGraph::refresh() {
    _opaqueMeshes.clear();
    _transparentMeshes.clear();
    _shadowMeshes.clear();
    _lights.clear();
    _emitters.clear();

    for (auto &root : _modelRoots) {
        refreshFromNode(*root);
    }
}

void SceneGraph::refreshFromNode(SceneNode &node) {
    bool propagate = true;

    switch (node.type()) {
    case SceneNodeType::Model: {
        // Ignore models that have been culled
        auto &model = static_cast<ModelSceneNode &>(node);
        if (model.isCulled()) {
            propagate = false;
        }
        break;
    }
    case SceneNodeType::Mesh: {
        // For model nodes, determine whether they should be rendered and cast shadows
        auto &modelNode = static_cast<MeshSceneNode &>(node);
        if (modelNode.shouldRender()) {
            // Sort model nodes into transparent and opaque
            if (modelNode.isTransparent()) {
                _transparentMeshes.push_back(&modelNode);
            } else {
                _opaqueMeshes.push_back(&modelNode);
            }
        }
        if (modelNode.shouldCastShadows()) {
            _shadowMeshes.push_back(&modelNode);
        }
        break;
    }
    case SceneNodeType::Light:
        _lights.push_back(static_cast<LightSceneNode *>(&node));
        break;
    case SceneNodeType::Emitter:
        _emitters.push_back(static_cast<EmitterSceneNode *>(&node));
        break;
    default:
        break;
    }

    if (propagate) {
        for (auto &child : node.children()) {
            refreshFromNode(*child);
        }
    }
}

void SceneGraph::prepareOpaqueLeafs() {
    _opaqueLeafs.clear();

    std::vector<SceneNode *> bucket;
    auto camera = _activeCamera->camera();

    // Group grass clusters into buckets without sorting
    for (auto &grass : _grassRoots) {
        if (!grass->isEnabled()) {
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

void SceneGraph::prepareTransparentLeafs() {
    _transparentLeafs.clear();

    auto camera = _activeCamera->camera();

    // Add meshes and emitters to transparent leafs
    std::vector<SceneNode *> leafs;
    for (auto &mesh : _transparentMeshes) {
        leafs.push_back(mesh);
    }
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
            }
            if (isFogEnabled()) {
                globals.fogNear = fogNear();
                globals.fogFar = fogFar();
                globals.fogColor = glm::vec4(fogColor(), 1.0f);
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
        pipeline.inRenderPass(RenderPassName::TransparentGeometry, [this](auto &pass) {
            renderTransparent(pass);
        });
        pipeline.inRenderPass(RenderPassName::PostProcessing, [this, &camera](auto &pass) {
            if (!_flareLights.empty()) {
                renderLensFlares(pass);
            }
        });
    }

    return pipeline.render();
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

bool SceneGraph::testElevation(const glm::vec2 &position, Collision &outCollision) const {
    static glm::vec3 down(0.0f, 0.0f, -1.0f);

    bool walkable = false;
    float minDistance = std::numeric_limits<float>::max();

    glm::vec3 origin {position, kElevationTestZ};
    for (auto &root : _walkmeshRoots) {
        if (!root->isEnabled()) {
            continue;
        }
        if (!root->walkmesh().isAreaWalkmesh()) {
            float distance2 = root->getSquareDistanceTo2D(position);
            if (distance2 > kMaxCollisionDistanceWalk2) {
                continue;
            }
        }
        auto objSpaceOrigin = glm::vec3(root->absoluteTransformInverse() * glm::vec4(origin, 1.0f));
        float distance = 0.0f;
        auto face = root->walkmesh().raycast(_walkcheckSurfaces, objSpaceOrigin, down, 2.0f * kElevationTestZ, distance);
        if (!face || distance >= minDistance) {
            continue;
        }
        walkable = _walkableSurfaces.count(face->material) > 0;
        if (walkable) {
            outCollision.user = root->user();
            outCollision.intersection = origin + distance * down;
            outCollision.normal = root->absoluteTransform() * glm::vec4 {face->normal, 0.0f};
            outCollision.material = face->material;
        }
        minDistance = distance;
    }

    return walkable;
}

bool SceneGraph::testLineOfSight(const glm::vec3 &origin, const glm::vec3 &dest, Collision &outCollision) const {
    auto originToDest = dest - origin;
    auto dir = glm::normalize(originToDest);
    float maxDistance = glm::length(originToDest);
    float minDistance = std::numeric_limits<float>::max();

    for (auto &root : _walkmeshRoots) {
        if (!root->isEnabled()) {
            continue;
        }
        glm::vec3 originLocal;
        glm::vec3 dirLocal;
        if (root->walkmesh().isAreaWalkmesh()) {
            if (!root->walkmesh().contains(origin) &&
                !root->walkmesh().contains(dest)) {
                continue;
            }
            originLocal = origin;
            dirLocal = dir;
        } else {
            if (root->getSquareDistanceTo(origin) > kMaxCollisionDistanceLineOfSight2) {
                continue;
            }
            originLocal = root->absoluteTransformInverse() * glm::vec4 {origin, 1.0f};
            dirLocal = root->absoluteTransformInverse() * glm::vec4 {dir, 0.0f};
        }
        float distance = 0.0f;
        auto face = root->walkmesh().raycast(_lineOfSightSurfaces, originLocal, dirLocal, maxDistance, distance);
        if (!face || distance > minDistance) {
            continue;
        }
        outCollision.user = root->user();
        outCollision.intersection = origin + distance * dir;
        outCollision.normal = root->absoluteTransform() * glm::vec4(face->normal, 0.0f);
        outCollision.material = face->material;
        minDistance = distance;
    }

    return minDistance != std::numeric_limits<float>::max();
}

bool SceneGraph::testWalk(const glm::vec3 &origin, const glm::vec3 &dest, const IUser *excludeUser, Collision &outCollision) const {
    glm::vec3 originToDest(dest - origin);
    glm::vec3 dir(glm::normalize(originToDest));
    float maxDistance = glm::length(originToDest);
    float minDistance = std::numeric_limits<float>::max();

    for (auto &root : _walkmeshRoots) {
        if (!root->isEnabled() || root->user() == excludeUser) {
            continue;
        }
        if (!root->walkmesh().isAreaWalkmesh()) {
            float distance2 = root->getSquareDistanceTo(origin);
            if (distance2 > kMaxCollisionDistanceWalk2) {
                continue;
            }
        }
        glm::vec3 objSpaceOrigin(root->absoluteTransformInverse() * glm::vec4(origin, 1.0f));
        glm::vec3 objSpaceDir(root->absoluteTransformInverse() * glm::vec4(dir, 0.0f));
        float distance = 0.0f;
        auto face = root->walkmesh().raycast(_walkcheckSurfaces, objSpaceOrigin, objSpaceDir, kMaxCollisionDistanceWalk, distance);
        if (!face || distance > maxDistance || distance > minDistance) {
            continue;
        }
        outCollision.user = root->user();
        outCollision.intersection = origin + distance * dir;
        outCollision.normal = root->absoluteTransform() * glm::vec4(face->normal, 0.0f);
        outCollision.material = face->material;
        minDistance = distance;
    }

    return minDistance != std::numeric_limits<float>::max();
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
