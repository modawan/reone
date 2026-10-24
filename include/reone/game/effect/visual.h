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

#include <functional>
#include <optional>

#include "../effect.h"

namespace reone {

namespace scene {
class ISceneGraph;
class ModelSceneNode;
class MeshSceneNode;
class SceneNode;
} // namespace scene

namespace audio {
class AudioSource;
}

namespace graphics {
class Model;
class Texture;
} // namespace graphics

namespace game {

class Area;
class Camera;
class Game;
struct ServicesView;
struct VisualEffectDesc;

/**
 * Where a visual's models attach on an object. Creatures name their nodes; a
 * placeable prefixes its with its model name after the first four characters,
 * a door with its whole model name. The sites are also the slots the models
 * take, in this order.
 */
enum class VisualSite {
    Root,
    Impact,
    Head
};

/** Wraps a body, its head model and its worn head item in a bumped-out shell of \p texture. */
void setShell(scene::ModelSceneNode &body, std::shared_ptr<graphics::Texture> texture);

/** Takes the bumped-out shell off a body, its head model and its worn head item. */
void clearShell(scene::ModelSceneNode &body);

/**
 * A visualeffects.2da row shown on an object or at a point: its models,
 * sounds and programs. An application that is not kept lasts as long as what
 * it shows. The row is integer 0 and the miss flag integer 2.
 */
class VisualEffect : public CopyableEffect<VisualEffect> {
public:
    VisualEffect(int visualEffectId, bool missEffect, ServicesView &services);
    VisualEffect(const VisualEffect &other);
    ~VisualEffect();

    EffectApplicationResult onApply(Object &object, EffectInstance &) override;
    void onRemove(Object &object, const EffectInstance &instance) override;
    void onUpdate(Object &object, const EffectInstance &, float dt) override;
    void retireAreaRuntime(
        const std::set<uint32_t> &retainedObjectIds) override;
    /** A visual sent to an area carries the point it plays at in floats 0 to 2. */
    void setLocation(glm::vec3 loc) {
        for (size_t i = 0; i < 3; ++i) {
            setSaveFacingFloat(i, loc[static_cast<glm::length_t>(i)]);
        }
    }
    /**
     * How long the visual shows at a point in the world: its impact model's
     * impact animation or its impact sound, whichever is longer. A row the
     * table does not have shows nothing.
     */
    float duration() const;
    /**
     * Shells share one slot on a body: the latest shell live on the holder
     * shows, whether a visual effect's or the creature's own, and with none
     * the slot is empty.
     */
    static void showLatestShell(Object &holder);
    /**
     * A rebuilt body takes back what the visual shows on it: its models come
     * off the old body and go back to the same nodes of the new one, carrying
     * on where they were. A model whose node the new body lacks is not shown.
     * detachFromBody takes them off the old body before it is rebuilt.
     */
    void detachFromBody();
    /** Puts the models taken off by detachFromBody on \p body, the rebuilt body of \p object. */
    void reattachToBody(Object &object, scene::ModelSceneNode &body);

private:
    /**
     * A model attached to one of the target's nodes: it plays its impact
     * animation, then its duration animation while the application is kept,
     * and its cessation animation, where it has one, when a kept application
     * ends. When it is not kept, or once it has ended, the models stay a
     * while after the first impact or cessation ends and then go together.
     */
    struct NodeModel {
        std::shared_ptr<scene::ModelSceneNode> model;
        /** The body the model attaches to; none when it stands at a point in the world. */
        std::shared_ptr<scene::ModelSceneNode> owner;
        /** The site the model attaches to on its owner. */
        VisualSite site {VisualSite::Root};
        /** The node the model hangs from; none at a point in the world, or when the body lacks the site's node. */
        scene::SceneNode *hook {nullptr};
        bool followsPositionOnly {false};
        float impactRemaining {0.0f};
    };

    /** The nodes a beam runs between, and the models that hold them. */
    struct BeamEnds {
        std::shared_ptr<scene::ModelSceneNode> sourceModel;
        scene::SceneNode *sourceNode {nullptr};
        std::shared_ptr<scene::ModelSceneNode> targetModel;
        scene::SceneNode *targetNode {nullptr};

        bool complete() const { return sourceNode && targetNode; }
    };

    std::optional<glm::vec3> _location;
    ServicesView &_services;

