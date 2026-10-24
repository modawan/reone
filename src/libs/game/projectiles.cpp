/*
 * Copyright (c) 2025 The reone project contributors
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

#include "reone/game/projectiles.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <boost/algorithm/string/case_conv.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/quaternion.hpp>

#include "reone/audio/mixer.h"
#include "reone/game/castspell.h"
#include "reone/game/combattables.h"
#include "reone/game/d20/spells.h"
#include "reone/game/di/services.h"
#include "reone/game/effect/creaturestate.h"
#include "reone/game/effect/damage.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/twodautil.h"
#include "reone/graphics/model.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/models.h"
#include "reone/scene/collision.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/model.h"
#include "reone/system/exception/validation.h"
#include "reone/system/randomutil.h"

using namespace reone::resource;

namespace reone {

namespace game {

// The advanced throw (a mode other than 0) times its flight by its own spell.
static SpellType lightsaberThrowSpell(int mode) {
    return mode != 0 ? SpellType::LightSaberThrowAdvanced : SpellType::LightSaberThrow;
}
// A thrown lightsaber deals a d6 for every two levels of its thrower.
static int lightsaberThrowDice(int level) {
    return level / 2;
}

// The visual effect each target of a thrown lightsaber shows as it is struck.
static constexpr int kLightsaberThrowHitVisual = 6001;
// A projectile with no node to follow aims this high above an object's feet.
static constexpr float kImpactHeight = 1.25f;
// A spell projectile with no spawn node leaves this high above its caster's feet.
static constexpr float kSpellSourceHeight = 1.0f;
// The thrown lightsaber's model is tilted this far from its thrower's facing, in radians.
static constexpr float kThrowElevation = 1.22173f;
// The creature appearance whose discharge rows carry its own prefix.
static constexpr int kPrefixedDischargeAppearance = 3;

static constexpr char kModelEventDetonate[] = "detonate";

// The height a high ballistic projectile rises to above its origin and target.
static constexpr float kHighBallisticHeight = 30.0f;
// A spiral swings this far out to the side, and closes over its last seconds.
static constexpr float kSpiralRadius = 2.0f;
static constexpr float kSpiralCloseSeconds = 2.5f;

struct Projectiles::ActiveProjectile {
    SavedProjectile saved;
    std::shared_ptr<scene::ModelSceneNode> model;
    std::shared_ptr<scene::ModelSceneNode> flash;
    ~ActiveProjectile() {
        if (model) {
            model->graph().removeRoot(*model);
        }
        if (flash) {
            flash->graph().removeRoot(*flash);
        }
    }
};

static SavedObjectReference referenceTo(std::optional<std::reference_wrapper<const Object>> object, const Game &game) {
    auto result = SavedObjectReference::fromRuntimeId(object ? object->get().id() : script::kObjectInvalid);
    game.bindSavedObjectReference(result);
    return result;
}
static glm::vec3 attachmentPosition(const SavedObjectReference &reference, const std::string &hook,
                                    const glm::vec3 &previous) {
    auto object = reference.boundObject();
    if (!object) {
        return previous;
    }
    auto model = std::dynamic_pointer_cast<scene::ModelSceneNode>(object->sceneNode());
    if (model)
        if (auto *node = model->getNodeByName(hook)) {
            return node->origin();
        }
    return object->position() + (hook == "impact" ? glm::vec3(0.0f, 0.0f, kImpactHeight) : glm::vec3(0.0f));
}
// A spell projectile leaves its caster's spawn node, or a metre above its feet
// when the model has no such node.
static glm::vec3 spellSourcePosition(const SavedObjectReference &reference, const std::string &hook,
                                     const glm::vec3 &previous) {
    auto object = reference.boundObject();
    if (!object) {
        return previous;
    }
    auto model = std::dynamic_pointer_cast<scene::ModelSceneNode>(object->sceneNode());
    if (model)
        if (auto *node = model->getNodeByName(hook)) {
            return node->origin();
        }
    return object->position() + glm::vec3(0.0f, 0.0f, kSpellSourceHeight);
}

// The node a spell's projectile spawn point names.
static std::string spellSpawnNode(const std::string &spawnPoint) {
    if (spawnPoint == "hand") {
        return "handconjure";
    }
    if (spawnPoint == "head") {
        return "headconjure";
    }
    if (spawnPoint == "throw") {
        return "rhand";
    }
    return "root";
}

static glm::vec3 throwTargetPosition(const SavedObjectReference &target,
                                     const SavedObjectReference &caster,
                                     const glm::vec3 &previous) {
    auto object = target.boundObject();
    if (!object) {
        return previous;
    }
    if (object == caster.boundObject()) {
        auto model = std::dynamic_pointer_cast<scene::ModelSceneNode>(object->sceneNode());
        if (model)
            if (auto *hand = model->getNodeByName("rhand")) {
                return hand->origin();
            }
    }
    return object->position() + glm::vec3(0.0f, 0.0f, kImpactHeight);
}

static glm::vec3 safeProjectileTargetPosition(const SavedObjectReference &reference,
                                              const std::string &hook, const glm::vec3 &previous) {
    auto object = reference.boundObject();
    if (!object) {
        return previous;
    }
    auto model = std::dynamic_pointer_cast<scene::ModelSceneNode>(object->sceneNode());
    if (model && !hook.empty())
        if (auto *node = model->getNodeByName(hook)) {
            return node->origin();
        }
    return object->position() + glm::vec3(0.0f, 0.0f, kImpactHeight);
}

static glm::vec3 safeLegDestination(const SavedProjectile &, const SavedProjectile::Leg &);

static glm::quat throwOrientation(const Object &source) {
    const float facing = source.getFacing();
    const glm::vec3 forward {-glm::sin(facing), glm::cos(facing), 0.0f};
    const glm::vec3 axis = glm::normalize(glm::cross(forward, glm::vec3(0, 0, 1)));
    const glm::vec3 thrown = glm::angleAxis(kThrowElevation, axis) * forward;
    return glm::rotation(glm::vec3(0, 1, 0), glm::normalize(thrown));
}

int rangedAttackAnimation(int attackType, CreatureWieldType wield, bool creatureModel) {
    // Rows per wield: blaster pistol, dual pistols, blaster rifle, none, heavy weapon.
    static constexpr int basic[] {217, 231, 239, 0, 352};
    static constexpr int rapid[] {218, 232, 240, 0, 353};
    static constexpr int sniper[] {219, 233, 241, 0, 354};
    static constexpr int power[] {362, 363, 364, 0, 365};
    const int *rows = basic;
    int creatureRow = 288;
    switch (static_cast<FeatType>(attackType)) {
    case FeatType::MultiShot:
    case FeatType::RapidShot:
    case FeatType::ImprovedRapidShot:
        rows = rapid;
        creatureRow = 289;
        break;
    case FeatType::ImprovedSniperShot:
    case FeatType::SniperShot:
    case FeatType::MasterSniperShot:
        rows = sniper;
        creatureRow = 351;
        break;
    case FeatType::ImprovedPowerBlast:
    case FeatType::PowerBlast:
    case FeatType::MasterPowerBlast:
        rows = power;
        creatureRow = 361;
        break;
    default:
        break;
    }
    if (creatureModel) {
        return creatureRow;
    }
    const int index = static_cast<int>(wield) - static_cast<int>(CreatureWieldType::BlasterPistol);
    return index >= 0 && index < 5 ? rows[index] : 0;
}

void Projectiles::init() {
    const auto weapons = getRequiredTwoDA(_twoDas, "weapondischarge");
    for (int row = 0; row < weapons->getRowCount(); ++row) {
        Discharge discharge;
        discharge.droid = weapons->getInt(row, "droid") != 0;
        const int shots = weapons->getInt(row, "shots");
        if (shots > 0) {
            ProjectileSpec spec;
            spec.hits = weapons->getInt(row, "hits");
            // A short or empty switch mask leaves the remaining discharges on the right hand.
            const std::string mask = weapons->getString(row, "switchmask");
            spec.shots.reserve(shots);
            for (int i = 0; i < shots; ++i) {
                ProjectileSpec::Shot shot;
                shot.timeMilliseconds = weapons->getInt(row, "shot" + std::to_string(i + 1));
                const char digit = static_cast<size_t>(i) < mask.size() ? mask[i] : '0';
                shot.hand = digit >= '0' && digit <= '9' ? digit - '0' : 0;
                spec.shots.push_back(shot);
            }
            discharge.spec = std::move(spec);
        }
        // The first row of a label is the one found.
        _discharges.emplace(boost::to_lower_copy(weapons->rows()[row].label), std::move(discharge));
    }

    const auto droids = getRequiredTwoDA(_twoDas, "droiddischarge");
    for (int row = 0; row < droids->getRowCount(); ++row) {
        auto &prefixes = _droidPrefixes[boost::to_lower_copy(droids->rows()[row].label)];
        for (const auto &column : droids->columns()) {
            prefixes.emplace(boost::to_lower_copy(column), droids->getString(row, column));
        }
    }
    auto grenadeSounds = getRequiredTwoDA(_twoDas, "grenadesnd");
    for (int row = 0; row < grenadeSounds->getRowCount(); ++row) {
        _grenadeSounds.push_back(boost::to_lower_copy(grenadeSounds->getString(row, "sound")));
    }
}

std::optional<ProjectileSpec> Projectiles::discharge(int animation, const Creature &attacker) const {
    const std::string animationLabel = std::to_string(animation);
    std::string label = animationLabel;
    const auto animationRow = _discharges.find(animationLabel);
    if (animationRow != _discharges.end() && animationRow->second.droid) {
        // Droid rows are selected by the appearance race and prefixed with its
        // droiddischarge cell. An empty cell selects no row.
        std::string prefix;
        const auto race = _droidPrefixes.find(boost::to_lower_copy(attacker.appearanceRace()));
        if (race != _droidPrefixes.end()) {
            const auto found = race->second.find(animationLabel);
            if (found != race->second.end()) {
                prefix = found->second;
            }
        }
        label = prefix + "_" + animationLabel;
    } else if (attacker.appearance() == kPrefixedDischargeAppearance) {
        label = std::to_string(kPrefixedDischargeAppearance) + "_" + animationLabel;
    }
    const auto row = _discharges.find(boost::to_lower_copy(label));
    return row != _discharges.end() ? row->second.spec : std::nullopt;
}

void Projectiles::clear() {
    retireAreaRuntime();
}

void Projectiles::launchLightsaberThrow(Creature &caster, const EffectInstance &owner,
                                        Game &game, ServicesView &services) {
    auto weapon = caster.getEquippedItem(InventorySlots::rightWeapon);
    if (!weapon) {
        return;
    }
    auto casterObject = game.getObjectById(caster.id());
    std::vector<std::shared_ptr<Object>> objects {casterObject};
    for (size_t index = 0; index < 3; ++index) {
        auto target = owner.boundObjectParameter(index);
        if (!target) {
            break;
        }
        objects.push_back(std::move(target));
    }
    if (objects.size() == 1) {
        return;
    }
    objects.push_back(casterObject);
    auto spell = services.game.spells.get(lightsaberThrowSpell(owner.integerParameter(0)));
    if (!spell || spell->castTime <= 0.0f) {
        throw ValidationException("spells.2da: invalid Lightsaber Throw cast time");
    }
    float totalDistance = 0.0f;
    for (size_t index = 1; index < objects.size(); ++index) {
        totalDistance += glm::distance(objects[index - 1]->position(), objects[index]->position());
    }
    if (totalDistance <= 0.0f) {
        return;
    }
    const float rate = totalDistance / (spell->castTime * 1000.0f) * 2000.0f;
    std::string modelName = caster.getWeaponModelName(InventorySlots::rightWeapon);
    auto body = std::dynamic_pointer_cast<scene::ModelSceneNode>(caster.sceneNode());
    if (body)
        if (auto *model = dynamic_cast<scene::ModelSceneNode *>(body->getAttachment("rhand"))) {
            modelName = model->model().name();
        }
    const int dice = lightsaberThrowDice(caster.getSpellLevel(false));
    uint32_t deadline = 0;
    for (size_t index = 1; index < objects.size(); ++index) {
        auto source = objects[index - 1];
        auto target = objects[index];
        const int milliseconds = static_cast<int>(glm::distance(source->position(), target->position()) / rate * 2000.0f);
        auto sourceRef = referenceTo(*source, game);
        auto targetRef = referenceTo(*target, game);
        auto active = std::make_shared<ActiveProjectile>();
        auto &route = active->saved;
        route.id = _nextPresentationId++;
        route.kind = ProjectileKind::LightsaberRoute;
        route.caster = referenceTo(caster, game);
        route.weapon = referenceTo(*weapon, game);
        route.spellId = static_cast<int>(spell->type);
        route.path = 1;
        route.model = modelName;
        route.travelRate = rate;
        route.activationDelay = deadline / 1000.0f;
        route.released = deadline == 0;
        route.legs.push_back({sourceRef, targetRef,
                              attachmentPosition(sourceRef, source == casterObject ? "rhand" : "impact", source->position()),
                              throwTargetPosition(targetRef, route.caster, target->position()),
                              milliseconds / 1000.0f});
        _active.push_back(active);
        if (route.released) {
            startLeg(*active, game, services);
        }
        // Each leg starts on its scheduled boundary. Moving targets may change
        // its visual duration, but never reschedule an already queued impact.
        deadline += milliseconds;
        if (target != casterObject) {
            DamagePacket packet;
            int amount = 0;
            for (int die = 0; die < dice; ++die) {
                amount += randomInt(1, 6);
            }
            packet.add(amount, DamageType::Blaster);
            packet.setDamageFlags(static_cast<int>(DamageType::Blaster));
            packet.resolveLightsaberThrow(*target, caster);
            showMitigationFeedback(*target, game.getObjectById<Creature>(caster.id()), packet.resolution());
            DamageEffect::ApplicationContext context;
            context.damageAmounts[12] = packet.resolvedDamage();
            context.damageAmounts[14] = packet.resolvedDamage();
            context.suppressDamageShields = true;
            auto damage = owner.linkedChild(std::make_shared<DamageEffect>(std::move(packet), context));
            damage.creator = casterObject;
            damage.creatorId = caster.id();
            damage.setIntegerParameter(DamageEffect::kReactionDelay, 0);
            damage.setDuration(DurationType::Instant, 0.0f);
            game.queueEffectApplication(*target, std::move(damage), deadline);
            auto visual = owner.linkedChild(std::make_shared<VisualEffectMarkerEffect>(kLightsaberThrowHitVisual));
            visual.objectParameters[0] = caster.id();
            visual.objectParameterObjects[0] = casterObject;
            visual.setDuration(DurationType::Instant, 0.0f);
            game.queueEffectApplication(*target, std::move(visual), deadline);
        }
    }
}

void Projectiles::attachPresentation(ActiveProjectile &active, Game &game, ServicesView &services, bool restoring) {
    auto &state = active.saved;
    auto caster = state.caster.boundObject();
    auto body = caster ? std::dynamic_pointer_cast<scene::ModelSceneNode>(caster->sceneNode()) : nullptr;
    if ((state.kind == ProjectileKind::LightsaberRoute && !state.released) || active.model || !body || state.model.empty()) {
        return;
    }
    auto resource = services.resource.models.get(state.model);
    if (!resource) {
        return;
    }
    active.model = body->graph().newModel(*resource, scene::ModelUsage::Projectile);
    active.model->setLocalTransform(glm::translate(state.position) * glm::mat4_cast(state.orientation));
    if (state.kind == ProjectileKind::LightsaberRoute) {
        active.model->playAnimation("throwout");
        active.model->setAnimationTime(state.elapsed);
    } else if (!restoring) {
        active.model->signalEvent(kModelEventDetonate);
    }
    body->graph().addRoot(active.model);
}

static void initializeMotion(SavedProjectile &s, bool &burstWentLeft) {
    auto &leg = s.legs[s.leg];
    s.acceleration = glm::vec3(0.0f);
    if (leg.duration <= 0.0f) {
        s.velocity = glm::vec3(0.0f);
        return;
    }
    const glm::vec3 delta = leg.destination - s.position;
    const float time = leg.duration;
    if (leg.motion == ProjectileMotion::Burst) {
        // A burst sets out up to 90 degrees to one side of its heading (the
        // other side from the last burst), rising or dipping, at a random
        // speed; its acceleration brings it to the target on time. A spell
        // projectile's heading is the world's +Y.
        const float offset = glm::radians(randomInt(0, 899) / 10.0f);
        const float angle = glm::half_pi<float>() + (burstWentLeft ? -offset : offset);
        const bool rises = randomInt(0, 1) != 0;
        const float rise = rises ? randomInt(0, 99) / 100.0f : -randomInt(0, 49) / 100.0f;
        burstWentLeft = !burstWentLeft;
        const glm::vec3 direction = glm::normalize(glm::vec3(glm::cos(angle), glm::sin(angle), rise));
        const int spread = std::abs(static_cast<int>(std::log(static_cast<double>(glm::length(delta))) * 5.0)) + 1;
        s.velocity = (static_cast<float>(randomInt(0, spread - 1)) + 3.0f) * direction;
        s.acceleration = 2.0f * (delta - s.velocity * time) / (time * time);
        return;
    }
    if (leg.motion == ProjectileMotion::Ballistic) {
        s.acceleration.z = -9.81f;
    } else if (leg.motion == ProjectileMotion::Accelerating) {
        s.acceleration = 2.0f * delta / (time * time);
    } else if (leg.motion == ProjectileMotion::SpiralOpen) {
        glm::vec3 arrival {delta.x, delta.y, 0.0f};
        if (glm::length2(arrival) > 0.0f) {
            arrival = glm::normalize(arrival) * 7.5f;
        }
        // Only the horizontal speed changes on the way: the height changes at
        // a steady speed.
        s.velocity = 2.0f * delta / time - arrival;
        s.acceleration = (arrival - s.velocity) / time;
        s.acceleration.z = 0.0f;
        return;
    }
    s.velocity = delta / time - 0.5f * s.acceleration * time;
}

void Projectiles::startLeg(ActiveProjectile &active, Game &game, ServicesView &services, bool restoring) {
    auto &s = active.saved;
    if (s.leg >= s.legs.size()) {
        return;
    }
    auto &leg = s.legs[s.leg];
    if (!restoring) {
        s.elapsed = 0.0f;
        if (s.kind == ProjectileKind::LightsaberRoute) {
            leg.origin = attachmentPosition(leg.source,
                                            leg.source.boundObject() == s.caster.boundObject() ? "rhand" : "impact", leg.origin);
            leg.destination = throwTargetPosition(leg.target, s.caster, leg.destination);
            s.position = leg.origin;
            auto source = leg.source.boundObject();
            auto target = leg.target.boundObject();
            if (s.travelRate > 0.0f) {
                const auto from = source ? source->position() : leg.origin;
                const auto to = target ? target->position() : leg.destination;
                leg.duration = static_cast<int>(glm::distance(from, to) / s.travelRate * 2000.0f) / 1000.0f;
            }
            if (source) {
                s.orientation = throwOrientation(*source);
            }
            if (active.model) {
                active.model->graph().removeRoot(*active.model);
                active.model.reset();
            }
        } else {
            leg.origin = s.position;
            if (s.kind == ProjectileKind::AttackBolt) {
                leg.destination = safeLegDestination(s, leg);
            }
            // A grenade is silent as it is thrown. Each bounce plays, where it
            // happens, the grenadesnd.2da sound of the surface the grenade
            // lands on next. The row is that surface's bit (1 << surface), not
            // its number; a row past the table or with no sound plays nothing.
            if (s.leg > 0 && leg.surface) {
                const auto row = static_cast<size_t>(1) << *leg.surface;
                if (row < _grenadeSounds.size() && !_grenadeSounds[row].empty()) {
                    if (auto clip = services.resource.audioClips.get(_grenadeSounds[row])) {
                        services.audio.mixer.play(std::move(clip), audio::AudioType::Sound, 1.0f, false, s.position);
                    }
                }
            }
        }
        initializeMotion(s, _burstWentLeft);
    }
    attachPresentation(active, game, services, restoring);
}

uint64_t Projectiles::beginSpell(Object &caster, std::optional<std::reference_wrapper<const Object>> target,
                                 const glm::vec3 &position, const Spell &spell, ProjectilePathType path,
                                 Game &game, ServicesView &services) {
    const auto selectedPath = effectiveProjectilePath(spell, path);
    if (!spell.projectile || !spell.projModel) {
        return 0;
    }
    auto active = std::make_shared<ActiveProjectile>();
    auto &s = active->saved;
    s.id = _nextPresentationId++;
    s.kind = ProjectileKind::Spell;
    s.spellId = static_cast<int>(spell.type);
    s.caster = referenceTo(caster, game);
    s.path = static_cast<int>(selectedPath);
    s.model = spell.projModel->name();
    s.sourceHook = spellSpawnNode(spell.projectileSpawn);
    s.orientationMode = spell.projectileOrientation == "target" ? 1 : spell.projectileOrientation == "path" ? 2
                                                                                                            : 0;
    s.orientation = glm::angleAxis(caster.getFacing(), glm::vec3(0, 0, 1));
    s.position = spellSourcePosition(s.caster, s.sourceHook, caster.position());
    s.clockwise = selectedPath == ProjectilePathType::Spiral && randomInt(0, 1) != 0;
    s.legs.push_back({s.caster, referenceTo(target, game), s.position, position, 0.0f});
    attachPresentation(*active, game, services, false);
    _active.push_back(active);
    return s.id;
}

void Projectiles::releaseSpell(uint64_t id, float duration, Game &game, ServicesView &services) {
    for (auto &active : _active) {
        auto &s = active->saved;
        if (s.id != id || s.released || s.kind != ProjectileKind::Spell) {
            continue;
        }
        s.released = true;
        s.elapsed = 0.0f;
        const auto releaseTarget = s.legs.front().target;
        const auto origin = spellSourcePosition(s.caster, s.sourceHook, s.position);
        const auto destination = attachmentPosition(releaseTarget, "impact", s.legs.front().destination);
        // Only a spell of item-targeting kind 3 follows its target; any other
        // flies to where the target was at the release.
        const auto spell = services.game.spells.get(static_cast<SpellType>(s.spellId));
        const auto target = spell && spell->itemTargeting == 3 ? releaseTarget : SavedObjectReference {};
        s.position = origin;
        s.legs.clear();
        s.leg = 0;
        duration = std::max(duration, 0.0f);
        auto append = [&](const glm::vec3 &end, float seconds, ProjectileMotion motion,
                          SavedObjectReference follow = {}, glm::vec3 offset = glm::vec3(0)) {
            const auto start = s.legs.empty() ? origin : s.legs.back().destination;
            s.legs.push_back({{}, std::move(follow), start, end, seconds, motion, offset});
        };
        auto &area = *game.module()->area();
        // Drops a point onto the room floor beneath it and gives that floor's
        // material. Once a point of this throw has found a floor, a later point
        // with none beneath it takes the height and material of the last floor
        // found.
        std::optional<scene::Collision> lastGround;
        auto ground = [&](glm::vec3 &point) -> std::optional<int> {
            scene::Collision collision;
            if (area.testRoomSurface(glm::vec2(point), collision)) {
                lastGround = collision;
            }
            if (!lastGround) {
                return std::nullopt;
            }
            point.z = lastGround->intersection.z;
            return lastGround->material;
        };
        switch (static_cast<ProjectilePathType>(s.path)) {
        case ProjectilePathType::HighBallistic: {
            const int halfMs = static_cast<int>(duration * 1000.0f) / 2;
            const glm::vec3 rise {0.0f, 0.0f, kHighBallisticHeight};
            append(origin + rise, halfMs / 1000.0f, ProjectileMotion::Homing);
            append(destination + rise, 0.0f, ProjectileMotion::Jump, target, rise);
            append(destination, duration - halfMs / 1000.0f, ProjectileMotion::Homing, target);
            break;
        }
        case ProjectilePathType::Spiral: {
            const auto delta = destination - origin;
            glm::vec3 side {-delta.y, delta.x, 0};
            if (!s.clockwise) {
                side = -side;
            }
            if (glm::length2(side) < 0.00001f) {
                side = glm::vec3(0, 1, 0);
            }
            side = glm::normalize(side) * kSpiralRadius;
            append(destination + side, std::max(0.0f, duration - kSpiralCloseSeconds), ProjectileMotion::SpiralOpen, target, side);
            append(destination, std::min(duration, kSpiralCloseSeconds), ProjectileMotion::SpiralClose, target);
            break;
        }
        case ProjectilePathType::Linked:
            append(destination, duration, ProjectileMotion::Linked, target);
            break;
        case ProjectilePathType::Burst:
            append(destination, duration, ProjectileMotion::Burst, target);
            break;
        case ProjectilePathType::Bounce: {
            const auto delta = destination - origin;
            float lateFraction = 0.25f;
            glm::vec3 late;
            do {
                lateFraction -= 0.05f;
                late = destination - delta * lateFraction;
                if (!ground(late)) {
                    late.z = (origin.z + destination.z) * 0.5f;
                }
            } while (lateFraction > 0.00001f && destination.z - late.z >= 1.0f);
            if (lateFraction <= 0.00001f) {
                append(destination, duration, ProjectileMotion::Ballistic);
                break;
            }
            float earlyFraction = 0.55f;
            glm::vec3 early;
            do {
                earlyFraction -= 0.05f;
                early = destination - delta * earlyFraction;
                if (!ground(early)) {
                    early.z = (origin.z + destination.z) * 0.5f;
                }
            } while (earlyFraction > lateFraction && late.z - early.z >= 0.5f);
            const int total = static_cast<int>(duration * 1000.0f);
            if (earlyFraction > lateFraction) {
                const int last = total * 2 / 10, middle = total * 3 / 10;
                append(early, (total - last - middle) / 1000.0f, ProjectileMotion::Ballistic);
                append(late, middle / 1000.0f, ProjectileMotion::Ballistic);
                append(destination, last / 1000.0f, ProjectileMotion::Ballistic);
            } else {
                const int last = total * 3 / 10;
                append(late, (total - last) / 1000.0f, ProjectileMotion::Ballistic);
                append(destination, last / 1000.0f, ProjectileMotion::Ballistic);
            }
            break;
        }
        case ProjectilePathType::Grenade: {
            // A grenade lands short and bounces twice to its target. When the
            // first contact point has no floor beneath it, it flies one arc
            // instead.
            std::array<glm::vec3, 4> contacts {glm::mix(origin, destination, 0.8f),
                                               glm::mix(origin, destination, 0.9f), glm::mix(origin, destination, 0.95f), destination};
            std::array<std::optional<int>, 4> surfaces;
            for (size_t i = 0; i < contacts.size(); ++i) {
                surfaces[i] = ground(contacts[i]);
            }
            if (!std::all_of(surfaces.begin(), surfaces.end(), [](const auto &surface) { return surface.has_value(); })) {
                append(destination, duration, ProjectileMotion::Ballistic);
                break;
            }
            const int total = static_cast<int>(duration * 1000.0f);
            const int first = static_cast<int>(total * 0.6f), second = static_cast<int>(total * 0.2f);
            const int third = static_cast<int>(total * 0.1f);
            const std::array<int, 4> times {first, second, third, total - first - second - third};
            for (size_t i = 0; i < contacts.size(); ++i) {
                append(contacts[i], times[i] / 1000.0f, ProjectileMotion::Ballistic);
                s.legs.back().surface = surfaces[i];
            }
            break;
        }
        default: {
            auto motion = ProjectileMotion::Homing;
            if (s.path == static_cast<int>(ProjectilePathType::Ballistic)) {
                motion = ProjectileMotion::Ballistic;
            } else if (s.path == static_cast<int>(ProjectilePathType::Accelerating)) {
                motion = ProjectileMotion::Accelerating;
            }
            append(destination, duration, motion, target);
            break;
        }
        }
        startLeg(*active, game, services);
        return;
    }
}
void Projectiles::cancelSpell(uint64_t id) {
    _active.erase(std::remove_if(_active.begin(), _active.end(), [id](const auto &p) {
                      return p->saved.id == id && p->saved.kind == ProjectileKind::Spell && !p->saved.released;
                  }),
                  _active.end());
}

static glm::vec3 safeProjectileSourcePosition(Creature &source, int hand) {
    auto body = std::dynamic_pointer_cast<scene::ModelSceneNode>(source.sceneNode());
    if (body) {
        if (hand == 2) {
            if (auto *hook = body->getNodeByName("impact")) {
                return hook->origin();
            }
        } else {
            const bool offhand = hand == 1;
            if (auto *hook = body->getNodeByName(offhand ? "lbullet" : "rbullet")) {
                return hook->origin();
            }
            auto *weapon = dynamic_cast<scene::ModelSceneNode *>(body->getAttachment(offhand ? "lhand" : "rhand"));
            if (weapon)
                if (auto *hook = weapon->getNodeByName("bullethook")) {
                    return hook->origin();
                }
        }
    }
    return source.position() + glm::vec3(0.0f, 0.0f, kImpactHeight);
}

static float safeProjectileShieldRadius(const Object &target, ServicesView &services) {
    auto *creature = dyn_cast<Creature>(&target);
    if (!creature) {
        return 0.0f;
    }
    int shield = 0;
    for (const auto &effect : creature->effects())
        if (effect.serializedType == SavedEffectTypes::forceShield) {
            shield = effect.integerParameter(0);
            break;
        }
    const auto found = services.game.combatTables.findForceShield(shield);
    if (!found) {
        return 0.0f;
    }
    const auto &row = found->get();
    // A blank appearance keeps the one before it.
    int slot = 0;
    int appearance = 0;
    while (appearance != creature->appearance()) {
        ++slot;
        if (slot > 4) {
            break;
        }
        appearance = row.appearances[slot - 1].value_or(appearance);
    }
    return std::max(0.0f, row.radii[slot]);
}

static glm::vec3 safeLegDestination(const SavedProjectile &projectile, const SavedProjectile::Leg &leg) {
    const auto &hook = leg.ownsTargetHook ? leg.targetHook : projectile.targetHook;
    auto endpoint = safeProjectileTargetPosition(leg.target, hook, leg.destination);
    auto approach = leg.origin - endpoint;
    if (leg.stopRadius > 0.0f && glm::length2(approach) > 0.0f) {
        endpoint += glm::normalize(approach) * leg.stopRadius;
    }
    return endpoint;
}

void Projectiles::launchSafeProjectile(Creature &source, Object &target, const Item &weapon,
                                       const SafeProjectileShot &shot, Game &game, ServicesView &services) {
    auto ammunition = weapon.ammunitionType();
    if (!ammunition || !ammunition->model) {
        return;
    }
    auto p = std::make_shared<ActiveProjectile>();
    auto &s = p->saved;
    s.id = _nextPresentationId++;
    s.kind = ProjectileKind::AttackBolt;
    s.path = static_cast<int>(ProjectilePathType::Homing);
    s.released = true;
    s.caster = referenceTo(source, game);
    s.weapon = referenceTo(weapon, game);
    s.model = ammunition->model->name();
    s.position = safeProjectileSourcePosition(source, shot.hand);
    s.combatResult = static_cast<int>(shot.result);
    s.soundVariant = shot.special ? 1 : 0;
    s.orientationMode = 2;
    auto destination = referenceTo(target, game);
    const auto result = shot.result;
    const glm::vec3 missedTarget = shot.endpoint;
    const bool intercepted = result == AttackResultType::Parried || result == AttackResultType::Deflected ||
                             result == AttackResultType::ShieldHit;
    const bool hit = result == AttackResultType::HitSuccessful || result == AttackResultType::CriticalHit ||
                     result == AttackResultType::AutomaticHit || result == AttackResultType::AttackResisted;
    SavedProjectile::Leg first;
    first.source = s.caster;
    first.target = hit || intercepted ? destination : SavedObjectReference {};
    first.origin = s.position;
    first.destination = hit || intercepted ? target.position() : missedTarget;
    first.targetHook = intercepted ? "impact_bolt" : "impact";
    first.ownsTargetHook = true;
    first.duration = shot.delayMilliseconds / 1000.0f;
    if (result == AttackResultType::ShieldHit) {
        first.targetHook.clear();
        first.stopRadius = safeProjectileShieldRadius(target, services);
    }
    first.destination = safeLegDestination(s, first);
    s.legs.push_back(first);
    if (intercepted) {
        SavedProjectile::Leg second;
        second.source = destination;
        second.target = result == AttackResultType::Deflected ? s.caster : SavedObjectReference {};
        second.origin = first.destination;
        second.destination = result == AttackResultType::Deflected ? source.position() : missedTarget;
        second.duration = (result == AttackResultType::Deflected ? shot.delayMilliseconds : shot.missedByMilliseconds) / 1000.0f;
        second.targetHook = "impact_bolt";
        second.ownsTargetHook = true;
        second.destination = safeLegDestination(s, second);
        s.legs.push_back(std::move(second));
    }
    startLeg(*p, game, services);
    attachPresentation(*p, game, services, false);
    if (auto body = std::dynamic_pointer_cast<scene::ModelSceneNode>(source.sceneNode())) {
        if (ammunition->muzzleFlash) {
            p->flash = body->graph().newModel(*ammunition->muzzleFlash, scene::ModelUsage::Projectile);
            p->flash->setLocalTransform(glm::translate(s.position));
            p->flash->signalEvent(kModelEventDetonate);
            body->graph().addRoot(p->flash);
        }
    }
    if (auto item = std::dynamic_pointer_cast<Item>(s.weapon.boundObject())) {
        item->playShotSound(s.soundVariant, s.position);
    }
    _active.push_back(std::move(p));
}

// The visual effect of a blaster bolt's spark.
static constexpr int kBlasterImpactVisual = 4024;

// A bolt that ends its flight after hitting a creature, or after the creature
// deflected it or stopped it with a shield, shows a blaster spark on that
// creature, turned along the bolt's last heading: at its impact node for a
// hit, at its bolt impact node otherwise. A parried bolt, a miss, a bolt at a
// door or placeable, and a bolt whose creature is gone show none.
static void presentBoltSpark(Area &area, const SavedProjectile &projectile) {
    const auto result = static_cast<AttackResultType>(projectile.combatResult);
    const bool hit = result == AttackResultType::HitSuccessful || result == AttackResultType::CriticalHit ||
                     result == AttackResultType::AutomaticHit || result == AttackResultType::AttackResisted;
    if (!hit && result != AttackResultType::Deflected && result != AttackResultType::ShieldHit) {
        return;
    }
    const auto creature = Creature::asCreature(projectile.legs.front().target.boundObject());
    if (!creature) {
        return;
    }
    area.presentHitSpark(creature->get(), kBlasterImpactVisual,
                         projectile.orientation * glm::vec3(0.0f, 1.0f, 0.0f), hit ? "impact" : "impact_bolt");
}

// Legs not yet set out still leave on time; only the saber flying now goes.
void Projectiles::dropThrownLightsaber(const Creature &caster) {
    _active.erase(std::remove_if(_active.begin(), _active.end(), [&](const auto &active) {
                      return active->saved.kind == ProjectileKind::LightsaberRoute && active->saved.released &&
                             active->saved.caster.boundObject().get() == &caster;
                  }),
                  _active.end());
}

void Projectiles::update(float dt, Game &game, ServicesView &services) {
    for (auto it = _active.begin(); it != _active.end();) {
        auto &active = **it;
        auto &s = active.saved;
        auto caster = s.caster.boundObject();
        if (!caster || (s.kind == ProjectileKind::LightsaberRoute && caster->isDead())) {
            it = _active.erase(it);
            continue;
        }
        float frame = std::max(0.0f, dt);
        if (s.kind == ProjectileKind::LightsaberRoute && !s.released) {
            const float wait = std::min(frame, s.activationDelay);
            s.activationDelay -= wait;
            frame -= wait;
            if (s.activationDelay <= 0.0f) {
                s.released = true;
                startLeg(active, game, services);
            }
        }
        attachPresentation(active, game, services, true);
        if (!s.released) {
            if (s.kind == ProjectileKind::Spell) {
                s.position = spellSourcePosition(s.caster, s.sourceHook, s.position);
            }
        } else {
            while (s.leg < s.legs.size()) {
                auto &leg = s.legs[s.leg];
                const float before = std::max(0.0f, leg.duration - s.elapsed);
                const float step = std::min(frame, before);
                if (s.kind == ProjectileKind::LightsaberRoute) {
                    leg.destination = throwTargetPosition(leg.target, s.caster, leg.destination);
                } else if (s.kind == ProjectileKind::AttackBolt) {
                    leg.destination = safeLegDestination(s, leg);
                } else if (leg.target.boundObject()) {
                    leg.destination = attachmentPosition(leg.target, "impact", leg.destination - leg.targetOffset) + leg.targetOffset;
                }
                const glm::vec3 delta = leg.destination - s.position;
                const glm::vec3 previous = s.position;
                const bool completed = frame >= before;
                s.elapsed = completed ? leg.duration : s.elapsed + step;
                frame -= step;
                const float remaining = std::max(0.0f, leg.duration - s.elapsed);
                if (completed || remaining <= 0.0f || leg.motion == ProjectileMotion::Jump || leg.motion == ProjectileMotion::Linked) {
                    s.position = leg.destination;
                } else if (leg.motion == ProjectileMotion::SpiralClose) {
                    glm::vec3 tangent {-delta.y, delta.x, 0.0f};
                    if (glm::length2(tangent) > 0.0f) {
                        tangent = glm::normalize(tangent) * (s.clockwise ? 1.0f : -1.0f);
                    }
                    s.velocity = delta / remaining + tangent * 7.5f;
                    s.position += s.velocity * step;
                    if (glm::length2(s.position - previous) > glm::length2(delta)) {
                        s.position = leg.destination;
                    }
                } else {
                    s.velocity = (2.0f * delta - s.acceleration * remaining * remaining) / (2.0f * remaining);
                    s.position += s.velocity * step;
                    if (glm::length2(s.position - previous) > glm::length2(delta)) {
                        s.position = leg.destination;
                    }
                }
                if (s.orientationMode) {
                    glm::vec3 direction = s.orientationMode == 1 ? leg.destination - s.position : s.position - previous;
                    if (glm::length2(direction) > 0.000001f) {
                        s.orientation = glm::rotation(glm::vec3(0, 1, 0), glm::normalize(direction));
                    }
                }
                if (s.elapsed < leg.duration) {
                    break;
                }
                if (active.model) {
                    active.model->setLocalTransform(glm::translate(s.position) * glm::mat4_cast(s.orientation));
                }
                // Presentation completion never applies gameplay damage.
                ++s.leg;
                // A bolt is heard once, where its flight ends, whether it hit,
                // missed or came back deflected.
                if (s.leg == s.legs.size()) {
                    if (s.kind == ProjectileKind::AttackBolt) {
                        if (auto item = std::dynamic_pointer_cast<Item>(s.weapon.boundObject())) {
                            item->playImpactSound(s.soundVariant, s.position);
                        }
                        presentBoltSpark(*game.module()->area(), s);
                    }
                    break;
                }
                startLeg(active, game, services);
            }
        }
        if (s.released && s.leg >= s.legs.size()) {
            it = _active.erase(it);
            continue;
        }
        if (active.model) {
            active.model->setLocalTransform(glm::translate(s.position) * glm::mat4_cast(s.orientation));
        }
        ++it;
    }
}

std::vector<SavedProjectile> Projectiles::savePresentations() const {
    std::vector<SavedProjectile> result;
    for (const auto &p : _active) {
        result.push_back(p->saved);
    }
    return result;
}
void Projectiles::restorePresentations(std::vector<SavedProjectile> records, Game &game, ServicesView &services) {
    retireAreaRuntime();
    for (auto &record : records) {
        // A projectile whose objects are gone is not shown again.
        if (!record.bindObjectReferences(game)) {
            continue;
        }
        auto active = std::make_shared<ActiveProjectile>();
        active->saved = std::move(record);
        _nextPresentationId = std::max(_nextPresentationId, active->saved.id + 1);
        _active.push_back(active);
        // No launch, random draw, payment, damage, reaction or item use on this path.
        attachPresentation(*active, game, services, true);
    }
}
void Projectiles::retireAreaRuntime() {
    _active.clear();
    _nextPresentationId = 1;
}

} // namespace game
} // namespace reone
