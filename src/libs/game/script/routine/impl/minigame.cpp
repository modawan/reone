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

#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/game/script/routine/argutil.h"
#include "reone/game/script/routine/context.h"
#include "reone/game/script/routines.h"
#include "reone/script/executioncontext.h"
#include "reone/script/routine/exception/notimplemented.h"
#include "reone/script/variable.h"

#define R_VOID script::VariableType::Void
#define R_INT script::VariableType::Int
#define R_FLOAT script::VariableType::Float
#define R_OBJECT script::VariableType::Object
#define R_STRING script::VariableType::String
#define R_EFFECT script::VariableType::Effect
#define R_EVENT script::VariableType::Event
#define R_LOCATION script::VariableType::Location
#define R_TALENT script::VariableType::Talent
#define R_VECTOR script::VariableType::Vector
#define R_ACTION script::VariableType::Action

using namespace reone::script;

namespace reone {

namespace game {

namespace {

Turret *activeTurret(const RoutineContext &ctx) {
    auto &turret = ctx.game.turret();
    return turret.isActive() ? &turret : nullptr;
}

Turret::Follower *activePlayer(const RoutineContext &ctx) {
    auto turret = activeTurret(ctx);
    return turret ? turret->player() : nullptr;
}

// The minigame object an argument names.
Turret::Actor *actorArg(const std::vector<Variable> &args, int index, const RoutineContext &ctx) {
    auto turret = activeTurret(ctx);
    auto object = turret ? getObjectOrNull(args, index, ctx) : nullptr;
    return object ? turret->actor(object->id()) : nullptr;
}

Turret::Follower *followerArg(const std::vector<Variable> &args, int index, const RoutineContext &ctx) {
    auto turret = activeTurret(ctx);
    auto object = turret ? getObjectOrNull(args, index, ctx) : nullptr;
    return object ? turret->follower(object->id()) : nullptr;
}

// The gun bank a follower argument and the bank id after it name.
Turret::GunBank *gunBankArg(const std::vector<Variable> &args, int index, const RoutineContext &ctx) {
    auto follower = followerArg(args, index, ctx);
    auto bank = getInt(args, index + 1);
    return follower ? follower->gunBank(bank) : nullptr;
}

// The bank of an enemy's gun that aims, which the follower argument and the
// bank id after it name; the player's guns do not aim.
Turret::GunBank *aimingGunBankArg(const std::vector<Variable> &args, int index, const RoutineContext &ctx) {
    auto follower = followerArg(args, index, ctx);
    return follower && !follower->player ? follower->gunBank(getInt(args, index + 1)) : nullptr;
}

// The engine (0) or death (1) sound a follower argument and the index after it
// name.
Turret::SoundSettings *soundArg(const std::vector<Variable> &args, int index, const RoutineContext &ctx) {
    auto follower = followerArg(args, index, ctx);
    auto sound = getInt(args, index + 1);
    if (!follower || sound < 0 || sound >= static_cast<int>(follower->sounds.size())) {
        return nullptr;
    }
    return &follower->sounds[sound];
}

Turret::Actor *selfActor(const RoutineContext &ctx) {
    auto turret = activeTurret(ctx);
    const Variable *caller = ctx.execution.findArg(ArgKind::Caller);
    return turret && caller ? turret->actor(caller->objectId) : nullptr;
}

Turret::Follower *selfFollower(const RoutineContext &ctx) {
    auto turret = activeTurret(ctx);
    const Variable *caller = ctx.execution.findArg(ArgKind::Caller);
    return turret && caller ? turret->follower(caller->objectId) : nullptr;
}

// A remembered follower, forgotten once it is gone.
uint32_t liveFollower(const RoutineContext &ctx, uint32_t &id) {
    if (!activeTurret(ctx)->follower(id)) {
        id = kObjectInvalid;
    }
    return id;
}

template <class T>
uint32_t entryId(const std::vector<T *> &entries, int index) {
    if (index < 0 || index >= static_cast<int>(entries.size())) {
        return kObjectInvalid;
    }
    return entries[index]->id();
}

} // namespace

static Variable SWMG_SetLateralAccelerationPerSecond(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fLAPS = getFloat(args, 0);

    // Transform
    // TSL scales the acceleration by the player's dexterity.
    if (ctx.game.isTSL()) {
        if (auto leader = ctx.game.party().getLeader()) {
            int dexterity = leader->attributes().getAbilityModifier(Ability::Dexterity);
            fLAPS += fLAPS * static_cast<float>(dexterity) * 0.3f;
        }
    }

    // Execute
    if (auto turret = activeTurret(ctx)) {
        turret->setLateralAcceleration(fLAPS);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetLateralAccelerationPerSecond(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto turret = activeTurret(ctx);
    return Variable::ofFloat(turret ? turret->lateralAcceleration() : 0.0f);
}

static Variable SWMG_SetSpeedBlurEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bEnabled = getInt(args, 0);
    auto fRatio = getFloatOrElse(args, 1, 0.75f);

    // Transform

    // Execute
    // The ratio is set first; the blur then follows bEnabled.
    ctx.game.setSpeedBlurRatio(fRatio);
    ctx.game.setSpeedBlur(bEnabled != 0);
    return Variable::ofNull();
}

static Variable SWMG_GetLastEvent(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto self = selfActor(ctx);
    return Variable::ofString(self ? self->lastEvent : "");
}

static Variable SWMG_GetLastEventModelName(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto self = selfActor(ctx);
    return Variable::ofString(self ? self->lastEventModel : "");
}

static Variable SWMG_GetObjectByName(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sName = getString(args, 0);

    // Execute
    auto turret = activeTurret(ctx);
    auto actor = turret ? turret->actorByName(sName) : nullptr;
    return Variable::ofObject(actor ? actor->id() : kObjectInvalid);
}

static Variable SWMG_PlayAnimation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto actor = actorArg(args, 0, ctx);
    auto sAnimName = getString(args, 1);
    auto bLooping = getIntOrElse(args, 2, 1);
    auto bOverlay = getIntOrElse(args, 4, 0);

    // Execute
    // Queueing is not supported: the animation plays at once.
    if (actor) {
        activeTurret(ctx)->playAnimation(*actor, sAnimName, bLooping != 0, bOverlay != 0);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetLastBulletHitDamage(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto self = selfActor(ctx);
    return Variable::ofInt(self ? self->lastBulletHit.damage : -1);
}

static Variable SWMG_GetLastBulletHitTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto self = selfActor(ctx);
    return Variable::ofInt(self ? static_cast<int>(self->lastBulletHit.targetType) : -1);
}

static Variable SWMG_GetLastBulletHitShooter(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto self = selfActor(ctx);
    if (!self) {
        return Variable::ofObject(kObjectInvalid);
    }
    return Variable::ofObject(liveFollower(ctx, self->lastBulletShooter));
}

static Variable SWMG_AdjustFollowerHitPoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto follower = followerArg(args, 0, ctx);
    auto nHP = getInt(args, 1);
    auto nAbsolute = getIntOrElse(args, 2, 0);

    // Execute
    if (follower) {
        activeTurret(ctx)->adjustHitPoints(*follower, nHP);
    }
    // Nothing is returned but what the script passed as nAbsolute.
    return Variable::ofInt(nAbsolute);
}

static Variable SWMG_OnBulletHit(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (auto self = selfActor(ctx)) {
        activeTurret(ctx)->onBulletHit(*self, true);
    }
    return Variable::ofNull();
}

static Variable SWMG_OnObstacleHit(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (auto self = selfFollower(ctx)) {
        activeTurret(ctx)->onObstacleHit(*self, true);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetLastFollowerHit(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto self = selfActor(ctx);
    if (!self) {
        return Variable::ofObject(kObjectInvalid);
    }
    return Variable::ofObject(liveFollower(ctx, self->lastFollowerHit));
}

static Variable SWMG_GetLastObstacleHit(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto self = selfFollower(ctx);
    if (!self) {
        return Variable::ofObject(kObjectInvalid);
    }
    if (!activeTurret(ctx)->obstacle(self->lastObstacleHit)) {
        self->lastObstacleHit = kObjectInvalid;
    }
    return Variable::ofObject(self->lastObstacleHit);
}

static Variable SWMG_GetLastBulletFiredDamage(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto self = selfFollower(ctx);
    return Variable::ofInt(self ? self->lastBulletFired.damage : -1);
}

static Variable SWMG_GetLastBulletFiredTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto self = selfFollower(ctx);
    return Variable::ofInt(self ? static_cast<int>(self->lastBulletFired.targetType) : -1);
}

static Variable SWMG_GetObjectName(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto actor = actorArg(args, 0, ctx);

    // Execute
    return Variable::ofString(actor ? actor->name : "");
}

static Variable SWMG_OnDeath(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (auto self = selfFollower(ctx)) {
        activeTurret(ctx)->onDeath(*self, true);
    }
    return Variable::ofNull();
}

static Variable SWMG_IsFollower(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto follower = followerArg(args, 0, ctx);

    // Execute
    return Variable::ofInt(follower ? 1 : 0);
}

static Variable SWMG_IsPlayer(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto follower = followerArg(args, 0, ctx);

    // Execute
    return Variable::ofInt(follower && follower->player ? 1 : 0);
}

static Variable SWMG_IsEnemy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto follower = followerArg(args, 0, ctx);

    // Execute
    return Variable::ofInt(follower && !follower->player ? 1 : 0);
}

static Variable SWMG_IsTrigger(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto follower = followerArg(args, 0, ctx);

    // Execute
    return Variable::ofInt(follower && follower->trigger ? 1 : 0);
}

static Variable SWMG_IsObstacle(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto actor = actorArg(args, 0, ctx);

    // Execute
    // A trigger enemy counts as an obstacle too.
    if (!actor) {
        return Variable::ofInt(0);
    }
    auto turret = activeTurret(ctx);
    auto follower = turret->follower(actor->id());
    return Variable::ofInt(!follower || follower->trigger ? 1 : 0);
}

static Variable SWMG_SetFollowerHitPoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto follower = followerArg(args, 0, ctx);
    auto nHP = getInt(args, 1);

