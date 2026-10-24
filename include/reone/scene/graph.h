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

#include "reone/scene/render/pipeline.h"

#include "fogproperties.h"
#include "node/camera.h"
#include "node/dummy.h"
#include "node/emitter.h"
#include "node/grass.h"
#include "node/light.h"
#include "node/mesh.h"
#include "node/model.h"
#include "node/sound.h"
#include "node/trigger.h"
#include "node/walkmesh.h"
#include "user.h"

namespace reone {

namespace graphics {

struct GraphicsOptions;
struct GraphicsServices;

class Walkmesh;

} // namespace graphics

namespace audio {

struct AudioServices;

}

namespace resource {

struct ResourceServices;

}

namespace scene {

struct Collision;

/** Which faces of a room walkmesh a vertical probe through a point meets. */
enum class RoomFaceProbe {
    /**
     * Walk-check faces, from a thousand units above the point to a thousand
     * below it: the faces by which the room under a point is found.
     */
    Locating,
    /**
     * Walk-check faces, from a thousand units above the walkmesh's origin to
     * a thousand below it, whatever the point's height.
     */
    WalkCheck,
    /**
     * Faces of every material, from a thousand units above the walkmesh's
     * origin to a thousand below it, whatever the point's height.
     */
    Any
};

class ModelSceneNode;

/** Whether a top-level model stands in a shown room (see ISceneGraph::setRoomTest). */
using RoomTest = std::function<bool(const ModelSceneNode &)>;

/** The face of a room walkmesh that a vertical probe meets first going down. */
struct RoomFace {
    /** The face's surface material. */
    uint32_t material {0};
    /** The height of the face's plane on the vertical, 0 for an upright face. */
    float height {0.0f};
    /** The face's normal. */
    glm::vec3 normal {0.0f};
};

class IAnimationEventListener;
class IRenderPass;
class IRenderPipelineFactory;

class ISceneGraph {
public:
    virtual ~ISceneGraph() = default;

    virtual void update(float dt) = 0;
    virtual graphics::Texture &render(const glm::ivec2 &dim) = 0;

    virtual void clear() = 0;

    /** Turns on or off the speed blur, which blends each frame with earlier ones. */
    virtual void setSpeedBlur(bool enabled) = 0;
    /** Sets the weight of the earlier frames in the speed blur. */
    virtual void setSpeedBlurRatio(float ratio) = 0;
    /** Whether the speed blur is on. */
    virtual bool isSpeedBlurEnabled() const = 0;
    /** The video effect laid over the rendered frame, if any. */
    virtual void setVideoEffect(std::optional<VideoEffect> effect) = 0;
    /** Whether the video effect turns the scene to Force Sight. */
    virtual bool isForceSightEnabled() const = 0;
    /** The Force Sight glow's pulse, between 0.5 and 1. */
    virtual float forceSightPulse() const = 0;

    virtual bool testLineOfSight(const glm::vec3 &origin, const glm::vec3 &dest, Collision &outCollision) const = 0;
    virtual bool testWalk(const glm::vec3 &origin, const glm::vec3 &dest, const IUser *excludeUser, Collision &outCollision) const = 0;
    /**
     * The first room walkmesh, in layout order, with a walk-check face on the
     * vertical through a point, within a thousand units above and below it; a
     * vertical that misses a room is tried on it once more, shifted 0.001
     * along the diagonal of x and y. Empty when no room lies there.
     */
    virtual std::shared_ptr<WalkmeshSceneNode> findRoomWalkmesh(const glm::vec3 &position) const = 0;
    /**
     * The face of a room walkmesh met first going down the vertical through a
     * point; a vertical that meets none is tried once more, shifted 0.001
     * along the diagonal of x and y. Empty when it meets no face.
     *
     * @param probe which faces are met, and over which heights
     */
    virtual std::optional<RoomFace> findRoomFace(const WalkmeshSceneNode &room, const glm::vec3 &position, RoomFaceProbe probe) const = 0;
    /**
     * Whether a level walk from origin to dest runs out of the rooms. It starts
     * in the room found under origin and passes from room to room across the
     * perimeter edges that lead into another. It runs out at origin when no
     * room lies there; past maxRooms rooms, where it enters the next; and
     * where it crosses a perimeter edge leading into no room, unless a
     * blocking face lies within radius of that point, between the heights the
     * walk spans in the room less a tenth and those plus height and a tenth.
     * The collision names the room the walk leaves, none when it starts off
     * the rooms, the point it leaves them at, and the level normal there,
     * facing back along the walk. The crossings are the fractions of the walk,
     * from 0 at origin to 1 at dest, at which it passes from one room into the
     * next, in order.
     */
    virtual bool testRoomWalk(const glm::vec3 &origin, const glm::vec3 &dest, float radius, float height,
                              std::optional<int> maxRooms, std::vector<float> &outCrossings, Collision &outCollision) const = 0;

