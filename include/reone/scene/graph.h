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

/**
 * Which faces of a room walkmesh a vertical probe through a point meets. Each
 * probe gives the face the point stands on (see
 * graphics::Walkmesh::standingFace): the first it meets going down, unless
 * the room lays floors over floors and that is a floor over the floor at the
 * point's level.
 */
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

/** What stands in a shown room (see ISceneGraph::setRoomTest). */
struct RoomTest {
    /** Whether a top-level model stands in a shown room. */
    std::function<bool(const ModelSceneNode &)> model;
    /** Whether a point, where an object's light stands, is in a shown room. */
    std::function<bool(const glm::vec3 &)> point;
};

/** The face of a room walkmesh that a vertical probe meets first going down. */
struct RoomFace {
    /** The face's surface material. */
    uint32_t material {0};
    /** The height of the face's plane on the vertical, 0 for an upright face. */
    float height {0.0f};
    /** The face's normal. */
    glm::vec3 normal {0.0f};
};

/** How a level walk through the rooms ends (see ISceneGraph::walkRooms). */
enum class RoomWalkEnd {
    /** It reaches its end in a room. */
    Dest,
    /** It starts where no room lies. */
    OffRooms,
    /** It crosses a perimeter edge leading into no room. */
    Edge,
    /** It is about to enter one room more than it may. */
    RoomLimit
};

/** The stretch of a level walk over one room (see ISceneGraph::walkRooms). */
struct RoomStretch {
    std::shared_ptr<WalkmeshSceneNode> room;
    /** How far along the walk it enters the room, from 0 at its start to 1 at its end. */
    float from {0.0f};
    /**
     * The heights it spans over the room's walkable faces, from where it
     * enters the room on toward the walk's end (see
     * graphics::Walkmesh::walkFaces); empty when no face lies where it enters,
     * or the walk cannot be followed across the faces, and when the heights
     * were not asked for.
     */
    std::optional<graphics::Walkmesh::HeightBand> band;
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
    /** Turns off the Force Sight look of the video effect, keeping its other parts. */
    virtual void clearForceSight() = 0;
    /** The Force Sight glow's pulse, between 0.5 and 1. */
    virtual float forceSightPulse() const = 0;
    /** Seconds the scene has been shown for, a pause included: texture procedures run on it. */
    virtual float textureTime() const = 0;

