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

/** Wraps a body and its head and hand models in a bumped-out shell of \p texture. */
void setShell(scene::ModelSceneNode &body, std::shared_ptr<graphics::Texture> texture);

/** Takes the bumped-out shell off a body and its head and hand models. */
void clearShell(scene::ModelSceneNode &body);

/**
 * A visualeffects.2da row shown on an object or at a point: its models,
 * sounds and programs. An application that is not kept lasts as long as what
 * it shows.
 */
class VisualEffect : public CopyableEffect<VisualEffect> {
public:
    VisualEffect(int visualEffectId, bool missEffect, ServicesView &services);
    VisualEffect(const VisualEffect &other);
    ~VisualEffect();

    EffectApplicationResult onApply(Object &object, EffectInstance &) override;
    EffectRemovalResult onRemove(Object &object, const EffectInstance &) override;
    void onUpdate(Object &object, const EffectInstance &, float dt) override;
    void retireAreaRuntime(
        const std::set<const Object *> &retainedObjects) override;
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

private:
    /**
     * A model attached to one of the target's nodes: it plays its impact
     * animation, then its duration animation while the application is kept,
     * or goes away when it is not.
     */
    struct NodeModel {
        std::shared_ptr<scene::ModelSceneNode> model;
        std::shared_ptr<scene::ModelSceneNode> owner;
        /** No hook: the model stands at a point in the world. */
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

    int _visualEffectId;
    bool _missEffect;
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
    /** The duration program that follows the impact program. */
    std::optional<int> _pendingDurationProgram;

    EffectApplicationResult present(Object &object, EffectInstance &instance);
    EffectApplicationResult present(Object &object, EffectInstance &instance, const VisualEffectDesc &desc);
    void attachNodeModels(Object &object, const std::shared_ptr<scene::ModelSceneNode> &body,
                          const VisualEffectDesc &desc, bool restoring);
    void startNodeModel(scene::ISceneGraph &graph, graphics::Model &model,
                        std::optional<std::reference_wrapper<scene::SceneNode>> hook, bool orientationOff,
                        std::shared_ptr<scene::ModelSceneNode> owner, const glm::vec3 &position, bool restoring);
    float nodeModelsImpactLength() const;
    void updateNodeModels(float dt);
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
};

} // namespace game

} // namespace reone