    virtual ModelSceneNode *pickModelAt(int x, int y, IUser *except = nullptr) const = 0;
    virtual std::optional<std::reference_wrapper<ModelSceneNode>> pickModelRay(const glm::vec3 &origin, const glm::vec3 &dir) const = 0;

    virtual const std::string &name() const = 0;
    virtual std::optional<std::reference_wrapper<CameraSceneNode>> camera() = 0;

    virtual void setAmbientLightColor(glm::vec3 color) = 0;
    virtual bool hasShadowLight() const = 0;
    virtual bool isShadowLightDirectional() const = 0;

    virtual bool isFogEnabled() const = 0;
    virtual void setFog(FogProperties fog) = 0;

    virtual void setWalkableSurfaces(std::set<uint32_t> surfaces) = 0;
    virtual void setWalkcheckSurfaces(std::set<uint32_t> surfaces) = 0;
    virtual void setLineOfSightSurfaces(std::set<uint32_t> surfaces) = 0;
    /**
     * The walkmeshes of the area's rooms, by their place in the layout; empty
     * for a room loaded without one. Walks through the rooms follow them.
     */
    virtual void setRoomWalkmeshes(std::vector<std::shared_ptr<WalkmeshSceneNode>> rooms) = 0;
    /**
     * The test a top-level model other than a room's must pass to be drawn
     * and picked: whether it stands in a shown room. The graph only watches
     * the test, which its owner keeps; once the owner lets it go, or after
     * clear, every model passes.
     */
    virtual void setRoomTest(std::weak_ptr<const RoomTest> test) = 0;

    virtual void setActiveCamera(CameraSceneNode *camera) = 0;
    virtual void setUpdateRoots(bool update) = 0;

    virtual void setRenderAABB(bool render) = 0;
    virtual void setRenderWalkmeshes(bool render) = 0;
    virtual void setRenderTriggers(bool render) = 0;

    // Roots

    virtual void addRoot(std::shared_ptr<ModelSceneNode> node) = 0;
    virtual void addRoot(std::shared_ptr<WalkmeshSceneNode> node) = 0;
    virtual void addRoot(std::shared_ptr<TriggerSceneNode> node) = 0;
    virtual void addRoot(std::shared_ptr<GrassSceneNode> node) = 0;
    virtual void addRoot(std::shared_ptr<SoundSceneNode> node) = 0;

    virtual void removeRoot(ModelSceneNode &node) = 0;
    virtual void removeRoot(WalkmeshSceneNode &node) = 0;
    virtual void removeRoot(TriggerSceneNode &node) = 0;
    virtual void removeRoot(GrassSceneNode &node) = 0;
    virtual void removeRoot(SoundSceneNode &node) = 0;

    // END Roots

    // Factory methods

