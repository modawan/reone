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

// The swoop race runs on MinigameSession, the turret on Turret. Every SWMG_
// routine but SWMG_SetSpeedBlurEffect has one body for each, and goes to the
// one that is running (see sessionOrTurret).
namespace session {

namespace {

MinigameSession *mgSession(const RoutineContext &ctx) {
    return ctx.game.minigameSession();
}

uint32_t mgCallerId(const RoutineContext &ctx) {
    if (const Variable *caller = ctx.execution.findArg(ArgKind::Caller)) {
        return caller->objectId;
    }
    return kObjectInvalid;
}

// The minigame actor an object argument names; OBJECT_SELF is the caller.
MinigameActor *mgActor(const std::vector<Variable> &args, int index, const RoutineContext &ctx) {
    auto session = mgSession(ctx);
    if (!session || index >= static_cast<int>(args.size()) || args[index].type != VariableType::Object) {
        return nullptr;
    }
    uint32_t id = args[index].objectId;
    if (id == kObjectSelf) {
        id = mgCallerId(ctx);
    }
    return session->actorByObjectId(id);
}

MinigameActor *mgCaller(const RoutineContext &ctx) {
    auto session = mgSession(ctx);
    return session ? session->actorByObjectId(mgCallerId(ctx)) : nullptr;
}

MinigameActor *mgFollower(MinigameActor *actor) {
    return (actor && actor->follower()) ? actor : nullptr;
}

MinigameGunBank *mgGunBank(MinigameActor *actor, int bank) {
    if (!actor || bank < 0 || bank >= static_cast<int>(actor->gunBanks.size())) {
        return nullptr;
    }
    return &actor->gunBanks[bank];
}

Variable mgObject(const MinigameActor *actor) {
    return Variable::ofObject(actor ? actor->objectId : kObjectInvalid);
}

int mgSoundSlot(int sound) {
    return (sound >= 0 && sound < 2) ? sound : -1;
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
    if (auto session = mgSession(ctx)) {
        session->setLateralAccel(fLAPS);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetLateralAccelerationPerSecond(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto session = mgSession(ctx);
    return Variable::ofFloat(session ? session->lateralAccel() : 0.0f);
}

static Variable SWMG_GetLastEvent(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto actor = mgCaller(ctx);
    return Variable::ofString(actor ? actor->lastEvent : std::string());
}

static Variable SWMG_GetLastEventModelName(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto actor = mgCaller(ctx);
    return Variable::ofString(actor ? actor->lastEventModelName : std::string());
}

static Variable SWMG_GetObjectByName(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sName = getString(args, 0);

    // Execute
    auto session = mgSession(ctx);
    return mgObject(session ? session->findByName(sName) : nullptr);
}

static Variable SWMG_PlayAnimation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = mgActor(args, 0, ctx);
    auto sAnimName = getString(args, 1);
    auto bLooping = getIntOrElse(args, 2, 1);
    auto bQueue = getIntOrElse(args, 3, 0);
    auto bOverlay = getIntOrElse(args, 4, 0);

    // Execute
    auto session = mgSession(ctx);
    if (session && oObject) {
        session->playAnimation(*oObject, sAnimName, bLooping != 0, bQueue != 0, bOverlay != 0);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetLastBulletHitDamage(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto actor = mgCaller(ctx);
    return Variable::ofInt(actor ? actor->lastBulletHitDamage : 0);
}

static Variable SWMG_GetLastBulletHitTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto actor = mgCaller(ctx);
    return Variable::ofInt(actor ? actor->lastBulletHitTarget : 0);
}

static Variable SWMG_GetLastBulletHitShooter(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto actor = mgCaller(ctx);
    auto session = mgSession(ctx);
    MinigameActor *shooter = (actor && session) ? session->actorByObjectId(actor->lastBulletHitShooter) : nullptr;
    return mgObject(shooter);
}

static Variable SWMG_AdjustFollowerHitPoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollower = mgFollower(mgActor(args, 0, ctx));
    auto nHP = getInt(args, 1);

    // Execute: a hit point change, which raises the follower's OnDamage.
    auto session = mgSession(ctx);
    if (!session || !oFollower) {
        return Variable::ofInt(0);
    }
    session->changeHitPoints(*oFollower, nHP);
    return Variable::ofInt(oFollower->hitPoints);
}

static Variable SWMG_OnBulletHit(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: the default bullet hit for the caller.
    auto session = mgSession(ctx);
    auto actor = mgCaller(ctx);
    if (session && actor) {
        session->defaultBulletHit(*actor);
    }
    return Variable::ofNull();
}

static Variable SWMG_OnObstacleHit(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: the default obstacle hit for the calling follower.
    auto session = mgSession(ctx);
    auto actor = mgFollower(mgCaller(ctx));
    if (session && actor) {
        session->defaultObstacleHit(*actor);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetLastFollowerHit(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto session = mgSession(ctx);
    auto actor = mgCaller(ctx);
    MinigameActor *hit = (session && actor) ? session->actorByObjectId(actor->lastFollowerHit) : nullptr;
    return mgObject(mgFollower(hit));
}

static Variable SWMG_GetLastObstacleHit(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto session = mgSession(ctx);
    auto actor = mgCaller(ctx);
    MinigameActor *hit = (session && actor) ? session->actorByObjectId(actor->lastObstacleHit) : nullptr;
    return mgObject((hit && hit->kind == MinigameActorKind::Obstacle) ? hit : nullptr);
}

static Variable SWMG_GetLastBulletFiredDamage(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto actor = mgCaller(ctx);
    return Variable::ofInt(actor ? actor->lastBulletFiredDamage : 0);
}

static Variable SWMG_GetLastBulletFiredTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto actor = mgCaller(ctx);
    return Variable::ofInt(actor ? actor->lastBulletFiredTarget : 0);
}

static Variable SWMG_GetObjectName(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oid = args.empty() ? mgCaller(ctx) : mgActor(args, 0, ctx);

    // Execute
    return Variable::ofString(oid ? oid->name : std::string());
}

static Variable SWMG_OnDeath(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: the default death for the calling follower.
    auto session = mgSession(ctx);
    auto actor = mgFollower(mgCaller(ctx));
    if (session && actor) {
        session->defaultDeath(*actor);
    }
    return Variable::ofNull();
}

static Variable SWMG_IsFollower(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oid = args.empty() ? mgCaller(ctx) : mgActor(args, 0, ctx);

    // Execute
    return Variable::ofInt(mgFollower(oid) ? 1 : 0);
}

static Variable SWMG_IsPlayer(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oid = args.empty() ? mgCaller(ctx) : mgActor(args, 0, ctx);

    // Execute
    return Variable::ofInt((oid && oid->kind == MinigameActorKind::Player) ? 1 : 0);
}

static Variable SWMG_IsEnemy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oid = args.empty() ? mgCaller(ctx) : mgActor(args, 0, ctx);

    // Execute
    return Variable::ofInt((oid && oid->kind == MinigameActorKind::Enemy) ? 1 : 0);
}

static Variable SWMG_IsTrigger(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oid = args.empty() ? mgCaller(ctx) : mgActor(args, 0, ctx);

    // Execute
    return Variable::ofInt((oid && oid->kind == MinigameActorKind::Enemy && oid->trigger) ? 1 : 0);
}

static Variable SWMG_IsObstacle(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oid = args.empty() ? mgCaller(ctx) : mgActor(args, 0, ctx);

    // Execute
    return Variable::ofInt((oid && oid->kind == MinigameActorKind::Obstacle) ? 1 : 0);
}

static Variable SWMG_SetFollowerHitPoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollower = mgFollower(mgActor(args, 0, ctx));
    auto nHP = getInt(args, 1);

    // Execute: sets the value directly, without an OnDamage event.
    if (oFollower && nHP > 0) {
        oFollower->hitPoints = nHP;
    }
    return Variable::ofNull();
}

static Variable SWMG_OnDamage(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: the default damage for the calling follower.
    auto session = mgSession(ctx);
    auto actor = mgFollower(mgCaller(ctx));
    if (session && actor) {
        session->defaultDamage(*actor);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetLastHPChange(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto actor = mgCaller(ctx);
    return Variable::ofInt(actor ? actor->lastHPChange : 0);
}

static Variable SWMG_RemoveAnimation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = mgActor(args, 0, ctx);
    auto sAnimName = getString(args, 1);

    // Execute
    auto session = mgSession(ctx);
    if (session && oObject) {
        session->removeAnimation(*oObject, sAnimName);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetCameraNearClip(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto session = mgSession(ctx);
    return Variable::ofFloat(session ? session->nearClip() : 0.0f);
}

static Variable SWMG_GetCameraFarClip(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto session = mgSession(ctx);
    return Variable::ofFloat(session ? session->farClip() : 0.0f);
}

static Variable SWMG_SetCameraClip(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fNear = getFloat(args, 0);
    auto fFar = getFloat(args, 1);

    // Execute
    if (auto session = mgSession(ctx)) {
        session->setCameraClip(fNear, fFar);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetPlayer(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto session = mgSession(ctx);
    return mgObject(session ? session->player() : nullptr);
}

static Variable SWMG_GetEnemyCount(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto session = mgSession(ctx);
    return Variable::ofInt(session ? session->enemyCount() : 0);
}

static Variable SWMG_GetEnemy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nEntry = getInt(args, 0);

    // Execute
    auto session = mgSession(ctx);
    return mgObject(session ? session->enemy(nEntry) : nullptr);
}

static Variable SWMG_GetObstacleCount(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto session = mgSession(ctx);
    return Variable::ofInt(session ? session->obstacleCount() : 0);
}

static Variable SWMG_GetObstacle(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nEntry = getInt(args, 0);

    // Execute
    auto session = mgSession(ctx);
    return mgObject(session ? session->obstacle(nEntry) : nullptr);
}

static Variable SWMG_GetHitPoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollower = mgFollower(mgActor(args, 0, ctx));

    // Execute
    return Variable::ofInt(oFollower ? oFollower->hitPoints : 0);
}

static Variable SWMG_GetMaxHitPoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollower = mgFollower(mgActor(args, 0, ctx));

    // Execute
    return Variable::ofInt(oFollower ? oFollower->maxHitPoints : 0);
}

static Variable SWMG_SetMaxHitPoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollower = mgFollower(mgActor(args, 0, ctx));
    auto nMaxHP = getInt(args, 1);

    // Execute
    if (oFollower && nMaxHP > 0) {
        oFollower->maxHitPoints = nMaxHP;
    }
    return Variable::ofNull();
}

static Variable SWMG_GetSphereRadius(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollower = mgFollower(mgActor(args, 0, ctx));

    // Execute
    return Variable::ofFloat(oFollower ? oFollower->sphereRadius : 0.0f);
}

static Variable SWMG_SetSphereRadius(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollower = mgFollower(mgActor(args, 0, ctx));
    auto fRadius = getFloat(args, 1);

    // Execute
    if (oFollower && fRadius >= 0.0f) {
        oFollower->sphereRadius = fRadius;
    }
    return Variable::ofNull();
}

static Variable SWMG_GetNumLoops(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollower = mgFollower(mgActor(args, 0, ctx));

    // Execute
    return Variable::ofInt(oFollower ? oFollower->numLoops : 0);
}

static Variable SWMG_SetNumLoops(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollower = mgFollower(mgActor(args, 0, ctx));
    auto nNumLoops = getInt(args, 1);

    // Execute
    if (oFollower) {
        oFollower->numLoops = nNumLoops;
    }
    return Variable::ofNull();
}

static Variable SWMG_GetPosition(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollower = mgActor(args, 0, ctx);

    // Execute
    // KotOR has no position for an obstacle.
    if (!oFollower || (!oFollower->follower() && !ctx.game.isTSL())) {
        return Variable::ofVector(glm::vec3(0.0f));
    }
    return Variable::ofVector(oFollower->position);
}

static Variable SWMG_GetGunBankCount(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollower = mgFollower(mgActor(args, 0, ctx));

    // Execute
    return Variable::ofInt(oFollower ? static_cast<int>(oFollower->gunBanks.size()) : 0);
}

static Variable SWMG_GetGunBankBulletModel(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = mgGunBank(mgFollower(mgActor(args, 0, ctx)), getInt(args, 1));

    // Execute
    return Variable::ofString(bank ? bank->spec.bullet.modelResRef : std::string());
}

static Variable SWMG_GetGunBankGunModel(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = mgGunBank(mgFollower(mgActor(args, 0, ctx)), getInt(args, 1));

    // Execute
    return Variable::ofString(bank ? bank->spec.gunModelResRef : std::string());
}

static Variable SWMG_GetGunBankDamage(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = mgGunBank(mgFollower(mgActor(args, 0, ctx)), getInt(args, 1));

    // Execute
    return Variable::ofInt(bank ? static_cast<int>(bank->spec.bullet.damage) : 0);
}

static Variable SWMG_GetGunBankTimeBetweenShots(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = mgGunBank(mgFollower(mgActor(args, 0, ctx)), getInt(args, 1));

    // Execute
    return Variable::ofFloat(bank ? bank->spec.bullet.rateOfFire : 0.0f);
}

static Variable SWMG_GetGunBankLifespan(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = mgGunBank(mgFollower(mgActor(args, 0, ctx)), getInt(args, 1));

    // Execute
    return Variable::ofFloat(bank ? bank->spec.bullet.lifespan : 0.0f);
}

static Variable SWMG_GetGunBankSpeed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = mgGunBank(mgFollower(mgActor(args, 0, ctx)), getInt(args, 1));

    // Execute
    return Variable::ofFloat(bank ? bank->spec.bullet.speed : 0.0f);
}

static Variable SWMG_GetGunBankTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = mgGunBank(mgFollower(mgActor(args, 0, ctx)), getInt(args, 1));