    // Execute
    if (follower && nHP > 0) {
        follower->hitPoints = nHP;
    }
    return Variable::ofNull();
}

static Variable SWMG_OnDamage(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (auto self = selfFollower(ctx)) {
        activeTurret(ctx)->onDamage(*self, true);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetLastHPChange(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto self = selfFollower(ctx);
    return Variable::ofInt(self ? self->hitPointAdjust : 0);
}

static Variable SWMG_RemoveAnimation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto actor = actorArg(args, 0, ctx);
    auto sAnimName = getString(args, 1);

    // Execute
    if (actor) {
        activeTurret(ctx)->removeAnimation(*actor, sAnimName);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetCameraNearClip(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto turret = activeTurret(ctx);
    return Variable::ofFloat(turret ? turret->nearClip() : 0.0f);
}

static Variable SWMG_GetCameraFarClip(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto turret = activeTurret(ctx);
    return Variable::ofFloat(turret ? turret->farClip() : 0.0f);
}

static Variable SWMG_SetCameraClip(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fNear = getFloat(args, 0);
    auto fFar = getFloat(args, 1);

    // Execute
    if (auto turret = activeTurret(ctx)) {
        turret->setCameraClip(fNear, fFar);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetPlayer(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto player = activePlayer(ctx);
    return Variable::ofObject(player ? player->id() : kObjectInvalid);
}

static Variable SWMG_GetEnemyCount(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto turret = activeTurret(ctx);
    return Variable::ofInt(turret ? static_cast<int>(turret->listedEnemies().size()) : 0);
}

static Variable SWMG_GetEnemy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nEntry = getInt(args, 0);

    // Execute
    auto turret = activeTurret(ctx);
    if (!turret) {
        return Variable::ofObject(kObjectInvalid);
    }
    return Variable::ofObject(entryId(turret->listedEnemies(), nEntry));
}

static Variable SWMG_GetObstacleCount(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto turret = activeTurret(ctx);
    return Variable::ofInt(turret ? static_cast<int>(turret->listedObstacles().size()) : 0);
}

static Variable SWMG_GetObstacle(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nEntry = getInt(args, 0);

    // Execute
    auto turret = activeTurret(ctx);
    if (!turret) {
        return Variable::ofObject(kObjectInvalid);
    }
    return Variable::ofObject(entryId(turret->listedObstacles(), nEntry));
}

static Variable SWMG_GetHitPoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto follower = followerArg(args, 0, ctx);

    // Execute
    return Variable::ofInt(follower ? follower->hitPoints : 0);
}

static Variable SWMG_GetMaxHitPoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto follower = followerArg(args, 0, ctx);

    // Execute
    return Variable::ofInt(follower ? follower->maxHitPoints : 0);
}

static Variable SWMG_SetMaxHitPoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto follower = followerArg(args, 0, ctx);
    auto nMaxHP = getInt(args, 1);

    // Execute
    if (follower && nMaxHP > 0) {
        follower->maxHitPoints = nMaxHP;
    }
    return Variable::ofNull();
}

static Variable SWMG_GetSphereRadius(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto follower = followerArg(args, 0, ctx);

    // Execute
    return Variable::ofFloat(follower ? follower->sphereRadius : 0.0f);
}

static Variable SWMG_SetSphereRadius(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto follower = followerArg(args, 0, ctx);
    auto fRadius = getFloat(args, 1);

    // Execute
    if (follower && fRadius >= 0.0f) {
        follower->sphereRadius = fRadius;
    }
    return Variable::ofNull();
}

static Variable SWMG_GetNumLoops(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto follower = followerArg(args, 0, ctx);

    // Execute
    return Variable::ofInt(follower ? follower->numLoops : 0);
}

static Variable SWMG_SetNumLoops(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto follower = followerArg(args, 0, ctx);
    auto nNumLoops = getInt(args, 1);

    // Execute
    if (follower) {
        activeTurret(ctx)->setNumLoops(*follower, nNumLoops);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetPosition(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto actor = actorArg(args, 0, ctx);

    // Execute
    // A follower is where its first model is. KotOR has no position for an
    // obstacle.
    if (!actor || actor->models.empty()) {
        return Variable::ofVector(glm::vec3(0.0f));
    }
    if (!activeTurret(ctx)->follower(actor->id()) && !ctx.game.isTSL()) {
        return Variable::ofVector(glm::vec3(0.0f));
    }
    return Variable::ofVector(glm::vec3(actor->models.front()->absoluteTransform()[3]));
}

static Variable SWMG_GetGunBankCount(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto follower = followerArg(args, 0, ctx);

    // Execute
    return Variable::ofInt(follower ? follower->gunBankCount() : 0);
}

static Variable SWMG_GetGunBankBulletModel(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = gunBankArg(args, 0, ctx);

    // Execute
    return Variable::ofString(bank ? bank->spec.bullet.modelResRef : "");
}

static Variable SWMG_GetGunBankGunModel(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = gunBankArg(args, 0, ctx);

    // Execute
    return Variable::ofString(bank ? bank->spec.gunModelResRef : "");
}

static Variable SWMG_GetGunBankDamage(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = gunBankArg(args, 0, ctx);

    // Execute
    return Variable::ofInt(bank ? static_cast<int>(bank->spec.bullet.damage) : 0);
}

static Variable SWMG_GetGunBankTimeBetweenShots(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = gunBankArg(args, 0, ctx);

    // Execute
    return Variable::ofFloat(bank ? bank->spec.bullet.rateOfFire : 0.0f);
}

static Variable SWMG_GetGunBankLifespan(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = gunBankArg(args, 0, ctx);

    // Execute
    return Variable::ofFloat(bank ? bank->spec.bullet.lifespan : 0.0f);
}

static Variable SWMG_GetGunBankSpeed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = gunBankArg(args, 0, ctx);

    // Execute
    return Variable::ofFloat(bank ? bank->spec.bullet.speed : 0.0f);
}

static Variable SWMG_GetGunBankTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = gunBankArg(args, 0, ctx);

    // Execute
    return Variable::ofInt(bank ? static_cast<int>(bank->spec.bullet.targetType) : 0);
}

static Variable SWMG_SetGunBankBulletModel(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = gunBankArg(args, 0, ctx);
    auto sBulletModel = getString(args, 2);

    // Execute
    if (bank) {
        bank->spec.bullet.modelResRef = sBulletModel;
    }
    return Variable::ofNull();
}

static Variable SWMG_SetGunBankGunModel(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto follower = followerArg(args, 0, ctx);
    auto bank = gunBankArg(args, 0, ctx);
    auto sGunModel = getString(args, 2);

    // Execute
    if (bank) {
        activeTurret(ctx)->setGunModel(*follower, *bank, sGunModel);
    }
    return Variable::ofNull();
}

static Variable SWMG_SetGunBankDamage(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = gunBankArg(args, 0, ctx);
    auto nDamage = getInt(args, 2);

    // Execute
    if (bank) {
        bank->spec.bullet.damage = static_cast<uint32_t>(nDamage);
    }
    return Variable::ofNull();
}

static Variable SWMG_SetGunBankTimeBetweenShots(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = gunBankArg(args, 0, ctx);
    auto fTBS = getFloat(args, 2);

    // Execute
    if (bank && fTBS >= 0.0f) {
        bank->spec.bullet.rateOfFire = fTBS;
        bank->timer.setRateOfFire(fTBS);
    }
    return Variable::ofNull();
}

static Variable SWMG_SetGunBankLifespan(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = gunBankArg(args, 0, ctx);
    auto fLifespan = getFloat(args, 2);

    // Execute
    if (bank && fLifespan > 0.0f) {
        bank->spec.bullet.lifespan = fLifespan;
    }
    return Variable::ofNull();
}

static Variable SWMG_SetGunBankSpeed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = gunBankArg(args, 0, ctx);
    auto fSpeed = getFloat(args, 2);

    // Execute
    if (bank && fSpeed >= 0.0f) {
        bank->spec.bullet.speed = fSpeed;
    }
    return Variable::ofNull();
}

static Variable SWMG_SetGunBankTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = gunBankArg(args, 0, ctx);
    auto nTarget = getInt(args, 2);