    virtual bool testLineOfSight(const glm::vec3 &origin, const glm::vec3 &dest, Collision &outCollision) const = 0;
    /**
     * Where the line first meets a face of a room model's AABB mesh of a
     * camera surface, as testCameraLine has them, or the posed model of a
     * creature, placeable or door, or a model attached to one, that takes
     * scene hits (see ModelSceneNode::takesSceneHits). Of an object whose
     * user is in \p excluded only the attached models count, except the one
     * attached at \p excludedHook.
     */
    virtual bool testSceneHit(const glm::vec3 &origin, const glm::vec3 &dest, const std::vector<const IUser *> &excluded, const std::string &excludedHook, Collision &outCollision) const = 0;
    virtual bool testWalk(const glm::vec3 &origin, const glm::vec3 &dest, const IUser *excludeUser, Collision &outCollision) const = 0;
    /**
     * Where the line first meets what stops a camera: a face of a room
     * model's AABB mesh, in any room, shown or not, of a camera surface
     * (setCameraSurfaces), walkable floor included; or a triangle, as it is
     * posed, of the model of a door or placeable that a camera does not pass
     * through, that takes scene hits (see ModelSceneNode::takesSceneHits) and
     * whose classification does not let it. The normal faces back along the
     * line.
     */
    virtual bool testCameraLine(const glm::vec3 &origin, const glm::vec3 &dest, Collision &outCollision) const = 0;
    /**
     * The first room walkmesh, in layout order, with a walk-check face on the
     * vertical through a point, within a thousand units above and below it.
     * Where the area lays floors over floors (see
     * hasStackedFloors), the first whose walk-check face that the point
     * stands on (see graphics::Walkmesh::standingFace) lies within
     * graphics::Walkmesh::kFloorHeadroom of its height comes before it. A
     * vertical that misses a room is tried on it once more, shifted 0.001
     * along the diagonal of x and y. Empty when no room lies there.
     */
    virtual std::shared_ptr<WalkmeshSceneNode> findRoomWalkmesh(const glm::vec3 &position) const = 0;
    /**
     * The face of a room walkmesh a point stands on, going down the vertical
     * through it (see RoomFaceProbe); a vertical that meets none is tried once
     * more, shifted 0.001 along the diagonal of x and y. Empty when it meets
     * no face.
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
    /**
     * Whether the area lays one walkable floor over another more than
     * graphics::Walkmesh::kFloorHeadroom apart, within a room or between two
     * (see graphics::Walkmesh::hasFloorOverFloor and
     * graphics::Walkmesh::hasFloorOver). Worked out from the room
     * walkmeshes and walkable surfaces when first asked, and again after
     * either is set.
     */
    virtual bool hasStackedFloors() const = 0;
    /**
     * How a level walk from origin to dest passes through the rooms. It
     * starts in the room found under origin and passes from room to room
     * across the perimeter edges that lead into another: in a room that lays
     * floors over floors, the edge where the walk leaves the faces it crosses
     * from where it enters (see graphics::Walkmesh::walkFaces), when the walk
     * can be followed across them, and the height of the floor there;
     * otherwise the first edge the level line crosses. The stretches are the
     * rooms it passes over, in order, each with the heights the walk spans
     * there when they are asked for. The collision, when the walk does not
     * reach dest, names the room it ends in, none when it starts off the
     * rooms, the point it ends at, and the level normal there: the crossed
     * edge's for an edge leading into no room, otherwise back along the walk.
     *
     * @param maxRooms how many rooms the walk may pass over, when limited
     * @param withBands whether each stretch is given the heights the walk
     *        spans there
     */
    virtual RoomWalkEnd walkRooms(const glm::vec3 &origin, const glm::vec3 &dest, std::optional<int> maxRooms, bool withBands,
                                  std::vector<RoomStretch> &outStretches, Collision &outCollision) const = 0;

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
    /** The surfaces whose faces of a room model's AABB mesh stop a camera (see testCameraLine). */
    virtual void setCameraSurfaces(std::set<uint32_t> surfaces) = 0;
    /**
     * The walkmeshes of the area's rooms, by their place in the layout; empty
     * for a room loaded without one. Walks through the rooms follow them.
     */
    virtual void setRoomWalkmeshes(std::vector<std::shared_ptr<WalkmeshSceneNode>> rooms) = 0;
    /**
     * The test a top-level model other than a room's must pass to be drawn
     * and picked: whether it stands in a shown room. A light of such a model
     * shines when it stands in a shown room itself, and a room's lights
     * shine while the room is shown, whether the model is drawn or not. The
     * graph only watches the test, which its owner keeps; once the owner lets
     * it go, or after clear, every model and light passes.
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
    virtual std::shared_ptr<ModelSceneNode> newModel(std::shared_ptr<graphics::Model> model, ModelUsage usage) = 0;
    virtual std::shared_ptr<WalkmeshSceneNode> newWalkmesh(std::shared_ptr<graphics::Walkmesh> walkmesh) = 0;
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
    void clearForceSight() override {
        if (_frameEffects.videoEffect) {
            _frameEffects.videoEffect->forceSight = false;
        }
    }
    float forceSightPulse() const override;
    float textureTime() const override { return _textureTime; }

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
    bool testSceneHit(const glm::vec3 &origin, const glm::vec3 &dest, const std::vector<const IUser *> &excluded, const std::string &excludedHook, Collision &outCollision) const override;
    bool testWalk(const glm::vec3 &origin, const glm::vec3 &dest, const IUser *excludeUser, Collision &outCollision) const override;
    bool testCameraLine(const glm::vec3 &origin, const glm::vec3 &dest, Collision &outCollision) const override;
    void setCameraSurfaces(std::set<uint32_t> surfaces) override { _cameraSurfaces = std::move(surfaces); }
    std::shared_ptr<WalkmeshSceneNode> findRoomWalkmesh(const glm::vec3 &position) const override;
    std::optional<RoomFace> findRoomFace(const WalkmeshSceneNode &room, const glm::vec3 &position, RoomFaceProbe probe) const override;
    bool testRoomWalk(const glm::vec3 &origin, const glm::vec3 &dest, float radius, float height,
                      std::optional<int> maxRooms, std::vector<float> &outCrossings, Collision &outCollision) const override;
    bool hasStackedFloors() const override { return floorStacking().area; }
    RoomWalkEnd walkRooms(const glm::vec3 &origin, const glm::vec3 &dest, std::optional<int> maxRooms, bool withBands,
                          std::vector<RoomStretch> &outStretches, Collision &outCollision) const override;

    ModelSceneNode *pickModelAt(int x, int y, IUser *except = nullptr) const override;
    std::optional<std::reference_wrapper<ModelSceneNode>> pickModelRay(const glm::vec3 &origin, const glm::vec3 &dir) const override;

    void setWalkableSurfaces(std::set<uint32_t> surfaces) override;
    void setWalkcheckSurfaces(std::set<uint32_t> surfaces) override;
    void setLineOfSightSurfaces(std::set<uint32_t> surfaces) override { _lineOfSightSurfaces = std::move(surfaces); }
    void setRoomWalkmeshes(std::vector<std::shared_ptr<WalkmeshSceneNode>> rooms) override;
    void setRoomTest(std::weak_ptr<const RoomTest> test) override { _roomTest = std::move(test); }

    // END Collision detection and object picking

    // Factory methods

    std::shared_ptr<CameraSceneNode> newCamera() override;
    std::shared_ptr<ModelSceneNode> newModel(std::shared_ptr<graphics::Model> model, ModelUsage usage) override;
    std::shared_ptr<WalkmeshSceneNode> newWalkmesh(std::shared_ptr<graphics::Walkmesh> walkmesh) override;
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
    /**
     * Where the line, of some length, meets a face of a room model's AABB
     * mesh of a camera surface nearer than \p nearest, which it brings in,
     * every room whether shown or not.
     */
    void testRoomModels(const glm::vec3 &origin, const glm::vec3 &dest, float &nearest, Collision &outCollision) const;