    virtual std::shared_ptr<CameraSceneNode> newCamera() = 0;
    virtual std::shared_ptr<ModelSceneNode> newModel(graphics::Model &model, ModelUsage usage) = 0;
    virtual std::shared_ptr<WalkmeshSceneNode> newWalkmesh(graphics::Walkmesh &walkmesh) = 0;
    virtual std::shared_ptr<TriggerSceneNode> newTrigger(std::vector<glm::vec3> geometry) = 0;
    virtual std::shared_ptr<SoundSceneNode> newSound() = 0;
    virtual std::shared_ptr<DummySceneNode> newDummy(graphics::ModelNode &modelNode) = 0;
    virtual std::shared_ptr<MeshSceneNode> newMesh(ModelSceneNode &model, graphics::ModelNode &modelNode) = 0;
    virtual std::shared_ptr<LightSceneNode> newLight(ModelSceneNode &model, graphics::ModelNode &modelNode) = 0;
    virtual std::shared_ptr<EmitterSceneNode> newEmitter(graphics::ModelNode &modelNode) = 0;
    virtual std::shared_ptr<ParticleSceneNode> newParticle(EmitterSceneNode &emitter) = 0;
    virtual std::shared_ptr<GrassSceneNode> newGrass(GrassProperties properties, graphics::ModelNode &aabbNode) = 0;
    virtual std::shared_ptr<GrassClusterSceneNode> newGrassCluster(GrassSceneNode &grass) = 0;

    // END Factory methods
};

class SceneGraph : public ISceneGraph, boost::noncopyable {
public:
    SceneGraph(
        std::string name,
        IRenderPipelineFactory &renderPipelineFactory,
        graphics::GraphicsOptions &graphicsOpt,
        graphics::GraphicsServices &graphicsSvc,
        audio::AudioServices &audioSvc,
        resource::ResourceServices &resourceSvc) :
        _name(std::move(name)),
        _renderPipelineFactory(renderPipelineFactory),
        _graphicsOpt(graphicsOpt),
        _graphicsSvc(graphicsSvc),
        _audioSvc(audioSvc),
        _resourceSvc(resourceSvc) {
    }

    void update(float dt) override;
    graphics::Texture &render(const glm::ivec2 &dim) override;

    void renderShadows(IRenderPass &pass);
    void renderOpaque(IRenderPass &pass);
    void renderTransparent(IRenderPass &pass);
    void renderLensFlares(IRenderPass &pass);

    const std::string &name() const override {
        return _name;
    }

    std::optional<std::reference_wrapper<CameraSceneNode>> camera() override {
        if (!_activeCamera) {
            return std::nullopt;
        }
        return *_activeCamera;
    }

    void setActiveCamera(CameraSceneNode *camera) override { _activeCamera = camera; }
    void setUpdateRoots(bool update) override { _updateRoots = update; }

    void setRenderAABB(bool render) override { _renderAABB = render; }
    void setRenderWalkmeshes(bool render) override { _renderWalkmeshes = render; }
    void setRenderTriggers(bool render) override { _renderTriggers = render; }

    // Roots

    void clear() override;

    void setSpeedBlur(bool enabled) override { _frameEffects.speedBlur = enabled; }
    void setSpeedBlurRatio(float ratio) override { _frameEffects.speedBlurRatio = ratio; }
    bool isSpeedBlurEnabled() const override { return _frameEffects.speedBlur; }
    void setVideoEffect(std::optional<VideoEffect> effect) override { _frameEffects.videoEffect = std::move(effect); }
    bool isForceSightEnabled() const override { return _frameEffects.videoEffect && _frameEffects.videoEffect->forceSight; }
    float forceSightPulse() const override;

    void addRoot(std::shared_ptr<ModelSceneNode> node) override;
    void addRoot(std::shared_ptr<WalkmeshSceneNode> node) override;
    void addRoot(std::shared_ptr<TriggerSceneNode> node) override;
    void addRoot(std::shared_ptr<GrassSceneNode> node) override;
    void addRoot(std::shared_ptr<SoundSceneNode> node) override;

    void removeRoot(ModelSceneNode &node) override;
    void removeRoot(WalkmeshSceneNode &node) override;
    void removeRoot(TriggerSceneNode &node) override;
    void removeRoot(GrassSceneNode &node) override;
    void removeRoot(SoundSceneNode &node) override;

    // END Roots

    // Lighting

    const glm::vec3 &ambientLightColor() const { return _ambientLightColor; }

