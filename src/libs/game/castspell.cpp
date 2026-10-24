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

#include "reone/game/castspell.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <glm/geometric.hpp>

#include "reone/audio/mixer.h"
#include "reone/game/d20/class.h"
#include "reone/game/d20/spell.h"
#include "reone/game/d20/spells.h"
#include "reone/game/di/services.h"
#include "reone/game/forcerules.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/graphics/model.h"
#include "reone/resource/provider/models.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/model.h"
#include "reone/system/exception/validation.h"

namespace reone {

namespace game {

// The spell of an effect that no spell made.
static constexpr uint32_t kNoEffectSpell = std::numeric_limits<uint32_t>::max();

// Animations (animations.2da rows) of a cast's conjure and cast clips.
static constexpr int kConjureAnimationId = 10015;
static constexpr int kDarkConjureAnimationId = 10016;
static constexpr int kUpConjureAnimationId = 11000;
static constexpr int kThrowConjureAnimationId = 10162;
static constexpr int kSelfCastAnimationId = 10017;
static constexpr int kDarkCastAnimationId = 10018;
static constexpr int kUpCastAnimationId = 10019;
static constexpr int kAreaCastAnimationId = 10020;
static constexpr int kFuryCastAnimationId = 10414;
static constexpr int kCrushCastAnimationId = 10415;
static constexpr int kMonsterFuryCastAnimationId = 279;
static constexpr int kThrowCastAnimationId = 10061;
static constexpr int kCatchAnimationId = 10161;

// Animations of an item use.
static constexpr int kWristLaunchAnimationId = 10417;
static constexpr int kNearThrowAnimationId = 10130;
static constexpr int kThrowAnimationId = 10129;
static constexpr int kInjectAnimationId = 10070;
static constexpr int kActivateAnimationId = 10136;
static constexpr int kDroidUtilityAnimationId = 11001;

// A grenade thrown at a target nearer than this takes the near throw.
static constexpr float kNearThrowDistance = 10.0f;

// Each grenade's spell (spells.2da rows), and the spell it casts from a TSL
// wrist launcher.
static constexpr std::array<std::pair<int, int>, 11> kWristLauncherSpells {{{87, 246},
                                                                            {88, 247},
                                                                            {89, 248},
                                                                            {90, 249},
                                                                            {91, 250},
                                                                            {92, 251},
                                                                            {93, 252},
                                                                            {94, 253},
                                                                            {95, 254},
                                                                            {186, 255},
                                                                            {187, 256}}};

// A wrist launcher worn on either arm.
static bool wearsWristLauncher(const Creature &user) {
    for (const int slot : {InventorySlots::rightArm, InventorySlots::leftArm}) {
        const auto it = user.equipment().find(slot);
        if (it != user.equipment().end() && it->second && it->second->itemType() == Item::kWristLauncherItemType) {
            return true;
        }
    }
    return false;
}

SpellType itemSpellForActor(const Object &actor, SpellType propertySpell) {
    const auto *creature = dyn_cast<Creature>(&actor);
    if (!actor.game().isTSL() || !creature || !wearsWristLauncher(*creature)) {
        return propertySpell;
    }
    for (const auto &[grenade, launched] : kWristLauncherSpells) {
        if (static_cast<int>(propertySpell) == grenade) {
            return static_cast<SpellType>(launched);
        }
    }
    return propertySpell;
}

void publishItemCastLevel(Creature &caster) {
    // An item's spell is cast at the level of the user's last class, whatever
    // the item's own caster level and the class the user casts with.
    const auto &classes = caster.attributes().classLevels();
    caster.spellScriptContext().levelOverride = classes.empty() ? 0 : classes[classes.size() - 1].second;
}

int queryCasterLevel(const Object &object) {
    const auto *owner = &object;
    std::shared_ptr<Object> areaEffectCreator;
    int spellId = object.spellCastContext().spellId;
    if (object.type() == ObjectType::AreaOfEffect) {
        areaEffectCreator = object.effectSource();
        if (!areaEffectCreator) {
            return 0;
        }
        owner = areaEffectCreator.get();
        spellId = static_cast<int>(object.effectSpellId());
    }
    if (const auto creature = dyn_cast<Creature>(owner)) {
        if (owner->spellScriptContext().levelOverride) {
            return *owner->spellScriptContext().levelOverride;
        }
        const int clazz = owner->spellCastContext().castingClass;
        switch (castingSourceKind(clazz)) {
        case CastingSourceKind::SpellLikeAbility:
            return creature->spellLikeAbilityCasterLevel(spellId);
        case CastingSourceKind::Class:
            return creature->adjustedClassLevel(clazz);
        case CastingSourceKind::UnselectedClass:
            break;
        }
    } else if (!isa<Placeable>(owner)) {
        return 0;
    }
    // A creature casting with no class, and a placeable, cast at a level by
    // the owner's current spell, even when the query is for an area of effect
    // it created.
    const auto spell = owner->services().game.spells.get(
        static_cast<SpellType>(owner->spellCastContext().spellId));
    return spell ? spell->innateCasterLevel() : 0;
}

void SpellScriptContext::setImpact(
    const SpellCastContext &context, const std::shared_ptr<Object> &target,
    const std::shared_ptr<Object> &item, std::optional<std::reference_wrapper<const Location>> location) {
    cast = context;
    _target = target;
    _item = item;
    _location = location ? std::make_shared<Location>(location->get().position(), location->get().facing()) : nullptr;
}

std::shared_ptr<Object> SpellScriptContext::target() const {
    return _target.resolve();
}
std::shared_ptr<Object> SpellScriptContext::item() const {
    return _item.resolve();
}
std::shared_ptr<Location> SpellScriptContext::location() const {
    return _location ? std::make_shared<Location>(_location->position(), _location->facing()) : nullptr;
}
std::shared_ptr<Object> SpellScriptContext::activeTarget() const {
    return _activeTarget.resolve();
}
void SpellScriptContext::setActiveTarget(const std::shared_ptr<Object> &target) {
    _activeTarget = target;
}
void SpellScriptContext::clearActiveTarget() {
    _activeTarget.reset();
}

void SpellScriptContext::restore(std::optional<std::reference_wrapper<const resource::Gff>> saved) {
    *this = SpellScriptContext {};
    if (!saved) {
        return;
    }
    const auto &state = saved->get();
    cast.spellId = state.getInt("SpellId", -1);
    cast.casterLevel = state.getInt("CasterLevel");
    cast.metaMagic = state.getInt("MetaMagic", 255);
    cast.forcePointCost = state.getInt("ForceCost");
    uint8_t castingClass;
    if (!state.readByte(castingClass, "CastingClass")) {
        throw ValidationException("SpellContext has no CastingClass");
    }
    cast.castingClass = castingClass;
    if (state.getBool("OverrideActive")) {
        levelOverride = state.getInt("LevelOverride");
    }
    _savedIds = {{state.getUint("TargetId", kSavedRuntimeInvalidObjectId),
                  state.getUint("ItemId", kSavedRuntimeInvalidObjectId),
                  state.getUint("ActiveTarget", kSavedRuntimeInvalidObjectId)}};
    if (state.getBool("HasLocation")) {
        const glm::vec3 position {state.getFloat("X"), state.getFloat("Y"), state.getFloat("Z")};
        _location = std::make_shared<Location>(position, state.getFloat("Facing"));
    }
    _needsBind = true;
    _restored = true;
}

void SpellScriptContext::bind(
    const std::function<std::shared_ptr<Object>(uint32_t)> &resolver) {
    if (!_needsBind) {
        return;
    }
    RuntimeObjectRef<Object> *references[] = {&_target, &_item, &_activeTarget};
    for (size_t index = 0; index < _savedIds.size(); ++index) {
        *references[index] = _savedIds[index] == kSavedRuntimeInvalidObjectId
                                 ? nullptr
                                 : resolver(_savedIds[index]);
    }
    _needsBind = false;
}

std::shared_ptr<resource::Gff> SpellScriptContext::save(
    const std::function<uint32_t(const Object &)> &objectId) const {
    if (cast.spellId < 0 && !_location && _target.empty() && _item.empty() &&
        _activeTarget.empty() && !_restored) {
        return nullptr;
    }
    const auto id = [&](const RuntimeObjectRef<Object> &reference) {
        const auto object = reference.resolve();
        return object ? objectId(*object) : kSavedRuntimeInvalidObjectId;
    };
    using resource::Gff;
    auto result = Gff::Builder()
                      .field(Gff::Field::newInt("SpellId", cast.spellId))
                      .field(Gff::Field::newInt("CasterLevel", cast.casterLevel))
                      .field(Gff::Field::newInt("MetaMagic", cast.metaMagic))
                      .field(Gff::Field::newInt("ForceCost", cast.forcePointCost))
                      .field(Gff::Field::newByte("CastingClass", static_cast<uint8_t>(cast.castingClass)))
                      .field(Gff::Field::newByte("OverrideActive", levelOverride.has_value()))
                      .field(Gff::Field::newInt("LevelOverride", levelOverride.value_or(0)))
                      .field(Gff::Field::newDword("TargetId", id(_target)))
                      .field(Gff::Field::newDword("ItemId", id(_item)))
                      .field(Gff::Field::newDword("ActiveTarget", id(_activeTarget)))
                      .field(Gff::Field::newByte("HasLocation", _location != nullptr));
    if (_location) {
        result.field(Gff::Field::newFloat("X", _location->position().x))
            .field(Gff::Field::newFloat("Y", _location->position().y))
            .field(Gff::Field::newFloat("Z", _location->position().z))
            .field(Gff::Field::newFloat("Facing", _location->facing()));
    }
    return result.build();
}

void SpellScriptContext::retireAreaRuntime(const std::set<uint32_t> &retainedObjectIds) {
    _needsBind = false;
    _savedIds.fill(kSavedRuntimeInvalidObjectId);
    for (auto *reference : {&_target, &_item, &_activeTarget}) {
        const auto object = reference->resolve();
        if (!object || retainedObjectIds.count(object->id()) == 0) {
            reference->reset();
        }
    }
}

SpellSchedule::State SpellSchedule::update(
    const CombatRound &round, Action &action, float dt) {
    _previousTime = _time;
    // Once started, a cast keeps its own time through any round pause.
    if (_state == SpellSchedule::WaitConjure && round.suspends(action)) {
        return _state;
    }
    return update(round.canExecute(action), dt);
}

SpellSchedule::State SpellSchedule::update(bool canStart, float dt) {
    _previousTime = _time;
    _time += std::max(0.0f, dt);

    switch (_state) {
    case SpellSchedule::WaitConjure: {
        if (canStart) {
            _time = _previousTime = 0.0f;
            _state = SpellSchedule::Conjure;
        }
        break;
    }
    case SpellSchedule::Conjure: {
        _state = SpellSchedule::WaitCast;
        break;
    }

    case SpellSchedule::WaitCast: {
        if (_time >= _conjTime) {
            _state = SpellSchedule::Cast;
        }
        break;
    }
    case SpellSchedule::Cast: {
        _state = SpellSchedule::WaitEffect;
        break;
    }
    case SpellSchedule::WaitEffect: {
        if (_time >= (_castTime + _conjTime)) {
            _state = SpellSchedule::Effect;
        }
        break;
    }
    case SpellSchedule::Effect: {
        _state = SpellSchedule::WaitFinish;
        break;
    }
    case SpellSchedule::WaitFinish: {
        // The cast ends as its cast time runs out, caught or not; the catch
        // only lengthens the round pause.
        if (_time >= _conjTime + _castTime) {
            _state = SpellSchedule::Finish;
        }
        break;
    }
    case SpellSchedule::Finish: {
        break;
    }
    }

    return _state;
}

void SpellSchedule::save(SavedCastAction &record) const {
    record.phase = _state;
    record.elapsed = _time;
    record.conjureTime = _conjTime;
    record.castTime = _castTime;
    record.catchTime = _catchTime;
}

void SpellSchedule::restore(const SavedCastAction &record) {
    _state = static_cast<State>(record.phase);
    _time = _previousTime = record.elapsed;
    _conjTime = record.conjureTime;
    _castTime = record.castTime;
    _catchTime = record.catchTime;
    // A phase saved as it began has already begun.
    if (_state == Conjure) {
        _state = WaitCast;
    } else if (_state == Cast) {
        _state = WaitEffect;
    } else if (_state == Effect) {
        _state = WaitFinish;
    }
}

// The clips a spell shows: a conjure one-shot, then the cast clip, which loops
// unless it is a one-shot, each with its animation ID. Creature models only
// conjure, and only the monster fury loop has a creature row.
struct CastPresentation {
    std::string conjure;
    std::string cast;
    bool castLoops {false};
    int conjureId {0};
    // None when the code has no cast clip.
    std::optional<int> castId;
};

static CastPresentation castClips(SpellCastAnimation code, bool creatureModel) {
    if (creatureModel) {
        switch (code) {
        case SpellCastAnimation::Throw:
            return {};
        case SpellCastAnimation::Up:
            return {"b0a1", "", false};
        case SpellCastAnimation::MonsterFury:
            return {"g0a1", "creadyrtw", true};
        default:
            return {"g0a1", "", false};
        }
    }
    switch (code) {
    case SpellCastAnimation::Self:
        return {"castout1", "castoutlp1", true};
    case SpellCastAnimation::Dark:
        return {"castout2", "castoutlp2", true};
    case SpellCastAnimation::Up:
        return {"castout3", "castoutlp3", true};
    case SpellCastAnimation::Throw:
        return {"throwsab", "throwsablp", true};
    case SpellCastAnimation::Area:
    case SpellCastAnimation::Jump:
    case SpellCastAnimation::Monster:
        return {"castout1", "", false};
    case SpellCastAnimation::Fury:
        return {"castout1", "forcerage", false};
    case SpellCastAnimation::Crush:
        return {"castout1", "forcecrush", false};
    case SpellCastAnimation::MonsterFury:
        return {"castout1", "creadyrtw", true};
    default:
        return {"castout1", "throwsablp", true};
    }
}

static int conjureAnimationId(SpellCastAnimation code) {
    switch (code) {
    case SpellCastAnimation::Dark:
        return kDarkConjureAnimationId;
    case SpellCastAnimation::Up:
        return kUpConjureAnimationId;
    case SpellCastAnimation::Throw:
        return kThrowConjureAnimationId;
    default:
        return kConjureAnimationId;
    }
}

static std::optional<int> castAnimationId(SpellCastAnimation code) {
    switch (code) {
    case SpellCastAnimation::Self:
        return kSelfCastAnimationId;
    case SpellCastAnimation::Dark:
        return kDarkCastAnimationId;
    case SpellCastAnimation::Up:
        return kUpCastAnimationId;
    case SpellCastAnimation::Area:
        return kAreaCastAnimationId;
    case SpellCastAnimation::Jump:
    case SpellCastAnimation::Monster:
        return std::nullopt;
    case SpellCastAnimation::Fury:
        return kFuryCastAnimationId;
    case SpellCastAnimation::Crush:
        return kCrushCastAnimationId;
    case SpellCastAnimation::MonsterFury:
        return kMonsterFuryCastAnimationId;
    default:
        return kThrowCastAnimationId;
    }
}

// The conjure's and the cast's animation IDs follow the code alone.
static CastPresentation castPresentation(SpellCastAnimation code, bool creatureModel) {
    CastPresentation look = castClips(code, creatureModel);
    look.conjureId = conjureAnimationId(code);
    look.castId = castAnimationId(code);
    return look;
}

static ItemUsePresentation itemUsePresentation(const Creature &user, int itemType, const Spell &spell,
                                               std::optional<std::reference_wrapper<const Creature>> targetCreature,
                                               const glm::vec3 &targetPosition) {
    const bool tsl = user.game().isTSL();
    // Only character and droid models use the item clips; others stand in their pose.
    const bool bodied = user.modelType() != Creature::ModelType::Creature;
    ItemUsePresentation look;
    switch (itemType) {
    case Item::kGrenadeItemType:
    case Item::kRocketItemType:
        look.impact = 0.8f;
        if (!bodied) {
            look.hold = false;
            break;
        }
        if (tsl && wearsWristLauncher(user)) {
            look.clip = "b11a3";
            look.clipId = kWristLaunchAnimationId;
            look.impact = 0.95f;
        } else if (glm::dot(targetPosition - user.position(), targetPosition - user.position()) <
                   kNearThrowDistance * kNearThrowDistance) {
            look.clip = "throwgren1";
            look.clipId = kNearThrowAnimationId;
            look.impact = 0.7f;
        } else {
            look.clip = "throwgren";
            look.clipId = kThrowAnimationId;
        }
        break;
    case Item::kStimItemType:
    case Item::kDroidRepairItemType:
    case Item::kMedicalItemType:
        look.onTarget = itemType != Item::kDroidRepairItemType;
        look.impact = 0.75f;
        if (!bodied) {
            look.hold = false;
            break;
        }
        if (!tsl || (targetCreature && &targetCreature->get() == &user)) {
            look.clip = "inject";
            look.clipId = kInjectAnimationId;
        } else {
            look.clip = "throwsablp";
            look.clipId = kThrowCastAnimationId;
            look.loops = true;
        }
        break;
    case Item::kSquadRecoveryItemType:
        look.onTarget = true;
        look.impact = 0.75f;
        if (!bodied) {
            look.hold = false;
            break;
        }
        look.clip = "activate";
        look.clipId = kActivateAnimationId;
        break;
    case Item::kForearmBandsItemType:
        look.impact = 0.6f;
        look.end = 1.0f;
        if (!bodied) {
            look.hold = false;
            break;
        }
        look.clip = "activate";
        look.clipId = kActivateAnimationId;
        break;
    case Item::kDroidUtilityItemType: {
        // Its conjure, then its loop; it takes effect as the loop starts.
        look.clip = "castout1";
        look.clipId = kDroidUtilityAnimationId;
        look.loopAt = 0.3f;
        auto model = std::dynamic_pointer_cast<scene::ModelSceneNode>(user.sceneNode());
        if (model) {
            if (auto conjure = model->model().getAnimation("castout1")) {
                look.loopAt = conjure->length() - 0.05f;
            }
        }
        look.impact = look.loopAt;
        break;
    }
    default:
        look.clip = "castoutlp1";
        look.clipId = kSelfCastAnimationId;
        look.loops = true;
        look.end = spell.castTime + 0.001f;
        break;
    }
    return look;
}

// The dead show their own pose.
static bool isPoseable(const Creature &creature) {
    return !creature.isDead() && !creature.isTemporarilyDead();
}

const ItemUsePresentation &CastPresenter::itemUse(const Creature &user, const Spell &spell, int itemType,
                                                  std::optional<std::reference_wrapper<const Creature>> targetCreature, const glm::vec3 &targetPosition) {
    _item = itemUsePresentation(user, itemType, spell, targetCreature, targetPosition);
    return *_item;
}

// The line the controlled caster reads when its cast fails to entanglement.
static constexpr int kEntangledCastStrRef = 0;

bool interruptEntangledCast(Creature &caster) {
    if (!caster.hasEffect(EffectType::Entangle)) {
        return false;
    }
    caster.showReadyAnimation();
    caster.setOrientationLock(script::kObjectInvalid);
    if (caster.game().party().getLeader().get() == &caster) {
        caster.game().addFeedbackMessage(kEntangledCastStrRef);
    }
    return true;
}

// The visuals hang from the head, the hand and the feet of the caster's body.
static constexpr std::array<const char *, 3> kCastVisualHooks {"headconjure", "handconjure", "root"};
// After the release, each set lasts this long in turn: the conjure visuals, then the cast visuals.
static constexpr float kCastVisualSeconds = 5.0f;

SpellCastVisuals::~SpellCastVisuals() {
    detach(_conjure);
    detach(_cast);
}

void SpellCastVisuals::attach(Set &set, Creature &caster, const std::array<std::string, 3> &models,
                              const std::string &animation) {
    auto body = std::dynamic_pointer_cast<scene::ModelSceneNode>(caster.sceneNode());
    if (!body) {
        return;
    }
    for (size_t index = 0; index < set.size(); ++index) {
        if (models[index].empty()) {
            continue;
        }
        auto model = caster.services().resource.models.get(models[index]);
        auto *hook = body->getNodeByName(kCastVisualHooks[index]);
        if (!model || !hook) {
            continue;
        }
        auto &attached = set[index];
        attached.body = body;
        attached.hook = kCastVisualHooks[index];
        attached.model = body->graph().newModel(*model, scene::ModelUsage::Projectile);
        hook->addChild(*attached.model);
        attached.model->playAnimation(animation);
    }
}

void SpellCastVisuals::detach(Attached &attached) {
    if (auto body = attached.body.lock(); body && attached.model) {
        body->getNodeByName(attached.hook)->removeChild(*attached.model);
    }
    attached = Attached {};
}

void SpellCastVisuals::detach(Set &set) {
    for (auto &attached : set) {
        detach(attached);
    }
}

void SpellCastVisuals::showConjure(Creature &caster, const Spell &spell) {
    detach(_conjure);
    detach(_cast);
    attach(_conjure, caster, spell.conjureVisuals, "conjure01");
    _released = false;
    _conjureRemaining = _castRemaining = kCastVisualSeconds;
}

void SpellCastVisuals::showCast(Creature &caster, const Spell &spell) {
    detach(_cast);
    attach(_cast, caster, spell.castVisuals, "cast01");
    // The conjure visuals fade out; one without a fade stays until its time runs out.
    for (auto &attached : _conjure) {
        if (!attached.model || !attached.model->model().getAnimation("fade")) {
            continue;
        }
        attached.model->playAnimation("fade");
        attached.fading = true;
    }
    _released = true;
}

void SpellCastVisuals::clear() {
    detach(_conjure);
    detach(_cast);
    _released = false;
}

void SpellCastVisuals::update(float dt) {
    // A faded conjure visual is gone once its fade has played.
    for (auto &attached : _conjure) {
        if (attached.fading && attached.model->isAnimationFinished()) {
            detach(attached);
        }
    }
    if (!_released) {
        return;
    }
    if (_conjureRemaining > 0.0f) {
        _conjureRemaining -= dt;
        if (_conjureRemaining <= 0.0f) {
            detach(_conjure);
            _conjureRemaining = 0.0f;
        }
    } else if (_castRemaining > 0.0f) {
        _castRemaining -= dt;
        if (_castRemaining <= 0.0f) {
            detach(_cast);
            _castRemaining = 0.0f;
            _released = false;
        }
    } else {
        _released = false;
    }
}

void CastPresenter::update(Creature &caster, const Spell &spell, std::optional<int> itemType, std::optional<std::reference_wrapper<Creature>> targetCreature,
                           const glm::vec3 &targetPosition, const SpellSchedule &schedule, SpellSchedule::State state) {
    if (itemType) {
        if (!_item) {
            std::optional<std::reference_wrapper<const Creature>> target;
            if (targetCreature) {
                target = targetCreature->get();
            }
            _item = itemUsePresentation(caster, *itemType, spell, target, targetPosition);
        }
        updateItem(caster, targetCreature, schedule, state);
        return;
    }
    // Nothing shows before the cast starts.
    if (state == SpellSchedule::WaitConjure) {
        return;
    }
    const auto look = castPresentation(spell.castAnimation, caster.modelType() == Creature::ModelType::Creature);
    if (state == SpellSchedule::Conjure || state == SpellSchedule::WaitCast) {
        caster.holdCastAnimation();
    } else if (state == SpellSchedule::Cast || state == SpellSchedule::WaitEffect) {
        caster.holdCastAnimation(look.castLoops ? look.cast : std::string());
    } else if (state == SpellSchedule::Effect && spell.catchTime > 0.0f) {
        // A caught cast plays its catch, and its cast clip stays until the
        // round pause end poses the caster.
        caster.holdCastAnimation(look.castLoops ? look.cast : std::string());
        caster.playFireForgetAnimation("catchsab", AnimationSource {kCatchAnimationId});
    }
    // The conjure and a one-shot cast clip wait in the caster's queue, over
    // the idle the hold gives; a cast loop arriving meanwhile waits for them.
    if (state == SpellSchedule::Conjure && spell.conjTime > 0.0f && !look.conjure.empty()) {
        caster.playFireForgetAnimation(look.conjure, AnimationSource {look.conjureId});
    }
    if (state == SpellSchedule::Cast && !look.castLoops && !look.cast.empty()) {
        caster.playFireForgetAnimation(look.cast, AnimationSource {*look.castId});
    }
    // Without a catch the cast ends in the pose as its time runs out; with one,
    // the round pause end poses the caster.
    if (spell.catchTime <= 0.0f && state == SpellSchedule::Effect && isPoseable(caster)) {
        caster.showPauseReadyAnimation(true);
    }
}

void CastPresenter::updateItem(Creature &user, std::optional<std::reference_wrapper<Creature>> targetCreature,
                               const SpellSchedule &schedule, SpellSchedule::State state) {
    if (state == SpellSchedule::WaitConjure) {
        return;
    }
    const auto &look = *_item;
    const float time = schedule.time();
    if (time < look.end) {
        if (look.onTarget && targetCreature && &targetCreature->get() != &user) {
            // The clip, or the pose, is drawn on the creature it is used on;
            // the user keeps its own.
            auto &target = targetCreature->get();
            if (state == SpellSchedule::Conjure) {
                if (!look.hold) {
                    if (isPoseable(target)) {
                        target.showPauseReadyAnimation(false);
                    }
                } else if (look.loops) {
                    target.playAnimation(look.clip, scene::AnimationProperties::fromFlags(scene::AnimationFlags::loop));
                } else {
                    target.markAnimationChosen();
                    target.playFireForgetAnimation(look.clip, AnimationSource {look.clipId});
                }
            }
        } else if (!look.hold) {
            if (state == SpellSchedule::Conjure && isPoseable(user)) {
                user.showPauseReadyAnimation(false);
            }
        } else {
            if (look.loopAt >= 0.0f && time >= look.loopAt) {
                user.holdCastAnimation("castoutlp1");
            } else {
                user.holdCastAnimation(look.loops ? look.clip : std::string());
            }
            if (state == SpellSchedule::Conjure && !look.loops) {
                user.playFireForgetAnimation(look.clip, AnimationSource {look.clipId});
            }
        }
    }
    if (schedule.reached(look.end) && isPoseable(user)) {
        user.showPauseReadyAnimation(true);
    }
}

// A restored cast shows its loop again at once. Its one-shot shows again only
// for a party member, which comes back without a clip of its own, so the
// one-shot the cast is still writing arrives anew and plays from its start;
// any other creature comes back already showing it and returns to its loop at
// once.
void CastPresenter::restore(Creature &caster, const Spell &spell, const SpellSchedule &schedule) {
    const float time = schedule.time();
    const bool replays = caster.game().party().isMember(caster);
    const auto look = castPresentation(spell.castAnimation, caster.modelType() == Creature::ModelType::Creature);
    const float castEnd = spell.conjTime + spell.castTime;
    if (time < spell.conjTime) {
        caster.holdCastAnimation();
        if (!look.conjure.empty() && replays) {
            caster.playFireForgetAnimation(look.conjure, AnimationSource {look.conjureId});
        }
    } else if (time < castEnd) {
        caster.holdCastAnimation(look.castLoops ? look.cast : std::string());
        if (!look.castLoops && !look.cast.empty() && replays) {
            caster.playFireForgetAnimation(look.cast, AnimationSource {*look.castId});
        }
    } else if (spell.catchTime > 0.0f) {
        // Caught: the cast clip stays until the round pause end poses the caster.
        caster.holdCastAnimation(look.castLoops ? look.cast : std::string());
        if (time < castEnd + spell.catchTime && replays) {
            caster.playFireForgetAnimation("catchsab", AnimationSource {kCatchAnimationId});
        }
    }
}

ProjectilePathType normalizeProjectilePath(ProjectilePathType path) {
    switch (path) {
    case ProjectilePathType::Default:
    case ProjectilePathType::Homing:
    case ProjectilePathType::Ballistic:
    case ProjectilePathType::HighBallistic:
    case ProjectilePathType::Accelerating:
    case ProjectilePathType::Spiral:
    case ProjectilePathType::Linked:
    case ProjectilePathType::Bounce:
    case ProjectilePathType::Burst:
    case ProjectilePathType::Grenade:
        return path;
    default:
        return ProjectilePathType::Default;
    }
}

std::optional<ProjectilePathType> projectilePathFromScript(int value) {
    switch (value) {
    case 0:
        return ProjectilePathType::Default;
    case 1:
        return ProjectilePathType::Homing;
    case 2:
        return ProjectilePathType::Ballistic;
    case 3:
        return ProjectilePathType::HighBallistic;
    case 4:
        return ProjectilePathType::Accelerating;
    default:
        return std::nullopt;
    }
}

std::optional<SpellSelection> scriptCastingSource(const Creature &caster, const Spell &spell) {
    const auto &classes = caster.attributes().classLevels();
    for (size_t index = 0; index < classes.size(); ++index) {
        const ClassType clazz = classes[index].first->type();
        if (!isForceUsingClass(clazz, caster.game().isTSL()) || !caster.hasSpellUsesLeft(spell, index)) {
            continue;
        }
        // A class level of 255 means the class cannot cast the spell.
        static constexpr int kUncastableClassLevel = 0xff;
        const auto required = spell.getClassLevelRequirement(clazz);
        if (required && *required >= 0 && *required != kUncastableClassLevel) {
            return SpellSelection {static_cast<int>(index), kUnspecifiedCasterLevel};
        }
    }
    // Only a ready spell-like ability casts.
    int readyLevel {0};
    if (!caster.readySpellLikeAbility(spell.type, readyLevel)) {
        return std::nullopt;
    }
    return SpellSelection {kSpellLikeAbilityClass, caster.spellLikeAbilityCasterLevel(static_cast<int>(spell.type))};
}

void presentSpellRelease(Object &caster, const Spell &spell) {
    if (spell.castSound) {
        caster.services().audio.mixer.play(spell.castSound, audio::AudioType::Sound, 1.0f, false, caster.position());
    }
    if (auto *creature = dyn_cast<Creature>(&caster)) {
        creature->spellCastVisuals().showCast(*creature, spell);
    }
}

ProjectilePathType effectiveProjectilePath(const Spell &spell, ProjectilePathType path) {
    return path == ProjectilePathType::Default ? spell.projectilePath : normalizeProjectilePath(path);
}
// A high ballistic projectile always flies this long; a spiral one flies this
// much longer than its path would take.
static constexpr uint32_t kHighBallisticMilliseconds = 2000;
static constexpr uint32_t kSpiralExtraMilliseconds = 2500;
static constexpr double kUntimedFlightMilliseconds = 4294967296.0;

uint32_t spellProjectileTimeMilliseconds(
    const Spell &spell, const glm::vec3 &origin, const glm::vec3 &destination,
    ProjectilePathType overridePath, bool tsl) {
    if (!spell.projectile) {
        return 0;
    }
    const float distance = glm::distance(origin, destination);
    // KotOR takes the logarithm in single precision; TSL takes it in double
    // precision and rounds it to single.
    const float logarithm = tsl ? static_cast<float>(std::log(static_cast<double>(distance)))
                                : std::log(distance);
    float speed = logarithm * 3.0f + 2.0f;
    const auto path = effectiveProjectilePath(spell, overridePath);
    if (path == ProjectilePathType::HighBallistic) {
        return kHighBallisticMilliseconds;
    }
    if (path == ProjectilePathType::Homing) {
        speed *= 2.0f;
    } else if (path == ProjectilePathType::Accelerating) {
        speed *= 1.5f;
    } else if (path == ProjectilePathType::Linked) {
        speed = distance * 0.5f;
    } else if (path == ProjectilePathType::Bounce) {
        speed *= 0.4f;
    }
    // A projectile too close to have a speed arrives at once.
    if (speed <= 0.0f) {
        return 1;
    }
    // A flight too long to be timed takes the longest time there is.
    const float travel = distance / speed * 1000.0f;
    auto milliseconds = static_cast<double>(travel) < kUntimedFlightMilliseconds
                            ? static_cast<uint32_t>(travel)
                            : std::numeric_limits<uint32_t>::max();
    // A spiral spell takes longer whatever path it is cast on, except on the
    // paths with a fixed time.
    if (spell.projectilePath == ProjectilePathType::Spiral) {
        milliseconds += kSpiralExtraMilliseconds;
    }
    return milliseconds;
}

bool equipmentAllowsSpell(const Creature &caster, const Spell &spell) {
    const auto mask = caster.forceItemMask();
    return !spell.isForbiddenByEquipment(mask) && spell.hasRequiredEquipment(mask);
}

bool admitSpellCast(const Object &actor, const Spell &spell, bool freeCast, bool itemCast,
                    std::optional<SpellSelection> selection) {
    const auto reject = [&](const char *reason) {
        debug(str(boost::format("Spell rejected: actor=%u spell=%d reason=%s") % actor.id() % static_cast<int>(spell.type) % reason), LogChannel::Combat);
        return false;
    };
    if (actor.isDead()) {
        return reject("dead caster");
    }
    const auto *creature = dyn_cast<Creature>(&actor);
    if (!creature) {
        return isa<Placeable>(&actor) || reject("unsupported caster");
    }
    if (!creature->canCastSpells()) {
        return reject("caster cannot act");
    }
    if (!itemCast && !equipmentAllowsSpell(*creature, spell)) {
        return reject("equipment disallows power");
    }
    if (freeCast) {
        return true;
    }
    if (selection) {
        switch (selection->sourceKind()) {
        case CastingSourceKind::UnselectedClass:
            return true;
        case CastingSourceKind::Class:
            return creature->hasSpellUsesLeft(spell, selection->classIndex) || reject("insufficient spell uses");
        case CastingSourceKind::SpellLikeAbility: {
            int level = 0;
            return creature->readySpellLikeAbility(spell.type, level) || reject("ability not ready");
        }
        }
    }
    if (creature->spellCasterLevel(spell) >= 0 && creature->attributes().hasSpell(spell.type)) {
        return creature->canPaySpellForcePointCost(spell) || reject("insufficient resources");
    }
    int level = 0;
    return creature->readySpellLikeAbility(spell.type, level) || reject("power not available to caster");
}

float spellRange(const Object &actor, const Spell &spell, std::optional<std::reference_wrapper<const Object>> target) {
    // The personal space of a creature caster and a creature target, less a
    // tenth of a metre each, lengthens the range.
    float range = spell.range;
    if (const auto *creature = dyn_cast<Creature>(&actor)) {
        range += creature->creaturePersonalSpace() - 0.1f;
    }
    if (const auto creature = target ? Creature::asCreature(target->get()) : std::nullopt) {
        range += creature->get().creaturePersonalSpace() - 0.1f;
    }
    return std::max(0.0f, range);
}

bool withinSpellRange(const Object &actor, const Spell &spell, const glm::vec3 &position, std::optional<std::reference_wrapper<const Object>> target) {
    return (target && &target->get() == &actor) || glm::distance(actor.position(), position) <= spellRange(actor, spell, target);
}

// A cast starts from its spell with no metamagic (these titles have none),
// no cost and no selected class.
static void beginSpellCastContext(SpellCastContext &context, const Spell &spell) {
    context.spellId = static_cast<int>(spell.type);
    context.metaMagic = 0;
    context.forcePointCost = 0;
    context.castingClass = kUnselectedCastingClass;
}

bool commitSpellCast(Object &actor, const Spell &spell, bool freeCast, bool instant,
                     SpellCastContext &context, std::optional<SpellSelection> selection) {
    beginSpellCastContext(context, spell);
    if (auto *creature = dyn_cast<Creature>(&actor)) {
        context.casterLevel = creature->spellCasterLevel(spell, freeCast);
        if (instant) {
            // An instant cast pays nothing and uses up nothing; its caster
            // keeps the casting class it last cast with.
            context.castingClass = creature->spellCastContext().castingClass;
        } else if (!freeCast && selection) {
            context.castingClass = selection->classIndex;
            switch (selection->sourceKind()) {
            case CastingSourceKind::SpellLikeAbility:
                creature->consumeSpellLikeAbility(spell.type, selection->casterLevel);
                context.casterLevel = creature->spellLikeAbilityCasterLevel(static_cast<int>(spell.type));
                break;
            case CastingSourceKind::UnselectedClass:
                // A cast with no class pays nothing.
                context.casterLevel = spell.innateCasterLevel();
                break;
            case CastingSourceKind::Class:
                creature->paySpellForcePointCost(spell);
                context.casterLevel = creature->adjustedClassLevel(selection->classIndex);
                break;
            }
            if (selection->hasExplicitLevel()) {
                context.casterLevel = selection->casterLevel;
            }
        } else if (!freeCast) {
            const bool knownForcePower = context.casterLevel >= 0 &&
                                         creature->attributes().hasSpell(spell.type);
            if (knownForcePower) {
                creature->paySpellForcePointCost(spell);
                context.castingClass = creature->spellCastingClass();
            } else {
                if (!creature->readySpellLikeAbility(spell.type, context.casterLevel)) {
                    return false;
                }
                creature->consumeSpellLikeAbility(spell.type, context.casterLevel);
                context.castingClass = kSpellLikeAbilityClass;
            }
        }
        // Every creature cast records what its charge spends in Force points,
        // whether or not it was paid.
        context.forcePointCost = creature->recordedSpellForcePointCost(spell);
    } else if (isa<Placeable>(&actor)) {
        context.casterLevel = spell.innateCasterLevel();
    } else {
        return false;
    }
    actor.setSpellCastContext(context);
    debug(str(boost::format("Spell committed: actor=%u spell=%d level=%d cost=%d") % actor.id() % context.spellId % context.casterLevel % context.forcePointCost), LogChannel::Combat);
    return true;
}

void useItemAtOnce(Game &game, Creature &user, const std::shared_ptr<Item> &item) {
    // The user becomes its own spell target.
    user.spellScriptContext().setActiveTarget(game.getObjectById(user.id()));
    const auto &properties = item->properties();
    // Immediate use takes the first cast-spell property, whether or not an
    // upgrade gates it.
    const auto property = std::find_if(properties.begin(), properties.end(), [](const Item::PropertyEntry &entry) {
        return entry.type() == ItemProperty::ActivateItem;
    });
    if (property == properties.end()) {
        return;
    }
    const size_t index = static_cast<size_t>(property - properties.begin());
    // The spell is aimed at the user and the origin, and takes effect after
    // the user's last measured spell flight. The use leaves the user's casting
    // class and level override as they were.
    if (auto spell = user.services().game.spells.get(static_cast<SpellType>(property->subtype))) {
        SpellCastContext context;
        beginSpellCastContext(context, *spell);
        context.castingClass = user.spellCastContext().castingClass;
        context.casterLevel = user.spellCasterLevel(*spell, true);
        context.forcePointCost = user.recordedSpellForcePointCost(*spell);
        user.setSpellCastContext(context);
        queueSpellImpact(game, *spell, user, game.getObjectById(user.id()), glm::vec3(0.0f),
                         context, item, user.lastSpellProjectileMilliseconds());
        // The impact goes out with the same release presentation as a cast.
        presentSpellRelease(user, *spell);
    }
    if (item->consumeSpellUse(index)) {
        game.postObjectDestruction(*item, 0.0f);
    }
    if (user.isInCombat()) {
        user.startItemUseCooldown();
    }
    game.breakForfeitCondition(user, Party::kForfeitNoItems);
}

glm::vec3 menuItemLocation(const Object &user, const Object &target) {
    return &target == &user ? glm::vec3(0.0f) : target.position();
}

bool spellTargetLost(const Object &caster, const Object &target) {
    const auto *creature = isa<Creature>(&caster) ? dyn_cast<Creature>(&target) : nullptr;
    return creature && (creature->isDead() || creature->isTemporarilyDead());
}

bool itemUseEndsWithItem(int itemType) {
    return itemType != Item::kDroidUtilityItemType;
}

// Reported when an item has no use left as its use would take effect.
static constexpr int kItemNoUseLeftStrRef = 1434;

void presentFailedItemUse(Game &game, Object &user, bool noUseLeft) {
    auto *creature = dyn_cast<Creature>(&user);
    if (!creature) {
        return;
    }
    if (!creature->isDead() && !creature->isTemporarilyDead()) {
        creature->showPauseReadyAnimation(false);
    }
    if (noUseLeft && game.party().getLeader().get() == creature) {
        game.addFeedbackMessage(kItemNoUseLeftStrRef);
    }
}

bool queueSpellImpact(Game &game, const Spell &spell, Object &caster,
                      const std::shared_ptr<Object> &target, const glm::vec3 &position,
                      const SpellCastContext &context, const std::shared_ptr<Object> &item, uint32_t delayMilliseconds) {
    const auto module = game.module();
    if (!caster.isRuntimeLive()) {
        warn("Spell impact rejected: caster no longer active");
        return false;
    }
    SavedEventRecord event;
    const uint64_t when = game.worldTimeMilliseconds() + delayMilliseconds;
    event.day = static_cast<uint32_t>(when / game.millisecondsPerWorldDay());
    event.time = static_cast<uint32_t>(when % game.millisecondsPerWorldDay());
    event.object = SavedObjectReference::fromRuntimeId(caster.id());
    event.caller = event.object;
    event.eventId = static_cast<uint32_t>(SavedEventType::SpellImpact);
    SavedSpellImpact impact;
    impact.spellId = static_cast<int>(spell.type);
    impact.caster = event.object;
    impact.target = SavedObjectReference::fromRuntimeId(target ? target->id() : script::kObjectInvalid);
    impact.area = SavedObjectReference::fromRuntimeId(module->area()->id());
    impact.item = SavedObjectReference::fromRuntimeId(item ? item->id() : script::kObjectInvalid);
    impact.script = spell.impactScript;
    impact.targetPosition = position;
    impact.finalForceCost = context.forcePointCost;
    event.payload = std::move(impact);
    const bool bound = event.bindObjectReferences(game);
    if (!bound) {
        warn("Spell impact rejected: object reference could not be resolved");
        return false;
    }
    debug(str(boost::format("Spell queued: actor=%u spell=%d delay=%u script=%s") % caster.id() % context.spellId % delayMilliseconds % spell.impactScript), LogChannel::Combat);
    module->enqueueBoundSaveEvent(std::move(event), true);
    return true;
}

void runSpellImpact(Game &game, const Spell &spell, Object &caster,
                    const std::shared_ptr<Object> &target, const std::shared_ptr<Location> &location,
                    std::optional<SpellCastContext> context, const std::shared_ptr<Object> &item,
                    std::optional<int> levelOverride) {
    SpellCastContext cast = context ? *context : caster.spellCastContext();
    cast.spellId = static_cast<int>(spell.type);
    std::optional<std::reference_wrapper<const Location>> impactLocation;
    if (location) {
        impactLocation = *location;
    }
    caster.spellScriptContext().setImpact(cast,
                                          target ? game.getObjectById(target->id()) : nullptr,
                                          item ? game.getObjectById(item->id()) : nullptr, impactLocation);
    // A creature's level override lasts for the impact alone, whether or not
    // the spell has an impact script.
    if (levelOverride && isa<Creature>(&caster)) {
        caster.spellScriptContext().levelOverride = *levelOverride;
    }
    struct ClearLevelOverride {
        Object &caster;
        ~ClearLevelOverride() { caster.spellScriptContext().levelOverride.reset(); }
    } clearLevelOverride {caster};
    if (spell.impactScript.empty()) {
        warn("Spell impact has no script: " + std::to_string(static_cast<int>(spell.type)));
        return;
    }
    std::vector<script::Argument> args {
        {script::ArgKind::Caller, script::Variable::ofObject(caster.id())},
    };
    // The caster knows the spell whose impact it is running while the impact
    // script runs.
    struct RestoreSpell {
        Object &object;
        SpellType previous;
        ~RestoreSpell() { object.setSpellCast(previous); }
    } restore {caster, caster.spellCast()};
    caster.setSpellCast(spell.type);
    // The effects the caster makes carry the spell while its impact script
    // runs, and no longer.
    struct ClearEffectSpell {
        Object &caster;
        ~ClearEffectSpell() { caster.setEffectSpellId(kNoEffectSpell); }
    } clearEffectSpell {caster};
    caster.setEffectSpellId(static_cast<uint32_t>(spell.type));
    debug(str(boost::format("Spell impact: actor=%u spell=%d target=%u script=%s") % caster.id() % static_cast<int>(spell.type) % (target ? target->id() : script::kObjectInvalid) % spell.impactScript), LogChannel::Combat);
    const int result = game.scriptRunner().run(spell.impactScript, args);
    debug(str(boost::format("Spell script returned: actor=%u spell=%d result=%d") % caster.id() % static_cast<int>(spell.type) % result), LogChannel::Combat);
}

} // namespace game

} // namespace reone
