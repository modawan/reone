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

#include "reone/graphics/mesh.h"

#include "modelnode.h"

namespace reone {

namespace graphics {
struct Material;
}

namespace scene {

class ModelSceneNode;

class MeshSceneNode : public ModelNodeSceneNode {
public:
    MeshSceneNode(
        ModelSceneNode &model,
        graphics::ModelNode &modelNode,
        ISceneGraph &sceneGraph,
        graphics::GraphicsServices &graphicsSvc,
        audio::AudioServices &audioSvc,
        resource::ResourceServices &resourceSvc) :
        ModelNodeSceneNode(
            modelNode,
            SceneNodeType::Mesh,
            sceneGraph,
            graphicsSvc,
            audioSvc,
            resourceSvc),
        _model(model) {
    }

    void init();

    void update(float dt) override;

    void render(IRenderPass &pass);
    /**
     * Draws the mesh pushed out along its normals by \p offset, in \p texture,
     * over itself.
     */
    void renderBumpedOutShell(IRenderPass &pass, graphics::Texture &texture, float offset);
    void renderShadow(IRenderPass &pass);

    bool shouldRender() const;
    bool shouldCastShadows() const;

    bool isTransparent() const;

    ModelSceneNode &model() { return _model; }
    const ModelSceneNode &model() const { return _model; }

    void setMainTexture(graphics::Texture *texture) override;
    void setEnvironmentMap(graphics::Texture *texture) override;
    void setAlpha(float alpha) { _alpha = alpha; }
    void setSelfIllumColor(glm::vec3 color) { _selfIllumColor = std::move(color); }
    /**
     * Draws this mesh as a beam: a translucent shadow its silhouette casts
     * away from \p target, in place of the mesh itself.
     */
    void setProjectedBeamTarget(const SceneNode &target);
    /** Leaves the beam without a target; it is not drawn until it has one. */
    void clearProjectedBeamTarget();

private:
    /** A beam's target and the geometry it is rebuilt in each frame. */
    struct ProjectedBeam {
        std::optional<std::reference_wrapper<const SceneNode>> target;
        std::unique_ptr<graphics::Mesh> mesh;
        std::vector<glm::vec3> sourceVertices;
        std::vector<float> faceSides;
        std::vector<std::pair<uint16_t, uint16_t>> edges;
        std::vector<bool> usedVertices;
        std::vector<uint16_t> nearBySource;
        std::vector<graphics::Mesh::Vertex> vertices;
        std::vector<graphics::Mesh::Face> faces;
    };

    struct NodeTextures {
        graphics::Texture *diffuse {nullptr};
        graphics::Texture *lightmap {nullptr};
        graphics::Texture *envmap {nullptr};
        graphics::Texture *bumpmap {nullptr};
    } _nodeTextures;

    struct DanglyVertex {
        glm::vec3 position {0.0f};
        glm::vec3 displacement {0.0f};
        glm::vec3 velocity {0.0f};
    };

    struct DanglyMesh {
        std::vector<DanglyVertex> vertices;
        glm::vec3 prevWorldPos {0.0f};
    } _dangly;

    struct SaberVertex {
        glm::vec3 position {0.0f};
        glm::vec3 displacement {0.0f};
    };

    struct SaberMesh {
        glm::vec3 displacement {0.0f};
        glm::vec3 prevWorldPos {0.0f};
    } _saber;

    ModelSceneNode &_model;

    glm::vec2 _uvOffset {0.0f};
    float _bumpmapCycleTime {0.0f};
    int _bumpmapCycleFrame {0};
    float _alpha {1.0f};
    glm::vec3 _selfIllumColor {0.0f};

    float _windTime {0.0f};

    std::unique_ptr<ProjectedBeam> _projectedBeam;

    void initTextures();
    void initDanglyMesh();

    void refreshAdditionalTextures();

    bool isLightingEnabled() const;
    /** The skinning transforms of the bones, in this node's space. */
    std::vector<glm::mat4> buildSkinBoneTransforms() const;
    /** Draws the mesh, skinned or dangling as it is, with \p material. */
    void drawWithMaterial(IRenderPass &pass, graphics::Material &material);
    /** Sets the Force Sight look of the model on \p material. */
    void applyForceSight(graphics::Material &material) const;
    /** Rebuilds the beam geometry for where the target is now and draws it. */
    void renderProjectedBeam(IRenderPass &pass);

    // Animation

    void updateUVAnimation(float dt, const graphics::ModelNode::TriangleMesh &mesh);
    void updateBumpmapAnimation(float dt, const graphics::ModelNode::TriangleMesh &mesh);
    void updateDanglyAnimation(float dt, const graphics::ModelNode::Danglymesh &mesh);
    void updateSaberAnimation(float dt);

    // END Animation
};

} // namespace scene

} // namespace reone