    void setAmbientLightColor(glm::vec3 color) override { _ambientLightColor = std::move(color); }

    // END Lighting

    // Fog

    bool isFogEnabled() const override {
        return _fog.enabled;
    }

    float fogNear() const {
        return _fog.nearPlane;
    }

    float fogFar() const {
        return _fog.farPlane;
    }

    const glm::vec3 &fogColor() const {
        return _fog.color;
    }

    void setFog(FogProperties fog) override {
        _fog = std::move(fog);
    }

    // END Fog

    // Shadows

    // Off by option means the frame has no shadow light at all, so the pass and
    // the uniforms both fall away together rather than a pass rendering into a
    // term nothing applies. Selection still runs, so turning shadows off does
    // not otherwise perturb the scene. Out of line because this header only
    // forward-declares GraphicsOptions.
    bool hasShadowLight() const override;
    bool isShadowLightDirectional() const override { return _shadowLight->isDirectional(); }

    glm::vec3 shadowLightPosition() const { return _shadowLight->origin(); }
    float shadowStrength() const { return _shadowStrength; }
    float shadowRadius() const { return _shadowLight->radius(); }

    // END Shadows

    // Collision detection and object picking

    bool testLineOfSight(const glm::vec3 &origin, const glm::vec3 &dest, Collision &outCollision) const override;
    bool testWalk(const glm::vec3 &origin, const glm::vec3 &dest, const IUser *excludeUser, Collision &outCollision) const override;
    std::shared_ptr<WalkmeshSceneNode> findRoomWalkmesh(const glm::vec3 &position) const override;
    std::optional<RoomFace> findRoomFace(const WalkmeshSceneNode &room, const glm::vec3 &position, RoomFaceProbe probe) const override;
    bool testRoomWalk(const glm::vec3 &origin, const glm::vec3 &dest, float radius, float height,
                      std::optional<int> maxRooms, std::vector<float> &outCrossings, Collision &outCollision) const override;

    ModelSceneNode *pickModelAt(int x, int y, IUser *except = nullptr) const override;
    std::optional<std::reference_wrapper<ModelSceneNode>> pickModelRay(const glm::vec3 &origin, const glm::vec3 &dir) const override;

    void setWalkableSurfaces(std::set<uint32_t> surfaces) override;
    void setWalkcheckSurfaces(std::set<uint32_t> surfaces) override;
    void setLineOfSightSurfaces(std::set<uint32_t> surfaces) override { _lineOfSightSurfaces = std::move(surfaces); }
    void setRoomWalkmeshes(std::vector<std::shared_ptr<WalkmeshSceneNode>> rooms) override { _roomWalkmeshes = std::move(rooms); }
    void setRoomTest(std::weak_ptr<const RoomTest> test) override { _roomTest = std::move(test); }

    // END Collision detection and object picking

    // Factory methods

    std::shared_ptr<CameraSceneNode> newCamera() override;
    std::shared_ptr<ModelSceneNode> newModel(graphics::Model &model, ModelUsage usage) override;
    std::shared_ptr<WalkmeshSceneNode> newWalkmesh(graphics::Walkmesh &walkmesh) override;
    std::shared_ptr<TriggerSceneNode> newTrigger(std::vector<glm::vec3> geometry) override;
    std::shared_ptr<SoundSceneNode> newSound() override;

    std::shared_ptr<DummySceneNode> newDummy(graphics::ModelNode &modelNode) override;
    std::shared_ptr<MeshSceneNode> newMesh(ModelSceneNode &model, graphics::ModelNode &modelNode) override;
    std::shared_ptr<LightSceneNode> newLight(ModelSceneNode &model, graphics::ModelNode &modelNode) override;

    std::shared_ptr<EmitterSceneNode> newEmitter(graphics::ModelNode &modelNode) override;
    std::shared_ptr<ParticleSceneNode> newParticle(EmitterSceneNode &emitter) override;

    std::shared_ptr<GrassSceneNode> newGrass(GrassProperties properties, graphics::ModelNode &aabbNode) override;
    std::shared_ptr<GrassClusterSceneNode> newGrassCluster(GrassSceneNode &grass) override;

