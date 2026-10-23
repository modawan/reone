/*
 * Copyright (c) 2026 The reone project contributors
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

#include "reone/game/effect/visual.h"

#include <algorithm>
#include <array>

#include "reone/audio/mixer.h"
#include "reone/audio/source.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/visualeffects.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/graphics/texture.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/provider/textures.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/mesh.h"
#include "reone/scene/node/model.h"
#include "reone/system/randomutil.h"

namespace reone {

namespace game {

// A second Speed Knight visual on an object that still carries one is not
// shown at all.
static constexpr int kSpeedKnightVisual = 1020;

// Programs (progfx_impact and progfx_duration values).
static constexpr int kHandFizzleProgram = 1201;
static constexpr int kResistProgram = 1202;
static constexpr int kViewAngleProgram = 1500;
static constexpr int kMotionBlurProgram = 1601;
static constexpr int kMotionBlurProgramAlternate = 1602;
static constexpr int kBreathProgram = 1703;

// Every impact program lasts one second, except the hand fizzle and the
// widened view.
static constexpr float kImpactProgramSeconds = 1.0f;

// Program 1500 on the player: the view angle widens to 135 degrees over
// 0.75 s with the dolly zoom out of combat, narrows back from the moment
// 4 s remain, and the saved angle returns when the program ends, 4.75 s on.
static constexpr float kWideViewAngle = 135.0f;
static constexpr float kWideningSeconds = 0.75f;
static constexpr float kNarrowingRemaining = 4.0f;
static constexpr float kViewAngleProgramSeconds = 4.75f;

// Programs 1601 and 1602 hold the motion blur. It follows the player's drive
// speed every frame, and leaves while the holder is not the player or is
// debilitated.
static constexpr float kMotionBlurRatio = 0.75f;

// A node model without an impact animation, on an application that is not
// kept, shows for this long.
static constexpr float kNodeModelHold = 1.0f;

// The cessation program runs for this long, unless it began while the impact
// program still ran, when it ends with the impact program's time.
static constexpr float kCessationProgramSeconds = 1.0f;

// An application that is not kept leaves its node models attached, as they
// are, for this long after the first of them ends its impact (or its hold),
// and then removes them all.
static constexpr float kNodeModelUnloadHold = 3.0f;

// The bumped-out shell stands this far off the body.
static constexpr float kShellOffset = 0.02f;

// The model a missed beam ends at.
static constexpr const char *kBeamStandInModel = "c_dummy";

static float impactDuration(const std::shared_ptr<graphics::Model> &model,
                            const std::shared_ptr<audio::AudioClip> &sound) {
    auto animation = model ? model->getAnimation("impact") : nullptr;
    return std::max(animation ? animation->length() : 0.0f, sound ? sound->duration() : 0.0f);
}

static void collectBeamMeshes(scene::SceneNode &node, std::vector<scene::MeshSceneNode *> &meshes) {
    if (node.type() == scene::SceneNodeType::Mesh) {
        meshes.push_back(static_cast<scene::MeshSceneNode *>(&node));
    }
    for (auto *child : node.children()) {
        collectBeamMeshes(*child, meshes);
    }
}

// Where a visual's models attach on an object. Creatures name their nodes;
// a placeable prefixes its with its model name after the first four
// characters, a door with its whole model name.
enum class VisualSite {
    Root,
    Impact,
    Head
};

static std::string placeableNodePrefix(const scene::ModelSceneNode &model) {
    const std::string &name = model.model().name();
    return name.substr(std::min<size_t>(4, name.size()));
}

static std::string visualSiteNode(const Object &object, const scene::ModelSceneNode &model, VisualSite site) {
    if (isa<Placeable>(&object)) {
        static constexpr const char *suffixes[] = {"_ground", "_impact", "_head_hit"};
        return placeableNodePrefix(model) + suffixes[static_cast<int>(site)];
    }
    if (isa<Door>(&object)) {
        static constexpr const char *suffixes[] = {"grnd", "impc", "hhit"};
        return model.model().name() + suffixes[static_cast<int>(site)];
    }
    static constexpr const char *nodes[] = {"root", "impact", "talkdummy"};
    return nodes[static_cast<int>(site)];
}

// The node a beam starts from on its source, by body part; the source's root
// when its model lacks that node.
static std::optional<std::reference_wrapper<scene::SceneNode>> beamSourceNode(const Object &source,
                                                                              scene::ModelSceneNode &model,
                                                                              int bodyNode) {
    std::string name;
    if (isa<Placeable>(&source)) {
        static constexpr const char *suffixes[] = {"_hand", "_impact", "_head"};
        name = placeableNodePrefix(model) + (bodyNode >= 0 && bodyNode <= 2 ? suffixes[bodyNode] : "_ground");
    } else if (isa<Door>(&source)) {
        name = model.model().name() + (bodyNode >= 0 && bodyNode <= 2 ? "impc" : "grnd");
    } else {
        name = beamSourceHook(static_cast<BodyNode>(bodyNode));
    }
    auto *node = model.getNodeByName(name);
    if (!node) {
        node = model.getNodeByName("root");
    }
    if (!node) {
        return std::nullopt;
    }
    return *node;
}

// A missed spell is aimed at a point near its target. From farther than 10 m
// the point lies up to 2 m from the target, turned up to 120 degrees either
// way from the direction of the caster, 1 m above the target's feet. From
// closer it lies 10 to 19 m beyond the target, pushed sideways past the
// target's personal space. A miss with no caster is aimed at the world
// origin.
static glm::vec3 spellRangedMissPoint(std::optional<std::reference_wrapper<const Object>> caster,
                                      const Object &target) {
    if (!caster) {
        return glm::vec3 {0.0f};
    }
    const auto &casterPosition = caster->get().position();
    float personalSpace = 0.5f;
    if (auto *creature = dyn_cast<Creature>(&target)) {
        personalSpace = creature->creaturePersonalSpace() - 0.1f;
    }
    const float reach = static_cast<float>(randomInt(0, 1999) + 1) / 1000.0f;
    const glm::vec3 &targetPosition = target.position();
    const glm::vec3 toCaster = casterPosition - targetPosition;
    if (glm::length2(toCaster) > 100.0f) {
        const auto direction = glm::normalize(toCaster);
        float angle = std::atan(direction.y / direction.x);
        const bool clockwise = randomInt(0, 1) == 1;
        const float turn = static_cast<float>(randomInt(0, 2093)) / 1000.0f;
        angle = clockwise ? angle - turn : angle + turn;
        if (angle > glm::two_pi<float>()) {
            angle -= glm::two_pi<float>();
        }
        if (angle < 0.0f) {
            angle += glm::two_pi<float>();
        }
        glm::vec2 offset {std::cos(angle), std::sin(angle)};
        if (direction.x < 0.0f) {
            offset = -offset;
        }
        offset *= reach;
        return glm::vec3 {targetPosition.x + offset.x, targetPosition.y + offset.y, targetPosition.z + 1.0f};
    }
    const glm::vec3 away = targetPosition - casterPosition;
    const auto direction = glm::normalize(away);
    glm::vec2 side = randomInt(0, 1) == 1 ? glm::vec2 {direction.y, -direction.x} : glm::vec2 {-direction.y, direction.x};
    side *= 1.0f + personalSpace;
    if (glm::length2(away) > 25.0f) {
        side *= 1.0f + static_cast<float>(randomInt(0, static_cast<int>(reach * 1000.0f) - 1)) / 1000.0f;
    }
    const auto heading = glm::normalize(glm::vec3 {away.x + side.x, away.y + side.y, away.z});
    const float distance = static_cast<float>(randomInt(0, 9)) + 10.0f;
    return glm::vec3 {targetPosition.x + distance * heading.x,
                      targetPosition.y + distance * heading.y,
                      2.0f * targetPosition.z + 1.0f};
}

// The size a visual chooses its root model by: an object that is not a
// creature counts as medium.
static CreatureSize visualTargetSize(const Object &object) {
    auto *creature = dyn_cast<Creature>(&object);
    return creature ? creature->size() : CreatureSize::Medium;
}

// The parts of a body a shell covers: the body, its head model, and the
// model of the item worn on the head. Weapons are not covered.
template <class Apply>
static void forEachShellModel(scene::ModelSceneNode &body, Apply &&apply) {
    apply(body);
    auto *head = dynamic_cast<scene::ModelSceneNode *>(body.getAttachment("headhook"));
    if (!head) {
        return;
    }
    apply(*head);
    if (auto *mask = dynamic_cast<scene::ModelSceneNode *>(head->getAttachment("gogglehook"))) {
        apply(*mask);
    }
}

void setShell(scene::ModelSceneNode &body, std::shared_ptr<graphics::Texture> texture) {
    forEachShellModel(body, [&texture](scene::ModelSceneNode &model) {
        model.setBumpedOutShell(texture, kShellOffset);
    });
}

void clearShell(scene::ModelSceneNode &body) {
    forEachShellModel(body, [](scene::ModelSceneNode &model) {
        model.clearBumpedOutShell();
    });
}

VisualEffect::VisualEffect(int visualEffectId, bool missEffect, ServicesView &services) :
    CopyableEffect(EffectType::Visual),
    _visualEffectId(visualEffectId),
    _missEffect(missEffect),
    _services(services) {
    setSaveFacingInteger(0, visualEffectId);
    setSaveFacingInteger(2, missEffect ? 1 : 0);
}

VisualEffect::VisualEffect(const VisualEffect &other) :
    CopyableEffect(other),
    _visualEffectId(other._visualEffectId),
    _missEffect(other._missEffect),
    _location(other._location),
    _services(other._services) {
    // Scene nodes, beams, shells and sounds belong to one application.
}

VisualEffect::~VisualEffect() {
    clearPresentation();
}

void VisualEffect::retireAreaRuntime(const std::set<const Object *> &) {
    // A held motion blur comes back when its holder is presented again.
    const bool motionBlur = _motionBlurProgram;
    clearPresentation();
    _motionBlurProgram = motionBlur;
}

void VisualEffect::clearPresentation(std::optional<std::reference_wrapper<Object>> holder) {
    endPresentationButNodeModels(holder);
    removeNodeModels();
    _cessationRemaining.reset();
    _programPhase = ProgramPhase::None;
}

void VisualEffect::endPresentationButNodeModels(std::optional<std::reference_wrapper<Object>> holder) {
    endSoundsAndBeams();
    stopProgram(holder);
}

void VisualEffect::endSoundsAndBeams() {
    if (_durationSound) {
        _durationSound->stop();
        _durationSound.reset();
    }
    for (auto *mesh : _beamMeshes) {
        mesh->clearProjectedBeamTarget();
    }
    _beamMeshes.clear();
    _beamSourceOwner.reset();
    _beamTargetOwner.reset();
    if (_node) {
        _node->graph().removeRoot(*_node);
        _node.reset();
    }
    if (_beamEnd) {
        _beamEnd->graph().removeRoot(*_beamEnd);
        _beamEnd.reset();
    }
}

void VisualEffect::removeNodeModels() {
    for (auto &nodeModel : _nodeModels) {
        if (nodeModel.hook && !nodeModel.followsPositionOnly) {
            nodeModel.hook->removeChild(*nodeModel.model);
        } else {
            nodeModel.model->graph().removeRoot(*nodeModel.model);
        }
    }
    _nodeModels.clear();
    _nodeModelsUnloadRemaining.reset();
}

float VisualEffect::duration() const {
    const auto row = _services.game.visualEffects.get(_visualEffectId);
    if (!row) {
        return 0.0f;
    }
    const VisualEffectDesc &desc = row->get();
    return impactDuration(desc.locationModel, desc.soundImpact);
}

EffectApplicationResult VisualEffect::onApply(Object &object, EffectInstance &instance) {
    // Each application presents a visual of its own, so the effect can be
    // applied again while one is showing.
    auto runtime = std::make_shared<VisualEffect>(instance.integerParameter(0), instance.integerParameter(2) != 0, _services);
    instance.effect = runtime;
    return runtime->present(object, instance);
}

// A beam or spell visual comes from object parameter 0, or from the creator
// when that parameter holds no object.
static std::shared_ptr<Object> visualSource(const EffectInstance &instance) {
    auto source = instance.boundObjectParameter(0);
    return source ? source : instance.boundCreator();
}

float VisualEffect::impactProgramLength(int program) const {
    // The hand fizzle lasts as long as its model's impact animation.
    if (program == kHandFizzleProgram) {
        if (auto model = _services.resource.models.get(spellVisualForProgram(program)->model)) {
            if (auto animation = model->getAnimation("impact")) {
                return animation->length();
            }
        }
    }
    if (program == kViewAngleProgram && _viewAngleHold) {
        return kViewAngleProgramSeconds;
    }
    return kImpactProgramSeconds;
}

bool VisualEffect::startViewAngleProgram(Object &object) {
    auto *active = object.game().getActiveCamera();
    if (!active || object.game().party().getLeader().get() != &object) {
        return false;
    }
    auto camera = std::dynamic_pointer_cast<Camera>(object.game().getObjectById(active->id()));
    if (!camera) {
        return false;
    }
    _viewAngleCamera = camera;
    _viewAngleHold = camera->beginViewAngleHold();
    camera->beginViewAngleAnimation(kWideViewAngle, kWideningSeconds);
    _viewAngleNarrowed = false;
    return true;
}

void VisualEffect::updateViewAngleProgram(Object &object) {
    if (!_viewAngleHold) {
        return;
    }
    auto camera = _viewAngleCamera.lock();
    // A pause has already returned the angle.
    if (!camera || !camera->holdsViewAngle(*_viewAngleHold)) {
        _viewAngleHold.reset();
        return;
    }
    if (object.game().party().getLeader().get() != &object) {
        stopViewAngleProgram();
        return;
    }
    if (!_viewAngleNarrowed && _impactRemaining <= kNarrowingRemaining) {
        camera->endViewAngleAnimation();
        camera->beginViewAngleAnimation(camera->heldViewAngle(), _impactRemaining);
        _viewAngleNarrowed = true;
    }
}

void VisualEffect::stopViewAngleProgram() {
    if (auto camera = _viewAngleCamera.lock()) {
        camera->releaseViewAngleHold(*_viewAngleHold);
    }
    _viewAngleCamera.reset();
    _viewAngleHold.reset();
}

void VisualEffect::startMotionBlurProgram(Object &object) {
    auto &game = object.game();
    _motionBlurGame = &game;
    _motionBlurProgram = true;
    game.addMotionBlurProgram(game.party().getLeader().get() == &object);
    _motionBlurApplied = true;
}

void VisualEffect::updateMotionBlurProgram(Object &object) {
    auto &game = object.game();
    auto *creature = dyn_cast<Creature>(&object);
    const bool player = creature && game.party().getLeader().get() == &object;
    if (!player || creature->isDebilitated()) {
        if (_motionBlurApplied) {
            game.removeMotionBlurProgram();
        }
        _motionBlurApplied = false;
        return;
    }
    if (!_motionBlurApplied) {
        game.addMotionBlurProgram(true);
        _motionBlurApplied = true;
    }
    const float speed = creature->driveSpeed() / creature->driveMaxSpeed();
    game.setSpeedBlurRatio(speed > 1.0f ? kMotionBlurRatio : kMotionBlurRatio * speed);
}

bool VisualEffect::startProgram(int program, Object &object, const EffectInstance &instance) {
    if (program == kViewAngleProgram) {
        return startViewAngleProgram(object);
    }
    if (program == kMotionBlurProgram || program == kMotionBlurProgramAlternate) {
        startMotionBlurProgram(object);
        return true;
    }
    auto body = std::dynamic_pointer_cast<scene::ModelSceneNode>(object.sceneNode());
    if (!body) {
        return false;
    }
    const bool tsl = object.game().isTSL();
    if (const auto beamName = beamModelForProgram(program, tsl); !beamName.empty()) {
        const auto ends = resolveBeamEnds(object, instance);
        auto model = ends.complete() ? _services.resource.models.get(beamName) : nullptr;
        if (!model) {
            return false;
        }
        _beam = true;
        _beamSourceOwner = ends.sourceModel;
        _beamTargetOwner = ends.targetModel;
        _node = body->graph().newModel(*model, scene::ModelUsage::Projectile);
        _node->setCullingEnabled(false);
        _node->setLocalTransform(ends.sourceNode->absoluteTransform());
        _node->playAnimation("cast01");
        collectBeamMeshes(*_node, _beamMeshes);
        for (auto *mesh : _beamMeshes) {
            mesh->setProjectedBeamTarget(*ends.targetNode);
        }
        body->graph().addRoot(_node);
        return true;
    }
    if (const std::string texture = shieldTextureForProgram(program); !texture.empty()) {
        _shellTexture = _services.resource.textures.get(texture, graphics::TextureUsage::MainTex);
        if (!_shellTexture) {
            return false;
        }
        _shellOwner = body;
        setShell(*body, _shellTexture);
        return true;
    }
    const auto spellVisual = spellVisualForProgram(program);
    const auto attachment = spellVisual ? spellVisual : modelAttachmentForProgram(program, tsl);
    if (!attachment) {
        return false;
    }
    auto model = _services.resource.models.get(attachment->model);
    // The breath hooks the head model, and the body only when there is no
    // head model.
    auto *head = program == kBreathProgram ? dynamic_cast<scene::ModelSceneNode *>(body->getAttachment("headhook")) : nullptr;
    auto *hook = head ? head->getNodeByName(attachment->hook) : body->getNodeByName(attachment->hook);
    if (!model || !hook) {
        return false;
    }
    _hookNode = hook;
    _attachedOwner = body;
    _attachmentProgram = program;
    if (program == kBreathProgram) {
        _releaseArea = object.game().module()->area();
    }
    _attached = body->graph().newModel(*model, scene::ModelUsage::Projectile);
    _hookNode->addChild(*_attached);
    if (program == kResistProgram) {
        // The resist visual faces its source.
        if (auto source = visualSource(instance)) {
            const glm::vec3 direction = source->position() - _hookNode->origin();
            if (glm::length2(direction) >= 1e-4f) {
                const float facing = glm::half_pi<float>() - glm::atan(direction.x, direction.y);
                _attached->setLocalTransform(_hookNode->absoluteTransformInverse() *
                                             glm::translate(_hookNode->origin()) * glm::eulerAngleZ(facing));
            }
        }
    }
    if (spellVisual) {
        _attached->playAnimation("impact");
    } else if (program == kBreathProgram) {
        _attached->playAnimation("impact", nullptr, scene::AnimationProperties::fromFlags(scene::AnimationFlags::loop));
    }
    return true;
}

// A beam runs from its source's node for the body part to the target's
// impact node, or its root when the model lacks one. A missed beam runs to its
// stand-in instead.
VisualEffect::BeamEnds VisualEffect::resolveBeamEnds(Object &object, const EffectInstance &instance) {
    BeamEnds ends;
    auto source = visualSource(instance);
    ends.sourceModel = source ? std::dynamic_pointer_cast<scene::ModelSceneNode>(source->sceneNode()) : nullptr;
    if (ends.sourceModel) {
        if (auto node = beamSourceNode(*source, *ends.sourceModel, instance.integerParameter(1))) {
            ends.sourceNode = &node->get();
        }
    }
    if (!_beamEndNode.empty()) {
        if (!_beamEnd) {
            return ends;
        }
        ends.targetModel = _beamEnd;
        ends.targetNode = _beamEnd->getNodeByName(_beamEndNode);
    } else {
        ends.targetModel = std::dynamic_pointer_cast<scene::ModelSceneNode>(object.sceneNode());
        if (ends.targetModel) {
            ends.targetNode = ends.targetModel->getNodeByName(visualSiteNode(object, *ends.targetModel, VisualSite::Impact));
        }
    }
    if (ends.targetModel && !ends.targetNode) {
        ends.targetNode = ends.targetModel->getNodeByName("root");
    }
    return ends;
}

void VisualEffect::showLatestShell(Object &holder) {
    const VisualEffect *latest = nullptr;
    uint64_t latestOrder = 0;
    auto consider = [&](const EffectInstance &applied) {
        auto *visual = dynamic_cast<const VisualEffect *>(applied.effect.get());
        if (visual && visual->_shellTexture && !visual->_shellOwner.expired() &&
            (!latest || applied.applicationOrder >= latestOrder)) {
            latest = visual;
            latestOrder = applied.applicationOrder;
        }
    };
    for (const auto &applied : holder.effects()) {
        consider(applied);
    }
    for (const auto &shown : holder.shownVisuals()) {
        consider(shown.get());
    }
    auto body = std::dynamic_pointer_cast<scene::ModelSceneNode>(holder.sceneNode());
    // A creature's own shell began after every application ordered before it.
    if (auto *creature = dyn_cast<Creature>(&holder)) {
        uint64_t order = 0;
        if (auto own = creature->latestOwnShell(order); own && (!latest || order > latestOrder)) {
            if (body) {
                setShell(*body, own);
            }
            return;
        }
    }
    if (latest) {
        if (auto owner = latest->_shellOwner.lock()) {
            setShell(*owner, latest->_shellTexture);
        }
    } else if (body) {
        clearShell(*body);
    }
}

void VisualEffect::stopProgram(std::optional<std::reference_wrapper<Object>> holder) {
    // The removed breath holds the first frame of its stop animation where
    // its hook last was, and the area keeps it until its emitter empties.
    auto releaseArea = _attachmentProgram == kBreathProgram ? _releaseArea.lock() : nullptr;
    if (_hookNode && _attached && releaseArea) {
        scene::AnimationProperties hold;
        hold.speed = 0.0f;
        _attached->playAnimation("stop", nullptr, hold);
        const glm::mat4 world = _attached->absoluteTransform();
        _hookNode->removeChild(*_attached);
        _attached->setLocalTransform(world);
        _attached->graph().addRoot(_attached);
        releaseArea->releaseEffectModel(std::move(_attached));
    } else if (_hookNode && _attached) {
        _hookNode->removeChild(*_attached);
    }
    _attached.reset();
    _hookNode = nullptr;
    _attachedOwner.reset();
    _attachmentProgram.reset();
    _releaseArea.reset();
    if (_viewAngleHold) {
        stopViewAngleProgram();
    }
    if (_motionBlurApplied && _motionBlurGame) {
        _motionBlurGame->removeMotionBlurProgram();
    }
    _motionBlurApplied = false;
    _motionBlurProgram = false;
    // Ending a shell empties the slot, and the latest shell left then shows
    // again.
    if (auto body = _shellOwner.lock()) {
        clearShell(*body);
        _shellOwner.reset();
        _shellTexture.reset();
        if (holder) {
            showLatestShell(holder->get());
        }
    }
    _shellOwner.reset();
    _shellTexture.reset();
}

// A node model plays its impact animation. Without one, a kept application
// goes straight to its looping duration animation and any other holds the
// model for a second.
void VisualEffect::startNodeModel(scene::ISceneGraph &graph, graphics::Model &model,
                                  std::optional<std::reference_wrapper<scene::SceneNode>> hook, bool orientationOff,
                                  std::shared_ptr<scene::ModelSceneNode> owner, const glm::vec3 &position,
                                  bool restoring, int slot) {
    NodeModel nodeModel;
    nodeModel.slot = slot;
    nodeModel.model = graph.newModel(model, scene::ModelUsage::Projectile);
    nodeModel.owner = std::move(owner);
    nodeModel.hook = hook ? &hook->get() : nullptr;
    nodeModel.followsPositionOnly = hook && orientationOff;
    if (hook && !nodeModel.followsPositionOnly) {
        hook->get().addChild(*nodeModel.model);
    } else {
        graph.addRoot(nodeModel.model);
        nodeModel.model->setLocalTransform(glm::translate(hook ? hook->get().origin() : position));
    }
    auto impact = restoring ? nullptr : model.getAnimation("impact");
    if (impact) {
        nodeModel.model->playAnimation("impact");
        nodeModel.impactRemaining = impact->length();
    } else if (_kept) {
        nodeModel.model->playAnimation("duration", nullptr, scene::AnimationProperties::fromFlags(scene::AnimationFlags::loop));
    } else {
        nodeModel.impactRemaining = kNodeModelHold;
    }
    _nodeModels.push_back(std::move(nodeModel));
}

// The root model goes to the object's root, chosen by its size, the impact
// model to its impact node and the head model to its head: on a creature, the
// talk dummy of its head model when it has one. A model whose node is missing
// is not shown.
void VisualEffect::attachNodeModels(Object &object, const std::shared_ptr<scene::ModelSceneNode> &body,
                                    const VisualEffectDesc &desc, bool restoring) {
    auto *creature = dyn_cast<Creature>(&object);
    // The sites are also the slots the models take, in this order.
    const std::pair<const std::shared_ptr<graphics::Model> *, VisualSite> models[] = {
        {&desc.rootModel(visualTargetSize(object)), VisualSite::Root},
        {&desc.impactModel, VisualSite::Impact},
        {&desc.headModel, VisualSite::Head}};
    for (const auto &[model, site] : models) {
        if (!*model) {
            continue;
        }
        scene::ModelSceneNode *owner = body.get();
        if (site == VisualSite::Head && creature) {
            if (auto *head = dynamic_cast<scene::ModelSceneNode *>(body->getAttachment("headhook"))) {
                owner = head;
            }
        }
        auto *hook = owner->getNodeByName(visualSiteNode(object, *body, site));
        if (!hook) {
            continue;
        }
        startNodeModel(body->graph(), **model, *hook, desc.orientationOff, body, glm::vec3 {0.0f}, restoring, static_cast<int>(site));
    }
}

void VisualEffect::updateNodeModels(float dt) {
    for (auto &nodeModel : _nodeModels) {
        if (nodeModel.followsPositionOnly) {
            nodeModel.model->setLocalTransform(glm::translate(nodeModel.hook->origin()));
        }
        if (nodeModel.impactRemaining > 0.0f) {
            nodeModel.impactRemaining -= dt;
            if (nodeModel.impactRemaining <= 0.0f) {
                if (_kept && !_ceasing) {
                    nodeModel.model->playAnimation("duration", nullptr, scene::AnimationProperties::fromFlags(scene::AnimationFlags::loop));
                } else if (!_ceasing && !_nodeModelsUnloadRemaining) {
                    _nodeModelsUnloadRemaining = kNodeModelUnloadHold;
                }
            }
        }
    }
    if (_nodeModelsUnloadRemaining) {
        *_nodeModelsUnloadRemaining -= dt;
        if (*_nodeModelsUnloadRemaining <= 0.0f) {
            removeNodeModels();
        }
    }
}

// The models are done the unload hold after the first cessation ends, every
// other model with it. The root model alone may hold them longer: when every
// model has a cessation and the root's is not the first to end, its countdown
// starts when the first ends, from the length of the last model's cessation
// (in root, impact, head order) and the hold; its own cessation ending before
// that countdown runs out restarts it at the hold.
std::optional<float> VisualEffect::startNodeModelCessation() {
    std::array<std::optional<float>, 3> lengths;
    bool withoutCessation = false;
    for (auto &nodeModel : _nodeModels) {
        auto cessation = nodeModel.model->model().getAnimation("cessation");
        if (!cessation) {
            nodeModel.impactRemaining = 0.0f;
            withoutCessation = true;
            continue;
        }
        nodeModel.model->playAnimation("cessation");
        nodeModel.impactRemaining = cessation->length();
        lengths[nodeModel.slot] = cessation->length();
    }
    std::optional<float> first;
    std::optional<float> last;
    for (const auto &length : lengths) {
        if (length) {
            first = std::min(first.value_or(*length), *length);
            last = length;
        }
    }
    if (!first) {
        return std::nullopt;
    }
    float done = *first + kNodeModelUnloadHold;
    const auto &root = lengths[static_cast<int>(VisualSite::Root)];
    if (root && !withoutCessation && *root > *first) {
        const float countdownEnd = *first + *last + kNodeModelUnloadHold;
        done = std::max(done, *root < countdownEnd ? *root + kNodeModelUnloadHold : countdownEnd);
    }
    return done;
}

float VisualEffect::nodeModelsPresentationLength() const {
    if (_nodeModels.empty()) {
        return 0.0f;
    }
    const auto first = std::min_element(_nodeModels.begin(), _nodeModels.end(), [](const NodeModel &a, const NodeModel &b) {
        return a.impactRemaining < b.impactRemaining;
    });
    return first->impactRemaining + kNodeModelUnloadHold;
}

EffectApplicationResult VisualEffect::present(Object &object, EffectInstance &instance) {
    const auto row = _services.game.visualEffects.get(_visualEffectId);
    // A second Speed Knight visual is not shown while the object still shows
    // one, kept or shown once; the application itself is kept or not as any
    // other.
    const auto isSpeedKnight = [](const EffectInstance &applied) {
        return applied.type() == EffectType::Visual && applied.integerParameter(0) == kSpeedKnightVisual;
    };
    const auto shown = object.shownVisuals();
    if (_visualEffectId == kSpeedKnightVisual &&
        (std::any_of(object.effects().begin(), object.effects().end(), isSpeedKnight) ||
         std::any_of(shown.begin(), shown.end(), [&](const EffectInstance &visual) { return isSpeedKnight(visual); }))) {
        _kept = instance.durationType() != DurationType::Instant && !_missEffect && !(row && row->get().fireAndForget);
        return _kept ? EffectApplicationResult::Retained : EffectApplicationResult::Applied;
    }
    if (row) {
        return present(object, instance, row->get());
    }
    // A row the table does not have shows nothing, but a timed application
    // that hit is kept all the same, and a miss still draws its point.
    if (_missEffect) {
        auto caster = instance.boundCreator();
        spellRangedMissPoint(caster ? std::optional<std::reference_wrapper<const Object>>(*caster) : std::nullopt, object);
    }
    _kept = instance.durationType() != DurationType::Instant && !_missEffect;
    return _kept ? EffectApplicationResult::Retained : EffectApplicationResult::Applied;
}

EffectApplicationResult VisualEffect::present(Object &object, EffectInstance &instance, const VisualEffectDesc &desc) {
    auto body = std::dynamic_pointer_cast<scene::ModelSceneNode>(object.sceneNode());
    scene::ISceneGraph *graph = body ? &body->graph() : nullptr;
    const bool tsl = object.game().isTSL();
    // A visual that shows a root model on its target runs no programs.
    const bool rooted = desc.rootModel(visualTargetSize(object)) != nullptr;
    const int impactProgramId = rooted ? -1 : desc.progFXImpact;
    const int durationProgramId = rooted ? -1 : desc.progFXDuration;
    // Only a timed application that hit, of a row that is not fire-and-forget,
    // is kept on the object. Any other is shown once as an impact, which never
    // runs the duration program; a beam shown once still runs it for the
    // application's duration.
    const bool beamRow = !beamModelForProgram(durationProgramId, tsl).empty();
    _kept = instance.durationType() != DurationType::Instant && !_missEffect && !desc.fireAndForget;
    const bool runsDuration = _kept || (beamRow && instance.duration != 0.0f);
    std::optional<int> durationProgram;
    if (runsDuration && durationProgramId >= 0) {
        durationProgram = durationProgramId;
    }
    if (_kept && !rooted && desc.progFXCessation >= 0) {
        _cessationProgram = desc.progFXCessation;
    }
    // A miss is aimed at a point near the target: a missed beam ends at a
    // stand-in there, reaching its impact node when the point is 1 m above
    // the target's feet and its root otherwise; any other missed visual is
    // shown at the point. TSL still draws the point's random terms but aims
    // every miss at the world origin.
    if (_missEffect) {
        auto caster = instance.boundCreator();
        const auto drawnPoint = spellRangedMissPoint(
            caster ? std::optional<std::reference_wrapper<const Object>>(*caster) : std::nullopt, object);
        const auto missPoint = tsl ? glm::vec3 {0.0f} : drawnPoint;
        if (beamRow) {
            _beamEndNode = missPoint.z == object.position().z + 1.0f ? "impact" : "root";
            auto standIn = graph ? _services.resource.models.get(kBeamStandInModel) : nullptr;
            if (standIn) {
                _beamEnd = graph->newModel(*standIn, scene::ModelUsage::Projectile);
                graph->addRoot(_beamEnd);
                _beamEnd->setLocalTransform(glm::translate(missPoint));
            }
        } else {
            _location = missPoint;
        }
    }
    const glm::vec3 position = _location.value_or(object.position());
    float presentationDuration = 0.0f;

    if (graph) {
        // The impact program runs first and the duration program follows it.
        // Without an impact program the duration program starts at once, and
        // without either a kept application starts with its cessation program.
        const bool impactProgram = !instance.restoring && impactProgramId >= 0;
        if (impactProgram) {
            startProgram(impactProgramId, object, instance);
            _impactRemaining = impactProgramLength(impactProgramId);
            _pendingDurationProgram = durationProgram;
            _programPhase = ProgramPhase::Impact;
            presentationDuration = _impactRemaining;
        } else if (durationProgram) {
            startProgram(*durationProgram, object, instance);
            _programPhase = ProgramPhase::Duration;
        } else if (!instance.restoring && _cessationProgram) {
            startProgram(*_cessationProgram, object, instance);
            _cessationRemaining = kCessationProgramSeconds;
            _programPhase = ProgramPhase::Cessation;
        }
        // A restored application only brings back what it keeps showing.
        if (!_beam && (!instance.restoring || _kept)) {
            if (_location) {
                if (desc.locationModel) {
                    startNodeModel(*graph, *desc.locationModel, std::nullopt, desc.orientationOff, nullptr, position, instance.restoring, static_cast<int>(VisualSite::Root));
                }
            } else if (body) {
                attachNodeModels(object, body, desc, instance.restoring);
            }
        }
        if (!_kept) {
            presentationDuration = std::max(presentationDuration, nodeModelsPresentationLength());
        }
        // The impact sound plays where the visual lands: a missed beam's
        // stand-in, otherwise the object or point.
        if (!instance.restoring && desc.soundImpact) {
            const glm::vec3 soundPosition = _beamEnd ? _beamEnd->origin() : position;
            _services.audio.mixer.play(desc.soundImpact, audio::AudioType::Sound, 1.0f, false, soundPosition);
            presentationDuration = std::max(presentationDuration, desc.soundImpact->duration());
        }
        if (desc.soundDuration) {
            _durationSound = _services.audio.mixer.play(desc.soundDuration, audio::AudioType::Sound, 1.0f, true, position);
        }
    }
    if (_kept) {
        return EffectApplicationResult::Retained;
    }
    // An application shown once is not kept on the object: it presents on its
    // own for as long as what it shows, and a beam shown once for the
    // application's duration.
    if (runsDuration && _beam) {
        presentationDuration = std::max(presentationDuration, instance.duration);
    }
    if (presentationDuration > 0.0f) {
        object.addShownVisual(instance, presentationDuration);
    }
    return EffectApplicationResult::Applied;
}

void VisualEffect::onUpdate(Object &object, const EffectInstance &instance, float dt) {
    if (_impactRemaining > 0.0f) {
        _impactRemaining -= dt;
        if (_impactRemaining <= 0.0f) {
            stopProgram(object);
            if (_pendingDurationProgram) {
                startProgram(*_pendingDurationProgram, object, instance);
            }
            _pendingDurationProgram.reset();
            _programPhase = _kept ? ProgramPhase::Duration : ProgramPhase::None;
        } else {
            updateViewAngleProgram(object);
        }
    }
    if (_cessationRemaining) {
        *_cessationRemaining -= dt;
        if (*_cessationRemaining <= 0.0f) {
            stopProgram(object);
            _cessationRemaining.reset();
            _programPhase = ProgramPhase::None;
        }
    }
    if (_motionBlurProgram && _impactRemaining <= 0.0f) {
        updateMotionBlurProgram(object);
    }
    updateNodeModels(dt);
    if (_durationSound) {
        _durationSound->setPosition(object.position());
    }
    if (_beam) {
        const auto ends = resolveBeamEnds(object, instance);
        if (ends.sourceNode && _node) {
            _node->setLocalTransform(ends.sourceNode->absoluteTransform());
        }
        for (auto *mesh : _beamMeshes) {
            if (ends.complete()) {
                mesh->setProjectedBeamTarget(*ends.targetNode);
            } else {
                mesh->clearProjectedBeamTarget();
            }
        }
        _beamSourceOwner = ends.sourceModel;
        _beamTargetOwner = ends.targetModel;
    }
}

// A kept application ends in its cessation. Its node models play their
// cessation animation, where they have one (see startNodeModelCessation). The
// impact or duration program running stops, and the cessation program, if
// any, takes its place: for a second, for what is left of the impact program,
// or, when no program ran, until the presentation ends. A cessation program
// already running from the start runs on. Sounds and beams end at once. The
// models stay until both they and the cessation program are done; with
// neither to wait for, everything goes at once.
EffectRemovalResult VisualEffect::onRemove(Object &object, const EffectInstance &instance) {
    if (!_kept || _ceasing) {
        clearPresentation(object);
        return EffectRemovalResult::Removed;
    }
    endSoundsAndBeams();
    const auto modelsDone = startNodeModelCessation();
    std::optional<float> programDone;
    switch (_programPhase) {
    case ProgramPhase::Impact:
        stopProgram(object);
        if (_cessationProgram) {
            startProgram(*_cessationProgram, object, instance);
            programDone = _impactRemaining;
        }
        break;
    case ProgramPhase::Duration:
        stopProgram(object);
        if (_cessationProgram) {
            startProgram(*_cessationProgram, object, instance);
            programDone = kCessationProgramSeconds;
        }
        break;
    case ProgramPhase::Cessation:
        programDone = _cessationRemaining;
        break;
    case ProgramPhase::None:
        if (_cessationProgram) {
            startProgram(*_cessationProgram, object, instance);
        }
        break;
    }
    _impactRemaining = 0.0f;
    _pendingDurationProgram.reset();
    _cessationRemaining = programDone;
    _programPhase = programDone ? ProgramPhase::Cessation : ProgramPhase::None;
    const float done = std::max(modelsDone.value_or(0.0f), programDone.value_or(0.0f));
    if (done <= 0.0f) {
        clearPresentation(object);
        return EffectRemovalResult::Removed;
    }
    _ceasing = true;
    object.addShownVisual(instance, done);
    return EffectRemovalResult::Removed;
}

} // namespace game

} // namespace reone
