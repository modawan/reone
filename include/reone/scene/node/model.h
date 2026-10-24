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

#include <optional>

#include "reone/graphics/animation.h"
#include "reone/graphics/lipanimation.h"
#include "reone/graphics/model.h"
#include "reone/graphics/types.h"

#include "../animeventlistener.h"
#include "../animproperties.h"
#include "../node.h"
#include "../types.h"

#include "dummy.h"
#include "emitter.h"
#include "light.h"
#include "mesh.h"

namespace reone {

namespace graphics {

class Textures;
class Uniforms;

} // namespace graphics

namespace scene {

class ModelSceneNode : public SceneNode {
public:
    enum class AnimationBlendMode {
        Single,
        Blend,
        Overlay
    };

    /**
     * How the model looks under Force Sight: grey, grey and see-through, or
     * glowing in its glow colour.
     */
    enum class ForceSightStyle {
        Grey,
        Translucent,
        Glow
    };

    struct AnimationStateFlags {
        static constexpr int transform = 1;
        static constexpr int alpha = 2;
        static constexpr int selfIllumColor = 4;
        static constexpr int color = 8;
        static constexpr int birthrate = 16;
    };

    struct AnimationState {
        int flags {0};
        glm::mat4 transform {1.0f};
        float alpha {0.0f};
        glm::vec3 selfIllumColor {0.0f};
        glm::vec3 color {0.0f};
        float birthrate {0.0f};
    };

    struct AnimationChannel {
        graphics::Animation *anim;
        std::shared_ptr<graphics::LipAnimation> lipAnim;
        AnimationProperties properties;
        float time {0.0f};
        std::unordered_map<uint16_t, AnimationState> stateByNodeNumber;
        bool freeze {false};     /**< channel time is not to be updated */
        bool transition {false}; /**< when computing states, use animation transition time as channel time */
        bool finished {false};   /**< finished channels will be erased from the queue */
        float weight {1.0f};     /**< how far a layer pulls the nodes it animates toward its own state */

        AnimationChannel(graphics::Animation &anim, std::shared_ptr<graphics::LipAnimation> lipAnim, AnimationProperties properties) :
            anim(&anim),
            lipAnim(std::move(lipAnim)),
            properties(std::move(properties)) {
            // A channel played backwards starts at its end.
            if (this->properties.speed < 0.0f) {
                time = this->lipAnim ? this->lipAnim->length() : anim.length();
            }
        }
    };

    ModelSceneNode(
        graphics::Model &model,
        ModelUsage usage,
        ISceneGraph &sceneGraph,
        graphics::GraphicsServices &graphicsSvc,
        audio::AudioServices &audioSvc,
        resource::ResourceServices &resourceSvc) :
        SceneNode(
            SceneNodeType::Model,
            sceneGraph,
            graphicsSvc,
            audioSvc,
            resourceSvc),
        _model(&model),
        _usage(usage) {
    }

    void init();

    void update(float dt) override;

    void renderLeafs(IRenderPass &pass, const std::vector<SceneNode *> &leafs) override;
    void renderAABB(IRenderPass &pass);

    void computeAABB();
    void signalEvent(const std::string &name);

    /**
     * Prewarm every continuous emitter in this model, so a scene that is shown
     * rather than entered does not have to build its effects up from nothing
     * in front of the viewer.
     *
     * Opt-in: nothing calls this on an ordinary model.
     */
    void prewarmEmitters();

    bool isPickable() const { return _pickable; }

    ModelNodeSceneNode *getNodeByNumber(uint16_t number);
    ModelNodeSceneNode *getNodeByName(const std::string &name);

    const graphics::Model &model() const { return *_model; }
    ModelUsage usage() const { return _usage; }
    float drawDistance() const { return _drawDistance; }

    void setModel(graphics::Model &model);
    void setDrawDistance(float distance) { _drawDistance = distance; }
    void setMainTexture(graphics::Texture *texture);
    void setEnvironmentMap(graphics::Texture *texture);
    /**
     * Draws a copy of every mesh pushed out along its normals by \p offset,
     * in \p texture, over the model.
     */
    void setBumpedOutShell(std::shared_ptr<graphics::Texture> texture, float offset);
    /** Stops drawing the shell. */
    void clearBumpedOutShell();
    bool hasBumpedOutShell() const { return static_cast<bool>(_bumpedOutShellTexture); }
    const std::shared_ptr<graphics::Texture> &bumpedOutShellTexture() const { return _bumpedOutShellTexture; }
    float bumpedOutShellOffset() const { return _bumpedOutShellOffset; }
    void setPickable(bool pickable) { _pickable = pickable; }
    void setAnimationEventListener(IAnimationEventListener &listener) { _animEventListener = &listener; }
    /** Stops sending animation events to the listener. */
    void clearAnimationEventListener() { _animEventListener = nullptr; }

    // Animation

    void playAnimation(const std::string &name, std::shared_ptr<graphics::LipAnimation> lipAnim = nullptr, AnimationProperties properties = AnimationProperties());
    void playAnimation(graphics::Animation &anim, std::shared_ptr<graphics::LipAnimation> lipAnim = nullptr, AnimationProperties properties = AnimationProperties());
    bool restartAnimation(const std::string &name);

    void pauseAnimation();
    void resumeAnimation();
    void setAnimationTime(float time);
    /**
     * Change the speed of the channels under the layers that play the named
     * animation, here and on the attachments, without starting them over.
     */
    void setAnimationSpeed(const std::string &name, float speed);