    // END Factory methods

private:
    std::string _name;
    IRenderPipelineFactory &_renderPipelineFactory;
    graphics::GraphicsOptions &_graphicsOpt;
    graphics::GraphicsServices &_graphicsSvc;
    audio::AudioServices &_audioSvc;
    resource::ResourceServices &_resourceSvc;

    std::unique_ptr<IRenderPipeline> _renderPipeline;
    FrameEffects _frameEffects;
    float _forceSightPhase {0.0f}; /**< seconds into the current Force Sight pulse */

    bool _updateRoots {true};

    bool _renderAABB {false};
    bool _renderWalkmeshes {false};
    bool _renderTriggers {false};

    std::set<std::shared_ptr<SceneNode>> _nodes;

    CameraSceneNode *_activeCamera {nullptr};
    std::vector<LightSceneNode *> _flareLights;

    // Roots

    std::list<std::shared_ptr<ModelSceneNode>> _modelRoots;
    std::list<std::shared_ptr<WalkmeshSceneNode>> _walkmeshRoots;
    std::list<std::shared_ptr<TriggerSceneNode>> _triggerRoots;
    std::list<std::shared_ptr<GrassSceneNode>> _grassRoots;
    std::list<std::shared_ptr<SoundSceneNode>> _soundRoots;
    /** The rooms' walkmeshes by their place in the layout; empty for a room without one. */
    std::vector<std::shared_ptr<WalkmeshSceneNode>> _roomWalkmeshes;
    /** See setRoomTest. */
    std::weak_ptr<const RoomTest> _roomTest;

    // END Roots

    // Leafs

    std::vector<MeshSceneNode *> _opaqueMeshes;
    std::vector<MeshSceneNode *> _transparentMeshes;
    std::vector<MeshSceneNode *> _shellMeshes;
    std::vector<MeshSceneNode *> _shadowMeshes;
    std::vector<LightSceneNode *> _lights;
    std::vector<EmitterSceneNode *> _emitters;

    std::vector<std::pair<SceneNode *, std::vector<SceneNode *>>> _opaqueLeafs;
    std::vector<std::pair<SceneNode *, std::vector<SceneNode *>>> _transparentLeafs;

    // END Leafs

    // Lighting

    glm::vec3 _ambientLightColor {0.5f};

    std::vector<LightSceneNode *> _activeLights;

    // END Lighting

    // Shadows

    bool _shadowActive {false};
    float _shadowStrength {0.0f};

    LightSceneNode *_shadowLight {nullptr};

    glm::mat4 _shadowLightSpace[graphics::kNumShadowLightSpace] {glm::mat4(1.0f)};
    glm::vec4 _shadowCascadeFarPlanes {glm::vec4(0.0f)};

    // END Shadows

    // Fog

    FogProperties _fog;

    // END Fog

    // Surfaces

    std::set<uint32_t> _walkableSurfaces;
    std::set<uint32_t> _walkcheckSurfaces;
    std::set<uint32_t> _lineOfSightSurfaces;
    /** The walk-check surfaces that are not walkable. */
    std::set<uint32_t> _blockingSurfaces;

    // END Surfaces

    void cullRoots();
    /** Keeps the blocking surfaces in step with the walkable and walk-check ones. */
    void updateBlockingSurfaces();

    void refresh();
    void refreshFromNode(SceneNode &node);

    void updateLighting();
    void updateShadowLight(float dt);
    void updateFlareLights();
    void updateSounds();

    void prepareOpaqueLeafs();
    void prepareTransparentLeafs();

    void computeLightSpaceMatrices();

    std::vector<LightSceneNode *> computeClosestLights(int count, const std::function<bool(const LightSceneNode &, float)> &pred) const;

    template <class T, class... Params>
    std::shared_ptr<T> newSceneNode(Params... params) {
        auto node = std::make_shared<T>(params..., *this, _graphicsSvc, _audioSvc, _resourceSvc);
        _nodes.insert(node);
        return node;
    }
};

} // namespace scene

} // namespace reone
