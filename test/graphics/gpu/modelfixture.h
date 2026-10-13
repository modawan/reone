/* Copyright (c) 2026 The reone project contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "reone/graphics/model.h"
#include "reone/graphics/modelnode.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/model.h"
#include "../../fixtures/audio.h"
#include "../../fixtures/resource.h"
#include "../../fixtures/scene.h"

namespace shader_output {

using namespace reone;
using namespace reone::graphics;
using namespace reone::scene;

// Redistributable geometry and authored inputs. Names are deliberately generic:
// tests must reach classification through production model/material properties.
struct NodeInput {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Texture> texture;
    bool dedicatedGeometry {false};
    glm::mat4 transform {1.0f};
    float opacity {1.0f};
    glm::vec3 selfIllum {0.0f};
    glm::vec3 ambient {1.0f};
    glm::vec3 diffuse {1.0f};
    std::shared_ptr<Texture> lightmap;
};

class ModelFixture {
public:
    ModelFixture(GraphicsOptions &options, GraphicsServices &graphics,
                 const std::vector<NodeInput> &inputs, ModelUsage usage = ModelUsage::Equipment) {
        _audio.init();
        _resources.init();
        EXPECT_CALL(_resources.textures(), get(testing::_, testing::_))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](const std::string &name, TextureUsage) {
                return _textures.at(name);
            });
        _scene = std::make_unique<SceneGraph>("synthetic_materials", _pipelineFactory,
                                             options, graphics, _audio.services(), _resources.services());
        _root = std::make_shared<ModelNode>(0, "node0", glm::vec3(0.0f),
                                            glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
        for (size_t i = 0; i < inputs.size(); ++i) {
            const auto &input = inputs[i];
            std::string textureName = "texture" + std::to_string(i);
            _textures.emplace(textureName, input.texture);
            auto node = std::make_shared<ModelNode>(i + 1, "node" + std::to_string(i + 1),
                                                    glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, _root.get());
            auto mesh = std::make_shared<ModelNode::TriangleMesh>();
            mesh->mesh = input.mesh;
            mesh->render = true;
            mesh->diffuseMap = textureName;
            mesh->saber = input.dedicatedGeometry;
            mesh->ambient = input.ambient;
            mesh->diffuse = input.diffuse;
            if (input.lightmap) {
                mesh->lightmap = "lightmap" + std::to_string(i);
                _textures.emplace(mesh->lightmap, input.lightmap);
            }
            node->setMesh(std::move(mesh));
            node->floatTracks()[ControllerTypes::alpha].add(0.0f, input.opacity);
            node->vectorTracks()[ControllerTypes::selfIllumColor].add(0.0f, input.selfIllum);
            _root->addChild(node);
        }
        _model = std::make_unique<Model>("synthetic_model", 0, _root,
                                         std::vector<std::shared_ptr<Animation>>(), "", 1.0f);
        _model->setAffectedByFog(false);
        _model->init();
        _node = std::make_shared<ModelSceneNode>(*_model, usage, *_scene, graphics,
                                                _audio.services(), _resources.services());
        _node->init();
        for (size_t i = 0; i < inputs.size(); ++i) {
            auto *meshNode = static_cast<MeshSceneNode *>(_node->getNodeByNumber(i + 1));
            meshNode->setLocalTransform(inputs[i].transform);
            _meshNodes.push_back(meshNode);
        }
    }

    MeshSceneNode &node(size_t i) { return *_meshNodes.at(i); }

    void render(IRenderPass &pass, const std::vector<size_t> &indices) {
        std::vector<SceneNode *> leafs;
        for (size_t i : indices) {
            auto *mesh = _meshNodes.at(i);
            if (mesh->shouldRender()) {
                leafs.push_back(mesh);
            }
        }
        _node->renderLeafs(pass, leafs);
    }

private:
    audio::TestAudioModule _audio;
    resource::TestResourceModule _resources;
    MockRenderPipelineFactory _pipelineFactory;
    std::map<std::string, std::shared_ptr<Texture>> _textures;
    std::unique_ptr<SceneGraph> _scene;
    std::shared_ptr<ModelNode> _root;
    std::unique_ptr<Model> _model;
    std::shared_ptr<ModelSceneNode> _node;
    std::vector<MeshSceneNode *> _meshNodes;
};

struct DrawRecord {
    enum class Route { Ordinary, Saber, Skinned, Dangly } route;
    Mesh *mesh;
    MaterialType material;
    Texture *texture;
    glm::mat4 transform;
    glm::vec4 color;
};

// Pure observation: forwards the exact geometry, transforms and material to
// the actual Retro/PBR pass. No test-side feature or shading classification.
class ObservedPass : public IRenderPass {
public:
    explicit ObservedPass(IRenderPass &pass) : _pass(pass) {}
    std::vector<DrawRecord> records;

    void draw(Mesh &m, Material &a, const glm::mat4 &t, const glm::mat4 &i) override {
        record(DrawRecord::Route::Ordinary, m, a, t); _pass.draw(m, a, t, i);
    }
    void drawSaber(Mesh &m, Material &a, const glm::mat4 &t, const glm::mat4 &i, const glm::vec4 &d) override {
        record(DrawRecord::Route::Saber, m, a, t); _pass.drawSaber(m, a, t, i, d);
    }
    void drawSkinned(Mesh &m, Material &a, const glm::mat4 &t, const glm::mat4 &i, const std::vector<glm::mat4> &b) override {
        record(DrawRecord::Route::Skinned, m, a, t); _pass.drawSkinned(m, a, t, i, b);
    }
    void drawDangly(Mesh &m, Material &a, const glm::mat4 &t, const glm::mat4 &i, const std::vector<glm::vec4> &p) override {
        record(DrawRecord::Route::Dangly, m, a, t); _pass.drawDangly(m, a, t, i, p);
    }
    void drawBillboard(Texture &t, const glm::vec4 &c, const glm::mat4 &m, const glm::mat4 &i, std::optional<float> s) override { _pass.drawBillboard(t, c, m, i, s); }
    void drawParticles(Texture &t, FaceCullMode c, bool a, const glm::ivec2 &g, const std::vector<ParticleInstance> &p) override { _pass.drawParticles(t, c, a, g, p); }
    void drawGrass(float r, float s, Texture &t, std::optional<std::reference_wrapper<Texture>> &l, const std::vector<GrassInstance> &g) override { _pass.drawGrass(r, s, t, l, g); }
    void drawAABB(const std::vector<glm::vec4> &c) override { _pass.drawAABB(c); }
    void drawImage(Texture &t, const glm::ivec2 &p, const glm::ivec2 &s, glm::vec4 c, glm::mat3x4 u, ImageAlphaMode a) override { _pass.drawImage(t, p, s, c, u, a); }

private:
    IRenderPass &_pass;
    void record(DrawRecord::Route route, Mesh &mesh, Material &material, const glm::mat4 &transform) {
        auto texture = material.textures.find(TextureUnits::mainTex);
        records.push_back({route, &mesh, material.type,
                           texture == material.textures.end() ? nullptr : &texture->second.get(),
                           transform, material.color});
    }
};

class ObservedUniforms : public IUniforms {
public:
    explicit ObservedUniforms(IUniforms &uniforms) : _uniforms(uniforms) {}
    std::vector<int> features;
    void setLocals(const std::function<void(LocalUniforms &)> &block) override {
        _uniforms.setLocals([&](LocalUniforms &locals) { block(locals); features.push_back(locals.featureMask); });
    }
    void setGlobals(const std::function<void(GlobalUniforms &)> &b) override { _uniforms.setGlobals(b); }
    void setBones(const std::function<void(BoneUniforms &)> &b) override { _uniforms.setBones(b); }
    void setDangly(const std::function<void(DanglyUniforms &)> &b) override { _uniforms.setDangly(b); }
    void setParticles(const std::function<void(ParticleUniforms &)> &b) override { _uniforms.setParticles(b); }
    void setGrass(const std::function<void(GrassUniforms &)> &b) override { _uniforms.setGrass(b); }
    void setWalkmesh(const std::function<void(WalkmeshUniforms &)> &b) override { _uniforms.setWalkmesh(b); }
    void setText(const std::function<void(TextUniforms &)> &b) override { _uniforms.setText(b); }
    void setScreenEffect(const std::function<void(ScreenEffectUniforms &)> &b) override { _uniforms.setScreenEffect(b); }
private:
    IUniforms &_uniforms;
};

class ObservedShaders : public IShaderRegistry {
public:
    explicit ObservedShaders(IShaderRegistry &shaders) : _shaders(shaders) {}
    std::vector<std::string> requested;
    ShaderProgram &get(const std::string &name) override { requested.push_back(name); return _shaders.get(name); }
private:
    IShaderRegistry &_shaders;
};

} // namespace shader_output