    // Execute
    // A target type is made of the player's bit, 1, and the enemies', 2.
    if (bank && (nTarget & ~3) == 0) {
        bank->spec.bullet.targetType = static_cast<uint32_t>(nTarget);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetLastBulletHitPart(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto self = selfActor(ctx);
    return Variable::ofString(self ? self->lastBulletHitPart : "");
}

static Variable SWMG_IsGunBankTargetting(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto follower = followerArg(args, 0, ctx);
    auto bank = gunBankArg(args, 0, ctx);

    // Execute
    // Enemy gun banks aim; the player's do not.
    return Variable::ofInt(bank && !follower->player ? 1 : 0);
}

static Variable SWMG_GetPlayerOffset(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto player = activePlayer(ctx);
    return Variable::ofVector(player ? activeTurret(ctx)->aim().offset() : glm::vec3(0.0f));
}

static Variable SWMG_GetPlayerInvincibility(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto player = activePlayer(ctx);
    return Variable::ofFloat(player ? player->invincibility : 0.0f);
}

static Variable SWMG_GetPlayerSpeed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto player = activePlayer(ctx);
    return Variable::ofFloat(player ? activeTurret(ctx)->speed() : 0.0f);
}

static Variable SWMG_GetPlayerMinSpeed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("SWMG_GetPlayerMinSpeed");
}

static Variable SWMG_GetPlayerAccelerationPerSecond(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("SWMG_GetPlayerAccelerationPerSecond");
}

static Variable SWMG_GetPlayerTunnelPos(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto player = activePlayer(ctx);
    return Variable::ofVector(player ? activeTurret(ctx)->aim().tunnelPositive() : glm::vec3(0.0f));
}

static Variable SWMG_SetPlayerOffset(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vOffset = getVector(args, 0);

    // Execute
    if (activePlayer(ctx)) {
        activeTurret(ctx)->aim().setOffset(vOffset);
    }
    return Variable::ofNull();
}

static Variable SWMG_SetPlayerInvincibility(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fInvincibility = getFloat(args, 0);

    // Execute
    auto player = activePlayer(ctx);
    if (player && fInvincibility >= 0.0f) {
        player->invincibility = fInvincibility;
    }
    return Variable::ofNull();
}

static Variable SWMG_SetPlayerSpeed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fSpeed = getFloat(args, 0);

    // Execute
    if (activePlayer(ctx)) {
        activeTurret(ctx)->setSpeed(fSpeed);
    }
    return Variable::ofNull();
}

static Variable SWMG_SetPlayerMinSpeed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fMinSpeed = getFloat(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SWMG_SetPlayerMinSpeed");
}