    // Execute
    return Variable::ofInt(bank ? static_cast<int>(bank->spec.bullet.targetType) : 0);
}

static Variable SWMG_SetGunBankBulletModel(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = mgGunBank(mgFollower(mgActor(args, 0, ctx)), getInt(args, 1));
    auto sBulletModel = getString(args, 2);

    // Execute
    if (bank) {
        bank->spec.bullet.modelResRef = sBulletModel;
    }
    return Variable::ofNull();
}

static Variable SWMG_SetGunBankGunModel(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = mgGunBank(mgFollower(mgActor(args, 0, ctx)), getInt(args, 1));
    auto sGunModel = getString(args, 2);

    // Execute
    if (bank) {
        bank->spec.gunModelResRef = sGunModel;
    }
    return Variable::ofNull();
}

static Variable SWMG_SetGunBankDamage(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = mgGunBank(mgFollower(mgActor(args, 0, ctx)), getInt(args, 1));
    auto nDamage = getInt(args, 2);

    // Execute
    if (bank) {
        bank->spec.bullet.damage = static_cast<uint32_t>(glm::max(0, nDamage));
    }
    return Variable::ofNull();
}

static Variable SWMG_SetGunBankTimeBetweenShots(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = mgGunBank(mgFollower(mgActor(args, 0, ctx)), getInt(args, 1));
    auto fTBS = getFloat(args, 2);

    // Execute
    if (bank) {
        bank->spec.bullet.rateOfFire = fTBS;
    }
    return Variable::ofNull();
}

static Variable SWMG_SetGunBankLifespan(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = mgGunBank(mgFollower(mgActor(args, 0, ctx)), getInt(args, 1));
    auto fLifespan = getFloat(args, 2);

    // Execute
    if (bank) {
        bank->spec.bullet.lifespan = fLifespan;
    }
    return Variable::ofNull();
}

static Variable SWMG_SetGunBankSpeed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = mgGunBank(mgFollower(mgActor(args, 0, ctx)), getInt(args, 1));
    auto fSpeed = getFloat(args, 2);

    // Execute
    if (bank) {
        bank->spec.bullet.speed = fSpeed;
    }
    return Variable::ofNull();
}

static Variable SWMG_SetGunBankTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = mgGunBank(mgFollower(mgActor(args, 0, ctx)), getInt(args, 1));
    auto nTarget = getInt(args, 2);

    // Execute
    if (bank) {
        bank->spec.bullet.targetType = static_cast<uint32_t>(glm::max(0, nTarget));
    }
    return Variable::ofNull();
}

static Variable SWMG_GetLastBulletHitPart(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto actor = mgCaller(ctx);
    return Variable::ofString(actor ? actor->lastBulletHitPart : std::string());
}

static Variable SWMG_IsGunBankTargetting(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollower = mgFollower(mgActor(args, 0, ctx));
    auto bank = mgGunBank(oFollower, getInt(args, 1));

    // Execute: enemy banks aim at the player; player banks fire where aimed.
    return Variable::ofInt((bank && oFollower->kind == MinigameActorKind::Enemy) ? 1 : 0);
}

static Variable SWMG_GetPlayerOffset(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto session = mgSession(ctx);
    return Variable::ofVector(session ? session->offset() : glm::vec3(0.0f));
}

