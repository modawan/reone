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

#include "reone/scene/node/mesh.h"

#include "reone/graphics/context.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/lumautil.h"
#include "reone/graphics/material.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/textureutil.h"
#include "reone/graphics/uniforms.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/textures.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/camera.h"
#include "reone/scene/node/light.h"
#include "reone/scene/node/model.h"
#include "reone/scene/render/pipeline.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"

using namespace reone::graphics;

namespace reone {

namespace scene {

static constexpr float kUvAnimationSpeed = 250.0f;

void MeshSceneNode::init() {
    _point = false;
    _modelNode.floatValueAtTime(ControllerTypes::alpha, 0.0f, _alpha);
    _modelNode.vectorValueAtTime(ControllerTypes::selfIllumColor, 0.0f, _selfIllumColor);

    initTextures();
    initDanglyMesh();
}

void MeshSceneNode::initTextures() {
    std::shared_ptr<ModelNode::TriangleMesh> mesh(_modelNode.mesh());
    if (!mesh) {
        return;
    }
    if (!mesh->diffuseMap.empty()) {
        auto diffuseMap = _resourceSvc.textures.get(mesh->diffuseMap, TextureUsage::MainTex);
        _nodeTextures.diffuse = diffuseMap.get();
    }
    if (!mesh->lightmap.empty()) {
        auto lightmap = _resourceSvc.textures.get(mesh->lightmap, TextureUsage::Lightmap);
        _nodeTextures.lightmap = lightmap.get();
    }
    if (!mesh->bumpmap.empty()) {
        auto bumpmap = _resourceSvc.textures.get(mesh->bumpmap, TextureUsage::BumpMap);
        _nodeTextures.bumpmap = bumpmap.get();
    }
    refreshAdditionalTextures();
}

void MeshSceneNode::refreshAdditionalTextures() {
    _nodeTextures.bumpmap = nullptr;
    if (!_nodeTextures.diffuse) {
        return;
    }
    const Texture::Features &features = _nodeTextures.diffuse->features();
    if (!features.envmapTexture.empty()) {
        _nodeTextures.envmap = _resourceSvc.textures.get(features.envmapTexture, TextureUsage::EnvironmentMap).get();
    } else if (!features.bumpyShinyTexture.empty()) {
        _nodeTextures.envmap = _resourceSvc.textures.get(features.bumpyShinyTexture, TextureUsage::EnvironmentMap).get();
    }
    if (!features.bumpmapTexture.empty()) {
        _nodeTextures.bumpmap = _resourceSvc.textures.get(features.bumpmapTexture, TextureUsage::BumpMap).get();
    }
}

void MeshSceneNode::update(float dt) {
    SceneNode::update(dt);

    std::shared_ptr<ModelNode::TriangleMesh> mesh(_modelNode.mesh());
    if (mesh) {
        updateUVAnimation(dt, *mesh);
        updateBumpmapAnimation(dt, *mesh);
        if (mesh->danglymesh) {
            updateDanglyAnimation(dt, *mesh->danglymesh);
        }
        if (mesh->saber) {
            updateSaberAnimation(dt);
        }
    }
}

void MeshSceneNode::updateUVAnimation(float dt, const ModelNode::TriangleMesh &mesh) {
    if (mesh.uvAnimation.dir.x != 0.0f || mesh.uvAnimation.dir.y != 0.0f) {
        _uvOffset += kUvAnimationSpeed * mesh.uvAnimation.dir * dt;
        _uvOffset -= glm::floor(_uvOffset);
    }
}

void MeshSceneNode::updateBumpmapAnimation(float dt, const ModelNode::TriangleMesh &mesh) {
    if (!_nodeTextures.bumpmap) {
        return;
    }
    const Texture::Features &features = _nodeTextures.bumpmap->features();
    if (features.procedureType == Texture::ProcedureType::Cycle) {
        int frameCount = features.numX * features.numY;
        float length = frameCount / static_cast<float>(features.fps);
        _bumpmapCycleTime = glm::min(_bumpmapCycleTime + dt, length);
        _bumpmapCycleFrame = static_cast<int>(glm::round((frameCount - 1) * (_bumpmapCycleTime / length)));
        if (_bumpmapCycleTime == length) {
            _bumpmapCycleTime = 0.0f;
        }
    }
}

static std::string vectorToString(const glm::vec3 &vec) {
    return str(boost::format("[%.04f, %.04f, %.04f]") % vec.x % vec.y % vec.z);
}

void MeshSceneNode::updateDanglyAnimation(float dt, const ModelNode::Danglymesh &mesh) {
    if (dt < 0.0125f) {
        dt = 0.0125f;
    } else if (dt > 0.035f) {
        dt = 0.035f;
    }
    glm::vec3 worldPos = _absTransform[3];
    glm::vec3 deltaPos {worldPos - _dangly.prevWorldPos};
    glm::vec3 objSpaceDeltaPos = _absTransformInv * glm::vec4 {worldPos - _dangly.prevWorldPos, 0.0f};

    _windTime = glm::mod(_windTime + dt, glm::two_pi<float>());
    auto wind = 0.01f * glm::vec3 {glm::abs(glm::sin(_windTime)), 0.0f, 0.0f};
    glm::vec3 objSpaceWind = _absTransformInv * glm::vec4 {wind, 0.0f};

    float deltaPosMag = glm::length(deltaPos);
    if (deltaPosMag <= 5.0f) {
        for (size_t i = 0; i < _dangly.vertices.size(); ++i) {
            if (mesh.constraints[i] == 0.0f) {
                continue;
            }
            auto &vertex = _dangly.vertices[i];
            auto displacement = vertex.displacement - objSpaceDeltaPos;

            glm::vec3 acceleration {0.0f};
            acceleration += -displacement * (0.5f * mesh.tightness * mesh.constraints[i]); // spring force
            acceleration += -vertex.velocity * (1.5f * mesh.period);                       // damp force
            vertex.velocity += acceleration * dt;
            displacement += vertex.velocity * dt;

            auto windScaled = 20.0f * mesh.displacement * (1.0f - mesh.constraints[i] / 255.0f) * objSpaceWind;
            displacement += windScaled;

            float dispmag = glm::length(displacement);
            if (dispmag > 0.0f) {
                float maxdisp = mesh.displacement * (1.0f - mesh.constraints[i] / 255.0f);
                vertex.displacement = glm::min(dispmag, maxdisp) * displacement / dispmag;
            } else {
                vertex.displacement = glm::vec3 {0.0f};
            }
        }
    }
    _dangly.prevWorldPos = std::move(worldPos);
}

void MeshSceneNode::updateSaberAnimation(float dt) {
    glm::vec3 worldPos = _absTransform[3];
    glm::vec3 deltaPos = worldPos - _saber.prevWorldPos;
    float deltaPosMag = glm::length(deltaPos);
    if (deltaPosMag > 1.0f) {
        _saber.displacement = glm::vec3 {0.0f};
    } else if (deltaPosMag > 0.0f) {
        glm::vec3 deltaLocal = _absTransformInv * glm::vec4 {deltaPos, 0.0f};
        _saber.displacement += deltaLocal;
    }
    _saber.displacement -= _saber.displacement * glm::min(8.0f * dt, 1.0f);
    _saber.prevWorldPos = worldPos;
}

bool MeshSceneNode::shouldRender() const {
    auto mesh = _modelNode.mesh();
    if (_projectedBeam) {
        return mesh && mesh->beaming && _projectedBeam->target;
    }
    if (!mesh || !mesh->render || _alpha * _model.fadeAlpha() == 0.0f) {
        return false;
    }
    // Renderability depends on the diffuse texture effectively bound to this node,
    // not on whether the model itself authored one. Creature bodies routinely defer
    // their skin to a runtime override applied through setMainTexture.
    return !_modelNode.isAABBMesh() && _nodeTextures.diffuse != nullptr;
}

bool MeshSceneNode::shouldCastShadows() const {
    if (_projectedBeam) {
        return false;
    }
    std::shared_ptr<ModelNode::TriangleMesh> mesh(_modelNode.mesh());
    if (!mesh) {
        return false;
    }
    // A model faded out completely casts no shadow.
    if (_model.fadeAlpha() == 0.0f) {
        return false;
    }
    if (_model.usage() == ModelUsage::Creature) {
        return mesh->shadow && !_modelNode.isSkinMesh();
    } else if (_model.usage() == ModelUsage::Placeable) {
        return mesh->render;
    } else {
        return false;
    }
}

bool MeshSceneNode::isTransparent() const {
    if (_projectedBeam) {
        return true;
    }
    // Under Force Sight, glowing and see-through models are blended over the
    // scene.
    if (_sceneGraph.isForceSightEnabled() && _model.forceSightStyle() != ModelSceneNode::ForceSightStyle::Grey) {
        return true;
    }
    if (!_nodeTextures.diffuse) {
        return false;
    }
    auto blending = _nodeTextures.diffuse->features().blending;
    switch (blending) {
    case Texture::Blending::Additive:
        return true;
    case Texture::Blending::PunchThrough:
        return false;
    default:
        break;
    }
    if (_alpha * _model.fadeAlpha() < 1.0f) {
        return true;
    }
    if (_nodeTextures.envmap || _nodeTextures.bumpmap) {
        return false;
    }
    if ((1.0f - rgbToLuma(_selfIllumColor)) < 0.01f) {
        return false;
    }
    return hasAlphaChannel(_nodeTextures.diffuse->pixelFormat());
}

static bool isLightingEnabledByUsage(ModelUsage usage) {
    return usage != ModelUsage::Projectile;
}

static bool isReceivingShadows(const ModelSceneNode &model, const MeshSceneNode &modelNode) {
    return model.usage() == ModelUsage::Room;
}

static constexpr float kProjectedBeamDepth = 20.0f;
static constexpr glm::vec4 kProjectedBeamColor {0.0f, 0.0f, 0.0f, 0.2f};
static constexpr uint16_t kNoAdjacentFace = 0xffff;

void MeshSceneNode::setProjectedBeamTarget(const SceneNode &target) {
    if (!_projectedBeam) {
        _projectedBeam = std::make_unique<ProjectedBeam>();
    }
    _projectedBeam->target = target;
}

void MeshSceneNode::clearProjectedBeamTarget() {
    _projectedBeam->target.reset();
}

// The beam is the shadow volume of the mesh's silhouette as seen from the
// target: each silhouette edge is extruded away from the target, closed by a
// fan from the node's origin at the near end and a point beyond the target at
// the far end.
void MeshSceneNode::renderProjectedBeam(IRenderPass &pass) {
    auto &beam = *_projectedBeam;
    const auto &sourceMesh = *_modelNode.mesh()->mesh;
    const auto &sourceFaces = sourceMesh.faces();
    bool deformed = _modelNode.isSkinMesh();
    glm::vec3 targetLocal {_absTransformInv * glm::vec4 {beam.target->get().origin(), 1.0f}};

    beam.sourceVertices.clear();
    if (deformed) {
        auto bones = buildSkinBoneTransforms();
        for (const auto &vertex : sourceMesh.vertices()) {
            glm::vec3 position {0.0f};
            for (int i = 0; i < 4; ++i) {
                int boneIndex = std::max(0, (*vertex.boneIndices)[i]);
                position += glm::vec3 {bones[boneIndex] * glm::vec4 {vertex.position, 1.0f}} * (*vertex.boneWeights)[i];
            }
            beam.sourceVertices.push_back(position);
        }
    } else {
        for (const auto &vertex : sourceMesh.vertices()) {
            beam.sourceVertices.push_back(vertex.position);
        }
    }
    const auto &sourceVertices = beam.sourceVertices;

    beam.faceSides.clear();
    for (const auto &face : sourceFaces) {
        glm::vec3 normal = face.normal;
        glm::vec3 centroid = face.centroid;
        if (deformed) {
            const glm::vec3 &a = sourceVertices[face.vertices[0]];
            const glm::vec3 &b = sourceVertices[face.vertices[1]];
            const glm::vec3 &c = sourceVertices[face.vertices[2]];
            normal = glm::normalize(glm::cross(b - a, c - a));
            centroid = (a + b + c) / 3.0f;
        }
        beam.faceSides.push_back(glm::dot(normal, targetLocal - centroid));
    }

    beam.edges.clear();
    beam.usedVertices.assign(sourceVertices.size(), false);
    for (size_t faceIndex = 0; faceIndex < sourceFaces.size(); ++faceIndex) {
        if (beam.faceSides[faceIndex] < 0.0f) {
            continue;
        }
        const auto &face = sourceFaces[faceIndex];
        for (size_t edgeIndex = 0; edgeIndex < face.vertices.size(); ++edgeIndex) {
            uint16_t adjacent = face.adjacentFaces[edgeIndex];
            if (adjacent != kNoAdjacentFace && beam.faceSides[faceIndex] * beam.faceSides[adjacent] > 0.0f) {
                continue;
            }
            uint16_t from = face.vertices[edgeIndex];
            uint16_t to = face.vertices[(edgeIndex + 1) % face.vertices.size()];
            beam.edges.emplace_back(from, to);
            beam.usedVertices[from] = true;
            beam.usedVertices[to] = true;
        }
    }

    beam.vertices.clear();
    beam.nearBySource.assign(sourceVertices.size(), 0);
    for (size_t i = 0; i < sourceVertices.size(); ++i) {
        if (!beam.usedVertices[i]) {
            continue;
        }
        glm::vec3 direction {glm::normalize(sourceVertices[i] - targetLocal)};
        beam.nearBySource[i] = static_cast<uint16_t>(beam.vertices.size());
        beam.vertices.push_back(Mesh::VertexBuilder().position(sourceVertices[i]).build());
        beam.vertices.push_back(Mesh::VertexBuilder().position(targetLocal + kProjectedBeamDepth * direction).build());
    }
    auto cap = static_cast<uint16_t>(beam.vertices.size());
    beam.vertices.push_back(Mesh::VertexBuilder().position(glm::vec3 {0.0f}).build());
    auto sink = static_cast<uint16_t>(beam.vertices.size());
    beam.vertices.push_back(Mesh::VertexBuilder().position(targetLocal - kProjectedBeamDepth * glm::normalize(targetLocal)).build());

    // Each edge is a strip of four triangles, consecutive edges wound in turn.
    beam.faces.clear();
    for (size_t edgeIndex = 0; edgeIndex < beam.edges.size(); ++edgeIndex) {
        const auto &[from, to] = beam.edges[edgeIndex];
        uint16_t nearFrom = beam.nearBySource[from];
        uint16_t nearTo = beam.nearBySource[to];
        auto farFrom = static_cast<uint16_t>(nearFrom + 1);
        auto farTo = static_cast<uint16_t>(nearTo + 1);
        std::array<uint16_t, 6> strip;
        if (edgeIndex % 2 == 0) {
            strip = {cap, nearFrom, nearTo, farFrom, farTo, sink};
        } else {
            strip = {sink, farTo, farFrom, nearTo, nearFrom, cap};
        }
        for (size_t i = 0; i < 4; ++i) {
            if (i % 2 == 0) {
                beam.faces.emplace_back(std::array<uint16_t, 3> {strip[i], strip[i + 1], strip[i + 2]});
            } else {
                beam.faces.emplace_back(std::array<uint16_t, 3> {strip[i + 1], strip[i], strip[i + 2]});
            }
        }
    }

    if (!beam.mesh) {
        Mesh::VertexLayout layout;
        layout.stride = 3 * sizeof(float);
        layout.offPosition = 0;
        beam.mesh = std::make_unique<Mesh>(beam.vertices, std::move(layout), beam.faces);
        beam.mesh->init();
    } else {
        beam.mesh->update(beam.vertices, beam.faces);
    }

    pass.drawProjectedBeam(*beam.mesh, _absTransform, _absTransformInv, kProjectedBeamColor);
}

void MeshSceneNode::render(IRenderPass &pass) {
    if (_projectedBeam) {
        renderProjectedBeam(pass);
        return;
    }

    auto mesh = _modelNode.mesh();
    if (!mesh || !_nodeTextures.diffuse) {
        return;
    }
    Material material;
    material.type = isTransparent()
                        ? MaterialType::TransparentModel
                        : MaterialType::OpaqueModel;
    material.textures.insert({TextureUnits::mainTex, *_nodeTextures.diffuse});
    if (_nodeTextures.lightmap) {
        material.textures.insert({TextureUnits::lightmap, *_nodeTextures.lightmap});
    }
    if (_nodeTextures.envmap) {
        if (_nodeTextures.envmap->isCubeMap()) {
            material.textures.insert({TextureUnits::envMapCube, *_nodeTextures.envmap});
        } else {
            material.textures.insert({TextureUnits::envMap, *_nodeTextures.envmap});
        }
    }
    if (_nodeTextures.bumpmap) {
        if (_nodeTextures.bumpmap->isGrayscale()) {
            material.textures.insert({TextureUnits::bumpMapArray, *_nodeTextures.bumpmap});
            material.bumpMapFrame = _bumpmapCycleFrame;
        } else {
            material.textures.insert({TextureUnits::normalMap, *_nodeTextures.bumpmap});
        }
    }
    material.uv = glm::mat3x4(
        glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec4(0.0f, 1.0f, 0.0f, 0.0f),
        glm::vec4(_uvOffset.x, _uvOffset.y, 0.0f, 0.0f));
    material.color = glm::vec4(1.0f, 1.0f, 1.0f, _alpha * _model.fadeAlpha());
    material.ambientColor = mesh->ambient;
    material.diffuseColor = mesh->diffuse;
    material.selfIllumColor = _selfIllumColor;
    if (const auto &hilite = _model.hiliteColor()) {
        material.diffuseColor *= *hilite;
        material.selfIllumColor += *hilite;
    }
    material.staticObject = _static;
    if (_sceneGraph.hasShadowLight() && isReceivingShadows(_model, *this)) {
        material.affectedByShadows = true;
    }
    if (_sceneGraph.isFogEnabled() && _model.model().isAffectedByFog()) {
        material.affectedByFog = true;
    }
    material.faceCulling = _nodeTextures.diffuse->features().decal ? FaceCullMode::None : FaceCullMode::Back;
    applyForceSight(material);
    drawWithMaterial(pass, material);
}

// Force Sight: a glowing model shows its glow colour, its alpha pulsing; a
// see-through model is drawn grey at three quarters.
static constexpr float kForceSightTranslucentAlpha = 0.75f;

void MeshSceneNode::applyForceSight(Material &material) const {
    if (!_sceneGraph.isForceSightEnabled()) {
        return;
    }
    switch (_model.forceSightStyle()) {
    case ModelSceneNode::ForceSightStyle::Glow: {
        const glm::vec4 &glow = _model.forceGlowColor();
        material.forceGlow = true;
        material.forceSightColor = glm::vec4(glm::vec3(glow), glow.a * _sceneGraph.forceSightPulse());
        break;
    }
    case ModelSceneNode::ForceSightStyle::Translucent:
        material.forceSightColor = glm::vec4(0.0f, 0.0f, 0.0f, kForceSightTranslucentAlpha);
        break;
    default:
        break;
    }
}

void MeshSceneNode::renderBumpedOutShell(IRenderPass &pass, Texture &texture, float offset) {
    Material material;
    material.type = MaterialType::TransparentModel;
    material.textures.insert({TextureUnits::mainTex, texture});
    material.color = glm::vec4(1.0f);
    material.ambientColor = glm::vec3(1.0f);
    material.diffuseColor = glm::vec3(1.0f);
    material.selfIllumColor = glm::vec3(1.0f);
    material.shellOffset = offset;
    material.faceCulling = texture.features().decal ? FaceCullMode::None : FaceCullMode::Back;
    applyForceSight(material);
    drawWithMaterial(pass, material);
}

void MeshSceneNode::drawWithMaterial(IRenderPass &pass, Material &material) {
    auto mesh = _modelNode.mesh();
    if (_modelNode.isSkinMesh()) {
        auto bones = buildSkinBoneTransforms();
        pass.drawSkinned(*mesh->mesh, material, _absTransform, _absTransformInv, std::move(bones));
    } else if (_modelNode.isDanglymesh()) {
        std::vector<glm::vec4> positions;
        positions.reserve(_dangly.vertices.size());
        for (const auto &vertex : _dangly.vertices) {
            positions.emplace_back(vertex.position + vertex.displacement, 1.0f);
        }
        pass.drawDangly(*mesh->mesh,
                        material,
                        _absTransform,
                        _absTransformInv,
                        positions);
    } else if (_modelNode.isSaberMesh()) {
        pass.drawSaber(*mesh->mesh,
                       material,
                       _absTransform,
                       _absTransformInv,
                       glm::vec4 {_saber.displacement, 0.0f});
    } else {
        pass.draw(*mesh->mesh, material, _absTransform, _absTransformInv);
    }
}

std::vector<glm::mat4> MeshSceneNode::buildSkinBoneTransforms() const {
    const auto &skin = *_modelNode.mesh()->skin;
    auto bones = std::vector<glm::mat4>(kMaxBones, glm::mat4(1.0f));
    for (size_t i = 0; i < kMaxBones; ++i) {
        if (i >= skin.boneNodeNumber.size()) {
            break;
        }
        uint16_t nodeNumber = skin.boneNodeNumber[i];
        if (nodeNumber == 0xffff) {
            continue;
        }
        auto bone = _model.getNodeByNumber(nodeNumber);
        if (!bone) {
            continue;
        }
        bones[i] = _modelNode.absoluteTransformInverse(); // convert bone transform in model space to bone transform in this model node space
        bones[i] *= _model.absoluteTransformInverse();    // convert bone transform in world space to bone transform in model space
        bones[i] *= bone->absoluteTransform();
        bones[i] *= skin.boneMatrices[skin.boneSerial[i]]; // extract changes to the bone transform in this model node space
    }
    return bones;
}

void MeshSceneNode::renderShadow(IRenderPass &pass) {
    std::shared_ptr<ModelNode::TriangleMesh> mesh(_modelNode.mesh());
    if (!mesh) {
        return;
    }
    Material material;
    material.type = _sceneGraph.isShadowLightDirectional()
                        ? MaterialType::DirLightShadow
                        : MaterialType::PointLightShadow;
    material.color = glm::vec4(1.0f, 1.0f, 1.0f, _alpha * _model.fadeAlpha());
    pass.draw(*mesh->mesh, material, _absTransform, _absTransformInv);
}

bool MeshSceneNode::isLightingEnabled() const {
    if (!isLightingEnabledByUsage(_model.usage())) {
        return false;
    }
    // Lighting is disabled when diffuse texture is additive
    if (_nodeTextures.diffuse && _nodeTextures.diffuse->features().blending == Texture::Blending::Additive) {
        return false;
    }
    return true;
}

void MeshSceneNode::setMainTexture(Texture *texture) {
    ModelNodeSceneNode::setMainTexture(texture);
    _nodeTextures.diffuse = texture;
    refreshAdditionalTextures();
}

void MeshSceneNode::setEnvironmentMap(Texture *texture) {
    ModelNodeSceneNode::setEnvironmentMap(texture);
    _nodeTextures.envmap = std::move(texture);
}

void MeshSceneNode::initDanglyMesh() {
    auto mesh = _modelNode.mesh();
    if (!mesh || !mesh->danglymesh) {
        return;
    }
    _dangly.vertices.reserve(mesh->danglymesh->positions.size());
    for (const auto &position : mesh->danglymesh->positions) {
        DanglyVertex vertex;
        vertex.position = position;
        _dangly.vertices.push_back(std::move(vertex));
    }
}

} // namespace scene

} // namespace reone