static Variable SWMG_SetPlayerAccelerationPerSecond(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fAPS = getFloat(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SWMG_SetPlayerAccelerationPerSecond");
}

static Variable SWMG_SetPlayerTunnelPos(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vTunnel = getVector(args, 0);

    // Execute
    if (activePlayer(ctx)) {
        activeTurret(ctx)->aim().setTunnelPositive(vTunnel);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetPlayerTunnelNeg(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto player = activePlayer(ctx);
    return Variable::ofVector(player ? activeTurret(ctx)->aim().tunnelNegative() : glm::vec3(0.0f));
}

static Variable SWMG_SetPlayerTunnelNeg(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vTunnel = getVector(args, 0);

    // Execute
    if (activePlayer(ctx)) {
        activeTurret(ctx)->aim().setTunnelNegative(vTunnel);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetPlayerOrigin(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto player = activePlayer(ctx);
    return Variable::ofVector(player ? activeTurret(ctx)->aim().origin() : glm::vec3(0.0f));
}

static Variable SWMG_SetPlayerOrigin(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vOrigin = getVector(args, 0);

    // Execute
    if (activePlayer(ctx)) {
        auto turret = activeTurret(ctx);
        turret->aim().setOrigin(vOrigin, turret->trackPlaying());
    }
    return Variable::ofNull();
}

static Variable SWMG_GetGunBankHorizontalSpread(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = aimingGunBankArg(args, 0, ctx);

    // Execute
    return Variable::ofFloat(bank ? bank->spec.horizSpread : 0.0f);
}

static Variable SWMG_GetGunBankVerticalSpread(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = aimingGunBankArg(args, 0, ctx);

    // Execute
    return Variable::ofFloat(bank ? bank->spec.vertSpread : 0.0f);
}

static Variable SWMG_GetGunBankSensingRadius(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = aimingGunBankArg(args, 0, ctx);

    // Execute
    return Variable::ofFloat(bank ? bank->spec.sensingRadius : 0.0f);
}

static Variable SWMG_GetGunBankInaccuracy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = aimingGunBankArg(args, 0, ctx);

    // Execute
    return Variable::ofFloat(bank ? bank->spec.inaccuracy : 0.0f);
}

// A spread is an angle either way in degrees, or -1 for none.
static Variable SWMG_SetGunBankHorizontalSpread(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = aimingGunBankArg(args, 0, ctx);
    auto fSpread = getFloat(args, 2);

    // Execute
    if (bank && (fSpread >= 0.0f || fSpread == -1.0f)) {
        bank->spec.horizSpread = fSpread;
    }
    return Variable::ofNull();
}

static Variable SWMG_SetGunBankVerticalSpread(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = aimingGunBankArg(args, 0, ctx);
    auto fSpread = getFloat(args, 2);

    // Execute
    if (bank && (fSpread >= 0.0f || fSpread == -1.0f)) {
        bank->spec.vertSpread = fSpread;
    }
    return Variable::ofNull();
}

static Variable SWMG_SetGunBankSensingRadius(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = aimingGunBankArg(args, 0, ctx);
    auto fRadius = getFloat(args, 2);

    // Execute
    if (bank && fRadius >= 0.0f) {
        bank->spec.sensingRadius = fRadius;
    }
    return Variable::ofNull();
}

static Variable SWMG_SetGunBankInaccuracy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = aimingGunBankArg(args, 0, ctx);
    auto fInaccuracy = getFloat(args, 2);

    // Execute
    if (bank && fInaccuracy >= 0.0f) {
        bank->spec.inaccuracy = fInaccuracy;
    }
    return Variable::ofNull();
}

static Variable SWMG_GetIsInvulnerable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto follower = followerArg(args, 0, ctx);

    // Execute
    return Variable::ofInt(follower && follower->invincibilityTimer > 0.0f ? 1 : 0);
}

static Variable SWMG_StartInvulnerability(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto follower = followerArg(args, 0, ctx);

    // Execute
    if (follower) {
        follower->invincibilityTimer = follower->invincibility;
    }
    return Variable::ofNull();
}

static Variable SWMG_GetPlayerMaxSpeed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("SWMG_GetPlayerMaxSpeed");
}

static Variable SWMG_SetPlayerMaxSpeed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fMaxSpeed = getFloat(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SWMG_SetPlayerMaxSpeed");
}

static Variable SWMG_GetSoundFrequency(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sound = soundArg(args, 0, ctx);

    // Execute
    return Variable::ofInt(sound ? sound->frequency : 0);
}

static Variable SWMG_SetSoundFrequency(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sound = soundArg(args, 0, ctx);
    auto nValue = getInt(args, 2);

    // Execute
    if (sound) {
        sound->frequency = nValue;
    }
    return Variable::ofNull();
}

static Variable SWMG_GetSoundFrequencyIsRandom(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sound = soundArg(args, 0, ctx);

    // Execute
    return Variable::ofInt(sound ? sound->frequencyIsRandom : 0);
}

static Variable SWMG_SetSoundFrequencyIsRandom(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sound = soundArg(args, 0, ctx);
    auto nValue = getInt(args, 2);

    // Execute
    if (sound) {
        sound->frequencyIsRandom = nValue;
    }
    return Variable::ofNull();
}

static Variable SWMG_GetSoundVolume(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sound = soundArg(args, 0, ctx);

    // Execute
    return Variable::ofInt(sound ? sound->volume : 0);
}

static Variable SWMG_SetSoundVolume(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto follower = followerArg(args, 0, ctx);
    auto nSound = getInt(args, 1);
    auto nValue = getInt(args, 2);

    // Execute
    if (follower) {
        activeTurret(ctx)->setSoundVolume(*follower, nSound, nValue);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetPlayerTunnelInfinite(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto player = activePlayer(ctx);
    return Variable::ofVector(player ? activeTurret(ctx)->aim().tunnelInfinite() : glm::vec3(0.0f));
}

static Variable SWMG_SetPlayerTunnelInfinite(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vInfinite = getVector(args, 0);

    // Execute
    if (activePlayer(ctx)) {
        activeTurret(ctx)->aim().setTunnelInfinite(vInfinite);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetSwoopUpgrade(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSlot = getInt(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SWMG_GetSwoopUpgrade");
}

static Variable SWMG_GetTrackPosition(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollower = getObject(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SWMG_GetTrackPosition");
}

static Variable SWMG_SetFollowerPosition(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vPos = getVector(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SWMG_SetFollowerPosition");
}

static Variable SWMG_DestroyMiniGameObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto actor = actorArg(args, 0, ctx);

    // Execute
    // An obstacle leaves the obstacle list and the scene, and keeps its object.
    if (actor) {
        auto turret = activeTurret(ctx);
        if (auto obstacle = turret->obstacle(actor->id())) {
            turret->removeObstacle(*obstacle);
        }
    }
    return Variable::ofNull();
}

static Variable SWMG_SetJumpSpeed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fSpeed = getFloat(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SWMG_SetJumpSpeed");
}

static Variable SWMG_PlayerApplyForce(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vForce = getVector(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SWMG_PlayerApplyForce");
}

void Routines::registerMinigameKotorRoutines() {
    insert(520, "SWMG_SetLateralAccelerationPerSecond", R_VOID, {R_FLOAT}, &SWMG_SetLateralAccelerationPerSecond);
    insert(521, "SWMG_GetLateralAccelerationPerSecond", R_FLOAT, {}, &SWMG_GetLateralAccelerationPerSecond);
    insert(563, "SWMG_SetSpeedBlurEffect", R_VOID, {R_INT, R_FLOAT}, &SWMG_SetSpeedBlurEffect);
    insert(583, "SWMG_GetLastEvent", R_STRING, {}, &SWMG_GetLastEvent);
    insert(584, "SWMG_GetLastEventModelName", R_STRING, {}, &SWMG_GetLastEventModelName);
    insert(585, "SWMG_GetObjectByName", R_OBJECT, {R_STRING}, &SWMG_GetObjectByName);
    insert(586, "SWMG_PlayAnimation", R_VOID, {R_OBJECT, R_STRING, R_INT, R_INT, R_INT}, &SWMG_PlayAnimation);
    insert(587, "SWMG_GetLastBulletHitDamage", R_INT, {}, &SWMG_GetLastBulletHitDamage);
    insert(588, "SWMG_GetLastBulletHitTarget", R_INT, {}, &SWMG_GetLastBulletHitTarget);
    insert(589, "SWMG_GetLastBulletHitShooter", R_OBJECT, {}, &SWMG_GetLastBulletHitShooter);
    insert(590, "SWMG_AdjustFollowerHitPoints", R_INT, {R_OBJECT, R_INT, R_INT}, &SWMG_AdjustFollowerHitPoints);
    insert(591, "SWMG_OnBulletHit", R_VOID, {}, &SWMG_OnBulletHit);
    insert(592, "SWMG_OnObstacleHit", R_VOID, {}, &SWMG_OnObstacleHit);
    insert(593, "SWMG_GetLastFollowerHit", R_OBJECT, {}, &SWMG_GetLastFollowerHit);
    insert(594, "SWMG_GetLastObstacleHit", R_OBJECT, {}, &SWMG_GetLastObstacleHit);
    insert(595, "SWMG_GetLastBulletFiredDamage", R_INT, {}, &SWMG_GetLastBulletFiredDamage);
    insert(596, "SWMG_GetLastBulletFiredTarget", R_INT, {}, &SWMG_GetLastBulletFiredTarget);
    insert(597, "SWMG_GetObjectName", R_STRING, {R_OBJECT}, &SWMG_GetObjectName);
    insert(598, "SWMG_OnDeath", R_VOID, {}, &SWMG_OnDeath);
    insert(599, "SWMG_IsFollower", R_INT, {R_OBJECT}, &SWMG_IsFollower);
    insert(600, "SWMG_IsPlayer", R_INT, {R_OBJECT}, &SWMG_IsPlayer);
    insert(601, "SWMG_IsEnemy", R_INT, {R_OBJECT}, &SWMG_IsEnemy);
    insert(602, "SWMG_IsTrigger", R_INT, {R_OBJECT}, &SWMG_IsTrigger);
    insert(603, "SWMG_IsObstacle", R_INT, {R_OBJECT}, &SWMG_IsObstacle);
    insert(604, "SWMG_SetFollowerHitPoints", R_VOID, {R_OBJECT, R_INT}, &SWMG_SetFollowerHitPoints);
    insert(605, "SWMG_OnDamage", R_VOID, {}, &SWMG_OnDamage);
    insert(606, "SWMG_GetLastHPChange", R_INT, {}, &SWMG_GetLastHPChange);
    insert(607, "SWMG_RemoveAnimation", R_VOID, {R_OBJECT, R_STRING}, &SWMG_RemoveAnimation);
    insert(608, "SWMG_GetCameraNearClip", R_FLOAT, {}, &SWMG_GetCameraNearClip);
    insert(609, "SWMG_GetCameraFarClip", R_FLOAT, {}, &SWMG_GetCameraFarClip);
    insert(610, "SWMG_SetCameraClip", R_VOID, {R_FLOAT, R_FLOAT}, &SWMG_SetCameraClip);
    insert(611, "SWMG_GetPlayer", R_OBJECT, {}, &SWMG_GetPlayer);
    insert(612, "SWMG_GetEnemyCount", R_INT, {}, &SWMG_GetEnemyCount);
    insert(613, "SWMG_GetEnemy", R_OBJECT, {R_INT}, &SWMG_GetEnemy);
    insert(614, "SWMG_GetObstacleCount", R_INT, {}, &SWMG_GetObstacleCount);
    insert(615, "SWMG_GetObstacle", R_OBJECT, {R_INT}, &SWMG_GetObstacle);
    insert(616, "SWMG_GetHitPoints", R_INT, {R_OBJECT}, &SWMG_GetHitPoints);
    insert(617, "SWMG_GetMaxHitPoints", R_INT, {R_OBJECT}, &SWMG_GetMaxHitPoints);
    insert(618, "SWMG_SetMaxHitPoints", R_VOID, {R_OBJECT, R_INT}, &SWMG_SetMaxHitPoints);
    insert(619, "SWMG_GetSphereRadius", R_FLOAT, {R_OBJECT}, &SWMG_GetSphereRadius);
    insert(620, "SWMG_SetSphereRadius", R_VOID, {R_OBJECT, R_FLOAT}, &SWMG_SetSphereRadius);
    insert(621, "SWMG_GetNumLoops", R_INT, {R_OBJECT}, &SWMG_GetNumLoops);
    insert(622, "SWMG_SetNumLoops", R_VOID, {R_OBJECT, R_INT}, &SWMG_SetNumLoops);
    insert(623, "SWMG_GetPosition", R_VECTOR, {R_OBJECT}, &SWMG_GetPosition);
    insert(624, "SWMG_GetGunBankCount", R_INT, {R_OBJECT}, &SWMG_GetGunBankCount);
    insert(625, "SWMG_GetGunBankBulletModel", R_STRING, {R_OBJECT, R_INT}, &SWMG_GetGunBankBulletModel);
    insert(626, "SWMG_GetGunBankGunModel", R_STRING, {R_OBJECT, R_INT}, &SWMG_GetGunBankGunModel);
    insert(627, "SWMG_GetGunBankDamage", R_INT, {R_OBJECT, R_INT}, &SWMG_GetGunBankDamage);
    insert(628, "SWMG_GetGunBankTimeBetweenShots", R_FLOAT, {R_OBJECT, R_INT}, &SWMG_GetGunBankTimeBetweenShots);
    insert(629, "SWMG_GetGunBankLifespan", R_FLOAT, {R_OBJECT, R_INT}, &SWMG_GetGunBankLifespan);
    insert(630, "SWMG_GetGunBankSpeed", R_FLOAT, {R_OBJECT, R_INT}, &SWMG_GetGunBankSpeed);
    insert(631, "SWMG_GetGunBankTarget", R_INT, {R_OBJECT, R_INT}, &SWMG_GetGunBankTarget);
    insert(632, "SWMG_SetGunBankBulletModel", R_VOID, {R_OBJECT, R_INT, R_STRING}, &SWMG_SetGunBankBulletModel);
    insert(633, "SWMG_SetGunBankGunModel", R_VOID, {R_OBJECT, R_INT, R_STRING}, &SWMG_SetGunBankGunModel);
    insert(634, "SWMG_SetGunBankDamage", R_VOID, {R_OBJECT, R_INT, R_INT}, &SWMG_SetGunBankDamage);
    insert(635, "SWMG_SetGunBankTimeBetweenShots", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &SWMG_SetGunBankTimeBetweenShots);
    insert(636, "SWMG_SetGunBankLifespan", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &SWMG_SetGunBankLifespan);
    insert(637, "SWMG_SetGunBankSpeed", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &SWMG_SetGunBankSpeed);
    insert(638, "SWMG_SetGunBankTarget", R_VOID, {R_OBJECT, R_INT, R_INT}, &SWMG_SetGunBankTarget);
    insert(639, "SWMG_GetLastBulletHitPart", R_STRING, {}, &SWMG_GetLastBulletHitPart);
    insert(640, "SWMG_IsGunBankTargetting", R_INT, {R_OBJECT, R_INT}, &SWMG_IsGunBankTargetting);
    insert(641, "SWMG_GetPlayerOffset", R_VECTOR, {}, &SWMG_GetPlayerOffset);
    insert(642, "SWMG_GetPlayerInvincibility", R_FLOAT, {}, &SWMG_GetPlayerInvincibility);
    insert(643, "SWMG_GetPlayerSpeed", R_FLOAT, {}, &SWMG_GetPlayerSpeed);
    insert(644, "SWMG_GetPlayerMinSpeed", R_FLOAT, {}, &SWMG_GetPlayerMinSpeed);
    insert(645, "SWMG_GetPlayerAccelerationPerSecond", R_FLOAT, {}, &SWMG_GetPlayerAccelerationPerSecond);
    insert(646, "SWMG_GetPlayerTunnelPos", R_VECTOR, {}, &SWMG_GetPlayerTunnelPos);
    insert(647, "SWMG_SetPlayerOffset", R_VOID, {R_VECTOR}, &SWMG_SetPlayerOffset);
    insert(648, "SWMG_SetPlayerInvincibility", R_VOID, {R_FLOAT}, &SWMG_SetPlayerInvincibility);
    insert(649, "SWMG_SetPlayerSpeed", R_VOID, {R_FLOAT}, &SWMG_SetPlayerSpeed);
    insert(650, "SWMG_SetPlayerMinSpeed", R_VOID, {R_FLOAT}, &SWMG_SetPlayerMinSpeed);
    insert(651, "SWMG_SetPlayerAccelerationPerSecond", R_VOID, {R_FLOAT}, &SWMG_SetPlayerAccelerationPerSecond);
    insert(652, "SWMG_SetPlayerTunnelPos", R_VOID, {R_VECTOR}, &SWMG_SetPlayerTunnelPos);
    insert(653, "SWMG_GetPlayerTunnelNeg", R_VECTOR, {}, &SWMG_GetPlayerTunnelNeg);
    insert(654, "SWMG_SetPlayerTunnelNeg", R_VOID, {R_VECTOR}, &SWMG_SetPlayerTunnelNeg);
    insert(655, "SWMG_GetPlayerOrigin", R_VECTOR, {}, &SWMG_GetPlayerOrigin);
    insert(656, "SWMG_SetPlayerOrigin", R_VOID, {R_VECTOR}, &SWMG_SetPlayerOrigin);
    insert(657, "SWMG_GetGunBankHorizontalSpread", R_FLOAT, {R_OBJECT, R_INT}, &SWMG_GetGunBankHorizontalSpread);
    insert(658, "SWMG_GetGunBankVerticalSpread", R_FLOAT, {R_OBJECT, R_INT}, &SWMG_GetGunBankVerticalSpread);
    insert(659, "SWMG_GetGunBankSensingRadius", R_FLOAT, {R_OBJECT, R_INT}, &SWMG_GetGunBankSensingRadius);
    insert(660, "SWMG_GetGunBankInaccuracy", R_FLOAT, {R_OBJECT, R_INT}, &SWMG_GetGunBankInaccuracy);
    insert(661, "SWMG_SetGunBankHorizontalSpread", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &SWMG_SetGunBankHorizontalSpread);
    insert(662, "SWMG_SetGunBankVerticalSpread", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &SWMG_SetGunBankVerticalSpread);
    insert(663, "SWMG_SetGunBankSensingRadius", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &SWMG_SetGunBankSensingRadius);
    insert(664, "SWMG_SetGunBankInaccuracy", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &SWMG_SetGunBankInaccuracy);
    insert(665, "SWMG_GetIsInvulnerable", R_INT, {R_OBJECT}, &SWMG_GetIsInvulnerable);
    insert(666, "SWMG_StartInvulnerability", R_VOID, {R_OBJECT}, &SWMG_StartInvulnerability);
    insert(667, "SWMG_GetPlayerMaxSpeed", R_FLOAT, {}, &SWMG_GetPlayerMaxSpeed);
    insert(668, "SWMG_SetPlayerMaxSpeed", R_VOID, {R_FLOAT}, &SWMG_SetPlayerMaxSpeed);
    insert(683, "SWMG_GetSoundFrequency", R_INT, {R_OBJECT, R_INT}, &SWMG_GetSoundFrequency);
    insert(684, "SWMG_SetSoundFrequency", R_VOID, {R_OBJECT, R_INT, R_INT}, &SWMG_SetSoundFrequency);
    insert(685, "SWMG_GetSoundFrequencyIsRandom", R_INT, {R_OBJECT, R_INT}, &SWMG_GetSoundFrequencyIsRandom);
    insert(686, "SWMG_SetSoundFrequencyIsRandom", R_VOID, {R_OBJECT, R_INT, R_INT}, &SWMG_SetSoundFrequencyIsRandom);
    insert(687, "SWMG_GetSoundVolume", R_INT, {R_OBJECT, R_INT}, &SWMG_GetSoundVolume);
    insert(688, "SWMG_SetSoundVolume", R_VOID, {R_OBJECT, R_INT, R_INT}, &SWMG_SetSoundVolume);
    insert(717, "SWMG_GetPlayerTunnelInfinite", R_VECTOR, {}, &SWMG_GetPlayerTunnelInfinite);
    insert(718, "SWMG_SetPlayerTunnelInfinite", R_VOID, {R_VECTOR}, &SWMG_SetPlayerTunnelInfinite);
}

void Routines::registerMinigameTslRoutines() {
    insert(520, "SWMG_SetLateralAccelerationPerSecond", R_VOID, {R_FLOAT}, &SWMG_SetLateralAccelerationPerSecond);
    insert(521, "SWMG_GetLateralAccelerationPerSecond", R_FLOAT, {}, &SWMG_GetLateralAccelerationPerSecond);
    insert(563, "SWMG_SetSpeedBlurEffect", R_VOID, {R_INT, R_FLOAT}, &SWMG_SetSpeedBlurEffect);
    insert(583, "SWMG_GetLastEvent", R_STRING, {}, &SWMG_GetLastEvent);
    insert(584, "SWMG_GetLastEventModelName", R_STRING, {}, &SWMG_GetLastEventModelName);
    insert(585, "SWMG_GetObjectByName", R_OBJECT, {R_STRING}, &SWMG_GetObjectByName);
    insert(586, "SWMG_PlayAnimation", R_VOID, {R_OBJECT, R_STRING, R_INT, R_INT, R_INT}, &SWMG_PlayAnimation);
    insert(587, "SWMG_GetLastBulletHitDamage", R_INT, {}, &SWMG_GetLastBulletHitDamage);
    insert(588, "SWMG_GetLastBulletHitTarget", R_INT, {}, &SWMG_GetLastBulletHitTarget);
    insert(589, "SWMG_GetLastBulletHitShooter", R_OBJECT, {}, &SWMG_GetLastBulletHitShooter);
    insert(590, "SWMG_AdjustFollowerHitPoints", R_INT, {R_OBJECT, R_INT, R_INT}, &SWMG_AdjustFollowerHitPoints);
    insert(591, "SWMG_OnBulletHit", R_VOID, {}, &SWMG_OnBulletHit);
    insert(592, "SWMG_OnObstacleHit", R_VOID, {}, &SWMG_OnObstacleHit);
    insert(593, "SWMG_GetLastFollowerHit", R_OBJECT, {}, &SWMG_GetLastFollowerHit);
    insert(594, "SWMG_GetLastObstacleHit", R_OBJECT, {}, &SWMG_GetLastObstacleHit);
    insert(595, "SWMG_GetLastBulletFiredDamage", R_INT, {}, &SWMG_GetLastBulletFiredDamage);
    insert(596, "SWMG_GetLastBulletFiredTarget", R_INT, {}, &SWMG_GetLastBulletFiredTarget);
    insert(597, "SWMG_GetObjectName", R_STRING, {R_OBJECT}, &SWMG_GetObjectName);
    insert(598, "SWMG_OnDeath", R_VOID, {}, &SWMG_OnDeath);
    insert(599, "SWMG_IsFollower", R_INT, {R_OBJECT}, &SWMG_IsFollower);
    insert(600, "SWMG_IsPlayer", R_INT, {R_OBJECT}, &SWMG_IsPlayer);
    insert(601, "SWMG_IsEnemy", R_INT, {R_OBJECT}, &SWMG_IsEnemy);
    insert(602, "SWMG_IsTrigger", R_INT, {R_OBJECT}, &SWMG_IsTrigger);
    insert(603, "SWMG_IsObstacle", R_INT, {R_OBJECT}, &SWMG_IsObstacle);
    insert(604, "SWMG_SetFollowerHitPoints", R_VOID, {R_OBJECT, R_INT}, &SWMG_SetFollowerHitPoints);
    insert(605, "SWMG_OnDamage", R_VOID, {}, &SWMG_OnDamage);
    insert(606, "SWMG_GetLastHPChange", R_INT, {}, &SWMG_GetLastHPChange);
    insert(607, "SWMG_RemoveAnimation", R_VOID, {R_OBJECT, R_STRING}, &SWMG_RemoveAnimation);
    insert(608, "SWMG_GetCameraNearClip", R_FLOAT, {}, &SWMG_GetCameraNearClip);
    insert(609, "SWMG_GetCameraFarClip", R_FLOAT, {}, &SWMG_GetCameraFarClip);
    insert(610, "SWMG_SetCameraClip", R_VOID, {R_FLOAT, R_FLOAT}, &SWMG_SetCameraClip);
    insert(611, "SWMG_GetPlayer", R_OBJECT, {}, &SWMG_GetPlayer);
    insert(612, "SWMG_GetEnemyCount", R_INT, {}, &SWMG_GetEnemyCount);
    insert(613, "SWMG_GetEnemy", R_OBJECT, {R_INT}, &SWMG_GetEnemy);
    insert(614, "SWMG_GetObstacleCount", R_INT, {}, &SWMG_GetObstacleCount);
    insert(615, "SWMG_GetObstacle", R_OBJECT, {R_INT}, &SWMG_GetObstacle);
    insert(616, "SWMG_GetHitPoints", R_INT, {R_OBJECT}, &SWMG_GetHitPoints);
    insert(617, "SWMG_GetMaxHitPoints", R_INT, {R_OBJECT}, &SWMG_GetMaxHitPoints);
    insert(618, "SWMG_SetMaxHitPoints", R_VOID, {R_OBJECT, R_INT}, &SWMG_SetMaxHitPoints);
    insert(619, "SWMG_GetSphereRadius", R_FLOAT, {R_OBJECT}, &SWMG_GetSphereRadius);
    insert(620, "SWMG_SetSphereRadius", R_VOID, {R_OBJECT, R_FLOAT}, &SWMG_SetSphereRadius);
    insert(621, "SWMG_GetNumLoops", R_INT, {R_OBJECT}, &SWMG_GetNumLoops);
    insert(622, "SWMG_SetNumLoops", R_VOID, {R_OBJECT, R_INT}, &SWMG_SetNumLoops);
    insert(623, "SWMG_GetPosition", R_VECTOR, {R_OBJECT}, &SWMG_GetPosition);
    insert(624, "SWMG_GetGunBankCount", R_INT, {R_OBJECT}, &SWMG_GetGunBankCount);
    insert(625, "SWMG_GetGunBankBulletModel", R_STRING, {R_OBJECT, R_INT}, &SWMG_GetGunBankBulletModel);
    insert(626, "SWMG_GetGunBankGunModel", R_STRING, {R_OBJECT, R_INT}, &SWMG_GetGunBankGunModel);
    insert(627, "SWMG_GetGunBankDamage", R_INT, {R_OBJECT, R_INT}, &SWMG_GetGunBankDamage);
    insert(628, "SWMG_GetGunBankTimeBetweenShots", R_FLOAT, {R_OBJECT, R_INT}, &SWMG_GetGunBankTimeBetweenShots);
    insert(629, "SWMG_GetGunBankLifespan", R_FLOAT, {R_OBJECT, R_INT}, &SWMG_GetGunBankLifespan);
    insert(630, "SWMG_GetGunBankSpeed", R_FLOAT, {R_OBJECT, R_INT}, &SWMG_GetGunBankSpeed);
    insert(631, "SWMG_GetGunBankTarget", R_INT, {R_OBJECT, R_INT}, &SWMG_GetGunBankTarget);
    insert(632, "SWMG_SetGunBankBulletModel", R_VOID, {R_OBJECT, R_INT, R_STRING}, &SWMG_SetGunBankBulletModel);
    insert(633, "SWMG_SetGunBankGunModel", R_VOID, {R_OBJECT, R_INT, R_STRING}, &SWMG_SetGunBankGunModel);
    insert(634, "SWMG_SetGunBankDamage", R_VOID, {R_OBJECT, R_INT, R_INT}, &SWMG_SetGunBankDamage);
    insert(635, "SWMG_SetGunBankTimeBetweenShots", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &SWMG_SetGunBankTimeBetweenShots);
    insert(636, "SWMG_SetGunBankLifespan", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &SWMG_SetGunBankLifespan);
    insert(637, "SWMG_SetGunBankSpeed", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &SWMG_SetGunBankSpeed);
    insert(638, "SWMG_SetGunBankTarget", R_VOID, {R_OBJECT, R_INT, R_INT}, &SWMG_SetGunBankTarget);
    insert(639, "SWMG_GetLastBulletHitPart", R_STRING, {}, &SWMG_GetLastBulletHitPart);
    insert(640, "SWMG_IsGunBankTargetting", R_INT, {R_OBJECT, R_INT}, &SWMG_IsGunBankTargetting);
    insert(641, "SWMG_GetPlayerOffset", R_VECTOR, {}, &SWMG_GetPlayerOffset);
    insert(642, "SWMG_GetPlayerInvincibility", R_FLOAT, {}, &SWMG_GetPlayerInvincibility);
    insert(643, "SWMG_GetPlayerSpeed", R_FLOAT, {}, &SWMG_GetPlayerSpeed);
    insert(644, "SWMG_GetPlayerMinSpeed", R_FLOAT, {}, &SWMG_GetPlayerMinSpeed);
    insert(645, "SWMG_GetPlayerAccelerationPerSecond", R_FLOAT, {}, &SWMG_GetPlayerAccelerationPerSecond);
    insert(646, "SWMG_GetPlayerTunnelPos", R_VECTOR, {}, &SWMG_GetPlayerTunnelPos);
    insert(647, "SWMG_SetPlayerOffset", R_VOID, {R_VECTOR}, &SWMG_SetPlayerOffset);
    insert(648, "SWMG_SetPlayerInvincibility", R_VOID, {R_FLOAT}, &SWMG_SetPlayerInvincibility);
    insert(649, "SWMG_SetPlayerSpeed", R_VOID, {R_FLOAT}, &SWMG_SetPlayerSpeed);
    insert(650, "SWMG_SetPlayerMinSpeed", R_VOID, {R_FLOAT}, &SWMG_SetPlayerMinSpeed);
    insert(651, "SWMG_SetPlayerAccelerationPerSecond", R_VOID, {R_FLOAT}, &SWMG_SetPlayerAccelerationPerSecond);
    insert(652, "SWMG_SetPlayerTunnelPos", R_VOID, {R_VECTOR}, &SWMG_SetPlayerTunnelPos);
    insert(653, "SWMG_GetPlayerTunnelNeg", R_VECTOR, {}, &SWMG_GetPlayerTunnelNeg);
    insert(654, "SWMG_SetPlayerTunnelNeg", R_VOID, {R_VECTOR}, &SWMG_SetPlayerTunnelNeg);
    insert(655, "SWMG_GetPlayerOrigin", R_VECTOR, {}, &SWMG_GetPlayerOrigin);
    insert(656, "SWMG_SetPlayerOrigin", R_VOID, {R_VECTOR}, &SWMG_SetPlayerOrigin);
    insert(657, "SWMG_GetGunBankHorizontalSpread", R_FLOAT, {R_OBJECT, R_INT}, &SWMG_GetGunBankHorizontalSpread);
    insert(658, "SWMG_GetGunBankVerticalSpread", R_FLOAT, {R_OBJECT, R_INT}, &SWMG_GetGunBankVerticalSpread);
    insert(659, "SWMG_GetGunBankSensingRadius", R_FLOAT, {R_OBJECT, R_INT}, &SWMG_GetGunBankSensingRadius);
    insert(660, "SWMG_GetGunBankInaccuracy", R_FLOAT, {R_OBJECT, R_INT}, &SWMG_GetGunBankInaccuracy);
    insert(661, "SWMG_SetGunBankHorizontalSpread", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &SWMG_SetGunBankHorizontalSpread);
    insert(662, "SWMG_SetGunBankVerticalSpread", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &SWMG_SetGunBankVerticalSpread);
    insert(663, "SWMG_SetGunBankSensingRadius", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &SWMG_SetGunBankSensingRadius);
    insert(664, "SWMG_SetGunBankInaccuracy", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &SWMG_SetGunBankInaccuracy);
    insert(665, "SWMG_GetIsInvulnerable", R_INT, {R_OBJECT}, &SWMG_GetIsInvulnerable);
    insert(666, "SWMG_StartInvulnerability", R_VOID, {R_OBJECT}, &SWMG_StartInvulnerability);
    insert(667, "SWMG_GetPlayerMaxSpeed", R_FLOAT, {}, &SWMG_GetPlayerMaxSpeed);
    insert(668, "SWMG_SetPlayerMaxSpeed", R_VOID, {R_FLOAT}, &SWMG_SetPlayerMaxSpeed);
    insert(683, "SWMG_GetSoundFrequency", R_INT, {R_OBJECT, R_INT}, &SWMG_GetSoundFrequency);
    insert(684, "SWMG_SetSoundFrequency", R_VOID, {R_OBJECT, R_INT, R_INT}, &SWMG_SetSoundFrequency);
    insert(685, "SWMG_GetSoundFrequencyIsRandom", R_INT, {R_OBJECT, R_INT}, &SWMG_GetSoundFrequencyIsRandom);
    insert(686, "SWMG_SetSoundFrequencyIsRandom", R_VOID, {R_OBJECT, R_INT, R_INT}, &SWMG_SetSoundFrequencyIsRandom);
    insert(687, "SWMG_GetSoundVolume", R_INT, {R_OBJECT, R_INT}, &SWMG_GetSoundVolume);
    insert(688, "SWMG_SetSoundVolume", R_VOID, {R_OBJECT, R_INT, R_INT}, &SWMG_SetSoundVolume);
    insert(717, "SWMG_GetPlayerTunnelInfinite", R_VECTOR, {}, &SWMG_GetPlayerTunnelInfinite);
    insert(718, "SWMG_SetPlayerTunnelInfinite", R_VOID, {R_VECTOR}, &SWMG_SetPlayerTunnelInfinite);
    insert(782, "SWMG_GetSwoopUpgrade", R_INT, {R_INT}, &SWMG_GetSwoopUpgrade);
    insert(789, "SWMG_GetTrackPosition", R_VECTOR, {R_OBJECT}, &SWMG_GetTrackPosition);
    insert(790, "SWMG_SetFollowerPosition", R_VECTOR, {R_VECTOR}, &SWMG_SetFollowerPosition);
    insert(792, "SWMG_DestroyMiniGameObject", R_VOID, {R_OBJECT}, &SWMG_DestroyMiniGameObject);
    insert(804, "SWMG_SetJumpSpeed", R_VOID, {R_FLOAT}, &SWMG_SetJumpSpeed);
    insert(825, "SWMG_PlayerApplyForce", R_VOID, {R_VECTOR}, &SWMG_PlayerApplyForce);
}

} // namespace game

} // namespace reone