    /**
     * Stop the channel playing the named animation, leaving every other channel
     * alone.
     *
     * Overlay animations are additive by design: playing one pushes a channel
     * and nothing ever pops it. That is fine for a fixed set of layers, but a
     * model whose overlay state changes over time - a HUD that swaps one pose
     * for another while unrelated layers keep running - would otherwise grow a
     * channel per change. Removing the outgoing animation by name keeps the set
     * bounded without disturbing the layers that should persist.
     *
     * Safe and idempotent: removing an animation that is not playing is a no-op.
     *
     * @return true if a channel was removed
     */
    bool removeAnimation(const std::string &name);

    /**
     * Let the named layer blend out from where it is, as a finished layer
     * does, leaving every other channel alone. The layer is erased once it
     * has blended out; played again before then, it starts over and blends
     * back in.
     *
     * @return true if a layer was found
     */
    bool fadeOutLayer(const std::string &name);

    /**
     * Stop every layer of this model, leaving the animations underneath and
     * the attachments' layers alone.
     */
    void removeLayers();

    bool isAnimationPlaying(const std::string &name) const;

    size_t animationChannelCount() const { return _animChannels.size(); }

    bool isAnimationFinished() const;

    std::string activeAnimationName() const;

    const std::deque<AnimationChannel> &animationChannels() const {
        return _animChannels;
    }

    /** The front channel under any layers, if there is one. */
    std::optional<std::reference_wrapper<const AnimationChannel>> baseAnimationChannel() const;

    // END Animation

    // Attachments

    void attach(const std::string &parentName, SceneNode &node);

    SceneNode *getAttachment(const std::string &parentName);

    /** A whole-object fade applied over every mesh alpha, attachments included. */
    float fadeAlpha() const { return _fadeAlpha; }
    /** Sets the whole-object fade of this model and its attached models. */
    void setFadeAlpha(float alpha);

    // END Attachments

    // Head look-at

    /**
     * Turn the named bone toward a world point, within the horizontal and
     * vertical arcs (degrees), closing a quarter-second of the gap each frame.
     * False when this model has no such bone.
     */
    bool beginLookAt(const std::string &bone, float arcH, float arcV);
    /** Moves the world point a running look-at turns toward. */
    void setLookAtPoint(const glm::vec3 &point);
    /** Return the bone to rest over a quarter second. */
    void endLookAt();

    // END Head look-at

    // Hilite

    /** Multiply the diffuse colour and add to the emissive colour of every mesh, attachments included. */
    void setHiliteColor(std::optional<glm::vec3> color);
    const std::optional<glm::vec3> &hiliteColor() const { return _hiliteColor; }

    /** Sets how the model looks under Force Sight and, for a glowing model, its glow colour. */
    void setForceSightStyle(ForceSightStyle style, glm::vec4 glowColor = glm::vec4 {0.0f}) {
        _forceSightStyle = style;
        _forceGlowColor = glowColor;
    }
    ForceSightStyle forceSightStyle() const { return _forceSightStyle; }
    const glm::vec4 &forceGlowColor() const { return _forceGlowColor; }

    // END Hilite

private:
    /** A bone turned toward a world point, or returning to rest from one. */
    struct LookAt {
        std::string bone;
        float arcH {0.0f};
        float arcV {0.0f};
        glm::vec3 point {0.0f};
        bool returning {false};
        float returnTime {0.0f};
        glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f};
        glm::quat returnFrom {1.0f, 0.0f, 0.0f, 0.0f};
    };

    graphics::Model *_model;
    ModelUsage _usage;

    IAnimationEventListener *_animEventListener {nullptr};
    float _drawDistance {std::numeric_limits<float>::max()};

    // Lookups

    std::unordered_map<uint16_t, ModelNodeSceneNode *> _nodeByNumber;
    std::unordered_map<std::string, ModelNodeSceneNode *> _nodeByName;
    std::unordered_map<std::string, SceneNode *> _attachments;

    // END Lookups

    // Animation

    /**
     * Layers lead the channels, newest first; the rest are the base channels,
     * which the blend mode describes.
     */
    std::deque<AnimationChannel> _animChannels;
    AnimationBlendMode _animBlendMode {AnimationBlendMode::Single};

    // END Animation

    // Head look-at

    std::optional<LookAt> _lookAt;

    // END Head look-at

    float _fadeAlpha {1.0f};
    std::optional<glm::vec3> _hiliteColor;
    ForceSightStyle _forceSightStyle {ForceSightStyle::Grey};
    glm::vec4 _forceGlowColor {0.0f};

    // Flags

    bool _pickable {false};
    std::shared_ptr<graphics::Texture> _bumpedOutShellTexture;
    float _bumpedOutShellOffset {0.0f};

    // END Flags

    void buildNodeTree(graphics::ModelNode &node, SceneNode &parent);

    // Animation

    /** Plays \p anim as a layer over the base channels. */
    void playLayer(graphics::Animation &anim, std::shared_ptr<graphics::LipAnimation> lipAnim, AnimationProperties properties);
    /** Plays \p anim in the base channels, under the layers. */
    void playBase(graphics::Animation &anim, std::shared_ptr<graphics::LipAnimation> lipAnim, AnimationProperties properties);
    /** The number of layers leading the channels. */
    size_t layerCount() const;

    void updateAnimations(float dt);
    void updateAnimationChannel(AnimationChannel &channel, float dt);
    void rearmSingleEmitters(const std::string &animationRoot);
    void computeAnimationStates(AnimationChannel &channel, float time, const graphics::ModelNode &modelNode);
    void applyAnimationStates(const graphics::ModelNode &modelNode, size_t layers);

    static AnimationBlendMode getAnimationBlendMode(int flags);

    // END Animation

    /** Turns the look-at bone, if any, by one frame. */
    void applyLookAt(float dt);
};

} // namespace scene

} // namespace reone