    std::string _name;
    IRenderPipelineFactory &_renderPipelineFactory;
    graphics::GraphicsOptions &_graphicsOpt;
    graphics::GraphicsServices &_graphicsSvc;
    audio::AudioServices &_audioSvc;
    resource::ResourceServices &_resourceSvc;

    std::unique_ptr<IRenderPipeline> _renderPipeline;
    FrameEffects _frameEffects;
    float _forceSightPhase {0.0f}; /**< seconds into the current Force Sight pulse */
    float _textureTime {0.0f};

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

    /** Which floors lie over others (see hasStackedFloors). */
    struct FloorStacking {
        /** Whether any do, within a room or between two. */
        bool area {false};
        /** The room walkmeshes that lay floors over their own. */
        std::set<const WalkmeshSceneNode *> rooms;
    };
    /** Worked out when first asked for; reset when the rooms or walkable surfaces are set. */
    mutable std::optional<FloorStacking> _floorStacking;

    // END Roots

    // Leafs

    std::vector<MeshSceneNode *> _opaqueMeshes;
    std::vector<MeshSceneNode *> _transparentMeshes;
    /** The transparent meshes that write their depth before they are blended. */
    std::vector<MeshSceneNode *> _blendedDepthMeshes;
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
    std::set<uint32_t> _cameraSurfaces;
    /** The walk-check surfaces that are not walkable. */
    std::set<uint32_t> _blockingSurfaces;

    // END Surfaces

    void cullRoots();
    /** Keeps the blocking surfaces in step with the walkable and walk-check ones. */
    void updateBlockingSurfaces();
    const FloorStacking &floorStacking() const;
    /** Whether a room walkmesh lays floors over its own. */
    bool isStacked(const WalkmeshSceneNode &room) const { return floorStacking().rooms.count(&room) > 0; }

    void refresh();
    /**
     * Collects the meshes, lights and emitters under a node. Under a model
     * that is culled but rendered, only its lights are collected. A light is
     * collected when \p roomTest, if any, passes at its position.
     */
    void refreshFromNode(SceneNode &node, const RoomTest *roomTest, bool lightsOnly);

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