static Variable SWMG_GetPlayerInvincibility(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto session = mgSession(ctx);
    auto player = session ? session->player() : nullptr;
    return Variable::ofFloat(player ? player->invincePeriod : 0.0f);
}

static Variable SWMG_GetPlayerSpeed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto session = mgSession(ctx);
    return Variable::ofFloat(session ? session->playerSpeed() : 0.0f);
}

static Variable SWMG_GetPlayerMinSpeed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto session = mgSession(ctx);
    return Variable::ofFloat(session ? session->minSpeed() : 0.0f);
}

static Variable SWMG_GetPlayerAccelerationPerSecond(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto session = mgSession(ctx);
    return Variable::ofFloat(session ? session->accelPerSecond() : 0.0f);
}

static Variable SWMG_GetPlayerTunnelPos(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto session = mgSession(ctx);
    return Variable::ofVector(session ? session->tunnelPos() : glm::vec3(0.0f));
}

static Variable SWMG_SetPlayerOffset(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vOffset = getVector(args, 0);

    // Execute
    if (auto session = mgSession(ctx)) {
        session->setPlayerOffset(vOffset);
    }
    return Variable::ofNull();
}

static Variable SWMG_SetPlayerInvincibility(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fInvincibility = getFloat(args, 0);

    // Execute
    auto session = mgSession(ctx);
    auto player = session ? session->player() : nullptr;
    if (player && fInvincibility >= 0.0f) {
        player->invincePeriod = fInvincibility;
    }
    return Variable::ofNull();
}

static Variable SWMG_SetPlayerSpeed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fSpeed = getFloat(args, 0);

    // Execute
    if (auto session = mgSession(ctx)) {
        session->setPlayerSpeed(fSpeed);
    }
    return Variable::ofNull();
}

static Variable SWMG_SetPlayerMinSpeed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fMinSpeed = getFloat(args, 0);

    // Execute
    if (auto session = mgSession(ctx)) {
        session->setMinSpeed(fMinSpeed);
    }
    return Variable::ofNull();
}

static Variable SWMG_SetPlayerAccelerationPerSecond(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fAPS = getFloat(args, 0);

    // Execute
    if (auto session = mgSession(ctx)) {
        session->setAccelPerSecond(fAPS);
    }
    return Variable::ofNull();
}