    std::shared_ptr<scene::ModelSceneNode> _node;
    std::vector<NodeModel> _nodeModels;
    /** A missed beam ends at a point: a stand-in model there, and the node of it the beam reaches. */
    std::shared_ptr<scene::ModelSceneNode> _beamEnd;
    std::string _beamEndNode;
    std::shared_ptr<scene::ModelSceneNode> _attached;
    std::shared_ptr<scene::ModelSceneNode> _attachedOwner;
    scene::SceneNode *_hookNode {nullptr};
    std::optional<int> _attachmentProgram;
    std::weak_ptr<Area> _releaseArea;
    std::weak_ptr<Camera> _viewAngleCamera;
    std::optional<uint32_t> _viewAngleHold;
    bool _viewAngleNarrowed {false};
    Game *_motionBlurGame {nullptr};
    bool _motionBlurProgram {false};
    bool _motionBlurApplied {false};
    std::weak_ptr<scene::ModelSceneNode> _shellOwner;
    std::shared_ptr<graphics::Texture> _shellTexture;
    std::shared_ptr<audio::AudioSource> _durationSound;
    std::vector<scene::MeshSceneNode *> _beamMeshes;
    std::shared_ptr<scene::ModelSceneNode> _beamSourceOwner;
    std::shared_ptr<scene::ModelSceneNode> _beamTargetOwner;
    bool _beam {false};
    /** Whether the application is kept on the object, rather than shown once. */
    bool _kept {false};
    /** Time left in the impact program. */
    float _impactRemaining {0.0f};
    /**
     * Time left before the node models go, for an application that is not
     * kept or a kept one that has ended in its cessation.
     */
    std::optional<float> _nodeModelsUnloadRemaining;
    /** The kept application has ended and its node models play their cessation. */
    bool _ceasing {false};
    /** Which of the application's programs runs. */
    enum class ProgramPhase {
        None,
        Impact,
        Duration,
        Cessation
    };
    ProgramPhase _programPhase {ProgramPhase::None};
    /** The program a kept application runs as it ends, if its row has one. */
    std::optional<int> _cessationProgram;
    /** Time left in the cessation program; none while it runs until the presentation ends. */
    std::optional<float> _cessationRemaining;
    /** The duration program that follows the impact program. */
    std::optional<int> _pendingDurationProgram;

    EffectApplicationResult present(Object &object, EffectInstance &instance);
    EffectApplicationResult present(Object &object, EffectInstance &instance, const VisualEffectDesc &desc);
    void attachNodeModels(Object &object, const std::shared_ptr<scene::ModelSceneNode> &body,
                          const VisualEffectDesc &desc, bool restoring);
    void startNodeModel(scene::ISceneGraph &graph, graphics::Model &model,
                        std::optional<std::reference_wrapper<scene::SceneNode>> hook, VisualSite site, bool orientationOff,
                        std::shared_ptr<scene::ModelSceneNode> owner, const glm::vec3 &position, bool restoring);
    /**
     * How long the node models of an application that is not kept show: until
     * the first impact ends, and the unload hold after it.
     */
    float nodeModelsPresentationLength() const;
    void updateNodeModels(float dt);
    /** Takes every node model off its node or the scene. */
    void removeNodeModels();
    /**
     * Plays the cessation animation of every node model that has one.
     *
     * @return how long the models then stay, from the first cessation and the
     *         unload hold after it; none when no model has a cessation
     */
    std::optional<float> startNodeModelCessation();
    BeamEnds resolveBeamEnds(Object &object, const EffectInstance &instance);
    bool startProgram(int program, Object &object, const EffectInstance &instance);
    /** Ending a shell shows the latest shell left on \p holder, if given. */
    void stopProgram(std::optional<std::reference_wrapper<Object>> holder = std::nullopt);
    bool startViewAngleProgram(Object &object);
    void updateViewAngleProgram(Object &object);
    void stopViewAngleProgram();
    void startMotionBlurProgram(Object &object);
    void updateMotionBlurProgram(Object &object);
    float impactProgramLength(int program) const;
    void clearPresentation(std::optional<std::reference_wrapper<Object>> holder = std::nullopt);
    /** Ends everything the application shows but its node models: sounds, beams and programs. */
    void endPresentationButNodeModels(std::optional<std::reference_wrapper<Object>> holder);
    /** Stops the duration sound and takes the beam and its end off the scene. */
    void endSoundsAndBeams();
};

} // namespace game

} // namespace reone