static Variable SWMG_SetPlayerTunnelPos(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vTunnel = getVector(args, 0);

    // Execute
    if (auto session = mgSession(ctx)) {
        session->setTunnelPos(vTunnel);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetPlayerTunnelNeg(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto session = mgSession(ctx);
    return Variable::ofVector(session ? session->tunnelNeg() : glm::vec3(0.0f));
}

static Variable SWMG_SetPlayerTunnelNeg(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vTunnel = getVector(args, 0);

    // Execute
    if (auto session = mgSession(ctx)) {
        session->setTunnelNeg(vTunnel);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetPlayerOrigin(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto session = mgSession(ctx);
    return Variable::ofVector(session ? session->origin() : glm::vec3(0.0f));
}

static Variable SWMG_SetPlayerOrigin(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vOrigin = getVector(args, 0);

    // Execute
    if (auto session = mgSession(ctx)) {
        session->setPlayerOrigin(vOrigin);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetGunBankHorizontalSpread(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = mgGunBank(mgFollower(mgActor(args, 0, ctx)), getInt(args, 1));

    // Execute
    return Variable::ofFloat(bank ? bank->spec.horizSpread : 0.0f);
}

static Variable SWMG_GetGunBankVerticalSpread(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = mgGunBank(mgFollower(mgActor(args, 0, ctx)), getInt(args, 1));

    // Execute
    return Variable::ofFloat(bank ? bank->spec.vertSpread : 0.0f);
}

static Variable SWMG_GetGunBankSensingRadius(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = mgGunBank(mgFollower(mgActor(args, 0, ctx)), getInt(args, 1));

    // Execute
    return Variable::ofFloat(bank ? bank->spec.sensingRadius : 0.0f);
}

static Variable SWMG_GetGunBankInaccuracy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = mgGunBank(mgFollower(mgActor(args, 0, ctx)), getInt(args, 1));

    // Execute
    return Variable::ofFloat(bank ? bank->spec.inaccuracy : 0.0f);
}

static Variable SWMG_SetGunBankHorizontalSpread(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = mgGunBank(mgFollower(mgActor(args, 0, ctx)), getInt(args, 1));
    auto fHorizontalSpread = getFloat(args, 2);

    // Execute
    if (bank) {
        bank->spec.horizSpread = fHorizontalSpread;
    }
    return Variable::ofNull();
}

static Variable SWMG_SetGunBankVerticalSpread(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = mgGunBank(mgFollower(mgActor(args, 0, ctx)), getInt(args, 1));
    auto fVerticalSpread = getFloat(args, 2);

    // Execute
    if (bank) {
        bank->spec.vertSpread = fVerticalSpread;
    }
    return Variable::ofNull();
}

static Variable SWMG_SetGunBankSensingRadius(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = mgGunBank(mgFollower(mgActor(args, 0, ctx)), getInt(args, 1));
    auto fSensingRadius = getFloat(args, 2);

    // Execute
    if (bank) {
        bank->spec.sensingRadius = fSensingRadius;
    }
    return Variable::ofNull();
}

static Variable SWMG_SetGunBankInaccuracy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bank = mgGunBank(mgFollower(mgActor(args, 0, ctx)), getInt(args, 1));
    auto fInaccuracy = getFloat(args, 2);

    // Execute
    if (bank) {
        bank->spec.inaccuracy = fInaccuracy;
    }
    return Variable::ofNull();
}

static Variable SWMG_GetIsInvulnerable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollower = mgFollower(mgActor(args, 0, ctx));

    // Execute
    return Variable::ofInt((oFollower && oFollower->invulnerable()) ? 1 : 0);
}

static Variable SWMG_StartInvulnerability(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollower = mgFollower(mgActor(args, 0, ctx));

    // Execute
    if (oFollower) {
        oFollower->invulnerableTime = oFollower->invincePeriod;
    }
    return Variable::ofNull();
}

static Variable SWMG_GetPlayerMaxSpeed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto session = mgSession(ctx);
    return Variable::ofFloat(session ? session->maxSpeed() : 0.0f);
}

static Variable SWMG_SetPlayerMaxSpeed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fMaxSpeed = getFloat(args, 0);

    // Execute
    if (auto session = mgSession(ctx)) {
        session->setPlayerMaxSpeed(fMaxSpeed);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetSoundFrequency(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollower = mgFollower(mgActor(args, 0, ctx));
    auto nSound = mgSoundSlot(getInt(args, 1));

    // Execute
    return Variable::ofInt((oFollower && nSound >= 0) ? oFollower->soundFrequency[nSound] : 0);
}

static Variable SWMG_SetSoundFrequency(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollower = mgFollower(mgActor(args, 0, ctx));
    auto nSound = mgSoundSlot(getInt(args, 1));
    auto nFrequency = getInt(args, 2);

    // Execute
    if (oFollower && nSound >= 0) {
        oFollower->soundFrequency[nSound] = nFrequency;
    }
    return Variable::ofNull();
}

static Variable SWMG_GetSoundFrequencyIsRandom(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollower = mgFollower(mgActor(args, 0, ctx));
    auto nSound = mgSoundSlot(getInt(args, 1));

    // Execute
    return Variable::ofInt((oFollower && nSound >= 0) ? oFollower->soundFrequencyRandom[nSound] : 0);
}

static Variable SWMG_SetSoundFrequencyIsRandom(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollower = mgFollower(mgActor(args, 0, ctx));
    auto nSound = mgSoundSlot(getInt(args, 1));
    auto bIsRandom = getInt(args, 2);

    // Execute
    if (oFollower && nSound >= 0) {
        oFollower->soundFrequencyRandom[nSound] = bIsRandom != 0 ? 1 : 0;
    }
    return Variable::ofNull();
}

static Variable SWMG_GetSoundVolume(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollower = mgFollower(mgActor(args, 0, ctx));
    auto nSound = mgSoundSlot(getInt(args, 1));

    // Execute
    return Variable::ofInt((oFollower && nSound >= 0) ? oFollower->soundVolume[nSound] : 0);
}

static Variable SWMG_SetSoundVolume(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollower = mgFollower(mgActor(args, 0, ctx));
    auto nSound = mgSoundSlot(getInt(args, 1));
    auto nVolume = getInt(args, 2);

    // Execute
    if (oFollower && nSound >= 0) {
        oFollower->soundVolume[nSound] = glm::clamp(nVolume, 0, 127);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetPlayerTunnelInfinite(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto session = mgSession(ctx);
    return Variable::ofVector(session ? session->tunnelInfinite() : glm::vec3(0.0f));
}

static Variable SWMG_SetPlayerTunnelInfinite(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vInfinite = getVector(args, 0);

    // Execute
    if (auto session = mgSession(ctx)) {
        session->setTunnelInfinite(vInfinite);
    }
    return Variable::ofNull();
}

static Variable SWMG_GetSwoopUpgrade(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSlot = getInt(args, 0);

    // Execute: the upgrade the party carries in swoop slot 1-3 (0-based here:
    // PT_SWOOP1-3); 0 for a slot out of range.
    const auto &upgrades = ctx.game.party().persistedState().swoopUpgrades;
    return Variable::ofInt(nSlot >= 0 && nSlot < static_cast<int>(upgrades.size()) ? static_cast<int>(upgrades[nSlot]) : 0);
}

static Variable SWMG_GetTrackPosition(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollower = mgFollower(mgActor(args, 0, ctx));

    // Execute: where the follower's hook sits on its track; the origin without a follower.
    return Variable::ofVector(oFollower ? glm::vec3(oFollower->hook[3]) : glm::vec3(0.0f));
}

static Variable SWMG_SetFollowerPosition(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vPos = getVector(args, 0);

    // Execute: moves the player's follower to the offset (kept inside the tunnel,
    // like SWMG_SetPlayerOffset) and answers the offset it ended on.
    auto session = mgSession(ctx);
    if (!session) {
        return Variable::ofVector(glm::vec3(0.0f));
    }
    session->setPlayerOffset(vPos);
    return Variable::ofVector(session->offset());
}

static Variable SWMG_DestroyMiniGameObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = mgActor(args, 0, ctx);

    // Execute: takes the object out of play and out of the enemy / obstacle lists without running its OnDeath.
    auto session = mgSession(ctx);
    if (session && oObject) {
        session->destroy(*oObject);
    }
    return Variable::ofNull();
}

static Variable SWMG_SetJumpSpeed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fSpeed = getFloat(args, 0);
    (void)fSpeed;

    // Execute
    // intentionally stubbed: the swoop model has no vertical motion for a jump speed to drive
    return Variable::ofNull();
}

static Variable SWMG_PlayerApplyForce(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vForce = getVector(args, 0);
    (void)vForce;

    // Execute
    // intentionally stubbed: the player moves along its track by steering only; there is no force model to push it
    return Variable::ofNull();
}

} // namespace session

namespace {

using RoutineFn = Variable (*)(const std::vector<Variable> &args, const RoutineContext &ctx);

template <RoutineFn SessionFn, RoutineFn TurretFn>
Variable sessionOrTurret(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return ctx.game.minigameSession() ? SessionFn(args, ctx) : TurretFn(args, ctx);
}

} // namespace

void Routines::registerMinigameKotorRoutines() {
    insert(520, "SWMG_SetLateralAccelerationPerSecond", R_VOID, {R_FLOAT}, &sessionOrTurret<&session::SWMG_SetLateralAccelerationPerSecond, &SWMG_SetLateralAccelerationPerSecond>);
    insert(521, "SWMG_GetLateralAccelerationPerSecond", R_FLOAT, {}, &sessionOrTurret<&session::SWMG_GetLateralAccelerationPerSecond, &SWMG_GetLateralAccelerationPerSecond>);
    insert(563, "SWMG_SetSpeedBlurEffect", R_VOID, {R_INT, R_FLOAT}, &SWMG_SetSpeedBlurEffect);
    insert(583, "SWMG_GetLastEvent", R_STRING, {}, &sessionOrTurret<&session::SWMG_GetLastEvent, &SWMG_GetLastEvent>);
    insert(584, "SWMG_GetLastEventModelName", R_STRING, {}, &sessionOrTurret<&session::SWMG_GetLastEventModelName, &SWMG_GetLastEventModelName>);
    insert(585, "SWMG_GetObjectByName", R_OBJECT, {R_STRING}, &sessionOrTurret<&session::SWMG_GetObjectByName, &SWMG_GetObjectByName>);
    insert(586, "SWMG_PlayAnimation", R_VOID, {R_OBJECT, R_STRING, R_INT, R_INT, R_INT}, &sessionOrTurret<&session::SWMG_PlayAnimation, &SWMG_PlayAnimation>);
    insert(587, "SWMG_GetLastBulletHitDamage", R_INT, {}, &sessionOrTurret<&session::SWMG_GetLastBulletHitDamage, &SWMG_GetLastBulletHitDamage>);
    insert(588, "SWMG_GetLastBulletHitTarget", R_INT, {}, &sessionOrTurret<&session::SWMG_GetLastBulletHitTarget, &SWMG_GetLastBulletHitTarget>);
    insert(589, "SWMG_GetLastBulletHitShooter", R_OBJECT, {}, &sessionOrTurret<&session::SWMG_GetLastBulletHitShooter, &SWMG_GetLastBulletHitShooter>);
    insert(590, "SWMG_AdjustFollowerHitPoints", R_INT, {R_OBJECT, R_INT, R_INT}, &sessionOrTurret<&session::SWMG_AdjustFollowerHitPoints, &SWMG_AdjustFollowerHitPoints>);
    insert(591, "SWMG_OnBulletHit", R_VOID, {}, &sessionOrTurret<&session::SWMG_OnBulletHit, &SWMG_OnBulletHit>);
    insert(592, "SWMG_OnObstacleHit", R_VOID, {}, &sessionOrTurret<&session::SWMG_OnObstacleHit, &SWMG_OnObstacleHit>);
    insert(593, "SWMG_GetLastFollowerHit", R_OBJECT, {}, &sessionOrTurret<&session::SWMG_GetLastFollowerHit, &SWMG_GetLastFollowerHit>);
    insert(594, "SWMG_GetLastObstacleHit", R_OBJECT, {}, &sessionOrTurret<&session::SWMG_GetLastObstacleHit, &SWMG_GetLastObstacleHit>);
    insert(595, "SWMG_GetLastBulletFiredDamage", R_INT, {}, &sessionOrTurret<&session::SWMG_GetLastBulletFiredDamage, &SWMG_GetLastBulletFiredDamage>);
    insert(596, "SWMG_GetLastBulletFiredTarget", R_INT, {}, &sessionOrTurret<&session::SWMG_GetLastBulletFiredTarget, &SWMG_GetLastBulletFiredTarget>);
    insert(597, "SWMG_GetObjectName", R_STRING, {R_OBJECT}, &sessionOrTurret<&session::SWMG_GetObjectName, &SWMG_GetObjectName>);
    insert(598, "SWMG_OnDeath", R_VOID, {}, &sessionOrTurret<&session::SWMG_OnDeath, &SWMG_OnDeath>);
    insert(599, "SWMG_IsFollower", R_INT, {R_OBJECT}, &sessionOrTurret<&session::SWMG_IsFollower, &SWMG_IsFollower>);
    insert(600, "SWMG_IsPlayer", R_INT, {R_OBJECT}, &sessionOrTurret<&session::SWMG_IsPlayer, &SWMG_IsPlayer>);
    insert(601, "SWMG_IsEnemy", R_INT, {R_OBJECT}, &sessionOrTurret<&session::SWMG_IsEnemy, &SWMG_IsEnemy>);
    insert(602, "SWMG_IsTrigger", R_INT, {R_OBJECT}, &sessionOrTurret<&session::SWMG_IsTrigger, &SWMG_IsTrigger>);
    insert(603, "SWMG_IsObstacle", R_INT, {R_OBJECT}, &sessionOrTurret<&session::SWMG_IsObstacle, &SWMG_IsObstacle>);
    insert(604, "SWMG_SetFollowerHitPoints", R_VOID, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_SetFollowerHitPoints, &SWMG_SetFollowerHitPoints>);
    insert(605, "SWMG_OnDamage", R_VOID, {}, &sessionOrTurret<&session::SWMG_OnDamage, &SWMG_OnDamage>);
    insert(606, "SWMG_GetLastHPChange", R_INT, {}, &sessionOrTurret<&session::SWMG_GetLastHPChange, &SWMG_GetLastHPChange>);
    insert(607, "SWMG_RemoveAnimation", R_VOID, {R_OBJECT, R_STRING}, &sessionOrTurret<&session::SWMG_RemoveAnimation, &SWMG_RemoveAnimation>);
    insert(608, "SWMG_GetCameraNearClip", R_FLOAT, {}, &sessionOrTurret<&session::SWMG_GetCameraNearClip, &SWMG_GetCameraNearClip>);
    insert(609, "SWMG_GetCameraFarClip", R_FLOAT, {}, &sessionOrTurret<&session::SWMG_GetCameraFarClip, &SWMG_GetCameraFarClip>);
    insert(610, "SWMG_SetCameraClip", R_VOID, {R_FLOAT, R_FLOAT}, &sessionOrTurret<&session::SWMG_SetCameraClip, &SWMG_SetCameraClip>);
    insert(611, "SWMG_GetPlayer", R_OBJECT, {}, &sessionOrTurret<&session::SWMG_GetPlayer, &SWMG_GetPlayer>);
    insert(612, "SWMG_GetEnemyCount", R_INT, {}, &sessionOrTurret<&session::SWMG_GetEnemyCount, &SWMG_GetEnemyCount>);
    insert(613, "SWMG_GetEnemy", R_OBJECT, {R_INT}, &sessionOrTurret<&session::SWMG_GetEnemy, &SWMG_GetEnemy>);
    insert(614, "SWMG_GetObstacleCount", R_INT, {}, &sessionOrTurret<&session::SWMG_GetObstacleCount, &SWMG_GetObstacleCount>);
    insert(615, "SWMG_GetObstacle", R_OBJECT, {R_INT}, &sessionOrTurret<&session::SWMG_GetObstacle, &SWMG_GetObstacle>);
    insert(616, "SWMG_GetHitPoints", R_INT, {R_OBJECT}, &sessionOrTurret<&session::SWMG_GetHitPoints, &SWMG_GetHitPoints>);
    insert(617, "SWMG_GetMaxHitPoints", R_INT, {R_OBJECT}, &sessionOrTurret<&session::SWMG_GetMaxHitPoints, &SWMG_GetMaxHitPoints>);
    insert(618, "SWMG_SetMaxHitPoints", R_VOID, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_SetMaxHitPoints, &SWMG_SetMaxHitPoints>);
    insert(619, "SWMG_GetSphereRadius", R_FLOAT, {R_OBJECT}, &sessionOrTurret<&session::SWMG_GetSphereRadius, &SWMG_GetSphereRadius>);
    insert(620, "SWMG_SetSphereRadius", R_VOID, {R_OBJECT, R_FLOAT}, &sessionOrTurret<&session::SWMG_SetSphereRadius, &SWMG_SetSphereRadius>);
    insert(621, "SWMG_GetNumLoops", R_INT, {R_OBJECT}, &sessionOrTurret<&session::SWMG_GetNumLoops, &SWMG_GetNumLoops>);
    insert(622, "SWMG_SetNumLoops", R_VOID, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_SetNumLoops, &SWMG_SetNumLoops>);
    insert(623, "SWMG_GetPosition", R_VECTOR, {R_OBJECT}, &sessionOrTurret<&session::SWMG_GetPosition, &SWMG_GetPosition>);
    insert(624, "SWMG_GetGunBankCount", R_INT, {R_OBJECT}, &sessionOrTurret<&session::SWMG_GetGunBankCount, &SWMG_GetGunBankCount>);
    insert(625, "SWMG_GetGunBankBulletModel", R_STRING, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetGunBankBulletModel, &SWMG_GetGunBankBulletModel>);
    insert(626, "SWMG_GetGunBankGunModel", R_STRING, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetGunBankGunModel, &SWMG_GetGunBankGunModel>);
    insert(627, "SWMG_GetGunBankDamage", R_INT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetGunBankDamage, &SWMG_GetGunBankDamage>);
    insert(628, "SWMG_GetGunBankTimeBetweenShots", R_FLOAT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetGunBankTimeBetweenShots, &SWMG_GetGunBankTimeBetweenShots>);
    insert(629, "SWMG_GetGunBankLifespan", R_FLOAT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetGunBankLifespan, &SWMG_GetGunBankLifespan>);
    insert(630, "SWMG_GetGunBankSpeed", R_FLOAT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetGunBankSpeed, &SWMG_GetGunBankSpeed>);
    insert(631, "SWMG_GetGunBankTarget", R_INT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetGunBankTarget, &SWMG_GetGunBankTarget>);
    insert(632, "SWMG_SetGunBankBulletModel", R_VOID, {R_OBJECT, R_INT, R_STRING}, &sessionOrTurret<&session::SWMG_SetGunBankBulletModel, &SWMG_SetGunBankBulletModel>);
    insert(633, "SWMG_SetGunBankGunModel", R_VOID, {R_OBJECT, R_INT, R_STRING}, &sessionOrTurret<&session::SWMG_SetGunBankGunModel, &SWMG_SetGunBankGunModel>);
    insert(634, "SWMG_SetGunBankDamage", R_VOID, {R_OBJECT, R_INT, R_INT}, &sessionOrTurret<&session::SWMG_SetGunBankDamage, &SWMG_SetGunBankDamage>);
    insert(635, "SWMG_SetGunBankTimeBetweenShots", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &sessionOrTurret<&session::SWMG_SetGunBankTimeBetweenShots, &SWMG_SetGunBankTimeBetweenShots>);
    insert(636, "SWMG_SetGunBankLifespan", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &sessionOrTurret<&session::SWMG_SetGunBankLifespan, &SWMG_SetGunBankLifespan>);
    insert(637, "SWMG_SetGunBankSpeed", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &sessionOrTurret<&session::SWMG_SetGunBankSpeed, &SWMG_SetGunBankSpeed>);
    insert(638, "SWMG_SetGunBankTarget", R_VOID, {R_OBJECT, R_INT, R_INT}, &sessionOrTurret<&session::SWMG_SetGunBankTarget, &SWMG_SetGunBankTarget>);
    insert(639, "SWMG_GetLastBulletHitPart", R_STRING, {}, &sessionOrTurret<&session::SWMG_GetLastBulletHitPart, &SWMG_GetLastBulletHitPart>);
    insert(640, "SWMG_IsGunBankTargetting", R_INT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_IsGunBankTargetting, &SWMG_IsGunBankTargetting>);
    insert(641, "SWMG_GetPlayerOffset", R_VECTOR, {}, &sessionOrTurret<&session::SWMG_GetPlayerOffset, &SWMG_GetPlayerOffset>);
    insert(642, "SWMG_GetPlayerInvincibility", R_FLOAT, {}, &sessionOrTurret<&session::SWMG_GetPlayerInvincibility, &SWMG_GetPlayerInvincibility>);
    insert(643, "SWMG_GetPlayerSpeed", R_FLOAT, {}, &sessionOrTurret<&session::SWMG_GetPlayerSpeed, &SWMG_GetPlayerSpeed>);
    insert(644, "SWMG_GetPlayerMinSpeed", R_FLOAT, {}, &sessionOrTurret<&session::SWMG_GetPlayerMinSpeed, &SWMG_GetPlayerMinSpeed>);
    insert(645, "SWMG_GetPlayerAccelerationPerSecond", R_FLOAT, {}, &sessionOrTurret<&session::SWMG_GetPlayerAccelerationPerSecond, &SWMG_GetPlayerAccelerationPerSecond>);
    insert(646, "SWMG_GetPlayerTunnelPos", R_VECTOR, {}, &sessionOrTurret<&session::SWMG_GetPlayerTunnelPos, &SWMG_GetPlayerTunnelPos>);
    insert(647, "SWMG_SetPlayerOffset", R_VOID, {R_VECTOR}, &sessionOrTurret<&session::SWMG_SetPlayerOffset, &SWMG_SetPlayerOffset>);
    insert(648, "SWMG_SetPlayerInvincibility", R_VOID, {R_FLOAT}, &sessionOrTurret<&session::SWMG_SetPlayerInvincibility, &SWMG_SetPlayerInvincibility>);
    insert(649, "SWMG_SetPlayerSpeed", R_VOID, {R_FLOAT}, &sessionOrTurret<&session::SWMG_SetPlayerSpeed, &SWMG_SetPlayerSpeed>);
    insert(650, "SWMG_SetPlayerMinSpeed", R_VOID, {R_FLOAT}, &sessionOrTurret<&session::SWMG_SetPlayerMinSpeed, &SWMG_SetPlayerMinSpeed>);
    insert(651, "SWMG_SetPlayerAccelerationPerSecond", R_VOID, {R_FLOAT}, &sessionOrTurret<&session::SWMG_SetPlayerAccelerationPerSecond, &SWMG_SetPlayerAccelerationPerSecond>);
    insert(652, "SWMG_SetPlayerTunnelPos", R_VOID, {R_VECTOR}, &sessionOrTurret<&session::SWMG_SetPlayerTunnelPos, &SWMG_SetPlayerTunnelPos>);
    insert(653, "SWMG_GetPlayerTunnelNeg", R_VECTOR, {}, &sessionOrTurret<&session::SWMG_GetPlayerTunnelNeg, &SWMG_GetPlayerTunnelNeg>);
    insert(654, "SWMG_SetPlayerTunnelNeg", R_VOID, {R_VECTOR}, &sessionOrTurret<&session::SWMG_SetPlayerTunnelNeg, &SWMG_SetPlayerTunnelNeg>);
    insert(655, "SWMG_GetPlayerOrigin", R_VECTOR, {}, &sessionOrTurret<&session::SWMG_GetPlayerOrigin, &SWMG_GetPlayerOrigin>);
    insert(656, "SWMG_SetPlayerOrigin", R_VOID, {R_VECTOR}, &sessionOrTurret<&session::SWMG_SetPlayerOrigin, &SWMG_SetPlayerOrigin>);
    insert(657, "SWMG_GetGunBankHorizontalSpread", R_FLOAT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetGunBankHorizontalSpread, &SWMG_GetGunBankHorizontalSpread>);
    insert(658, "SWMG_GetGunBankVerticalSpread", R_FLOAT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetGunBankVerticalSpread, &SWMG_GetGunBankVerticalSpread>);
    insert(659, "SWMG_GetGunBankSensingRadius", R_FLOAT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetGunBankSensingRadius, &SWMG_GetGunBankSensingRadius>);
    insert(660, "SWMG_GetGunBankInaccuracy", R_FLOAT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetGunBankInaccuracy, &SWMG_GetGunBankInaccuracy>);
    insert(661, "SWMG_SetGunBankHorizontalSpread", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &sessionOrTurret<&session::SWMG_SetGunBankHorizontalSpread, &SWMG_SetGunBankHorizontalSpread>);
    insert(662, "SWMG_SetGunBankVerticalSpread", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &sessionOrTurret<&session::SWMG_SetGunBankVerticalSpread, &SWMG_SetGunBankVerticalSpread>);
    insert(663, "SWMG_SetGunBankSensingRadius", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &sessionOrTurret<&session::SWMG_SetGunBankSensingRadius, &SWMG_SetGunBankSensingRadius>);
    insert(664, "SWMG_SetGunBankInaccuracy", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &sessionOrTurret<&session::SWMG_SetGunBankInaccuracy, &SWMG_SetGunBankInaccuracy>);
    insert(665, "SWMG_GetIsInvulnerable", R_INT, {R_OBJECT}, &sessionOrTurret<&session::SWMG_GetIsInvulnerable, &SWMG_GetIsInvulnerable>);
    insert(666, "SWMG_StartInvulnerability", R_VOID, {R_OBJECT}, &sessionOrTurret<&session::SWMG_StartInvulnerability, &SWMG_StartInvulnerability>);
    insert(667, "SWMG_GetPlayerMaxSpeed", R_FLOAT, {}, &sessionOrTurret<&session::SWMG_GetPlayerMaxSpeed, &SWMG_GetPlayerMaxSpeed>);
    insert(668, "SWMG_SetPlayerMaxSpeed", R_VOID, {R_FLOAT}, &sessionOrTurret<&session::SWMG_SetPlayerMaxSpeed, &SWMG_SetPlayerMaxSpeed>);
    insert(683, "SWMG_GetSoundFrequency", R_INT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetSoundFrequency, &SWMG_GetSoundFrequency>);
    insert(684, "SWMG_SetSoundFrequency", R_VOID, {R_OBJECT, R_INT, R_INT}, &sessionOrTurret<&session::SWMG_SetSoundFrequency, &SWMG_SetSoundFrequency>);
    insert(685, "SWMG_GetSoundFrequencyIsRandom", R_INT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetSoundFrequencyIsRandom, &SWMG_GetSoundFrequencyIsRandom>);
    insert(686, "SWMG_SetSoundFrequencyIsRandom", R_VOID, {R_OBJECT, R_INT, R_INT}, &sessionOrTurret<&session::SWMG_SetSoundFrequencyIsRandom, &SWMG_SetSoundFrequencyIsRandom>);
    insert(687, "SWMG_GetSoundVolume", R_INT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetSoundVolume, &SWMG_GetSoundVolume>);
    insert(688, "SWMG_SetSoundVolume", R_VOID, {R_OBJECT, R_INT, R_INT}, &sessionOrTurret<&session::SWMG_SetSoundVolume, &SWMG_SetSoundVolume>);
    insert(717, "SWMG_GetPlayerTunnelInfinite", R_VECTOR, {}, &sessionOrTurret<&session::SWMG_GetPlayerTunnelInfinite, &SWMG_GetPlayerTunnelInfinite>);
    insert(718, "SWMG_SetPlayerTunnelInfinite", R_VOID, {R_VECTOR}, &sessionOrTurret<&session::SWMG_SetPlayerTunnelInfinite, &SWMG_SetPlayerTunnelInfinite>);
}

void Routines::registerMinigameTslRoutines() {
    insert(520, "SWMG_SetLateralAccelerationPerSecond", R_VOID, {R_FLOAT}, &sessionOrTurret<&session::SWMG_SetLateralAccelerationPerSecond, &SWMG_SetLateralAccelerationPerSecond>);
    insert(521, "SWMG_GetLateralAccelerationPerSecond", R_FLOAT, {}, &sessionOrTurret<&session::SWMG_GetLateralAccelerationPerSecond, &SWMG_GetLateralAccelerationPerSecond>);
    insert(563, "SWMG_SetSpeedBlurEffect", R_VOID, {R_INT, R_FLOAT}, &SWMG_SetSpeedBlurEffect);
    insert(583, "SWMG_GetLastEvent", R_STRING, {}, &sessionOrTurret<&session::SWMG_GetLastEvent, &SWMG_GetLastEvent>);
    insert(584, "SWMG_GetLastEventModelName", R_STRING, {}, &sessionOrTurret<&session::SWMG_GetLastEventModelName, &SWMG_GetLastEventModelName>);
    insert(585, "SWMG_GetObjectByName", R_OBJECT, {R_STRING}, &sessionOrTurret<&session::SWMG_GetObjectByName, &SWMG_GetObjectByName>);
    insert(586, "SWMG_PlayAnimation", R_VOID, {R_OBJECT, R_STRING, R_INT, R_INT, R_INT}, &sessionOrTurret<&session::SWMG_PlayAnimation, &SWMG_PlayAnimation>);
    insert(587, "SWMG_GetLastBulletHitDamage", R_INT, {}, &sessionOrTurret<&session::SWMG_GetLastBulletHitDamage, &SWMG_GetLastBulletHitDamage>);
    insert(588, "SWMG_GetLastBulletHitTarget", R_INT, {}, &sessionOrTurret<&session::SWMG_GetLastBulletHitTarget, &SWMG_GetLastBulletHitTarget>);
    insert(589, "SWMG_GetLastBulletHitShooter", R_OBJECT, {}, &sessionOrTurret<&session::SWMG_GetLastBulletHitShooter, &SWMG_GetLastBulletHitShooter>);
    insert(590, "SWMG_AdjustFollowerHitPoints", R_INT, {R_OBJECT, R_INT, R_INT}, &sessionOrTurret<&session::SWMG_AdjustFollowerHitPoints, &SWMG_AdjustFollowerHitPoints>);
    insert(591, "SWMG_OnBulletHit", R_VOID, {}, &sessionOrTurret<&session::SWMG_OnBulletHit, &SWMG_OnBulletHit>);
    insert(592, "SWMG_OnObstacleHit", R_VOID, {}, &sessionOrTurret<&session::SWMG_OnObstacleHit, &SWMG_OnObstacleHit>);
    insert(593, "SWMG_GetLastFollowerHit", R_OBJECT, {}, &sessionOrTurret<&session::SWMG_GetLastFollowerHit, &SWMG_GetLastFollowerHit>);
    insert(594, "SWMG_GetLastObstacleHit", R_OBJECT, {}, &sessionOrTurret<&session::SWMG_GetLastObstacleHit, &SWMG_GetLastObstacleHit>);
    insert(595, "SWMG_GetLastBulletFiredDamage", R_INT, {}, &sessionOrTurret<&session::SWMG_GetLastBulletFiredDamage, &SWMG_GetLastBulletFiredDamage>);
    insert(596, "SWMG_GetLastBulletFiredTarget", R_INT, {}, &sessionOrTurret<&session::SWMG_GetLastBulletFiredTarget, &SWMG_GetLastBulletFiredTarget>);
    insert(597, "SWMG_GetObjectName", R_STRING, {R_OBJECT}, &sessionOrTurret<&session::SWMG_GetObjectName, &SWMG_GetObjectName>);
    insert(598, "SWMG_OnDeath", R_VOID, {}, &sessionOrTurret<&session::SWMG_OnDeath, &SWMG_OnDeath>);
    insert(599, "SWMG_IsFollower", R_INT, {R_OBJECT}, &sessionOrTurret<&session::SWMG_IsFollower, &SWMG_IsFollower>);
    insert(600, "SWMG_IsPlayer", R_INT, {R_OBJECT}, &sessionOrTurret<&session::SWMG_IsPlayer, &SWMG_IsPlayer>);
    insert(601, "SWMG_IsEnemy", R_INT, {R_OBJECT}, &sessionOrTurret<&session::SWMG_IsEnemy, &SWMG_IsEnemy>);
    insert(602, "SWMG_IsTrigger", R_INT, {R_OBJECT}, &sessionOrTurret<&session::SWMG_IsTrigger, &SWMG_IsTrigger>);
    insert(603, "SWMG_IsObstacle", R_INT, {R_OBJECT}, &sessionOrTurret<&session::SWMG_IsObstacle, &SWMG_IsObstacle>);
    insert(604, "SWMG_SetFollowerHitPoints", R_VOID, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_SetFollowerHitPoints, &SWMG_SetFollowerHitPoints>);
    insert(605, "SWMG_OnDamage", R_VOID, {}, &sessionOrTurret<&session::SWMG_OnDamage, &SWMG_OnDamage>);
    insert(606, "SWMG_GetLastHPChange", R_INT, {}, &sessionOrTurret<&session::SWMG_GetLastHPChange, &SWMG_GetLastHPChange>);
    insert(607, "SWMG_RemoveAnimation", R_VOID, {R_OBJECT, R_STRING}, &sessionOrTurret<&session::SWMG_RemoveAnimation, &SWMG_RemoveAnimation>);
    insert(608, "SWMG_GetCameraNearClip", R_FLOAT, {}, &sessionOrTurret<&session::SWMG_GetCameraNearClip, &SWMG_GetCameraNearClip>);
    insert(609, "SWMG_GetCameraFarClip", R_FLOAT, {}, &sessionOrTurret<&session::SWMG_GetCameraFarClip, &SWMG_GetCameraFarClip>);
    insert(610, "SWMG_SetCameraClip", R_VOID, {R_FLOAT, R_FLOAT}, &sessionOrTurret<&session::SWMG_SetCameraClip, &SWMG_SetCameraClip>);
    insert(611, "SWMG_GetPlayer", R_OBJECT, {}, &sessionOrTurret<&session::SWMG_GetPlayer, &SWMG_GetPlayer>);
    insert(612, "SWMG_GetEnemyCount", R_INT, {}, &sessionOrTurret<&session::SWMG_GetEnemyCount, &SWMG_GetEnemyCount>);
    insert(613, "SWMG_GetEnemy", R_OBJECT, {R_INT}, &sessionOrTurret<&session::SWMG_GetEnemy, &SWMG_GetEnemy>);
    insert(614, "SWMG_GetObstacleCount", R_INT, {}, &sessionOrTurret<&session::SWMG_GetObstacleCount, &SWMG_GetObstacleCount>);
    insert(615, "SWMG_GetObstacle", R_OBJECT, {R_INT}, &sessionOrTurret<&session::SWMG_GetObstacle, &SWMG_GetObstacle>);
    insert(616, "SWMG_GetHitPoints", R_INT, {R_OBJECT}, &sessionOrTurret<&session::SWMG_GetHitPoints, &SWMG_GetHitPoints>);
    insert(617, "SWMG_GetMaxHitPoints", R_INT, {R_OBJECT}, &sessionOrTurret<&session::SWMG_GetMaxHitPoints, &SWMG_GetMaxHitPoints>);
    insert(618, "SWMG_SetMaxHitPoints", R_VOID, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_SetMaxHitPoints, &SWMG_SetMaxHitPoints>);
    insert(619, "SWMG_GetSphereRadius", R_FLOAT, {R_OBJECT}, &sessionOrTurret<&session::SWMG_GetSphereRadius, &SWMG_GetSphereRadius>);
    insert(620, "SWMG_SetSphereRadius", R_VOID, {R_OBJECT, R_FLOAT}, &sessionOrTurret<&session::SWMG_SetSphereRadius, &SWMG_SetSphereRadius>);
    insert(621, "SWMG_GetNumLoops", R_INT, {R_OBJECT}, &sessionOrTurret<&session::SWMG_GetNumLoops, &SWMG_GetNumLoops>);
    insert(622, "SWMG_SetNumLoops", R_VOID, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_SetNumLoops, &SWMG_SetNumLoops>);
    insert(623, "SWMG_GetPosition", R_VECTOR, {R_OBJECT}, &sessionOrTurret<&session::SWMG_GetPosition, &SWMG_GetPosition>);
    insert(624, "SWMG_GetGunBankCount", R_INT, {R_OBJECT}, &sessionOrTurret<&session::SWMG_GetGunBankCount, &SWMG_GetGunBankCount>);
    insert(625, "SWMG_GetGunBankBulletModel", R_STRING, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetGunBankBulletModel, &SWMG_GetGunBankBulletModel>);
    insert(626, "SWMG_GetGunBankGunModel", R_STRING, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetGunBankGunModel, &SWMG_GetGunBankGunModel>);
    insert(627, "SWMG_GetGunBankDamage", R_INT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetGunBankDamage, &SWMG_GetGunBankDamage>);
    insert(628, "SWMG_GetGunBankTimeBetweenShots", R_FLOAT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetGunBankTimeBetweenShots, &SWMG_GetGunBankTimeBetweenShots>);
    insert(629, "SWMG_GetGunBankLifespan", R_FLOAT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetGunBankLifespan, &SWMG_GetGunBankLifespan>);
    insert(630, "SWMG_GetGunBankSpeed", R_FLOAT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetGunBankSpeed, &SWMG_GetGunBankSpeed>);
    insert(631, "SWMG_GetGunBankTarget", R_INT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetGunBankTarget, &SWMG_GetGunBankTarget>);
    insert(632, "SWMG_SetGunBankBulletModel", R_VOID, {R_OBJECT, R_INT, R_STRING}, &sessionOrTurret<&session::SWMG_SetGunBankBulletModel, &SWMG_SetGunBankBulletModel>);
    insert(633, "SWMG_SetGunBankGunModel", R_VOID, {R_OBJECT, R_INT, R_STRING}, &sessionOrTurret<&session::SWMG_SetGunBankGunModel, &SWMG_SetGunBankGunModel>);
    insert(634, "SWMG_SetGunBankDamage", R_VOID, {R_OBJECT, R_INT, R_INT}, &sessionOrTurret<&session::SWMG_SetGunBankDamage, &SWMG_SetGunBankDamage>);
    insert(635, "SWMG_SetGunBankTimeBetweenShots", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &sessionOrTurret<&session::SWMG_SetGunBankTimeBetweenShots, &SWMG_SetGunBankTimeBetweenShots>);
    insert(636, "SWMG_SetGunBankLifespan", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &sessionOrTurret<&session::SWMG_SetGunBankLifespan, &SWMG_SetGunBankLifespan>);
    insert(637, "SWMG_SetGunBankSpeed", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &sessionOrTurret<&session::SWMG_SetGunBankSpeed, &SWMG_SetGunBankSpeed>);
    insert(638, "SWMG_SetGunBankTarget", R_VOID, {R_OBJECT, R_INT, R_INT}, &sessionOrTurret<&session::SWMG_SetGunBankTarget, &SWMG_SetGunBankTarget>);
    insert(639, "SWMG_GetLastBulletHitPart", R_STRING, {}, &sessionOrTurret<&session::SWMG_GetLastBulletHitPart, &SWMG_GetLastBulletHitPart>);
    insert(640, "SWMG_IsGunBankTargetting", R_INT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_IsGunBankTargetting, &SWMG_IsGunBankTargetting>);
    insert(641, "SWMG_GetPlayerOffset", R_VECTOR, {}, &sessionOrTurret<&session::SWMG_GetPlayerOffset, &SWMG_GetPlayerOffset>);
    insert(642, "SWMG_GetPlayerInvincibility", R_FLOAT, {}, &sessionOrTurret<&session::SWMG_GetPlayerInvincibility, &SWMG_GetPlayerInvincibility>);
    insert(643, "SWMG_GetPlayerSpeed", R_FLOAT, {}, &sessionOrTurret<&session::SWMG_GetPlayerSpeed, &SWMG_GetPlayerSpeed>);
    insert(644, "SWMG_GetPlayerMinSpeed", R_FLOAT, {}, &sessionOrTurret<&session::SWMG_GetPlayerMinSpeed, &SWMG_GetPlayerMinSpeed>);
    insert(645, "SWMG_GetPlayerAccelerationPerSecond", R_FLOAT, {}, &sessionOrTurret<&session::SWMG_GetPlayerAccelerationPerSecond, &SWMG_GetPlayerAccelerationPerSecond>);
    insert(646, "SWMG_GetPlayerTunnelPos", R_VECTOR, {}, &sessionOrTurret<&session::SWMG_GetPlayerTunnelPos, &SWMG_GetPlayerTunnelPos>);
    insert(647, "SWMG_SetPlayerOffset", R_VOID, {R_VECTOR}, &sessionOrTurret<&session::SWMG_SetPlayerOffset, &SWMG_SetPlayerOffset>);
    insert(648, "SWMG_SetPlayerInvincibility", R_VOID, {R_FLOAT}, &sessionOrTurret<&session::SWMG_SetPlayerInvincibility, &SWMG_SetPlayerInvincibility>);
    insert(649, "SWMG_SetPlayerSpeed", R_VOID, {R_FLOAT}, &sessionOrTurret<&session::SWMG_SetPlayerSpeed, &SWMG_SetPlayerSpeed>);
    insert(650, "SWMG_SetPlayerMinSpeed", R_VOID, {R_FLOAT}, &sessionOrTurret<&session::SWMG_SetPlayerMinSpeed, &SWMG_SetPlayerMinSpeed>);
    insert(651, "SWMG_SetPlayerAccelerationPerSecond", R_VOID, {R_FLOAT}, &sessionOrTurret<&session::SWMG_SetPlayerAccelerationPerSecond, &SWMG_SetPlayerAccelerationPerSecond>);
    insert(652, "SWMG_SetPlayerTunnelPos", R_VOID, {R_VECTOR}, &sessionOrTurret<&session::SWMG_SetPlayerTunnelPos, &SWMG_SetPlayerTunnelPos>);
    insert(653, "SWMG_GetPlayerTunnelNeg", R_VECTOR, {}, &sessionOrTurret<&session::SWMG_GetPlayerTunnelNeg, &SWMG_GetPlayerTunnelNeg>);
    insert(654, "SWMG_SetPlayerTunnelNeg", R_VOID, {R_VECTOR}, &sessionOrTurret<&session::SWMG_SetPlayerTunnelNeg, &SWMG_SetPlayerTunnelNeg>);
    insert(655, "SWMG_GetPlayerOrigin", R_VECTOR, {}, &sessionOrTurret<&session::SWMG_GetPlayerOrigin, &SWMG_GetPlayerOrigin>);
    insert(656, "SWMG_SetPlayerOrigin", R_VOID, {R_VECTOR}, &sessionOrTurret<&session::SWMG_SetPlayerOrigin, &SWMG_SetPlayerOrigin>);
    insert(657, "SWMG_GetGunBankHorizontalSpread", R_FLOAT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetGunBankHorizontalSpread, &SWMG_GetGunBankHorizontalSpread>);
    insert(658, "SWMG_GetGunBankVerticalSpread", R_FLOAT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetGunBankVerticalSpread, &SWMG_GetGunBankVerticalSpread>);
    insert(659, "SWMG_GetGunBankSensingRadius", R_FLOAT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetGunBankSensingRadius, &SWMG_GetGunBankSensingRadius>);
    insert(660, "SWMG_GetGunBankInaccuracy", R_FLOAT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetGunBankInaccuracy, &SWMG_GetGunBankInaccuracy>);
    insert(661, "SWMG_SetGunBankHorizontalSpread", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &sessionOrTurret<&session::SWMG_SetGunBankHorizontalSpread, &SWMG_SetGunBankHorizontalSpread>);
    insert(662, "SWMG_SetGunBankVerticalSpread", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &sessionOrTurret<&session::SWMG_SetGunBankVerticalSpread, &SWMG_SetGunBankVerticalSpread>);
    insert(663, "SWMG_SetGunBankSensingRadius", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &sessionOrTurret<&session::SWMG_SetGunBankSensingRadius, &SWMG_SetGunBankSensingRadius>);
    insert(664, "SWMG_SetGunBankInaccuracy", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &sessionOrTurret<&session::SWMG_SetGunBankInaccuracy, &SWMG_SetGunBankInaccuracy>);
    insert(665, "SWMG_GetIsInvulnerable", R_INT, {R_OBJECT}, &sessionOrTurret<&session::SWMG_GetIsInvulnerable, &SWMG_GetIsInvulnerable>);
    insert(666, "SWMG_StartInvulnerability", R_VOID, {R_OBJECT}, &sessionOrTurret<&session::SWMG_StartInvulnerability, &SWMG_StartInvulnerability>);
    insert(667, "SWMG_GetPlayerMaxSpeed", R_FLOAT, {}, &sessionOrTurret<&session::SWMG_GetPlayerMaxSpeed, &SWMG_GetPlayerMaxSpeed>);
    insert(668, "SWMG_SetPlayerMaxSpeed", R_VOID, {R_FLOAT}, &sessionOrTurret<&session::SWMG_SetPlayerMaxSpeed, &SWMG_SetPlayerMaxSpeed>);
    insert(683, "SWMG_GetSoundFrequency", R_INT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetSoundFrequency, &SWMG_GetSoundFrequency>);
    insert(684, "SWMG_SetSoundFrequency", R_VOID, {R_OBJECT, R_INT, R_INT}, &sessionOrTurret<&session::SWMG_SetSoundFrequency, &SWMG_SetSoundFrequency>);
    insert(685, "SWMG_GetSoundFrequencyIsRandom", R_INT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetSoundFrequencyIsRandom, &SWMG_GetSoundFrequencyIsRandom>);
    insert(686, "SWMG_SetSoundFrequencyIsRandom", R_VOID, {R_OBJECT, R_INT, R_INT}, &sessionOrTurret<&session::SWMG_SetSoundFrequencyIsRandom, &SWMG_SetSoundFrequencyIsRandom>);
    insert(687, "SWMG_GetSoundVolume", R_INT, {R_OBJECT, R_INT}, &sessionOrTurret<&session::SWMG_GetSoundVolume, &SWMG_GetSoundVolume>);
    insert(688, "SWMG_SetSoundVolume", R_VOID, {R_OBJECT, R_INT, R_INT}, &sessionOrTurret<&session::SWMG_SetSoundVolume, &SWMG_SetSoundVolume>);
    insert(717, "SWMG_GetPlayerTunnelInfinite", R_VECTOR, {}, &sessionOrTurret<&session::SWMG_GetPlayerTunnelInfinite, &SWMG_GetPlayerTunnelInfinite>);
    insert(718, "SWMG_SetPlayerTunnelInfinite", R_VOID, {R_VECTOR}, &sessionOrTurret<&session::SWMG_SetPlayerTunnelInfinite, &SWMG_SetPlayerTunnelInfinite>);
    insert(782, "SWMG_GetSwoopUpgrade", R_INT, {R_INT}, &sessionOrTurret<&session::SWMG_GetSwoopUpgrade, &SWMG_GetSwoopUpgrade>);
    insert(789, "SWMG_GetTrackPosition", R_VECTOR, {R_OBJECT}, &sessionOrTurret<&session::SWMG_GetTrackPosition, &SWMG_GetTrackPosition>);
    insert(790, "SWMG_SetFollowerPosition", R_VECTOR, {R_VECTOR}, &sessionOrTurret<&session::SWMG_SetFollowerPosition, &SWMG_SetFollowerPosition>);
    insert(792, "SWMG_DestroyMiniGameObject", R_VOID, {R_OBJECT}, &sessionOrTurret<&session::SWMG_DestroyMiniGameObject, &SWMG_DestroyMiniGameObject>);
    insert(804, "SWMG_SetJumpSpeed", R_VOID, {R_FLOAT}, &sessionOrTurret<&session::SWMG_SetJumpSpeed, &SWMG_SetJumpSpeed>);
    insert(825, "SWMG_PlayerApplyForce", R_VOID, {R_VECTOR}, &sessionOrTurret<&session::SWMG_PlayerApplyForce, &SWMG_PlayerApplyForce>);
}

} // namespace game

} // namespace reone
