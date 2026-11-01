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

#include "reone/game/object/creature.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/gtx/quaternion.hpp>

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/game/action.h"
#include "reone/game/action/attackobject.h"
#include "reone/game/action/castspellatlocation.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/movetoobject.h"
#include "reone/game/action/movetopoint.h"
#include "reone/game/action/switchweapons.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/animations.h"
#include "reone/game/animationutil.h"
#include "reone/game/attack.h"
#include "reone/game/combat.h"
#include "reone/game/combatfeedback.h"
#include "reone/game/combattables.h"
#include "reone/game/contextaction.h"
#include "reone/game/creaturespeeds.h"
#include "reone/game/d20/class.h"
#include "reone/game/d20/classes.h"
#include "reone/game/d20/feats.h"
#include "reone/game/d20/races.h"
#include "reone/game/d20/skills.h"
#include "reone/game/d20/spell.h"
#include "reone/game/d20/spells.h"
#include "reone/game/debug.h"
#include "reone/game/di/services.h"
#include "reone/game/effect/abilitydecrease.h"
#include "reone/game/effect/abilityincrease.h"
#include "reone/game/effect/acdecrease.h"
#include "reone/game/effect/acincrease.h"
#include "reone/game/effect/attackdecrease.h"
#include "reone/game/effect/attackincrease.h"
#include "reone/game/effect/blasterdeflectiondecrease.h"
#include "reone/game/effect/blasterdeflectionincrease.h"
#include "reone/game/effect/bonusfeat.h"
#include "reone/game/effect/creaturestate.h"
#include "reone/game/effect/damage.h"
#include "reone/game/effect/damagedecrease.h"
#include "reone/game/effect/damageimmunitydecrease.h"
#include "reone/game/effect/damageimmunityincrease.h"
#include "reone/game/effect/damageincrease.h"
#include "reone/game/effect/damagereduction.h"
#include "reone/game/effect/damageresistance.h"
#include "reone/game/effect/damageshield.h"
#include "reone/game/effect/death.h"
#include "reone/game/effect/disguise.h"
#include "reone/game/effect/forceresistanceincrease.h"
#include "reone/game/effect/forceshield.h"
#include "reone/game/effect/immunity.h"
#include "reone/game/effect/invisibility.h"
#include "reone/game/effect/linkeffects.h"
#include "reone/game/effect/purealignmentpowers.h"
#include "reone/game/effect/regenerate.h"
#include "reone/game/effect/savingthrowdecrease.h"
#include "reone/game/effect/savingthrowincrease.h"
#include "reone/game/effect/skilldecrease.h"
#include "reone/game/effect/skillincrease.h"
#include "reone/game/effect/source.h"
#include "reone/game/effect/trueseeing.h"
#include "reone/game/effect/visual.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/footstepsounds.h"
#include "reone/game/forcerules.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/areaofeffect.h"
#include "reone/game/object/door.h"
#include "reone/game/object/encounter.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/trigger.h"
#include "reone/game/party.h"
#include "reone/game/physicalcombatrules.h"
#include "reone/game/portraits.h"
#include "reone/game/projectiles.h"
#include "reone/game/reputes.h"
#include "reone/game/script/runner.h"
#include "reone/game/surfaces.h"
#include "reone/game/talent.h"
#include "reone/game/twodautil.h"
#include "reone/game/visualeffects.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/context.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/textureregistry.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/gff.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/provider/soundsets.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/resources.h"
#include "reone/resource/strings.h"
#include "reone/scene/di/services.h"
#include "reone/scene/drawdebug.h"
#include "reone/scene/graphs.h"
#include "reone/scene/types.h"
#include "reone/script/types.h"
#include "reone/system/clock.h"
#include "reone/system/di/services.h"
#include "reone/system/exception/validation.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"
#include "reone/system/timer.h"

#include "../action/commonactions.h"

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;
using namespace reone::script;

namespace reone {

namespace game {

namespace {

// The vitality a resurrection restores: a creature still above zero keeps
// its own. In TSL the percentage of the maximum is restored, one point when
// that share is less than a whole point; KotOR restores one point.
constexpr int getResurrectionHitPoints(bool tsl, int current, int maximum, int percentage) {
    if (current > 0) {
        return current;
    }
    if (!tsl || percentage <= 0) {
        return 1;
    }
    const int product = maximum * percentage;
    return product <= -100 || product >= 100 ? product / 100 : 1;
}

// A death cast by a spell checks death immunity when it is magical.
constexpr bool shouldCheckDeathImmunity(uint32_t spellId, EffectCategory category) {
    return spellId != UINT32_MAX && category == EffectCategory::Magical;
}

// The visual effects that are beams, which a death leaves in place, in both
// games and in TSL alone.
constexpr std::array<int, 16> kBeamVisualEffects {
    2026, 2027, 2028, 2029, 2037, 2038, 2049, 2050,
    2051, 2052, 2053, 2061, 2065, 2066, 4037, 6000};
constexpr std::array<int, 2> kTSLBeamVisualEffects {2068, 2069};

bool isBeamVisual(bool tsl, int visualId) {
    const auto contains = [visualId](const auto &visuals) {
        return std::find(visuals.begin(), visuals.end(), visualId) != visuals.end();
    };
    return contains(kBeamVisualEffects) || (tsl && contains(kTSLBeamVisualEffects));
}

inline bool isEffectPreservedOnDeath(const EffectInstance &record,
                                     const std::vector<int> &keptTypes, bool tsl) {
    if (record.durationType() == DurationType::Innate ||
        record.durationType() == DurationType::Equipped)
        return true;
    const int scriptType = record.scriptEffectType(tsl);
    for (int kept : keptTypes) {
        if (kept == scriptType) {
            return true;
        }
        if (kept == static_cast<int>(EffectType::Beam) && record.serializedType == SavedEffectTypes::visualEffect &&
            isBeamVisual(tsl, record.integerParameter(0)))
            return true;
    }
    return false;
}

// Advance the live-array index even after removing the current entry.
// Do not restart the scan or consume the shifted entry at the same index.
template <class Collection, class RemovePackage>
void removeResurrectionEffects(Collection &effects, RemovePackage removePackage) {
    for (std::size_t index = 0; index < effects.size(); ++index) {
        const auto &record = effects[index];
        if (record.serializedType > SavedEffectTypes::hitPointChangeWhenDying) {
            break;
        }
        if (record.serializedType == SavedEffectTypes::hitPointChangeWhenDying && record.durationType() == DurationType::Temporary) {
            removePackage(record.id);
        }
    }
}

// The gameeffects.2da row an effect-immunity query of a saved effect type
// reads; a state effect picks its row by its state.
constexpr std::optional<int> getEffectImmunityRow(bool tsl, int type, int state = 0) {
    switch (type) {
    case SavedEffectTypes::setState:
        switch (static_cast<CreatureState>(state)) {
        case CreatureState::Confusion:
            return 22;
        case CreatureState::Fear:
            return 24;
        case CreatureState::DroidStun:
            return 21;
        case CreatureState::Stun:
            return 19;
        case CreatureState::Paralysis:
            return 18;
        case CreatureState::Sleep:
            return 23;
        case CreatureState::Choke:
            return 15;
        case CreatureState::Horrified:
            return 13;
        case CreatureState::MindTrick:
            return tsl ? std::optional<int>(26) : std::nullopt;
        case CreatureState::DroidScramble:
            return tsl ? std::optional<int>(25) : std::nullopt;
        default:
            return std::nullopt;
        }
    case SavedEffectTypes::attackDecrease:
        return 4;
    case SavedEffectTypes::damageDecrease:
        return 5;
    case SavedEffectTypes::damageImmunityDecrease:
        return 6;
    case SavedEffectTypes::entangle:
        return 1;
    case SavedEffectTypes::death:
        return 20;
    case SavedEffectTypes::savingThrowDecrease:
        return 9;
    case SavedEffectTypes::movementSpeedDecrease:
        return 8;
    case SavedEffectTypes::forceResistanceDecrease:
        return 10;
    case SavedEffectTypes::poison:
        return 2;
    case SavedEffectTypes::abilityDecrease:
        return 3;
    case SavedEffectTypes::acDecrease:
        return 7;
    case SavedEffectTypes::skillDecrease:
        return 11;
    case SavedEffectTypes::blasterDeflectionDecrease:
        return 12;
    case SavedEffectTypes::horrified:
        return 13;
    case SavedEffectTypes::damageForcePoints:
        return 14;
    case SavedEffectTypes::choke:
        return 15;
    case SavedEffectTypes::psychicStatic:
        return 16;
    case SavedEffectTypes::lightsaberThrow:
        return 17;
    // A Force push has no row of its own; its child effects are queried instead.
    default:
        return std::nullopt;
    }
}

// Stun, paralysis and sleep hold a creature where it stands.
bool isImmobilizingState(CreatureState state) {
    return state == CreatureState::Stun || state == CreatureState::Paralysis || state == CreatureState::Sleep;
}

using OptionalCreature = std::optional<std::reference_wrapper<const Creature>>;
using OptionalItem = std::optional<std::reference_wrapper<const Item>>;
using OptionalObject = std::optional<std::reference_wrapper<const Object>>;

// Whether the weapon of an attack, if any, is this very item.
bool isAttackWeapon(const std::shared_ptr<Item> &weapon, const Item &item) {
    return weapon.get() == &item;
}

struct DeathExperience {
    int row {-1};
    int column {-1};
    float amount {0.0f};
    int awarded {0};  // ceilf: party award and world floaty
    int reported {0}; // truncate: death feedback packet
};

inline int getDeathExperienceRow(
    const ICombatTables &tables, std::uint32_t experience, bool tsl) {
    const int top = tsl ? 50 : 20;
    for (int row = top; row >= 0; --row) {
        if (experience >= tables.experienceThreshold(row)) {
            return tsl ? std::min(row, 30) : row;
        }
    }
    return -1;
}

inline int getDeathExperienceColumn(bool tsl, float challengeRating,
                                    bool autoBalance, int spawnLevel, int challengeModifier) {
    int challenge = static_cast<int>(challengeRating);
    if (tsl && autoBalance) {
        int level = spawnLevel;
        if (!level) {
            level = 1;
        }
        challenge = std::max(0, level + challengeModifier);
    }
    if (tsl && challenge > 30) {
        return 31;
    }
    return challenge + 1;
}

// The kill's share is the npc.2da general share row; each companion adds the
// bonus row's share.
inline DeathExperience resolveDeathExperience(
    const ICombatTables &tables, const Party &party, int row, int column, bool tsl, int companionCount) {
    DeathExperience result;
    result.row = row;
    result.column = column;
    const float base = tables.killExperience(row, column);
    const float percent = party.percentXPFraction(tsl ? 13 : 9);
    result.amount = (percent / 100.0f) * base;
    const int bonus = party.percentXP(tsl ? 14 : 10);
    if (bonus > 0) {
        const float perCompanion = static_cast<float>(bonus) / 100.0f;
        const float factor = static_cast<float>(companionCount) * perCompanion + 1.0f;
        result.amount *= factor;
    }
    result.awarded = static_cast<int>(std::ceil(result.amount));
    result.reported = static_cast<int>(result.amount);
    return result;
}

inline int calculateSpellLevel(int classLevels, bool autoBalance, int spawnLevel,
                               int globalPlayerLevel, float multiplier) {
    if (autoBalance) {
        int level = spawnLevel == 0 ? globalPlayerLevel : spawnLevel;
        if (level <= 0) {
            level = 1;
        }
        classLevels += static_cast<int>(static_cast<float>(level) * multiplier) - 1;
        if (classLevels < 0) {
            classLevels = 1;
        }
    }
    return classLevels;
}

inline int calculateSpellSaveDC(bool tsl, int spell, int level,
                                int wisdom, int charisma,
                                bool sense, bool advanced, bool mastery) {
    // The Force Scream powers of TSL start from a higher base.
    const bool scream = tsl && spell >= static_cast<int>(SpellType::ForceScream) &&
                        spell <= static_cast<int>(SpellType::MasterForceScream);
    int base = scream ? 10 : 5;
    int focus = mastery ? 4 : advanced ? (tsl ? 3 : 2)
                          : sense      ? (tsl ? 2 : 1)
                                       : 0;
    return level + wisdom + charisma + base + focus;
}

template <class Roll>
int rollDamageShieldDice(const DamageCost &cost, Roll roll) {
    int result = 0;
    for (int index = 0; index < cost.numDice; ++index) {
        result += roll(1, cost.die);
    }
    return result;
}
// The sound set entry a creature falling onto a surface material sounds
// from; none for a surface with no fall sound.
inline std::string Creature::AppearanceSoundSet::*fallSound(int material) {
    switch (material) {
    case 1:  // Dirt
    case 3:  // Grass
    case 14: // Leaves
        return &Creature::AppearanceSoundSet::fallDirt;
    case 4:  // Stone
    case 5:  // Wood
    case 9:  // Carpet
    case 15: // Lava
        return &Creature::AppearanceSoundSet::fallHard;
    case 6:  // Water
    case 11: // Puddles
    case 12: // Swamp
    case 13: // Mud
        return &Creature::AppearanceSoundSet::fallWater;
    case 10: // Metal
        return &Creature::AppearanceSoundSet::fallMetal;
    default:
        return nullptr;
    }
}

} // namespace

// A heartbeat comes every three to a little over four seconds.
static constexpr int kMinHeartbeatIntervalMilliseconds = 3000;
static constexpr int kMaxHeartbeatIntervalMilliseconds = 4199;

static constexpr int kStrRefRemains = 38151;
static constexpr uint64_t kSwitchWeaponsCooldownMicros = 1000000;
static constexpr float kItemUseCooldown = 3.0f;
static constexpr float kRangedEnemySearchRange = 20.0f;
// A choreographed exchange is fought from 1.4 metres in TSL; in KotOR from
// the two creature personal spaces with half a metre and a fifth of a metre
// more.
static constexpr float kChoreographedAttackRange = 1.4f;
static constexpr float kChoreographedAttackLead = 0.5f;
static constexpr float kChoreographedAttackTail = 0.2f;
// Use ranges: a creature in TSL; the spacing added to the two creature
// personal spaces in KotOR; beyond the personal space for a trigger and for a
// door or placeable; a precise use; a door while a door saber is queued in
// TSL; the extra reach of a corpse.
static constexpr float kCreatureUseRange = 1.4f;
static constexpr float kCreatureUseSpacing = 0.3f;
static constexpr float kTriggerUseSpacing = 0.5f;
static constexpr float kObjectUseSpacing = 0.75f;
static constexpr float kPreciseUseRange = 0.1f;
static constexpr float kDoorSaberUseRange = 1.0f;
static constexpr float kCorpseUseSpacing = 5.0f;
// A use range is met a tenth of a metre beyond it, along a line this far
// above the ground.
static constexpr float kUseRangeAllowance = 0.1f;
static constexpr float kUseLineHeight = 1.5f;
// The height above the ground at which a walk tests its line to where it goes.
static constexpr float kGroundLineHeight = 0.1f;
// A walk to a use point keeps its point while the current one lies within a
// millimetre of it.
static constexpr float kUsePointTolerance = 0.001f;
// Walking into other creatures: five blocked steps are planned round, the
// sixth ends the walk.
static constexpr int kMaxPathBumps = 5;
static constexpr float kSameBlockerDistance2 = 1e-5f;
static constexpr int kStrRefCannotGetAround = 47859;
static constexpr char kPathFailScript[] = "k_def_pathfail01";
static constexpr float kPushAsideSpace = 0.1f;      // kept beyond both personal spaces by a creature pushed aside
static constexpr float kPushAsideSpotRadius = 5.0f; // how far round its spot a pushed creature's place is looked for
static constexpr float kPushBehindRadius = 4.0f;    // how far behind the pusher a creature that cannot step aside goes
// Below this run distance an appearance cannot move.
static constexpr float kImmobileRunSpeed = 0.0001f;
// The slowest run, as a share of the distance one loop of the run clip covers
// in a second, that still shows the run clip.
static constexpr float kRunClipMinimumPace = 0.37f;
// A path walk slows over its last half second of travel, to no less than a
// tenth of its speed, which it keeps for the last hundredth of that stretch.
// It gains at most its full speed in a second; its clips gain twice as fast.
// A run going at under half its speed shows the walk clip.
static constexpr float kWalkBrakeTime = 0.5f;
static constexpr float kWalkBrakeEndFraction = 0.01f;
static constexpr float kMinimumWalkPace = 0.1f;
static constexpr float kWalkPaceGain = 1.0f;
static constexpr float kClipPaceGain = 2.0f;
static constexpr float kRunClipMinimumWalkPace = 0.5f;
// A walk given no more range than this goes onto its point: it ends on the
// step that reaches the end of its path, and one that sets out within a tenth
// of a metre of the point steps onto it at once.
static constexpr float kNoMoveRange = 0.001f;
static constexpr float kArrivalSnapDistance2 = 0.01f;
static constexpr int kMaximumDamageEffectModifier = 36;
static constexpr int kMaximumElementalDamageBonus = 108;
static constexpr int kBaseDamageFlag = 0x4000;
static constexpr int kAllSavingThrows = 0;
static constexpr int kFortitudeSavingThrow = 1;
static constexpr int kReflexSavingThrow = 2;
static constexpr int kWillSavingThrow = 3;
// The kinds of action a combat round takes.
static constexpr int kAttackRoundAction = 1;
static constexpr int kCastRoundAction = 9;
static constexpr int kItemCastRoundAction = 10;
static constexpr int kTotalDefenseRoundAction = 13;
// A Force push carries a creature at 25 metres a second; a leap covers its
// ground distance in 0.33 seconds, however far it goes.
static constexpr float kForcePushSpeed = 25.0f;
static constexpr float kLeapDuration = 0.33f;

// Animation IDs of the poses, fidgets and turns a creature shows.
static constexpr int kPauseAnimationId = 10000;
static constexpr int kReadyAnimationId = 10001;
// The one animation ID that names no clip shows nothing rather than the pause.
static constexpr int kNoPauseFallbackAnimationId = 10098;
static constexpr int kWalkAnimationId = 10002;
static constexpr int kRunAnimationId = 10004;
static constexpr int kStealthWalkAnimationId = 10133;
static constexpr int kInjuredWalkAnimationId = 10093;
static constexpr int kInjuredRunAnimationId = 10094;
static constexpr int kDeadAnimationId = 10006;
static constexpr int kDead1AnimationId = 10008;
static constexpr int kDead3AnimationId = 10156;
static constexpr int kHeadTurnLeftAnimationId = 10053;
static constexpr int kHeadTurnRightAnimationId = 10054;
static constexpr int kPauseScratchHeadAnimationId = 10055;
static constexpr int kPauseBoredAnimationId = 10056;
static constexpr int kInjuredPauseAnimationId = 10092;
static constexpr int kFidgetPauseAnimationId = 10147;
static constexpr int kTurnLeftAnimationId = 367;
static constexpr int kTurnRightAnimationId = 368;
static constexpr int kForceResistedAnimationId = 10145;
static constexpr int kWeaponFlourishAnimationId = 10158;
static constexpr int kWeaponDrawAnimationId = 10246;
static constexpr int kDamageFlinchAnimationId = 10302;
static constexpr int kWeaponDrawMilliseconds = 1500;

static std::string formatCombatAnimation(const std::string &format, CreatureWieldType wield, int variant) {
    return str(boost::format(format) % static_cast<int>(wield) % variant);
}

static constexpr uint32_t kMoveToPointActionId = 1;
static constexpr uint32_t kMoveToObjectActionId = 17;
static constexpr uint32_t kFollowLeaderActionId = 61;

static int rollDamageShieldContribution(ServicesView &services, int selector) {
    if (selector <= 5) {
        return selector;
    }
    return rollDamageShieldDice(services.game.combatTables.damageCost(selector),
                                [](int low, int high) { return randomInt(low, high); });
}

static int getHighestOwnedFeatRank(
    const Creature &creature,
    FeatType firstRank,
    int rankCount) {

    int first = static_cast<int>(firstRank);
    for (int rank = rankCount; rank >= 1; --rank) {
        if (creature.hasEffectiveFeat(static_cast<FeatType>(first + rank - 1))) {
            return rank;
        }
    }
    return 0;
}

static int getHighestTotalDefenseClassLevel(
    const CreatureAttributes &attributes) {

    return std::max({
        attributes.getClassLevel(ClassType::Scoundrel),
        attributes.getClassLevel(ClassType::JediSentinel),
        attributes.getClassLevel(ClassType::JediWatchman),
        attributes.getClassLevel(ClassType::SithAssassin),
    });
}

// Stealth
static constexpr FeatType kStealthFeat = FeatType::PersonalCloakingShield;

static constexpr int kStealthInCombatStrRef = 1452;
static constexpr int kSkillUnusableStrRef = 1435;
static constexpr int kStealthFieldVisual = 8002;
static constexpr int kCloakRemovalVisual = 8001;
static constexpr char kStealthCue[] = "sdr_invisible";

// Movement rates, in metres per second
static constexpr float kMinimumWalkSpeed = 0.1f;
static constexpr float kMinimumRunSpeed = 1.0f;

// Perception and stealth detection
static constexpr float kPerceptionRollInterval = 20.0f;
static constexpr int kPlayerPerceptionRangeRow = 12;
static constexpr int kCombatPerceptionRangeRow = 18;
static constexpr int kAppearancePerceptionRange = 11;
static constexpr int kStealthSpottedStrRef = 38023;
static constexpr int kStealthObserverStrRef = 42123;
static constexpr int kStealthHiderStrRef = 42122;
static constexpr int kStealthStationaryStrRef = 42125;
static constexpr int kStealthDistanceStrRef = 42126;
static constexpr int kStealthFacingStrRef = 42127;
static constexpr int kStealthRunningStrRef = 42128;
static constexpr int kStealthCombatStrRef = 42129;

static constexpr float kCloseRangeAttackDistance2 = 25.0f;

static constexpr char kBonusCostTable[] = "iprp_bonuscost";
static constexpr char kMeleeCostTable[] = "iprp_meleecost";
static constexpr char kDecreaseCostTable[] = "iprp_neg5cost";
static constexpr char kResistanceCostTable[] = "iprp_resistcost";
static constexpr char kReductionCostTable[] = "iprp_soakcost";
static constexpr char kVulnerabilityCostTable[] = "iprp_damvulcost";
static constexpr char kDamageTypeTable[] = "iprp_damagetype";
static constexpr char kProtectionTable[] = "iprp_protection";
static constexpr int kImprovedForceResistanceCostTable = 11;
/** The iprp_costtable row of the Decreased Ability Score amounts (iprp_neg10cost). */
static constexpr int kDecreasedAbilityCostTable = 21;
/** The iprp_costtable row of the Improved Saving Throw amounts (iprp_bonuscost). */
static constexpr int kImprovedSavingThrowCostTable = 1;
static constexpr int kItemLightVisualEffect = 5000;

/** The ability an ability property's subtype names; a subtype past Charisma names Strength. */
static Ability getPropertyAbility(uint16_t subtype) {
    return subtype <= static_cast<uint16_t>(Ability::Charisma) ? static_cast<Ability>(subtype) : Ability::Strength;
}

static std::string g_talkDummyNode("talkdummy");

static const std::string g_headHookNode("headhook");
// The talking head plays once as a layer over the body's pose, on the head too.
static constexpr int kHeadTalkFlags = AnimationFlags::layer | AnimationFlags::propagate;
static const std::string g_maskHookNode("gogglehook");
static const std::string g_rightHandNode("rhand");
static const std::string g_leftHandNode("lhand");

static int getEquipabilitySlot(int slot) {
    switch (slot) {
    case InventorySlots::rightWeapon2:
        return InventorySlots::rightWeapon;
    case InventorySlots::leftWeapon2:
        return InventorySlots::leftWeapon;
    default:
        return slot;
    }
}

/**
 * Whether an attack modifier is for every attack or for attacks of
 * \p attackType. An extra on-hand attack also takes the on-hand and creature
 * weapon modifiers, an extra unarmed attack the unarmed ones.
 */
static bool attackModifierApplies(AttackBonus modifierType, AttackBonus attackType) {
    return modifierType == AttackBonus::Misc || modifierType == attackType ||
           (attackType == AttackBonus::ExtraOnhand &&
            (modifierType == AttackBonus::Onhand || modifierType == AttackBonus::CreatureWeapon)) ||
           (attackType == AttackBonus::ExtraUnarmed && modifierType == AttackBonus::Unarmed);
}

static bool racialTypeMatches(int racialType, const Creature &target) {
    auto type = static_cast<RacialType>(racialType);
    return type == RacialType::All || type == target.racialType();
}

/**
 * Whether an attack or damage property against an alignment group applies:
 * one against every group or the Neutral group applies always, even without
 * a target; another needs a target of its group.
 */
static bool attackAlignmentGroupMatches(int alignment, const OptionalCreature &target) {
    auto group = static_cast<Alignment>(alignment);
    return group == Alignment::All ||
           group == Alignment::Neutral ||
           (target && group == target->get().alignment());
}

static bool defenseAlignmentGroupMatches(int alignment, const Creature &attacker) {
    auto group = static_cast<Alignment>(alignment);
    return group == Alignment::All || group == attacker.alignment();
}

static DamageType getItemPropertyDamageType(
    ServicesView &services,
    int subtype) {

    validateTwoDARowIndex(kDamageTypeTable, subtype, services.game.combatTables.damageTypeCount());
    return static_cast<DamageType>(1 << subtype);
}

static DamagePower getDamageReductionPower(
    ServicesView &services,
    int subtype) {

    validateTwoDARowIndex(kProtectionTable, subtype, services.game.combatTables.protectionCount());
    return static_cast<DamagePower>(subtype + 1);
}

static bool attackPropertyApplies(
    ItemProperty property,
    int subtype,
    const OptionalCreature &target) {

    switch (property) {
    case ItemProperty::EnhancementBonus:
    case ItemProperty::AttackBonus:
        return true;
    case ItemProperty::EnhancementBonusVsAlignmentGroup:
    case ItemProperty::AttackBonusVsAlignmentGroup:
        return attackAlignmentGroupMatches(subtype, target);
    case ItemProperty::EnhancementBonusVsRacialGroup:
    case ItemProperty::AttackBonusVsRacialGroup:
        return target && racialTypeMatches(subtype, *target);
    default:
        return false;
    }
}

static void getSituationalAttackBonuses(
    const Creature &attacker,
    const Creature &target,
    const std::shared_ptr<Item> &weapon,
    int &closeProximityRangedBonus,
    int &meleeOnRangedBonus) {

    closeProximityRangedBonus = 0;
    meleeOnRangedBonus = 0;

    if (weapon && weapon->isRanged()) {
        closeProximityRangedBonus = getCloseProximityRangedAttackBonus(
            attacker.game().isTSL(),
            attacker.getSquareDistanceTo(target) <= kCloseRangeAttackDistance2,
            attacker.hasEffectiveFeat(FeatType::CloseCombat),
            attacker.hasEffectiveFeat(FeatType::ImprovedCloseCombat));
        return;
    }

    auto targetWeapon = target.getEquippedItem(InventorySlots::rightWeapon);
    meleeOnRangedBonus = getMeleeOnRangedAttackBonus(
        attacker.game().isTSL(),
        targetWeapon && targetWeapon->isRanged(),
        target.hasEffectiveFeat(FeatType::CloseCombat),
        target.hasEffectiveFeat(FeatType::ImprovedCloseCombat));
}

/**
 * Whether the attack modifiers of an item worn in \p slot count for an
 * attack of \p attackType: a hand's for attacks of that hand (a
 * double-bladed weapon's for both hands), the gloves' for unarmed attacks,
 * the left creature weapon's for its attacks, and those of an item worn
 * elsewhere for every attack.
 */
static bool equippedItemAppliesToAttack(int slot, const Item &item, AttackBonus attackType) {
    switch (slot) {
    case InventorySlots::rightWeapon:
        return attackModifierApplies(AttackBonus::Onhand, attackType) ||
               (item.weaponWield() == WeaponWield::DoubleBladedSword && attackModifierApplies(AttackBonus::Offhand, attackType));
    case InventorySlots::leftWeapon:
        return attackModifierApplies(AttackBonus::Offhand, attackType);
    case InventorySlots::hands:
        return attackModifierApplies(AttackBonus::Unarmed, attackType);
    case InventorySlots::cWeaponL:
        return attackModifierApplies(AttackBonus::CreatureWeapon, attackType);
    case InventorySlots::cWeaponR:
    case InventorySlots::cWeaponB:
    case InventorySlots::rightWeapon2:
    case InventorySlots::leftWeapon2:
        return false;
    default:
        return true;
    }
}

// Only the left creature weapon attacks.
static bool isCreatureWeaponAttack(const Creature &attacker, const std::shared_ptr<Item> &weapon) {
    const auto creatureWeapon = attacker.getEquippedItem(InventorySlots::cWeaponL);
    return creatureWeapon && isAttackWeapon(weapon, *creatureWeapon);
}

/**
 * Whether an attack without a weapon is one of a creature whose creature
 * weapons are all in the right or bite slot, so that no item attacks. KotOR
 * takes such an attack as a main-hand attack, TSL as an unarmed one.
 */
static bool isEmptyCreatureWeaponAttack(const Creature &attacker, const std::shared_ptr<Item> &weapon) {
    return !weapon && !attacker.getEquippedItem(InventorySlots::rightWeapon) &&
           !attacker.getEquippedItem(InventorySlots::leftWeapon) &&
           !attacker.getEquippedItem(InventorySlots::cWeaponL) &&
           (attacker.getEquippedItem(InventorySlots::cWeaponR) || attacker.getEquippedItem(InventorySlots::cWeaponB));
}

static bool isHandSpecificAttackModifierSlot(int slot) {
    switch (slot) {
    case InventorySlots::rightWeapon:
    case InventorySlots::leftWeapon:
    case InventorySlots::hands:
    case InventorySlots::cWeaponL:
    case InventorySlots::cWeaponR:
    case InventorySlots::cWeaponB:
        return true;
    default:
        return false;
    }
}

static void addAttackModifier(int modifier, int &bonus, int &penalty) {
    if (modifier > 0) {
        bonus += modifier;
    } else if (modifier < 0) {
        penalty -= modifier;
    }
}

enum class CostColumn {
    Value,
    Amount
};

static int costCell(const CostTable &table, int row, CostColumn column, int blankValue) {
    const auto entry = table.row(row);
    return (column == CostColumn::Amount ? entry.amount : entry.value).value_or(blankValue);
}

static int getCostTableValue(
    ServicesView &services,
    const std::string &resRef,
    int row,
    CostColumn column,
    int blankValue) {

    return costCell(services.game.combatTables.costTable(resRef), row, column, blankValue);
}

static int getItemPropertyValue(
    ServicesView &services,
    const Item::PropertyEntry &property,
    CostColumn column,
    int blankValue) {

    return costCell(services.game.combatTables.costTableAt(property.costTable), property.costValue, column, blankValue);
}

static bool savingThrowModifierApplies(
    int modifierSave,
    SavingThrowType modifierType,
    int requestedSave,
    SavingThrowType requestedType) {

    return (modifierSave == kAllSavingThrows ||
            modifierSave == requestedSave) &&
           (modifierType == SavingThrowType::All ||
            modifierType == requestedType);
}

/**
 * Whether a saving throw item property changes the requested save. Each of
 * the four kinds changes the save its subtype numbers when that is
 * Fortitude, Reflex or Will, and every save otherwise, against every kind of
 * threat.
 */
static bool savingThrowPropertyApplies(
    ItemProperty propertyType,
    int subtype,
    int requestedSave) {

    switch (propertyType) {
    case ItemProperty::ImprovedSavingThrow:
    case ItemProperty::ImprovedSavingThrowSpecific:
    case ItemProperty::DecreasedSavingThrows:
    case ItemProperty::DecreasedSavingThrowsSpecific: {
        const bool oneSave = subtype >= kFortitudeSavingThrow && subtype <= kWillSavingThrow;
        return !oneSave || subtype == requestedSave;
    }
    default:
        return false;
    }
}

enum class DamageBonusHand {
    Misc = 0,
    MainHand = 1,
    OffHand = 2,
    CreatureWeapon = 3,
    /** An on-hand attack with a weapon while the on-hand attack count is below zero. */
    ExtraMainHand = 6,
    Unarmed = 7,
    /** An unarmed attack while the on-hand attack count is below zero. */
    ExtraUnarmed = 8,
};

/**
 * Whether a damage modifier for \p modifierHand counts for an attack of
 * \p attackHand: a miscellaneous one always, a hand's for its attacks; an
 * extra on-hand attack also takes the main-hand and creature weapon ones, an
 * extra unarmed attack the unarmed ones.
 */
static bool damageHandApplies(DamageBonusHand modifierHand, DamageBonusHand attackHand) {
    return modifierHand == DamageBonusHand::Misc || modifierHand == attackHand ||
           (attackHand == DamageBonusHand::ExtraMainHand &&
            (modifierHand == DamageBonusHand::MainHand || modifierHand == DamageBonusHand::CreatureWeapon)) ||
           (attackHand == DamageBonusHand::ExtraUnarmed && modifierHand == DamageBonusHand::Unarmed);
}

struct DamageModifier {
    int costValue;
    int numDice;
    int die;
    int flat;
    DamageType type;
    int rank {0};
    bool usesDice {false};
};

static DamageModifier getDamageModifier(
    ServicesView &services,
    int costValue,
    DamageType type) {

    const auto cost = services.game.combatTables.damageCost(costValue);
    DamageModifier result {costValue, 0, 0, costValue, type};
    result.rank = cost.rank;
    result.usesDice = costValue >= 6;
    if (result.usesDice) {
        result.flat = 0;
        result.numDice = cost.numDice;
        result.die = cost.die;
    }
    return result;
}

static int rollDamageModifier(
    const DamageModifier &modifier,
    int multiplier,
    DamageDiceRoll dice = DamageDiceRoll::Rolled) {

    if (!modifier.usesDice) {
        return multiplier * modifier.flat;
    }

    int result = 0;
    for (int multiple = 0; multiple < multiplier; ++multiple) {
        for (int die = 0; die < modifier.numDice; ++die) {
            result += dice == DamageDiceRoll::Highest  ? modifier.die
                      : dice == DamageDiceRoll::Lowest ? 1
                                                       : randomInt(1, modifier.die);
        }
    }
    return result;
}

/** A hand-specific damage increase of an attack: its dice roll once a critical multiple, its flat amount once. */
static int rollHandDamageModifier(
    const DamageModifier &modifier,
    int criticalMultiplier,
    DamageDiceRoll dice) {

    return rollDamageModifier(modifier, modifier.usesDice ? criticalMultiplier : 1, dice);
}

static bool damagePropertyApplies(
    ItemProperty property,
    int subtype,
    const OptionalCreature &target) {

    switch (property) {
    case ItemProperty::EnhancementBonus:
    case ItemProperty::DamageBonus:
    case ItemProperty::DecreasedDamage:
        return true;
    case ItemProperty::EnhancementBonusVsAlignmentGroup:
    case ItemProperty::DamageBonusVsAlignmentGroup:
        return attackAlignmentGroupMatches(subtype, target);
    case ItemProperty::EnhancementBonusVsRacialGroup:
    case ItemProperty::DamageBonusVsRacialGroup:
        return target && racialTypeMatches(subtype, *target);
    default:
        return false;
    }
}

static bool equippedItemPropertiesAreActive(int slot) {
    return slot != InventorySlots::rightWeapon2 &&
           slot != InventorySlots::leftWeapon2;
}

/** A damage increase or decrease that an item property or an effect gives an attack. */
struct DamageModifierEntry {
    bool increase {true};
    /** An iprp_damagecost row from 6 up, else a flat amount. */
    int code {0};
    int flags {0};
    /** For one kind of attack rather than every attack. */
    bool handSpecific {false};
    /** Whether it is for this attack and its target. */
    bool applies {true};
    /** Entries of one spell share a miscellaneous slot; an entry without one keeps its own. */
    uint32_t spellId {std::numeric_limits<uint32_t>::max()};
};

/** An attack's damage modifiers after merging, increases before decreases. */
struct MergedDamageModifiers {
    std::vector<DamageModifierEntry> handIncreases;
    /** The hand-specific decrease kept for each damage type. */
    std::map<int, int> handDecreases;
    std::vector<DamageModifierEntry> miscellaneousIncreases;
    std::vector<DamageModifierEntry> miscellaneousDecreases;
};

/**
 * The damage type a modifier's flags give it: the highest flag in KotOR; in
 * TSL the flag nearest the logarithm of the flags, so mixed flags may round up.
 */
static DamageType getDamageModifierType(bool tsl, int flags) {
    if (!tsl) {
        return getPrimaryDamageType(flags);
    }
    const int index = static_cast<int>(std::log10(static_cast<float>(flags)) * 3.321928f + 0.5f);
    return static_cast<DamageType>(1 << index);
}

/**
 * Merges an attack's damage modifiers, increases before decreases, of those
 * \p counts admits. Hand-specific increases are all kept, and each sets the
 * rank the following entries are measured against. A hand-specific decrease
 * takes its type's place only when that rank exceeds the rank of the decrease
 * already there. A miscellaneous entry takes a slot of its own, unless one of
 * the same spell and flags holds a slot, which it takes over only when that
 * rank exceeds the slot's; KotOR has 36 slots each for increases and
 * decreases, TSL 108.
 */
static MergedDamageModifiers mergeDamageModifiers(
    ServicesView &services,
    bool tsl,
    const std::vector<DamageModifierEntry> &entries,
    const std::function<bool(const DamageModifierEntry &)> &counts) {

    const size_t slots = tsl ? 108 : 36;
    const auto &tables = services.game.combatTables;
    MergedDamageModifiers result;
    int rank = 0;
    for (const bool increases : {true, false}) {
        for (const auto &entry : entries) {
            if (entry.increase != increases || !counts(entry)) {
                continue;
            }
            if (entry.handSpecific) {
                if (entry.increase) {
                    result.handIncreases.push_back(entry);
                    rank = tables.damageCost(entry.code).rank;
                    continue;
                }
                auto &kept = result.handDecreases[static_cast<int>(getDamageModifierType(tsl, entry.flags))];
                if (rank > tables.damageCost(kept).rank) {
                    kept = entry.code;
                }
                continue;
            }
            auto &list = entry.increase ? result.miscellaneousIncreases : result.miscellaneousDecreases;
            if (entry.spellId != std::numeric_limits<uint32_t>::max()) {
                auto shared = std::find_if(list.begin(), list.end(), [&](const DamageModifierEntry &held) {
                    return held.spellId == entry.spellId && held.flags == entry.flags;
                });
                if (shared != list.end()) {
                    if (rank > tables.damageCost(shared->code).rank) {
                        shared->code = entry.code;
                    }
                    continue;
                }
            }
            if (list.size() < slots) {
                list.push_back(entry);
            }
        }
    }
    return result;
}

/**
 * Whether the damage properties of an item worn in \p slot are for an attack
 * of \p attackHand: those of a hand for its attacks, a double-bladed weapon's
 * for both hands, the left creature weapon's for its attacks, and those of an
 * item worn elsewhere for every attack.
 */
static bool itemDamageAppliesToHand(int slot, const Item &item, DamageBonusHand attackHand) {
    switch (slot) {
    case InventorySlots::rightWeapon:
        return damageHandApplies(DamageBonusHand::MainHand, attackHand) ||
               (item.weaponWield() == WeaponWield::DoubleBladedSword && damageHandApplies(DamageBonusHand::OffHand, attackHand));
    case InventorySlots::leftWeapon:
        return damageHandApplies(DamageBonusHand::OffHand, attackHand);
    case InventorySlots::hands:
        return damageHandApplies(DamageBonusHand::Unarmed, attackHand);
    case InventorySlots::cWeaponL:
        return damageHandApplies(DamageBonusHand::CreatureWeapon, attackHand);
    case InventorySlots::cWeaponR:
    case InventorySlots::cWeaponB:
    case InventorySlots::rightWeapon2:
    case InventorySlots::leftWeapon2:
        return false;
    default:
        return true;
    }
}

/**
 * Appends a damage modifier for each active damage property of an item: an
 * enhancement or a damage decrease typed by the item's damage flags, a damage
 * bonus typed by its property. One limited to an alignment or racial group
 * applies against a target of that group only.
 */
static void appendItemDamageModifiers(
    ServicesView &services,
    const Item &item,
    int itemFlags,
    const OptionalCreature &target,
    bool forAttack,
    bool handSpecific,
    std::vector<DamageModifierEntry> &entries) {

    for (const auto &property : item.properties()) {
        if (!item.isPropertyActive(property)) {
            continue;
        }
        DamageModifierEntry entry;
        entry.handSpecific = handSpecific;
        entry.applies = forAttack && damagePropertyApplies(property.type(), property.subtype, target);
        switch (property.type()) {
        case ItemProperty::EnhancementBonus:
        case ItemProperty::EnhancementBonusVsAlignmentGroup:
        case ItemProperty::EnhancementBonusVsRacialGroup:
            entry.code = getCostTableValue(services, kMeleeCostTable, property.costValue, CostColumn::Value, 0);
            entry.flags = itemFlags;
            break;
        case ItemProperty::DamageBonus:
            entry.code = property.costValue;
            entry.flags = static_cast<int>(getItemPropertyDamageType(services, property.subtype));
            break;
        case ItemProperty::DamageBonusVsAlignmentGroup:
        case ItemProperty::DamageBonusVsRacialGroup:
            entry.code = property.costValue;
            entry.flags = static_cast<int>(getItemPropertyDamageType(services, property.paramValue));
            break;
        case ItemProperty::DecreasedDamage:
            // The cost table holds the decrease as a negative value.
            entry.increase = false;
            entry.code = -getCostTableValue(services, kDecreaseCostTable, property.costValue, CostColumn::Value, 0);
            entry.flags = itemFlags;
            break;
        default:
            continue;
        }
        if (entry.code != 0) {
            entries.push_back(entry);
        }
    }
}

/**
 * The damage modifiers of the creature's items and effects for an attack of
 * \p attackHand against \p target, items before effects, each marked with
 * whether it applies to that attack.
 */
static std::vector<DamageModifierEntry> collectDamageModifiers(
    const Creature &creature,
    const OptionalCreature &target,
    DamageBonusHand attackHand) {

    std::vector<DamageModifierEntry> entries;
    // The weapon set not in use gives no modifiers.
    for (const auto &[slot, item] : creature.equipment()) {
        if (!item || !equippedItemPropertiesAreActive(slot)) {
            continue;
        }
        // An unarmed attack types the gloves' modifiers as bludgeoning.
        const int itemFlags = slot == InventorySlots::hands ? static_cast<int>(DamageType::Bludgeoning) : item->damageFlags();
        appendItemDamageModifiers(creature.services(), *item, itemFlags, target,
                                  itemDamageAppliesToHand(slot, *item, attackHand),
                                  isHandSpecificAttackModifierSlot(slot), entries);
    }
    for (const auto &applied : creature.effects()) {
        if (!applied.hasLiveRuntimeSource() ||
            (applied.type() != EffectType::DamageIncrease && applied.type() != EffectType::DamageDecrease)) {
            continue;
        }
        const auto hand = static_cast<DamageBonusHand>(applied.integerParameter(5));
        DamageModifierEntry entry;
        entry.increase = applied.type() == EffectType::DamageIncrease;
        entry.code = applied.integerParameter(0);
        entry.flags = applied.integerParameter(1);
        entry.handSpecific = hand != DamageBonusHand::Misc;
        entry.applies = applied.appliesVersus(target) && damageHandApplies(hand, attackHand);
        entry.spellId = applied.spellId;
        entries.push_back(entry);
    }
    return entries;
}

static void applyDeathExperience(
    Creature &victim, const std::shared_ptr<Object> &damager, const std::string &victimName,
    Game &game, ServicesView &services) {

    // A player character or party member is worth nothing. Reputation queries
    // from an NPC faction to faction zero use the player row.
    if (victim.actsAsPC() || game.party().isMember(victim) ||
        services.game.reputes.getReputation(Faction::Player, victim.faction()) > 10)
        return;
    // The award is measured against the experience of the creature under the
    // player's control.
    const auto player = game.party().player();
    if (!player) {
        return;
    }
    const auto &tables = services.game.combatTables;
    const auto &context = victim.autoBalanceContext();
    const bool autoBalance = game.isTSL() && context.multiplierSet != 0 &&
                             !game.party().isMember(victim);
    const int challengeModifier = autoBalance
                                      ? tables.autoBalance(context.multiplierSet).challengeRatingModifier
                                      : 0;
    const int row = getDeathExperienceRow(tables, std::uint32_t(player->xp()), game.isTSL());
    const int column = getDeathExperienceColumn(game.isTSL(), victim.challengeRating(),
                                                autoBalance, context.playerLevelAtSpawn, challengeModifier);
    const auto xp = resolveDeathExperience(tables, game.party(), row, column, game.isTSL(),
                                           game.party().companionCount());

    // Kill experience goes to the party without the experience-gained line.
    // The killer is only credited in the death message.
    if (xp.awarded > 0) {
        game.party().awardXP(xp.awarded, XPSource::Combat);
    }

    // A trap's kill is credited to its creator; without a creature to credit,
    // the controlled creature takes it.
    std::shared_ptr<Creature> recipient;
    if (damager) {
        if (auto *trap = dyn_cast<Trigger>(damager.get())) {
            recipient = trap->trapCreator();
        } else {
            recipient = dyn_cast<Creature>(damager);
        }
    }
    if (!recipient) {
        recipient = game.party().getLeader();
    }
    // The controlled creature hears of the kill when it is on the killer's
    // side and within 30 m of it.
    const auto leader = game.party().getLeader();
    if (recipient && leader && leader->faction() == recipient->faction() &&
        leader->getSquareDistanceTo(*recipient) <= 900.0f) {
        addDeathExperienceFeedback(game, recipient->name(), victimName, xp.reported);
    }
    game.floatingText().addExperience(victim, xp.awarded);
}

Creature::Creature(
    uint32_t id,
    std::string sceneName,
    Game &game,
    ServicesView &services) :
    Object(id, ObjectType::Creature, std::move(sceneName), game, services) {

    // Initialize unsaved perception ranges from PercepRngDefault in ranges.2da.
    _perception.sightRange = 20.0f;
    _perception.hearingRange = 20.0f;
    startUnsavedCounters();
    // A creature's destroyed body waits 8 s before it fades.
    _fadeOutTime = 8000;
}

void Creature::startUnsavedCounters() {
    // Detection rolls start at d10 and are refreshed at the first check.
    _perceptionRolls.hide = randomInt(1, 10);
    _perceptionRolls.moveSilently = randomInt(1, 10);
    _perceptionRolls.spot = randomInt(1, 10);
    _perceptionRolls.listen = randomInt(1, 10);
    _perceptionRolls.age = kPerceptionRollInterval;
    _perceptionThrottle = randomInt(0, 99);
    _heartbeatInterval = randomInt(kMinHeartbeatIntervalMilliseconds, kMaxHeartbeatIntervalMilliseconds);
    _heartbeatThrottle = randomInt(0, 99);
    // The first idle fidget waits one to two and a half minutes.
    _fidgetTime = 0;
    _fidgetDelay = 60000 + 30000 * randomInt(0, 3);
}

// The first heartbeat due after the creation script has run only starts the
// interval over, without scripts. A state the creature holds makes the
// heartbeat due at once.
void Creature::startUnstampedHeartbeat() {
    if (_internalStateEffectId != kUnassignedEffectId) {
        _stateSupportTimer.reset(0.0f);
    } else if (_spawnScriptFired) {
        _heartbeatThrottle = 0;
        _heartbeatInterval = randomInt(kMinHeartbeatIntervalMilliseconds, kMaxHeartbeatIntervalMilliseconds);
        _stateSupportTimer.reset(_heartbeatInterval / 1000.0f);
    } else {
        _stateSupportTimer.reset(0.0f);
    }
}

void Creature::retireAreaRuntime(
    Pathfinder &pathfinder,
    const std::set<uint32_t> &retainedObjectIds) {
    retireAreaRuntimeState(retainedObjectIds);

    if (_path) {
        releasePath(pathfinder, *_path);
        _path.reset();
    }
    _pathVelocity = glm::vec3(0.0f);
    _previousPosition = _position;
    _stuckTimer.reset(0.0f);
    _stuckForce = glm::vec3(0.0f);
    setMovementType(MovementType::None);
    _lastBlockingDoorId = script::kObjectInvalid;

    setCombatState(false);
    // What the creature's record does not keep starts over, as it does for a
    // creature built from its record: the excitement, the item-use timer, the
    // stance and its Total Defense start, the kind of the round's last
    // action, the end-of-round decision timer, the orientation lock, the spot
    // it could not see its attack target from, the last projectile time and
    // the fractions of regeneration.
    _excitedMilliseconds = 0;
    _itemUseCooldown = 0.0f;
    _combatStance = CombatStance::None;
    clearTotalDefenseStart();
    _roundActionKind = 0;
    _combatState.decisionScriptTime = 3.0f;
    _orientationLock = script::kObjectInvalid;
    _blockedAttackSight.reset();
    _lastSpellProjectileMilliseconds = 0;
    _hitPointAccumulator = 0.0f;
    _forcePointAccumulator = 0.0f;
    // So do its detection rolls and its perception, heartbeat and fidget
    // counters, and its heartbeat has not been stamped.
    startUnsavedCounters();
    startUnstampedHeartbeat();
    // Its presentation starts over too: no queued or running one-shot, no
    // loop to return to, no overlay layers, no engaged swing, no head look
    // and no cast visuals, the weapon swap ready and the next pose chosen
    // afresh.
    flushFireForgetQueue();
    _oneShot.reset();
    _animFireForget = false;
    _storedLoop.reset();
    _swingAttack.reset();
    _weaponDrawPending = false;
    _injuredIdle = false;
    _lastMeleeAttackVariant = -1;
    _engagedExchange = false;
    _switchWeaponsReadyAt = 0;
    if (auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode)) {
        model->removeLayers();
    }
    if (_lookAtRunning) {
        for (auto &model : lookAtModels()) {
            model.get().endLookAt();
        }
    }
    _lookAtId = script::kObjectInvalid;
    _lookAtTarget.reset();
    _lookAtRunning = false;
    _spellCastVisuals.clear();
    _specialAttackVisuals.clear();
    _animDirty = true;
    _currentCombatAction.reset();
    _lastAttackResult = AttackResultType::Invalid;
    _incomingAttacker = SavedObjectReference {};
    _attackerList.clear();
    _receivedAttack = {};
    _lastWeaponUsed = script::kObjectInvalid;
    _clientCombatMode = false;

    _perception.clear();
    _perceptionStarted = false;
    stopTalking();
    stopStuntMode();
    if (_audioSourceVoice) _audioSourceVoice->stop();
    if (_audioSourceFootstep) _audioSourceFootstep->stop();
    _audioSourceVoice.reset();
    _audioSourceFootstep.reset();
}

std::optional<std::reference_wrapper<const Creature>> Creature::asCreature(const Object &object) {
    const auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return std::nullopt;
    }
    return std::cref(*creature);
}

std::optional<std::reference_wrapper<const Creature>> Creature::asCreature(const std::shared_ptr<Object> &object) {
    if (!object) {
        return std::nullopt;
    }
    return asCreature(*object);
}

std::shared_ptr<Gff> Creature::findTemplate(resource::IGffs &gffs, std::string &resRef) {
    static constexpr char kSubstituteTemplate[] = "nw_badger";
    if (auto utc = gffs.get(resRef, ResType::Utc)) {
        return utc;
    }
    auto substitute = gffs.get(kSubstituteTemplate, ResType::Utc);
    if (substitute) {
        resRef = kSubstituteTemplate;
    }
    return substitute;
}

bool Creature::loadFromBlueprint(const std::string &blueprintResRef) {
    std::string resRef(blueprintResRef);
    auto utc = findTemplate(_services.resource.gffs, resRef);
    if (!utc) {
        return false;
    }
    // A blueprint is a single source, so deserialize it once. Routing through
    // deserialize() would re-read the self-referential TemplateResRef and
    // deserialize the same data twice, doubling accumulated class levels.
    deserializeAll(*utc, SerializedIdentityContext::templateResource(resRef));
    restoreSerializedVitality();
    deserializeOwnedItemsAndEquipment(*utc, SerializedIdentityContext::templateResource(resRef), false);
    countLoadedItems();
    updateTransform();
    loadAppearance();
    return true;
}

// The sneaking pace and the top driving speed of an appearance whose cell is
// blank or zero.
static constexpr float kDefaultStealthWalkSpeed = 2.0f;
static constexpr float kDefaultDriveMaxSpeed = 6.0f;

void Creature::loadAppearanceProperties() {
    auto appearances = getRequiredTwoDA(_services.resource.twoDas, "appearance");

    const auto modelType = parseModelType(appearances->getString(_appearance, "modeltype"));
    // Which hands are powered belongs to the model's kind of animations: a
    // model of another kind starts with no hand powered.
    if (modelType != _modelType) {
        _poweredHands = PoweredHands {};
    }
    _modelType = modelType;
    _walkDistance = appearances->getFloat(_appearance, "walkdist", 1.0f);
    _runDistance = appearances->getFloat(_appearance, "rundist", 1.0f);
    _stealthWalkSpeed = appearances->getFloat(_appearance, "driveanimwalk", 0.0f);
    if (_stealthWalkSpeed == 0.0f) {
        _stealthWalkSpeed = kDefaultStealthWalkSpeed;
    }
    _driveMaxSpeed = appearances->getFloat(_appearance, "drivemaxspeed", 0.0f);
    if (_driveMaxSpeed == 0.0f) {
        _driveMaxSpeed = kDefaultDriveMaxSpeed;
    }
    _personalSpace = appearances->getFloat(_appearance, "perspace", 0.6f);
    // A blank space kept from other creatures is no space at all.
    _creaturePersonalSpace = appearances->getFloat(_appearance, "creperspace", 0.0f);
    // A blank height is zero where the head falls back on it, while the
    // walking space of such an appearance reaches a metre up.
    const auto height = appearances->getFloatOpt(_appearance, "height");
    _height = height.value_or(0.0f);
    _collisionHeight = height.value_or(kBlankCollisionHeight);
    const auto sizeCategory = appearances->getIntOpt(_appearance, "sizecategory");
    _size = static_cast<CreatureSize>(sizeCategory.value_or(static_cast<int>(CreatureSize::Invalid)));
    _hasSizeCategory = sizeCategory.has_value();
    _footstepType = appearances->getInt(_appearance, "footsteptype", -1);
    _disableInjuredAnim = appearances->getInt(_appearance, "disableinjuredanim") != 0;
    _abortOnParry = appearances->getInt(_appearance, "abortonparry") != 0;
    _groundTiltAppearance = appearances->getBool(_appearance, "groundtilt");
    _envmap = boost::to_lower_copy(appearances->getString(_appearance, "envmap"));
    _deathVisual = appearances->getIntOpt(_appearance, "deathvfx");
    // A death visual without a node of its own goes off at the impact node.
    _deathVisualNode = appearances->getString(_appearance, "deathvfxnode", "impact");
    _appearanceBodyBag = appearances->getIntOpt(_appearance, "body_bag");
    _fadeDelayOnDeath = appearances->getIntOpt(_appearance, "fadedelayondeath");
    _hitRadius = appearances->getFloat(_appearance, "hitradius");
    loadAppearanceSoundSet(appearances->getInt(_appearance, "soundapptype"));
    _destroyObjectDelay = appearances->getFloat(_appearance, "destroyobjectdelay", kDefaultDestroyObjectDelay);
    _appearanceRace = appearances->getString(_appearance, "race");
    loadAppearanceMoveRate(*appearances);
    _bloodColour = appearances->getString(_appearance, "bloodcolr");
    _headTrack = appearances->getBool(_appearance, "headtrack");
    _headBone = boost::to_lower_copy(appearances->getString(_appearance, "headbone"));
    if (_headBone.empty()) {
        _headBone = kDefaultHeadBone;
    }
    _headArcH = appearances->getFloat(_appearance, "head_arc_h", kDefaultHeadArcH);
    _headArcV = appearances->getFloat(_appearance, "head_arc_v", kDefaultHeadArcV);
    // A blank cell shows no video effect.
    _freeLookEffect = appearances->getInt(_appearance, "freelookeffect", -1);
    _equipSlotsLocked = static_cast<uint32_t>(appearances->getInt(_appearance, "equipslotslocked"));
    // An appearance hands the model of a body variation over to another
    // appearance by naming it as "&N".
    for (size_t variation = 0; variation < _armourAppearances.size(); ++variation) {
        const auto model = appearances->getString(_appearance, std::string("model") + static_cast<char>('a' + variation));
        _armourAppearances[variation] = !model.empty() && model.front() == '&' ? std::stoi(model.substr(1)) : 0;
    }
    _appearancePerceptionRange = appearances->getInt(_appearance, "perceptiondist");

    if (_portraitId > 0) {
        _portrait = _services.game.portraits.getTextureByIndex(_portraitId);
    } else {
        _portrait = _services.game.portraits.getTextureByAppearance(_appearance);
    }
}

void Creature::loadAppearanceMoveRate(const TwoDA &appearances) {
    _appearanceMoveRate = appearances.getString(_appearance, "moverate");
}

void Creature::loadAppearanceSoundSet(int row) {
    auto soundSets = getRequiredTwoDA(_services.resource.twoDas, "appearancesndset");
    _appearanceSoundSet.weapon = soundSets->getInt(row, "weapon");
    _appearanceSoundSet.armorType = boost::to_lower_copy(soundSets->getString(row, "armortype"));
    _appearanceSoundSet.fallDirt = boost::to_lower_copy(soundSets->getString(row, "falldirt"));
    _appearanceSoundSet.fallHard = boost::to_lower_copy(soundSets->getString(row, "fallhard"));
    _appearanceSoundSet.fallMetal = boost::to_lower_copy(soundSets->getString(row, "fallmetal"));
    _appearanceSoundSet.fallWater = boost::to_lower_copy(soundSets->getString(row, "fallwater"));
}

void Creature::loadAppearance() {
    loadAppearanceProperties();

    auto modelSceneNode = buildModel();
    if (modelSceneNode) {
        finalizeModel(*modelSceneNode);
        _sceneNode = std::move(modelSceneNode);
        _sceneNode->setUser(*this);
        _sceneNode->setLocalTransform(_transform);
        _sceneNode->setEnabled(_visible && _clientPresent);
        _stealthShell.reset();
        // A rebuilt model keeps the stealth field.
        if (_stealthMode) {
            presentStealth(true, false);
        }
        // Its walk and run clips pick up their pace from nothing.
        _clipPace = 0.0f;
    }

    // A new model ends the running one-shot.
    _oneShot.reset();
    _animDirty = true;
}

Creature::ModelType Creature::parseModelType(const std::string &s) const {
    if (s == "S" || s == "L") {
        return ModelType::Creature;
    } else if (s == "F") {
        return ModelType::Droid;
    } else if (s == "B") {
        return ModelType::Character;
    }

    throw std::invalid_argument(str(boost::format("Model type '%s' is not supported") % s));
}

void Creature::updateModel() {
    if (!_sceneNode) {
        return;
    }
    auto bodyModelName = getBodyModelName();
    if (bodyModelName.empty()) {
        return;
    }
    auto replacement = _services.resource.models.get(bodyModelName);
    if (!replacement) {
        return;
    }
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    // Visual effects and special-attack weapon visuals carry over to the new
    // model without starting over, and the latest shell covers its new head
    // and weapons.
    for (const auto &applied : effects()) {
        if (auto *visual = dynamic_cast<VisualEffect *>(applied.effect.get())) {
            visual->detachFromBody();
        }
    }
    _specialAttackVisuals.detachFromBody();
    model->setModel(*replacement);
    finalizeModel(*model);
    // The new model shows the presented facing and ground tilt.
    updateTransform();
    for (const auto &applied : effects()) {
        if (auto *visual = dynamic_cast<VisualEffect *>(applied.effect.get())) {
            visual->reattachToBody(*this, *model);
        }
    }
    _specialAttackVisuals.reattachToBody(*this, *model);
    VisualEffect::showLatestShell(*this);
    _oneShot.reset();
    _animDirty = true;
}

void Creature::loadTransformFromGIT(const resource::generated::GIT_Creature_List &git) {
    _position[0] = git.XPosition;
    _position[1] = git.YPosition;
    _position[2] = git.ZPosition;

    float cosine = git.XOrientation;
    float sine = git.YOrientation;
    setFacing(-glm::atan(cosine, sine));
}

bool Creature::isDebilitated(bool meditating) const {
    if (isTemporarilyDead()) {
        return true;
    }
    const auto state = static_cast<CreatureState>(_effectState);
    if (!_game.isTSL()) {
        return state != CreatureState::None;
    }
    return (state != CreatureState::None && state != CreatureState::Confusion && state != CreatureState::DroidConfused) ||
           (meditating && combatStance() == CombatStance::Meditative);
}

bool Creature::isTemporarilyDead() const {
    return _game.party().isMember(*this) && currentHitPoints() <= 0;
}

bool Creature::hasForceShield() const {
    const auto shield = std::find_if(effects().begin(), effects().end(), [](const EffectInstance &effect) {
        return effect.type() == EffectType::ForceShield;
    });
    return shield != effects().end() && shield->integerParameter(0) != 0;
}

bool Creature::isUnableToReact() const {
    return isDead() || isDebilitated() || combatStance() == CombatStance::Meditative;
}

static constexpr int kLargeSparksVisual = 4003;
static constexpr int kLightsaberSparksVisual = 4004;
static constexpr int kMetalParrySparksVisual = 4011;

// A hit, a critical hit, an automatic hit or a resisted attack of a melee
// swing lands; a parried or missed one clashes against a parrying target.
void Creature::presentSwingHit() {
    if (!_swingAttack || _swingAttack->ranged) {
        return;
    }
    auto target = _game.getObjectById(_swingAttack->targetId);
    if (!target) {
        return;
    }
    auto *creature = dyn_cast<Creature>(target.get());
    switch (_swingAttack->result) {
    case AttackResultType::HitSuccessful:
    case AttackResultType::CriticalHit:
    case AttackResultType::AutomaticHit:
    case AttackResultType::AttackResisted:
        playHitSound(*target, false);
        if (creature) {
            if (creature->_modelType == ModelType::Creature && !creature->isDebilitated(true)) {
                creature->playOverlayAnimation(creature->getDamageFlinchAnimation());
            }
            // A living target the blow does not kill grunts in pain: the leader
            // always, anyone else one time in five.
            if (creature->currentHitPoints() > 0 && !_swingAttack->killingBlow &&
                (_game.party().getLeader().get() == creature || randomInt(0, 4) == 0)) {
                creature->playSound(randomInt(0, 1) == 1 ? SoundSetEntry::PainGrunt1 : SoundSetEntry::PainGrunt2);
            }
        }
        presentSwingSparks(*target);
        return;
    case AttackResultType::Parried:
    case AttackResultType::Miss:
        if (creature && creature->isShowingParry()) {
            playHitSound(*target, true);
            // An appearance that aborts on parry breaks off its swing.
            if (_abortOnParry) {
                cutOneShot();
            }
        }
        return;
    default:
        return;
    }
}

// A creature struck by the blow shows sparks at its impact node, turned
// towards the attacker: large sparks for a bare-handed blow, lightsaber sparks
// otherwise. A door shows large sparks and metal parry sparks at its look-at
// hook, or at the world's origin when it has none. Nothing else shows sparks.
void Creature::presentSwingSparks(Object &target) {
    // A target taken out of the world before the blow lands shows none.
    const auto area = target.spatialArea();
    if (!area) {
        return;
    }
    if (auto *creature = dyn_cast<Creature>(&target)) {
        const int visual = _swingAttack->weaponKind == PhysicalAttackKind::Unarmed ? kLargeSparksVisual
                                                                                   : kLightsaberSparksVisual;
        area->get().presentHitSpark(*creature, visual, _position - creature->position(), "impact");
        return;
    }
    if (!isa<Door>(&target)) {
        return;
    }
    glm::vec3 position {0.0f};
    if (auto body = std::dynamic_pointer_cast<ModelSceneNode>(target.sceneNode())) {
        if (auto *hook = body->getNodeByName("lookathook")) {
            position = hook->origin();
        }
    }
    area->get().presentVisualAt(kLargeSparksVisual, position);
    area->get().presentVisualAt(kMetalParrySparksVisual, position);
}

void Creature::playHitSound(const Object &target, bool parried) {
    // The attacker's appearance may name its own weapon sounds; otherwise the
    // item the attack struck with does.
    int row = _appearanceSoundSet.weapon;
    if (row == 0) {
        if (auto weapon = weaponForHitSound(_swingAttack->weaponKind)) {
            row = weapon->weaponMaterialType();
        }
    }
    const std::string sound = parried ? "parry" : hitSoundMaterial(target);
    playWeaponSound(row, sound + std::to_string(randomInt(0, 1)));
}

// The main hand swings the right weapon, as both hands of a double-bladed
// wield do; the off hand swings the left one.
std::shared_ptr<Item> Creature::weaponForHitSound(PhysicalAttackKind kind) const {
    if (kind == PhysicalAttackKind::MainHand || kind == PhysicalAttackKind::ExtraMainHand ||
        getWieldType() == CreatureWieldType::DoubleBladedSword) {
        return getEquippedItem(InventorySlots::rightWeapon);
    }
    return kind == PhysicalAttackKind::Offhand ? getEquippedItem(InventorySlots::leftWeapon) : nullptr;
}

// A creature sounds as its force shield, else as its appearance's armour,
// else as its body armour, else as leather. A door or placeable sounds as the
// armour of its sound type.
std::string Creature::hitSoundMaterial(const Object &target) const {
    if (const auto *creature = dyn_cast<const Creature>(&target)) {
        if (creature->hasForceShield()) {
            return "forcefield";
        }
        auto material = creature->_appearanceSoundSet.armorType;
        if (material.empty()) {
            if (auto armor = creature->getEquippedItem(InventorySlots::body)) {
                material = armor->armorType();
            }
        }
        return material.empty() ? "leather" : material;
    }
    if (const auto *door = dyn_cast<const Door>(&target)) {
        return door->soundArmorType();
    }
    if (const auto *placeable = dyn_cast<const Placeable>(&target)) {
        return placeable->soundArmorType();
    }
    return "";
}

// The creature swings its right weapon, or a lightsaber in its left hand.
// Its swing sounds begin with its first swing.
void Creature::playSwingSound(const std::string &name, int variants) {
    if (!_swingAttack) {
        return;
    }
    int row = 0;
    if (auto right = getEquippedItem(InventorySlots::rightWeapon)) {
        row = right->weaponMaterialType();
    }
    auto left = getEquippedItem(InventorySlots::leftWeapon);
    if (left && left->isLightsaber()) {
        row = left->weaponMaterialType();
    }
    playWeaponSound(row, name + std::to_string(randomInt(0, variants - 1)));
}

// A weapon sound plays once, 1.5 m above the creature's feet; a weapon with
// no sound in the column plays nothing.
void Creature::playWeaponSound(int weaponSoundRow, const std::string &column) {
    const auto &resRef = _services.game.combatTables.weaponSound(weaponSoundRow, column);
    if (resRef.empty()) {
        return;
    }
    if (auto clip = _services.resource.audioClips.get(resRef)) {
        _services.audio.mixer.play(std::move(clip), AudioType::Sound, 1.0f, false, _position + glm::vec3(0.0f, 0.0f, 1.5f));
    }
}

// A creature hitting the ground plays its sound set's fall sound for the
// surface under its feet once, at its feet; another surface, or a sound set
// with no sound for the surface, plays nothing.
void Creature::playFallSound() {
    const auto sound = fallSound(_game.module()->area()->getSurfaceMaterial(_position));
    if (!sound) {
        return;
    }
    const auto &resRef = _appearanceSoundSet.*sound;
    if (resRef.empty()) {
        return;
    }
    if (auto clip = _services.resource.audioClips.get(resRef)) {
        _services.audio.mixer.play(std::move(clip), AudioType::Sound, 1.0f, false, _position);
    }
}

bool Creature::isShowingParry() const {
    return _oneShot && _services.game.animations.isParry(_oneShot->clip);
}

bool Creature::isHeardByLeader() const {
    auto leader = _game.party().getLeader();
    auto area = _game.module()->area();
    return leader && area->isObjectResident(*leader) && area->isObjectResident(*this) &&
           getSquareDistanceTo(*leader) <= 900.0f;
}

void Creature::playBattleCry() {
    if (isHeardByLeader()) {
        playSound(static_cast<SoundSetEntry>(randomInt(static_cast<int>(SoundSetEntry::BattleCry1),
                                                       static_cast<int>(SoundSetEntry::BattleCry5))));
    }
}

bool Creature::isInvisibleTo(const Creature &observer) const {
    for (const EffectInstance &applied : effects()) {
        if (!applied.hasLiveRuntimeSource() ||
            applied.type() != EffectType::Invisibility ||
            !applied.appliesVersus(std::cref(observer))) {
            continue;
        }

        auto type = static_cast<InvisibilityType>(
            applied.integerParameter(0));
        switch (type) {
        case InvisibilityType::Normal:
        case InvisibilityType::Improved:
            if (observer.hasVisibilityCounter(
                    kSeeInvisibleCounter | kTrueSeeingCounter)) {
                continue;
            }
            return true;
        case InvisibilityType::Darkness:
            if (observer.hasVisibilityCounter(
                    kUltravisionCounter | kTrueSeeingCounter)) {
                continue;
            }
            return true;
        }
    }
    return false;
}

void Creature::setVisibilityCounter(int bit) {
    _visibilityCounterBits |= bit;
}

void Creature::restoreBlindnessCounter(int mask, uint64_t removedApplication) {
    int remaining = _visibilityCounterBits & ~mask;
    for (const auto &effect : effects()) {
        if (effect.serializedType > SavedEffectTypes::blindness) {
            break;
        }
        if (effect.serializedType == SavedEffectTypes::blindness && effect.applicationOrder != removedApplication) {
            remaining |= effect.integerParameter(0);
        }
    }
    _visibilityCounterBits = remaining;
}

bool Creature::hasVisibilityCounter(int bits) const {
    // An equipped effect whose item is gone counts as removed: a counter that
    // only such effects of its type give is off.
    int effective = _visibilityCounterBits;
    const std::array<std::pair<EffectType, int>, 3> grants {{
        {EffectType::SeeInvisible, kSeeInvisibleCounter},
        {EffectType::TrueSeeing, kTrueSeeingCounter},
        {EffectType::Ultravision, kUltravisionCounter},
    }};
    for (const auto &[type, bit] : grants) {
        bool retired = false;
        bool live = false;
        for (const auto &applied : effects()) {
            if (applied.type() != type) {
                continue;
            }
            if (applied.hasLiveRuntimeSource()) {
                live = true;
            } else {
                retired = true;
            }
        }
        if (retired && !live) {
            effective &= ~bit;
        }
    }
    return (effective & bits) != 0;
}

void Creature::restoreVisibilityCounter(
    EffectType type,
    int bit,
    uint64_t removedApplication,
    bool trueSeeingGivesUltravision) {

    _visibilityCounterBits &= ~bit;
    bool another = std::any_of(
        effects().begin(),
        effects().end(),
        [type, removedApplication](const EffectInstance &applied) {
            return applied.applicationOrder != removedApplication &&
                   applied.hasLiveRuntimeSource() &&
                   applied.type() == type;
        });
    if (another) {
        if (trueSeeingGivesUltravision) {
            _visibilityCounterBits |= kUltravisionCounter;
        } else {
            _visibilityCounterBits |= bit;
        }
    }
}

void Creature::refreshVisibilityPerception() {
    auto module = _game.module();
    auto area = module ? module->area() : nullptr;
    if (area && area->isObjectResident(*this)) {
        area->refreshPerceptionFor(*this);
    }
}

// The creature keeps its own entry.
void Creature::perceiveAfresh() {
    std::vector<uint32_t> perceived;
    for (const auto *entries : {&_perception.seen, &_perception.heard, &_perception.invisible}) {
        for (const auto &entry : *entries) {
            if (entry.first != _id) {
                perceived.push_back(entry.first);
            }
        }
    }
    for (const uint32_t id : perceived) {
        forgetPerceived(id);
    }
    if (!actsAsPC() && isDead()) {
        return;
    }
    _game.module()->area()->perceiveNow(_game.getObjectById<Creature>(_id));
}

// The hostile action groups removed when the object turns invisible or the
// creature is pacified: an attack, a cast or an item cast aimed at it.
void Creature::clearHostileActionsAgainst(const Object &object) {
    std::vector<OrdinaryActionQueue::Node> nodes(_actions.nodes.begin(), _actions.nodes.end());
    detail::discardCombatActionGroups(nodes, _game.isTSL(), [](const auto &node) { return node->groupId; }, [](const auto &node) { return node->actionId; }, [&object](uint32_t action, const OrdinaryActionQueue::Node &node) {
            if (!node->action) {
                return false;
            }
            const auto type = static_cast<SavedActionType>(action);
            auto &command = node->action->combatAction();
            if (type == SavedActionType::AttackObject) {
                if (const auto *attack = dyn_cast<AttackObjectAction>(&command)) {
                    return attack->target().get() == &object;
                }
                const auto *feat = dyn_cast<UseFeatAction>(&command);
                return feat && feat->target().get() == &object;
            }
            if (type == SavedActionType::CastSpell || type == SavedActionType::ItemCastSpell) {
                const auto *cast = dyn_cast<CastSpellAction>(&command);
                return cast && cast->target().get() == &object;
            }
            return false; }, [this](const OrdinaryActionQueue::Node &node) { discardGroupNode(node); });
}

// A Force push holds only the creature's moves, through its AI state.
bool Creature::canExecuteActions() const {
    return !_dead && !isTemporarilyDead() && !hasEffect(EffectType::Stunned) &&
           !isImmobilizingState(static_cast<CreatureState>(_effectState));
}

// The commands a party member at zero vitality still carries out; it fails
// every other.
static bool runsWhileDown(SavedActionType type) {
    switch (type) {
    case SavedActionType::SpeakString:
    case SavedActionType::StartConversation:
    case SavedActionType::DisarmMine:
    case SavedActionType::RecoverMine:
    case SavedActionType::FlagMine:
    case SavedActionType::ExamineMine:
    case SavedActionType::SetMine:
    case SavedActionType::SpeakStringByStrRef:
    case SavedActionType::GiveItem:
    case SavedActionType::TakeItem:
    case SavedActionType::RandomWalk:
    case SavedActionType::Appear:
    case SavedActionType::FollowCheck:
    case SavedActionType::BarkString:
    case SavedActionType::CombatDispatch:
    case SavedActionType::SurrenderToEnemies:
    case SavedActionType::SwitchWeapons:
        return true;
    default:
        return false;
    }
}

bool Creature::takesUpActions() const {
    return canExecuteActions() || isTemporarilyDead();
}

bool Creature::permitsAction(const Action &action) const {
    const Action &command = action.combatAction();
    if (isTemporarilyDead()) {
        return runsWhileDown(static_cast<SavedActionType>(command.serializedActionId()));
    }
    switch (command.type()) {
    case ActionType::MoveToObject:
        return canMove() || clicksTriggerInReach(*static_cast<const MoveToObjectAction &>(command).target());
    case ActionType::MoveToPoint:
    case ActionType::MoveToLocation:
    case ActionType::MoveAwayFromObject:
    case ActionType::MoveAwayFromLocation:
    case ActionType::RandomWalk:
    case ActionType::Follow:
    case ActionType::FollowLeader:
    case ActionType::FollowOwner:
    case ActionType::ForceFollowObject:
        return canMove();
    case ActionType::AttackObject:
        return canAttack();
    case ActionType::UseFeat:
        return !isPhysicalAttackFeat(static_cast<const UseFeatAction &>(command).feat()) || canAttack();
    default:
        return canExecuteActions();
    }
}

bool Creature::clicksTriggerInReach(const Object &target) const {
    const auto *trigger = dyn_cast<Trigger>(&target);
    return trigger && !trigger->isTrap() && !isDead() && !isTemporarilyDead() && isInUseRange(*trigger, 0.0f);
}

bool Creature::isSelectable() const {
    // A creature the player's side has not detected cannot be picked. Its
    // items are never looted from it directly, but from its body bag.
    return _clientPresent && !isTemporarilyDead() && (!_dead || _selectableWhenDead);
}

bool Creature::isRunLimited() const {
    return _runLimited || movesStealthily();
}

bool Creature::movesStealthily() const {
    return _stealthMode && (!_game.isTSL() || !hasEffectiveFeat(FeatType::StealthRun));
}

void Creature::setCombatStance(CombatStance stance) {
    if (_combatStance == stance) {
        return;
    }
    _combatStance = stance;
    _animDirty = true;
}

void Creature::cancelAllCombatModes() {
    setOrientationLock(script::kObjectInvalid);
    // The ready pose is refused while dead or, for party members, at zero vitality.
    if (!isDead() && (!_game.party().isMember(*this) || currentHitPoints() > 0)) {
        resumeStateDrivenAnimation();
    }
    if (auto action = _currentCombatAction.lock()) {
        action->combatAction().clearSpecialAttacks();
    }
}

bool Creature::isInTotalDefense() const {
    return _combatStance == CombatStance::TotalDefense || _roundActionKind == kTotalDefenseRoundAction;
}

bool Creature::addStanceActions(CombatStance stance, const std::shared_ptr<Object> &target,
                                bool clearActions, bool toFront, bool fromRound) {
    if (!isCommandable()) {
        return false;
    }
    if (stance == CombatStance::Meditative) {
        addAction(_game.newAction<CombatStanceAction>(CombatStance::Meditative));
        return true;
    }
    if (stance != CombatStance::TotalDefense) {
        return true;
    }
    if (fromRound) {
        if (clearActions) {
            clearAllActions(true);
        }
        auto action = _game.newAction<CombatStanceAction>(CombatStance::TotalDefense, target);
        if (toFront) {
            addActionOnTop(std::move(action));
        } else {
            addAction(std::move(action));
        }
        broadcastCombatState(_id);
        return true;
    }
    // Outside combat the stance request replaces the controlled creature's
    // queue before the round entry and its dispatcher are installed.
    if (!isInCombat()) {
        _game.combat().clearControlledActions(*this);
    }
    _game.combat().scheduleStance(*this, target);
    return true;
}

bool Creature::addSwitchWeaponsAction(bool immediate) {
    if (!isCommandable()) {
        return false;
    }
    if (!immediate) {
        if (!isInCombat() && !_game.combat().hasScheduled(*this)) {
            _game.combat().clearControlledActions(*this);
        }
        if (!actions().nodes.empty()) {
            _game.combat().scheduleSwitchWeapons(*this);
            return true;
        }
    }
    // A swap taken at once, or asked for with nothing queued, clears the
    // queue forced, whoever the creature is and whether or not it fights.
    clearAllActions(true);
    addAction(_game.newAction<SwitchWeaponsAction>());
    return true;
}

bool Creature::isSwitchWeaponsCoolingDown() const {
    return _services.system.clock.micros() < _switchWeaponsReadyAt;
}

void Creature::startSwitchWeaponsCooldown() {
    _switchWeaponsReadyAt = _services.system.clock.micros() + kSwitchWeaponsCooldownMicros;
}

void Creature::startItemUseCooldown() {
    _itemUseCooldown = kItemUseCooldown;
}

bool Creature::requestSwitchWeapons(bool immediate) {
    if (isSwitchWeaponsCoolingDown()) {
        return false;
    }
    if (_game.module()->area()->playerRestrictMode()) {
        return false;
    }
    startSwitchWeaponsCooldown();
    // A swap asked for at once clears the queue, forced, and is made as the
    // creature next runs its actions, also under an open menu.
    return addSwitchWeaponsAction(immediate);
}

void Creature::beginSpellActivity(int spellId, bool itemCast, bool hostile, bool interrupts) {
    _roundActionKind = itemCast ? kItemCastRoundAction : kCastRoundAction;
    // A cast round, a power or an item, is a spell-cast round.
    _combatState.attackAction = ActionType::CastSpell;
    // The round remembers its power; an item use counts as power 0.
    _combatState.roundSpell = itemCast ? 0 : spellId;
    // A fake cast takes the round but interrupts nothing.
    if (!interrupts) {
        return;
    }
    if (itemCast) {
        // Only a hostile item spell gives the user away.
        if (hostile) {
            interruptActivities();
        }
        return;
    }
    // TSL casts leave activities alone while a conversation holds the screen,
    // and a stealthed caster keeps stealth through its stealth-compatible powers.
    if (_game.isTSL() && _game.isConversationActive()) {
        return;
    }
    bool keepStealth = false;
    if (_game.isTSL() && _stealthMode) {
        switch (static_cast<SpellType>(spellId)) {
        case SpellType::MindTrick:
        case SpellType::BeastTrick:
        case SpellType::BeastConfusion:
        case SpellType::Confusion:
        case SpellType::DroidTrick:
        case SpellType::DroidConfusion:
            keepStealth = true;
            break;
        default:
            break;
        }
    }
    interruptActivities(keepStealth);
}

void Creature::interruptActivities(bool keepStealth) {
    if (!keepStealth) {
        setStealthMode(false);
    }
    if (isInConversation() && !_conversationPaused) {
        _game.stopConversationParticipation(*this);
    }
}

void Creature::setStealthMode(bool enabled) {
    if (_stealthMode == enabled) {
        return;
    }
    _stealthMode = enabled;
    _animDirty = true;
    if (isRunLimited() && _movementType == MovementType::Run) {
        setMovementType(MovementType::Walk);
    }
    presentStealth(enabled, true);
}

// Entering stealth plays the BeginStealth sound and wraps the creature in the
// stealth field; leaving it removes the field with the cloak-removal effect.
// The controlled creature also hears the stealth cue both ways.
void Creature::presentStealth(bool enabled, bool announce) {
    // A creature loaded in stealth has no model yet; its model takes up the
    // field once built.
    if (!_sceneNode) {
        return;
    }
    if (enabled && announce) {
        playSound(SoundSetEntry::BeginStealth);
    }
    presentStealthField(enabled, announce);
    if (announce && _game.party().getLeader().get() == this) {
        if (auto cue = _services.resource.audioClips.get(kStealthCue)) {
            _services.audio.mixer.play(std::move(cue), AudioType::Sound, 1.0f, false, _position);
        }
    }
    // TSL shows the field on an owner's puppet too, and the owner's stealth
    // sound again; the puppet itself does not stealth.
    if (announce && _game.isTSL() && _assignedPuppet != -1) {
        if (auto puppet = _game.party().getAvailablePuppet(_assignedPuppet)) {
            if (enabled) {
                playSound(SoundSetEntry::BeginStealth);
            }
            puppet->presentStealthField(enabled, true);
        }
    }
}

void Creature::presentStealthField(bool enabled, bool announce) {
    // An owner's puppet that is not spawned has no model to show the field on.
    if (!_sceneNode) {
        return;
    }
    auto &effects = _services.game.visualEffects;
    // Each shell shares the body's one shell slot with the visual effects'
    // shells: the latest one live shows.
    if (enabled) {
        const VisualEffectDesc &field = effects.get(kStealthFieldVisual).value();
        const auto texture = shieldTextureForProgram(field.progFXDuration);
        _stealthShell = texture.empty() ? nullptr : _services.resource.textures.get(texture, TextureUsage::MainTex);
        _stealthShellOrder = _nextEffectApplicationOrder;
    } else {
        _stealthShell.reset();
        if (announce) {
            // The cloak removal is shown as an impact: its impact program's
            // shell for one second, with the row's impact sound.
            const VisualEffectDesc &removal = effects.get(kCloakRemovalVisual).value();
            const auto texture = shieldTextureForProgram(removal.progFXImpact);
            _cloakRemovalShell = texture.empty() ? nullptr : _services.resource.textures.get(texture, TextureUsage::MainTex);
            _cloakRemovalShellOrder = _nextEffectApplicationOrder;
            _cloakRemovalTime = _cloakRemovalShell ? 1.0f : 0.0f;
            if (removal.soundImpact) {
                _services.audio.mixer.play(removal.soundImpact, AudioType::Sound, 1.0f, false, _position);
            }
        }
    }
    VisualEffect::showLatestShell(*this);
}

// A live stealth field always began after the last exit flash.
std::shared_ptr<graphics::Texture> Creature::latestOwnShell(uint64_t &order) const {
    if (_cloakRemovalShell && !_stealthShell) {
        order = _cloakRemovalShellOrder;
        return _cloakRemovalShell;
    }
    order = _stealthShellOrder;
    return _stealthShell;
}

// TSL grants the skill through the stealth feat and the Force Camouflage powers.
bool Creature::canUseStealthSkill() const {
    if (_game.isTSL() && hasTSLStealthTalent()) {
        return true;
    }
    return _attributes.hasSkill(SkillType::Stealth);
}

bool Creature::hasTSLStealthTalent() const {
    return hasEffectiveFeat(kStealthFeat) || _attributes.hasSpell(SpellType::ForceCamouflage) ||
           _attributes.hasSpell(SpellType::ImprovedForceCamouflage) ||
           _attributes.hasSpell(SpellType::MasterForceCamouflage);
}

// Outside restrict mode, with the skill, and with a stealth unit worn (or, in
// TSL, the stealth feat or powers).
bool Creature::isStealthCapable() const {
    if (_game.module()->area()->playerRestrictMode()) {
        return false;
    }
    if (!canUseStealthSkill()) {
        return false;
    }
    for (int slot : {InventorySlots::belt, InventorySlots::cArmour}) {
        auto item = getEquippedItem(slot);
        if (item && item->itemType() == Item::kStealthUnitItemType) {
            return true;
        }
    }
    return _game.isTSL() && hasTSLStealthTalent();
}

void Creature::toggleStealth() {
    if (isDead() || (isPartyMember() && currentHitPoints() <= 0)) {
        return;
    }
    if (!canUseStealthSkill()) {
        return;
    }
    if (_stealthMode) {
        setStealthMode(false);
    } else if (_combatState.active) {
        if (_game.party().getLeader().get() == this) {
            _game.addFeedbackMessage(kStealthInCombatStrRef);
        }
    } else if (!isInConversation()) {
        setStealthMode(true);
    }
}

void Creature::useStealthSkill() {
    if (!canUseStealthSkill()) {
        if (_game.party().getLeader().get() == this) {
            _game.addFeedbackMessage(kSkillUnusableStrRef);
        }
        return;
    }
    toggleStealth();
}

void Creature::updateMindTrickPerception(const Creature &target, bool heard, bool seen) {
    if (!_game.isTSL() || (!heard && !seen) || target.isStealthed()) {
        return;
    }
    if (_effectState != static_cast<int>(CreatureState::MindTrick) &&
        _effectState != static_cast<int>(CreatureState::DroidScramble))
        return;
    if (getReputationToward(target) <= 10 && getSquareDistanceTo(target) <= 10.0f) {
        removeMindTrickEffects();
    }
}

std::shared_ptr<Area> Creature::battleMusicArea() const {
    if (_game.party().getLeader().get() != this) {
        return nullptr;
    }
    auto area = _game.module()->area();
    return area->isObjectResident(*this) ? area : nullptr;
}

void Creature::setExcitedState(int row) {
    static constexpr float kBattleMusicEnemyRange = 30.0f;
    const auto duration = _services.game.combatTables.excitedDuration(row);
    if (duration <= _excitedMilliseconds) {
        return;
    }
    _excitedMilliseconds = duration;
    auto area = battleMusicArea();
    if (!area || !Combat::findNearestEnemy(*area, *this, *this, kBattleMusicEnemyRange)) {
        return;
    }
    area->playBattleMusic(true);
}

// Excitement counts down by the frame time and runs out, once no more than a
// frame is left, without looking for enemies.
void Creature::updateExcitement() {
    if (_excitedMilliseconds == 0) {
        return;
    }
    const auto frame = static_cast<uint32_t>(std::max(0, _frameMilliseconds));
    if (_excitedMilliseconds > frame) {
        _excitedMilliseconds -= frame;
        return;
    }
    _excitedMilliseconds = 0;
    if (auto area = battleMusicArea()) {
        area->playBattleMusic(false);
    }
}

void Creature::update(float dt) {
    _frameMilliseconds = static_cast<int>(dt * 1000.0f);
    updateExcitement();
    _itemUseCooldown = std::max(0.0f, _itemUseCooldown - dt);
    _perceptionRolls.age += dt;
    if (_cloakRemovalTime > 0.0f) {
        _cloakRemovalTime -= dt;
        if (_cloakRemovalTime <= 0.0f) {
            _cloakRemovalShell.reset();
            VisualEffect::showLatestShell(*this);
        }
    }
    Object::update(dt);
    standOnGround();
    updateMineCheck(dt);
    updateForcedMove(dt);
    updateStateHeartbeat(dt);
    updateFidget();
    updateTurning();
    if (_weaponDrawPending && !_game.isPaused()) {
        playWeaponDraw();
    }
    animateFireAndForget(dt);
    _spellCastVisuals.update(dt);
    _specialAttackVisuals.update();
    updateGroundTilt();
    updateLookAt();
    updateCombat(dt);
    updateRegeneration(dt);
    updateLightsaberSoundPositions();
}

// With the clock stopped the frame takes no time, so nothing turns.
void Creature::runActions() {
    _frameMilliseconds = 0;
    Object::runActions();
    standOnGround();
}

void Creature::standOnGround() {
    if (auto area = spatialArea()) {
        area->get().groundCreature(*this);
    }
}

// A dead creature follows the ground only on a slope of 10 to 25 degrees; a
// living one whose appearance tilts follows any slope under 50 degrees. Each
// update shows the tilt half way between the last aim and the new one.
void Creature::updateGroundTilt() {
    if (!_sceneNode || _stunt) {
        return;
    }
    const glm::vec3 up {0.0f, 0.0f, 1.0f};
    glm::vec3 axis {up};
    const bool dead = _dead || isTemporarilyDead();
    if (dead || _groundTiltAppearance) {
        // Outside an area the ground is level.
        const auto area = spatialArea();
        const glm::vec3 normal {area ? area->get().groundNormal(_position) : up};
        const float pitch = glm::degrees(std::atan2(normal.z, glm::length(glm::vec2(normal))));
        if (dead ? (pitch >= 65.0f && pitch <= 80.0f) : pitch > 40.0f) {
            axis = glm::normalize(normal);
        }
    }
    const glm::quat target = glm::rotation(up, axis);
    const glm::quat level {1.0f, 0.0f, 0.0f, 0.0f};
    if (target == level && _groundTiltTarget == level && _groundTilt == level) {
        return;
    }
    _groundTilt = glm::slerp(_groundTiltTarget, target, 0.5f);
    _groundTiltTarget = target;
    updateTransform();
}

// The ground tilt turns the model only; the creature keeps its facing.
void Creature::updateTransform() {
    Object::updateTransform();
    if (_sceneNode && !_stunt) {
        _sceneNode->setLocalTransform(glm::translate(glm::mat4(1.0f), _position) * glm::mat4_cast(_groundTilt * presentedOrientation()));
    }
}

static constexpr int kPartyMemberDiedTutorial = 15;
// Script event type that runs the module's player-death script.
static constexpr int kPlayerDeathEvent = 10;
static constexpr int kSpectacularDeathVisual = 6003;

// Head look-at

static const std::string kCameraHookNode("camerahook");
static const std::string kFreeLookHookNode("freelookhook");
static const std::string kHeadNode("head_g");

static std::shared_ptr<ModelSceneNode> objectModel(const Object &object) {
    auto node = object.sceneNode();
    return node && node->type() == SceneNodeType::Model ? std::static_pointer_cast<ModelSceneNode>(node) : nullptr;
}

// The body model and a separate head model; the look runs in whichever has the bone.
const std::vector<std::reference_wrapper<ModelSceneNode>> &Creature::lookAtModels() const {
    _lookAtModels.clear();
    auto body = objectModel(*this);
    // A creature that has not built its model has nothing to turn.
    if (!body) {
        return _lookAtModels;
    }
    _lookAtModels.push_back(*body);
    auto head = body->getAttachment(g_headHookNode);
    if (head && head->type() == SceneNodeType::Model) {
        _lookAtModels.push_back(static_cast<ModelSceneNode &>(*head));
    }
    return _lookAtModels;
}

// The point looked at: a creature's camera hook, or the root of anything else.
static glm::vec3 lookAtAimPoint(const Object &target) {
    auto model = objectModel(target);
    if (model && target.type() == ObjectType::Creature) {
        if (auto hook = model->getNodeByName(kCameraHookNode)) {
            return glm::vec3(hook->absoluteTransform()[3]);
        }
    }
    return model ? glm::vec3(model->absoluteTransform()[3]) : target.position();
}

float Creature::headTurnHorizontal() const {
    return _headArcH;
}

float Creature::headHeight() const {
    auto model = objectModel(*this);
    float headZ = 0.0f;
    if (auto head = model->getNodeByName(kHeadNode)) {
        headZ = head->absoluteTransform()[3].z;
    }
    if (headZ != 0.0f) {
        const float height = headZ - _position.z;
        if (!(height < 0.0f)) {
            return height;
        }
    }
    return _height;
}

glm::vec3 Creature::freeLookPoint() const {
    if (auto model = objectModel(*this)) {
        for (const auto &name : {kFreeLookHookNode, kCameraHookNode}) {
            if (auto hook = model->getNodeByName(name)) {
                return glm::vec3(hook->absoluteTransform()[3]);
            }
        }
    }
    return _position + glm::vec3(0.0f, 0.0f, 2.0f);
}

int Creature::freeLookVideoEffect() const {
    return _freeLookEffect;
}

float Creature::cameraHookHeight() const {
    auto model = objectModel(*this);
    if (auto hook = model->getNodeByName(kCameraHookNode)) {
        return hook->localTransform()[3].z;
    }
    return headHeight();
}

bool Creature::lookAt(const std::shared_ptr<Object> &target, float maxDistance) {
    const uint32_t id = target ? target->id() : script::kObjectInvalid;
    if (_combatState.active || _headLookSuspended || id == _lookAtId) {
        return false;
    }
    const auto &models = lookAtModels();
    if (models.empty()) {
        return false;
    }
    if (!_headTrack || !target) {
        _lookAtId = script::kObjectInvalid;
        _lookAtTarget.reset();
        _lookAtRunning = false;
        for (auto &model : models) {
            model.get().endLookAt();
        }
        return false;
    }
    if (!objectModel(*target)) {
        return false;
    }
    // Out of reach: whatever look runs carries on.
    const glm::vec3 offset {target->position() - _position};
    if (glm::dot(offset, offset) > maxDistance * maxDistance) {
        return false;
    }
    _lookAtId = id;
    _lookAtTarget = RuntimeObjectRef<Object>(target);
    _lookAtDistance = maxDistance;
    _lookAtRunning = false;
    const glm::vec3 aim {lookAtAimPoint(*target)};
    for (auto &model : models) {
        if (model.get().beginLookAt(_headBone, _headArcH, _headArcV)) {
            model.get().setLookAtPoint(aim);
            _lookAtRunning = true;
        }
    }
    return true;
}

// A look ends by itself once its object is gone or out of reach; the object
// is still remembered, so asking again for it does nothing.
void Creature::updateLookAt() {
    if (!_lookAtRunning) {
        return;
    }
    auto target = _lookAtTarget.resolve();
    const auto &models = lookAtModels();
    const glm::vec3 offset {target ? target->position() - _position : glm::vec3(0.0f)};
    if (!target || glm::dot(offset, offset) > _lookAtDistance * _lookAtDistance) {
        _lookAtRunning = false;
        for (auto &model : models) {
            model.get().endLookAt();
        }
        return;
    }
    const glm::vec3 aim {lookAtAimPoint(*target)};
    for (auto &model : models) {
        model.get().setLookAtPoint(aim);
    }
}

// END Head look-at

static constexpr float kMineCheckInterval = 0.1f;
static constexpr float kMineCheckRange = 20.0f;
static constexpr int kMineDetectionSummaryStrRef = 42132;
static constexpr int kMineDetectionRollStrRef = 42123;

// Every creature searches for traps it does not know about ten times a second.
// Each search rolls d20 (d10 + 10) plus Awareness per candidate, 5 less while running.
namespace {

// What the mine check reads of an object's trap: the trap, whether it is set
// (a trap trigger's always is, a door's or placeable's while armed), the point
// it is measured to (a trigger's nearest point, or else the object's position)
// and the faction it belongs to.
struct MineCheckTarget {
    std::reference_wrapper<Trap> trap;
    bool set;
    glm::vec3 point;
    Faction faction;
};

std::optional<MineCheckTarget> findMineCheckTarget(Object &object, const glm::vec3 &from) {
    const auto trap = object.findTrap();
    if (!trap) {
        return std::nullopt;
    }
    if (auto *trigger = dyn_cast<Trigger>(&object)) {
        return MineCheckTarget {*trap, trigger->isTrapped(), trigger->nearestPoint(from), trigger->faction()};
    }
    const bool armed = trap->get().isArmed();
    if (auto *door = dyn_cast<Door>(&object)) {
        return MineCheckTarget {*trap, armed, object.position(), door->faction()};
    }
    return MineCheckTarget {*trap, armed, object.position(), static_cast<const Placeable &>(object).faction()};
}

} // namespace

void Creature::updateMineCheck(float dt) {
    if (isPresentationOnly()) {
        return;
    }
    _mineCheckTime += dt;
    if (_mineCheckTime < kMineCheckInterval) {
        return;
    }
    auto area = _game.module()->area();
    _mineCheckTime = 0.0f;

    const int rank = getUnopposedSkillRank(SkillType::Awareness);
    auto &reputes = _services.game.reputes;
    for (const auto &object : area->objects()) {
        // Only the area's set traps are searched.
        const auto target = findMineCheckTarget(*object, _position);
        if (!target) {
            continue;
        }
        auto &trap = target->trap.get();
        auto &detection = trap.detection();
        if (detection.isDetectedBy(_id) || !target->set || !trap.isDetectable()) {
            continue;
        }
        const glm::vec3 offset {target->point - _position};
        if (glm::dot(offset, offset) > kMineCheckRange * kMineCheckRange) {
            continue;
        }
        // A trap of the creature's own faction, or of one it regards as friendly, is not searched for.
        if (reputes.getReputation(target->faction, faction()) > Trap::kHostileStanding || target->faction == faction()) {
            continue;
        }
        const int dc = trap.detectDC(_game);
        const int roll = randomInt(1, 10) + 10 - (_movementType == MovementType::Run ? 5 : 0);
        const bool found = rank + roll >= dc;
        if (!found && !detection.flagged) {
            continue;
        }
        if (auto *trigger = dyn_cast<Trigger>(object.get()); found && !detection.flagged && trigger) {
            reportMineDetection(*trigger, roll, rank, dc);
        }
        // A party member finds the trap for the whole party.
        if (isPartyMember()) {
            for (const auto &member : _game.party().members())
                if (member.creature) {
                    detection.addDetectedBy(member.creature->id());
                }
        } else {
            detection.addDetectedBy(_id);
        }
    }
}

// The detector's side is told when its controlled creature shares the detector's faction.
void Creature::reportMineDetection(const Trigger &trap, int roll, int rank, int dc) {
    auto viewer = _game.party().getLeader();
    if (!viewer || viewer->faction() != faction()) {
        return;
    }
    const int total = roll + rank;
    _game.setCustomToken(0, _name);
    _game.setCustomToken(1, trap.name());
    _game.setCustomToken(2, std::to_string(total));
    _game.setCustomToken(3, std::to_string(dc));
    _game.messageLog().add(
        MessageLog::kFeedbackMessageType,
        MessageLog::Style::Combat,
        _game.getFeedbackText(kMineDetectionSummaryStrRef));
    _game.setCustomToken(0, _name);
    _game.setCustomToken(1, std::to_string(total));
    _game.setCustomToken(2, std::to_string(roll));
    _game.setCustomToken(3, std::to_string(rank));
    _game.messageLog().add(
        MessageLog::kFeedbackMessageType,
        MessageLog::Style::Normal,
        _game.getFeedbackText(kMineDetectionRollStrRef));
}

// The leader looking around in free-look keeps the plain pause, walk and run.
static bool isFreeLookLeader(const Game &game, const Creature &creature) {
    return game.isFreeLook() && game.party().getLeader().get() == &creature;
}

// The pause poses of animations.2da (rows 6-9, 12-15, 72, 256, 257, 264, 304,
// 326 and 357-359, and in TSL also 457, 458, 465 and 471).
static bool isPauseAnimation(const std::string &name) {
    static const std::unordered_set<std::string> kNames {
        "pause1", "pause2", "pauseinj", "pausestl", "pausesh", "pausebrd", "pausetrd", "pausepsn", "choke",
        "cpause1", "cpause2", "default", "pause", "pause3", "meditatesit", "meditatestand", "standstill", "scanning"};
    return kNames.count(name) > 0;
}

// A cast that has reached its end no longer occupies its creature.
static bool isEndedCast(const OrdinaryActionQueue::Node &node) {
    if (!node->action) {
        return false;
    }
    auto &command = node->action->combatAction();
    if (command.isCompleted() || command.isCancelled()) {
        return true;
    }
    if (const auto *cast = dyn_cast<CastSpellAction>(&command)) {
        return cast->castEnded();
    }
    return false;
}

// The queue read from the front: 1 moving, 2 attacking (an attack or
// counter-spell with at most movement before it), 14 casting (a spell or item
// cast before any other busy action), 10 another busy action, 33 in a stance,
// and 0 for none of these. A queue above kLastFreeQueueState is busy and
// keeps its actions when the player gives an order.
static constexpr int kLastFreeQueueState = 9;

static int queueState(const OrdinaryActionQueue &queue) {
    int state = 0;
    for (const auto &node : queue.nodes) {
        switch (node->actionId) {
        case 1:
        case 51:
            if (state == 0) {
                state = 1;
            }
            break;
        case 12:
        case 50:
            if (state <= 1) {
                state = 2;
            }
            break;
        case 15:
        case 46:
            if (state <= 9 && !isEndedCast(node)) {
                state = 14;
            }
            break;
        case 68:
            if (state <= 1) {
                state = 33;
            }
            break;
        case 69:
            if (state == 0) {
                state = 33;
            }
            break;
        case 7:
        case 8:
        case 9:
        case 11:
        case 20:
        case 21:
        case 25:
        case 26:
        case 27:
        case 28:
        case 29:
        case 34:
        case 35:
        case 38:
        case 39:
        case 40:
        case 41:
        case 42:
        case 54:
        case 56:
        case 67:
            if (state <= 9) {
                state = 10;
            }
            break;
        default:
            break;
        }
    }
    return state;
}

// Whether the queue leaves the creature attacking or casting.
static bool isAttackingOrCasting(const OrdinaryActionQueue &queue) {
    const int state = queueState(queue);
    return state == 2 || state == 14;
}

// The controlled leader acting freely shows its ready or injured pose, or any
// pose while looking around in free-look, over whatever loop it holds, and
// otherwise keeps a pause loop and drops any other. A following party member
// in the first two follow slots shows its pose over any loop, except a
// dialog animation while a conversation runs.
//
// Everyone else follows the default rules. The ready loop falls back to the
// pause once the creature neither fights nor attacks or casts. A pause loop
// becomes the ready loop while it attacks or casts, and otherwise the injured
// pause while it is injured, except a choke and except for the leader looking
// around in free-look. The injured pause then stays until another loop is
// chosen.
//
// The flinch pose of hands with no flinch is no pause: the leader and a
// following member leave it for their pose, and everyone else keeps it.
void Creature::updateIdleLoop(const std::string &active) {
    const bool flinchPose = showsDamageFlinchPose();
    switch (animationUpdater()) {
    case AnimationUpdater::Drive: {
        if (isDebilitated() || !canMove()) {
            return;
        }
        const bool injured = !_combatState.active && isInjured() && !isFreeLookLeader(_game, *this);
        const std::string pose = _combatState.active ? getReadyAnimation() : getPauseAnimation(injured);
        const bool force = _combatState.active || injured || _game.isFreeLook();
        if (flinchPose || (active != pose && (force || !isPauseAnimation(active)))) {
            _injuredPause = false;
            _injuredIdle = injured;
            _castAnimation.reset();
            _animDirty = true;
        }
        return;
    }
    case AnimationUpdater::Follow: {
        if (isDebilitated() || followSlot() == -1) {
            return;
        }
        if (_game.isConversationActive() && isPlayingDialogAnimation()) {
            return;
        }
        const bool injured = !_combatState.active && isInjured();
        const std::string pose = _combatState.active ? getReadyAnimation() : getPauseAnimation(injured);
        if (flinchPose || active != pose) {
            _injuredPause = false;
            _injuredIdle = injured;
            _castAnimation.reset();
            _animDirty = true;
        }
        return;
    }
    case AnimationUpdater::Default:
        break;
    }
    if (flinchPose) {
        return;
    }
    const bool attackingOrCasting = isAttackingOrCasting(_actions);
    if (active == getReadyAnimation()) {
        if (!_combatState.active && !attackingOrCasting) {
            _animDirty = true;
        }
    } else if (isPauseAnimation(active)) {
        if (attackingOrCasting) {
            _animDirty = true;
        } else if (active != "choke" && isInjured() && !isFreeLookLeader(_game, *this) &&
                   active != getPauseAnimation(true)) {
            _injuredIdle = true;
            _animDirty = true;
        }
    }
}

namespace {
// The pose a creature held by a state keeps, by its ambient animation state:
// its animation ID and the animations.2da rows that ID names on a character
// and on a creature model, where the model has one.
struct StatePose {
    int ambient;
    int id;
    int characterRow;
    std::optional<int> creatureRow;
};

// The ambient animation state of a choking creature.
constexpr int kChokedAmbientState = 5;

constexpr std::array<StatePose, 10> kStatePoses {{
    {2, 10058, 15, 256},                   // stunned: pausepsn / cpause1
    {kChokedAmbientState, 10150, 72, 264}, // choked
    {6, 10117, 75, 266},                   // whirlwind
    {7, 10124, 74, 74},                    // horrified
    {8, 10118, 270, 270},                  // disabled droid
    {9, 10400, 84, 271},                   // force pushed: g1x1 / ckdbcklp
    {11, 10137, 76, 267},                  // asleep
    {12, 10138, 78, 269},                  // paralyzed
    {15, 10139, 79, std::nullopt},         // knocked down, prone
    {16, 10416, 460, 460},                 // crushed
}};
} // namespace

static std::optional<std::reference_wrapper<const StatePose>> findStatePose(int ambient) {
    for (const auto &pose : kStatePoses) {
        if (pose.ambient == ambient) {
            return std::cref(pose);
        }
    }
    return std::nullopt;
}

void Creature::updateModelAnimation() {
    // A world-space animation of an animated cutscene holds the creature.
    if (holdsCutsceneAnimation()) {
        return;
    }
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model) {
        return;
    }

    if (_animFireForget) {
        const auto base = model->baseAnimationChannel();
        if (base && !base->get().finished) {
            return;
        }

        _animFireForget = false;
        returnToStoredLoop();
    }
    // A queued one-shot holds any new pose until its time runs out.
    if (_oneShot) {
        return;
    }
    // An injured creature limps, except the leader looking around in free-look.
    const auto limps = [this] { return isInjured() && !isFreeLookLeader(_game, *this); };
    if (!_animDirty && _movementType == MovementType::None && !liesDead()) {
        updateIdleLoop(getActiveAnimationName());
    }
    // A walker or runner starts or stops limping as soon as its injury changes,
    // swaps between the walk and run clips as soon as its speed calls for the
    // other, and otherwise its clip follows its speed without starting over.
    else if (!_animDirty && _movementType != MovementType::None) {
        const int id = currentAnimationId();
        if (limps() != (id == kInjuredWalkAnimationId || id == kInjuredRunAnimationId)) {
            _animDirty = true;
        } else if (id == kWalkAnimationId || id == kRunAnimationId || id == kStealthWalkAnimationId ||
                   id == kInjuredWalkAnimationId || id == kInjuredRunAnimationId) {
            const MovementClip clip = movementClip(*model);
            const auto base = model->baseAnimationChannel();
            if (clip.id != id || (clip.anim && base && base->get().anim != clip.anim.get())) {
                _animDirty = true;
            } else if (clip.anim) {
                model->setAnimationSpeed(clip.anim->name(), clip.speed);
            }
        }
    }
    if (!_animDirty) {
        return;
    }

    std::shared_ptr<Animation> anim;
    // Each pose is known by its animation ID.
    AnimationSource source;
    // The corpse, and a state's pose that does not loop, hold their last frame.
    bool holdLastFrame = liesDead();
    // Under the default rules a state's pose that is a pause gives way, as the
    // pause does, to the ready loop while attacking or casting and to the
    // injured pause, choking excepted.
    auto statePose = findStatePose(_effectAmbientState);
    std::optional<int> statePoseRow;
    if (statePose) {
        statePoseRow = _modelType == ModelType::Creature ? statePose->get().creatureRow
                                                         : std::optional<int>(statePose->get().characterRow);
        const std::string clip = statePoseRow
                                     ? _services.game.animations.getNameById(static_cast<uint32_t>(*statePoseRow))
                                     : std::string();
        if (animationUpdater() == AnimationUpdater::Default && isPauseAnimation(clip) &&
            (isAttackingOrCasting(_actions) || (_injuredIdle && statePose->get().ambient != kChokedAmbientState)))
            statePose.reset();
    }

    // Walk and run clips play so that the feet keep pace with the ground.
    float movementClipSpeed = 1.0f;
    switch (_movementType) {
    case MovementType::Run:
    case MovementType::Walk: {
        const MovementClip clip = movementClip(*model);
        anim = clip.anim;
        source.id = clip.id;
        movementClipSpeed = clip.speed;
        // A clip already playing takes its new speed without starting over.
        if (anim) {
            model->setAnimationSpeed(anim->name(), movementClipSpeed);
        }
        break;
    }
    default:
        if (liesDead()) {
            // Each dead pose is its own loop, which a raised creature gets up from.
            anim = model->model().getAnimation(getDeadAnimation());
            source.id = _deathPose == DeathPose::Dead1   ? kDead1AnimationId
                        : _deathPose == DeathPose::Dead3 ? kDead3AnimationId
                                                         : kDeadAnimationId;
        } else if (statePose) {
            if (statePoseRow) {
                const auto row = static_cast<uint32_t>(*statePoseRow);
                anim = model->model().getAnimation(_services.game.animations.getNameById(row));
                holdLastFrame = !_services.game.animations.isLoopingById(row);
            }
            source.id = statePose->get().id;
        } else if (_castAnimation && !_castAnimation->empty()) {
            anim = model->model().getAnimation(*_castAnimation);
        } else {
            // An idle pause turns into the injured pause while the creature is
            // injured, and stays so until another pose is chosen.
            const bool freeLookLeader = isFreeLookLeader(_game, *this);
            if (!_combatState.active && !freeLookLeader && isInjured()) {
                _injuredIdle = true;
            }
            const bool injured = !freeLookLeader && (_injuredPause || _injuredIdle);
            anim = model->model().getAnimation(getPauseAnimation(injured));
            // The pose is the ready one by the creature's state, whatever its
            // clip: a class-0 ready pose shows the pause clip.
            source.id = (_combatState.active || isAttackingOrCasting(_actions))
                            ? kReadyAnimationId
                            : (injured ? kInjuredPauseAnimationId : kPauseAnimationId);
        }
        break;
    }
    setAnimationSource(source);
    // A pose shown standing is the loop one-shots return to. A loop that went
    // on under a one-shot comes back as it was; only a new one is shown anew.
    const bool newLoop = anim && !(_storedLoop && boost::iequals(_storedLoop->clip, anim->name()));
    if (anim && _movementType == MovementType::None) {
        storeLoop(anim->name(), AnimationProperties(), true, source);
    }

    if (anim) {
        // The corpse pose is a short clip; looping/blending it makes the
        // model jerk and never settle, so play it once and hold the final
        // frame. Living poses keep looping and blending.
        int animFlags = holdLastFrame ? AnimationFlags::propagate
                                      : (AnimationFlags::loopBlend | AnimationFlags::propagate);
        auto properties = AnimationProperties::fromFlags(animFlags);
        properties.speed = movementClipSpeed;
        model->playAnimation(*anim, nullptr, properties);
    }
    if (newLoop) {
        presentEquippedItems(anim->name());
    }

    _animDirty = false;
}

// Walk and run clips play so that the feet keep pace with the ground. On a
// path they also go at their own pace, which picks up and falls off with the
// walk; before it has any, they play at their natural speed.
Creature::MovementClip Creature::movementClip(const ModelSceneNode &model) const {
    MovementClip clip;
    const bool running = _movementType == MovementType::Run && !movesStealthily();
    const float speed = running ? runSpeed() : walkSpeed();
    // A run slowed well below its clip's pace shows the walk instead, as does
    // a run on a path while it goes at under half its speed.
    const bool runClip = running && speed >= _runDistance * kRunClipMinimumPace &&
                         !(_path && _walkPace < kRunClipMinimumWalkPace);
    // Limping comes before sneaking. Creature models have no injured run,
    // and a run too slow for its clip shows the walk: the plain walk on
    // creature models. A clip the model lacks leaves the last one playing.
    const bool injured = isInjured() && !isFreeLookLeader(_game, *this);
    if (injured && running) {
        clip.anim = model.model().getAnimation(runClip ? getFirstIfCreatureModel("crun", "runinj")
                                                       : getFirstIfCreatureModel("cwalk", "walkinj"));
        clip.id = kInjuredRunAnimationId;
    } else if (injured) {
        clip.anim = model.model().getAnimation(getFirstIfCreatureModel("cwalkinj", "walkinj"));
        clip.id = kInjuredWalkAnimationId;
    } else if (movesStealthily()) {
        // Walking and running alike, a stealthy creature sneaks.
        clip.anim = model.model().getAnimation(getStealthWalkAnimation());
        clip.id = kStealthWalkAnimationId;
    } else if (runClip) {
        clip.anim = model.model().getAnimation(getRunAnimation());
        clip.id = kRunAnimationId;
    } else {
        clip.anim = model.model().getAnimation(getWalkAnimation());
        clip.id = kWalkAnimationId;
    }
    const float loopDistance = runClip ? _runDistance : _walkDistance;
    const float pace = _path ? _clipPace : 1.0f;
    if (clip.anim && speed > 0.0f && loopDistance > 0.0f && clip.anim->length() > 0.0f && pace > 0.0f) {
        clip.speed = pace * speed * clip.anim->length() / loopDistance;
    }
    return clip;
}

void Creature::animateFireAndForget(float dt) {
    // The flinch pose keeps the time left of the one-shot it cut, without
    // counting it down.
    if (_oneShot && _oneShot->id != kDamageFlinchAnimationId) {
        _oneShot->remainingMilliseconds -= dt * 1000.0f;
        if (_oneShot->remainingMilliseconds < 0.0f) {
            if (_oneShot->overlayRow) {
                switchOffOverlay(*_oneShot);
            }
            _oneShot.reset();
            returnToStoredLoop();
        }
    }
    updateModelAnimation();
    if (!_fireForgetQueue.empty() && currentClipLoops()) {
        playTopFireForgetAnimation();
    }
}

// The first queued clip starts. One that waited is sped up to end on time,
// and runs for at least 50 ms; one given no speed plays at its natural speed.
// A layer plays at its natural speed over the loop, so its time only paces
// the queue. A swing starting carries its attack to its hit.
void Creature::playTopFireForgetAnimation() {
    // A world-space animation of an animated cutscene holds the creature.
    if (holdsCutsceneAnimation()) {
        return;
    }
    static constexpr int kMinFireForgetMilliseconds = 50;
    const FireForgetEntry entry = std::move(_fireForgetQueue.front());
    _fireForgetQueue.pop_front();
    // Only the time of day of the wait counts, and a start still ahead has no wait.
    const int waited = static_cast<int>(
        _game.worldTimeSince(entry.startDay, entry.startTime) % _game.millisecondsPerWorldDay());
    const int length = std::max(kMinFireForgetMilliseconds, entry.lengthMilliseconds - waited);
    const bool overlayRow = _services.game.animations.isOverlay(entry.clip);
    const bool layered = entry.layered || overlayRow;
    AnimationProperties properties;
    if (layered) {
        properties.flags = AnimationFlags::overlay | AnimationFlags::layer | AnimationFlags::fireForget |
                           AnimationFlags::propagate;
    } else {
        properties.flags = AnimationFlags::blend | AnimationFlags::propagate;
        properties.speed = static_cast<float>(entry.lengthMilliseconds) * entry.rate / static_cast<float>(length);
        if (properties.speed == 0.0f) {
            properties.speed = 1.0f;
        }
        if (playsBackwards(entry.source.id)) {
            properties.speed = -properties.speed;
        }
    }
    std::static_pointer_cast<ModelSceneNode>(_sceneNode)->playAnimation(entry.clip, nullptr, properties);
    presentEquippedItems(entry.clip);
    _oneShot = RunningOneShot {entry.clip, entry.source.id, layered, overlayRow, static_cast<float>(length)};
    setAnimationSource(entry.source);
    if (entry.attack) {
        _swingAttack = entry.attack;
    }
}

// A one-shot ends in the loop it interrupted or the loop that arrived while
// it ran; a loop still running under a layer carries on. A pose chosen
// meanwhile goes through the pose path.
void Creature::returnToStoredLoop() {
    // A world-space animation of an animated cutscene holds the creature.
    if (holdsCutsceneAnimation()) {
        return;
    }
    // Returning to the loop shows again the items a one-shot put away.
    presentEquippedItems(_storedLoop ? _storedLoop->clip : getActiveAnimationName());
    if (_animDirty) {
        return;
    }
    const bool current = _storedLoop && boost::iequals(getActiveAnimationName(), _storedLoop->clip);
    if (_storedLoop) {
        setAnimationSource(_storedLoop->source);
    }
    if (!current && (!_storedLoop || _storedLoop->stateDriven)) {
        _animDirty = true;
        return;
    }
    if (!current) {
        std::static_pointer_cast<ModelSceneNode>(_sceneNode)->playAnimation(_storedLoop->clip, nullptr, _storedLoop->properties);
    }
}

void Creature::presentEquippedItems(const std::string &clip) {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    const bool hides = _services.game.animations.hidesEquippedItems(clip);
    if (!hides && (!_equipmentHidden || _game.isConversationActive())) {
        return;
    }
    for (const auto *hand : {&g_rightHandNode, &g_leftHandNode})
        if (auto *weapon = model->getAttachment(*hand)) {
            weapon->setEnabled(!hides);
        }
    _equipmentHidden = hides;
    if (!hides) {
        _services.game.projectiles.dropThrownLightsaber(*this);
    }
}

bool Creature::storeLoop(const std::string &clip, const AnimationProperties &properties, bool stateDriven,
                         AnimationSource source) {
    if (!_storedLoop || !boost::iequals(_storedLoop->clip, clip)) {
        flushFireForgetQueue();
        if (_storedLoop) {
            queueLoopTransition(source.id, _storedLoop->source.id, properties.speed);
        }
    }
    _storedLoop = StoredLoop {clip, properties, stateDriven, source};
    return !isPlayingOneShotAnimation();
}

// A loop taking over from another may first show the clip leading from one to
// the other, lasting its length at the loop's speed, or that clip and a second
// one-shot.
void Creature::queueLoopTransition(int newId, int oldId, float speed) {
    const bool characterModel = _modelType != ModelType::Creature;
    const LoopTransition transition = loopTransition(newId, oldId, currentAnimationId(), characterModel);
    if (transition.cutsOneShot) {
        cutOneShot();
        _animFireForget = false;
    }
    const int row = loopTransitionRow(transition.clip, characterModel);
    if (row < 0) {
        return;
    }
    const std::string clip = _services.game.animations.getNameById(static_cast<uint32_t>(row));
    const int followRow = loopTransitionRow(transition.follow, characterModel);
    if (followRow < 0) {
        addFireForgetAnimation(clip, false, AnimationSource {transition.clip}, speed);
        return;
    }
    queueTransitionPair(clip, transition.clip, _services.game.animations.getNameById(static_cast<uint32_t>(followRow)),
                        transition.follow);
}

// A transition clip followed by a second one-shot. The first lasts its own
// length, or a second for the die and knock-down clips; the second lasts twice
// the first's length. A clip the model lacks has no length.
void Creature::queueTransitionPair(const std::string &clip, int clipId, const std::string &follow, int followId) {
    static constexpr int kSecondMilliseconds = 1000;
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    auto anim = model->model().getAnimation(clip);
    const float clipLength = anim ? anim->length() * 1000.0f : 0.0f;
    const bool timed = clipId >= 10219 && clipId <= 10222;
    const float length = timed ? static_cast<float>(kSecondMilliseconds) : clipLength;
    addFireForgetAnimation(clip, static_cast<int>(length), length != 0.0f ? clipLength / length : 0.0f, false,
                           AnimationSource {clipId});
    auto followAnim = model->model().getAnimation(follow);
    const float followClipLength = followAnim ? followAnim->length() * 1000.0f : 0.0f;
    const float followLength = 2.0f * clipLength;
    addFireForgetAnimation(follow, static_cast<int>(followLength),
                           followLength != 0.0f ? followClipLength / followLength : 0.0f, false,
                           AnimationSource {followId});
}

// An overlay row is switched off on the body and then the head: the overlays
// that fade out do so from where they are, the others stop at once.
void Creature::switchOffOverlay(const RunningOneShot &oneShot) {
    auto body = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    auto head = static_cast<ModelSceneNode *>(body->getAttachment(g_headHookNode));
    if (overlayFadesOut(oneShot.id)) {
        if (body->fadeOutLayer(oneShot.clip) && head) {
            head->fadeOutLayer(oneShot.clip);
        }
    } else {
        if (body->removeAnimation(oneShot.clip) && head) {
            head->removeAnimation(oneShot.clip);
        }
    }
}

void Creature::setAnimationSource(AnimationSource source) {
    if (source.id != kPauseAnimationId) {
        _fidgetTime = 0;
    }
    _animationSource = source;
    const bool worldSpace = isWorldSpaceCutClip(source.id);
    setPlacedAtOrigin(worldSpace);
    if (worldSpace) {
        _headLookSuspended = true;
    }
}

// A creature still loading has no model yet; it shows nothing, so it queues nothing.
void Creature::addFireForgetAnimation(const std::string &clip, int lengthMilliseconds, float rate, bool layered,
                                      AnimationSource source, std::optional<SwingAttack> attack) {
    // A world-space animation of an animated cutscene holds the creature.
    if (holdsCutsceneAnimation()) {
        return;
    }
    if (!_sceneNode) {
        return;
    }
    _fireForgetQueue.push_back(FireForgetEntry {clip, _game.worldTimeDay(), _game.worldTimeOfDay(), lengthMilliseconds,
                                                rate, layered, source, attack});
}

// Without a speed the clip lasts its natural length. A clip the model lacks
// has no length, so it holds the queue for the shortest time and shows
// nothing.
bool Creature::addFireForgetAnimation(const std::string &clip, bool layered, AnimationSource source, float speed) {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    // A creature still loading has no model yet, and queues nothing.
    if (!model) {
        return false;
    }
    auto anim = model->model().getAnimation(clip);
    float length = anim ? anim->length() * 1000.0f : 0.0f;
    if (speed != 0.0f) {
        length /= std::abs(speed);
    }
    addFireForgetAnimation(clip, static_cast<int>(length), speed, layered, source);
    return true;
}

void Creature::abortFireForgetAnimation(bool clearLayers) {
    // A world-space animation of an animated cutscene holds the creature.
    if (holdsCutsceneAnimation()) {
        return;
    }
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (clearLayers && model) {
        model->removeLayers();
    }
    // The running one-shot, or the flinch pose, ends in the stored loop; with
    // neither the loop is already current. The class-0 ready pose, which
    // does not loop, then gives way to the pause it shows.
    if (_oneShot || _animFireForget || showsDamageFlinchPose()) {
        _oneShot.reset();
        _animFireForget = false;
        returnToStoredLoop();
    }
    if (showsClassZeroReady()) {
        setAnimationSource(AnimationSource {kPauseAnimationId});
    }
}

void Creature::cutOneShot() {
    if (_oneShot) {
        _oneShot->remainingMilliseconds = 0.0f;
    }
}

void Creature::flushFireForgetQueue() {
    _fireForgetQueue.clear();
}

// The dead pose is a loop, though it is shown held on its last frame. The
// class-0 ready pose and the flinch pose are not looping rows, though they
// show the pause clip.
bool Creature::currentClipLoops() const {
    if (_movementType != MovementType::None || isPlayingOneShotAnimation()) {
        return false;
    }
    if (showsClassZeroReady() || showsDamageFlinchPose()) {
        return false;
    }
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    const auto base = model->baseAnimationChannel();
    if (!base) {
        return false;
    }
    if (liesDead() && boost::iequals(base->get().anim->name(), getDeadAnimation())) {
        return true;
    }
    return (base->get().properties.flags & AnimationFlags::loop) != 0;
}

int Creature::maxHitPoints() const {
    const auto hasVitalityFeat = [this](FeatType feat) { return hasEffectiveFeat(feat); };
    // The player character's level history holds while no companion is
    // controlled in its place.
    return _isPC == 1 && _game.party().controlledNpc() == -1
               ? _attributes.getLevelHistoryMaxHitPoints(
                     [this](int level) {
                         // A level without a recorded entry grants no hit die.
                         return static_cast<size_t>(level) < _levelStats.size() ? _levelStats[level].hitDie : 0;
                     },
                     _vitalityConstitutionModifier, hasVitalityFeat)
               : _attributes.getMaxHitPoints(_hitPoints, _vitalityConstitutionModifier, hasVitalityFeat);
}

void Creature::refreshVitalityConstitution() {
    // A creature without levels has no Constitution term.
    _vitalityConstitutionModifier = _attributes.getAggregateLevel() > 0
                                        ? getEffectiveAbilityModifier(Ability::Constitution)
                                        : 0;
}

bool Creature::hitPointsAtDeathThreshold() const {
    // A party member is never dead by its vitality alone; a player character
    // dies at -10, any other creature at 0.
    return !_game.party().isMember(*this) && currentHitPoints() <= (actsAsPC() ? -10 : 0);
}

void Creature::updateDeathFromCurrentHitPoints() {
    if (_minOneHP && currentHitPoints() < 1) {
        _currentHitPoints = 1;
    }
    refreshDeath();
}

void Creature::refreshDeath() {
    _dead = hitPointsAtDeathThreshold();
}

void Creature::followMaximumHitPoints(int previousMaximum) {
    Object::setCurrentHitPoints(currentHitPointsWithoutTemporary() + maxHitPoints() - previousMaximum);
}

std::optional<int> Creature::abilityPoolMaximum(const EffectInstance &effect) const {
    if (effect.type() != EffectType::AbilityIncrease && effect.type() != EffectType::AbilityDecrease) {
        return std::nullopt;
    }
    const int ability = effect.integerParameter(0, -1);
    if (ability == static_cast<int>(Ability::Constitution)) {
        return maxHitPoints();
    }
    if (ability == static_cast<int>(Ability::Wisdom) || ability == static_cast<int>(Ability::Charisma)) {
        return maxForcePoints();
    }
    return std::nullopt;
}

void Creature::followAbilityPool(const EffectInstance &effect, int previousMaximum) {
    if (effect.integerParameter(0, -1) != static_cast<int>(Ability::Constitution)) {
        // Wisdom and Charisma move current Force points with their maximum.
        _currentForce += maxForcePoints() - previousMaximum;
        return;
    }
    // A party member at zero or less is dying rather than dead.
    const bool partyMember = _game.party().isMember(*this);
    const bool deadBefore = !partyMember && hitPointsAtDeathThreshold();
    const bool dyingBefore = isTemporarilyDead();
    refreshVitalityConstitution();
    followMaximumHitPoints(previousMaximum);
    const bool killed = isTemporarilyDead()
                            ? !dyingBefore
                            : !partyMember && !deadBefore && hitPointsAtDeathThreshold();
    if (!killed) {
        return;
    }
    auto death = std::make_shared<DeathEffect>(false, true, false);
    auto instance = death->saveFacingInstance();
    instance.effect = std::move(death);
    instance.setDuration(DurationType::Instant, 0.0f);
    instance.creatorId = effect.creatorId;
    instance.creator = effect.creator;
    _game.queueEffectApplication(*this, std::move(instance));
}

void Creature::adjustBaseAbilityScore(Ability ability, int amount) {
    const auto setBase = [&]() {
        _attributes.setAbilityScore(ability, _attributes.getAbilityScore(ability) + amount);
    };
    if (ability != Ability::Constitution) {
        setBase();
        return;
    }
    const int previousMaximum = maxHitPoints();
    setBase();
    refreshVitalityConstitution();
    followMaximumHitPoints(previousMaximum);
}

void Creature::restoreSerializedVitality() {
    // Current vitality and Force points are serialized relative to their
    // maxima (current - maximum + base). Rebuild them once attributes,
    // levels and feats are known and before equipment and effects apply.
    refreshVitalityConstitution();
    const int savedHitPoints = currentHitPointsWithoutTemporary();
    const int savedForce = _currentForce;
    if (_isPC) {
        Object::setCurrentHitPoints(savedHitPoints + maxHitPoints() - _hitPoints);
        _currentForce = savedForce + maxForcePoints() - _forcePoints;
    } else {
        // Other creatures take back only the ability terms.
        const int level = _attributes.getAggregateLevel();
        Object::setCurrentHitPoints(rebuildSavedPool(
            savedHitPoints, _hitPoints, _vitalityConstitutionModifier, level));
        int forceModifier = 0;
        if (level > 0) {
            forceModifier = getEffectiveAbilityModifier(Ability::Wisdom);
            if (!_game.isTSL()) {
                forceModifier += getEffectiveAbilityModifier(Ability::Charisma);
            }
        }
        _currentForce = rebuildSavedPool(savedForce, _forcePoints, forceModifier, level);
    }
    updateDeathFromCurrentHitPoints();
}

int Creature::serializedCurrentHitPoints() const {
    return currentHitPointsWithoutTemporary() - maxHitPoints() + _hitPoints;
}

int Creature::serializedCurrentForce() const {
    return _currentForce - maxForcePoints() + _forcePoints;
}

void Creature::recalculatePermanentVitality() {
    const int previousMaximum = maxHitPoints();
    refreshVitalityConstitution();
    followMaximumHitPoints(previousMaximum);
    updateDeathFromCurrentHitPoints();
}

void Creature::setMaxHitPoints(int baseHitPoints) {
    const int previousMaximum = maxHitPoints();
    const int oldCurrent = _currentHitPoints;
    _hitPoints = baseHitPoints;
    refreshVitalityConstitution();
    if (oldCurrent > 0) {
        _currentHitPoints = oldCurrent + maxHitPoints() - previousMaximum;
    }
    updateDeathFromCurrentHitPoints();
}

int Creature::getUnopposedSkillRank(SkillType skill, bool baseOnly) const {
    return skillRank(skill, baseOnly, std::nullopt);
}

int Creature::getSkillRankVersus(SkillType skill, const Creature &versus) const {
    return skillRank(skill, false, std::cref(versus));
}

int Creature::skillRank(SkillType skill, bool baseOnly, const OptionalCreature &versus) const {
    // A skill the skills table has no row for, as a script may ask, has no rank.
    const auto rules = _services.game.skills.get(skill);
    if (!rules) {
        return 0;
    }
    const int row = static_cast<int>(skill);
    int rank = _attributes.getSkillRank(skill);
    if (baseOnly) {
        return rank;
    }
    if (_game.isTSL() && skill == SkillType::Stealth && rank == 0 &&
        _attributes.hasSpell(SpellType::ForceCamouflage))
        rank = 1;
    if (rank == 0 && !rules->untrained) {
        return 0;
    }

    // Increases and decreases each count the strongest amount of every item
    // or spell and every other effect on its own, up to 90 points.
    struct Bucket {
        EffectSourceKey source;
        int amount;
    };
    std::vector<Bucket> increases;
    std::vector<Bucket> decreases;
    auto add = [](auto &buckets, EffectSourceKey source, int amount) {
        if (source.kind != EffectSourceKind::Independent) {
            for (auto &bucket : buckets) {
                if (bucket.source.kind == source.kind && bucket.source.value == source.value) {
                    bucket.amount = std::max(bucket.amount, amount);
                    return;
                }
            }
        }
        buckets.push_back({source, amount});
    };
    for (const auto &effect : effects()) {
        if (!effect.hasLiveRuntimeSource()) {
            continue;
        }
        if (effect.type() != EffectType::SkillIncrease && effect.type() != EffectType::SkillDecrease) {
            continue;
        }
        const int selectedSkill = effect.integerParameter(0);
        if (selectedSkill != 255 && selectedSkill != row) {
            continue;
        }
        if (versus) {
            if (!effect.appliesVersus(versus)) {
                continue;
            }
        } else if (effect.integerParameter(2) != static_cast<int>(RacialType::All) ||
                   effect.integerParameter(4) != 0) {
            // An unopposed query has no versus object.
            continue;
        }
        add(effect.type() == EffectType::SkillIncrease ? increases : decreases,
            getEffectSourceKey(effect), effect.integerParameter(1));
    }
    auto total = [](const auto &buckets) {
        int value = 0;
        for (const auto &bucket : buckets) {
            value += bucket.amount;
        }
        return std::min(value, 90);
    };
    rank += total(increases) - total(decreases);

    const Ability ability = rules->keyAbility;
    rank += getEffectiveAbilityModifier(ability);
    if ((ability == Ability::Strength || ability == Ability::Dexterity) &&
        hasEffect(EffectType::Blindness))
        rank -= 4;

    switch (skill) {
    case SkillType::ComputerUse:
    case SkillType::Repair:
    case SkillType::Security:
        if (hasEffectiveFeat(FeatType::GearHeadMaster)) {
            rank += 3;
        } else if (hasEffectiveFeat(FeatType::GearHeadAdept)) {
            rank += 2;
        } else if (hasEffectiveFeat(FeatType::GearHead)) {
            ++rank;
        }
        break;
    case SkillType::Demolitions:
    case SkillType::Stealth:
        if (hasEffectiveFeat(FeatType::MasterCaution)) {
            rank += 3;
        } else if (hasEffectiveFeat(FeatType::ImprovedCaution)) {
            rank += 2;
        } else if (hasEffectiveFeat(FeatType::Cautious)) {
            ++rank;
        }
        break;
    case SkillType::Awareness:
    case SkillType::Persuade:
    case SkillType::TreatInjury:
        if (hasEffectiveFeat(FeatType::MasterEmpathy)) {
            rank += 3;
        } else if (hasEffectiveFeat(FeatType::ImprovedEmpathy)) {
            rank += 2;
        } else if (hasEffectiveFeat(FeatType::Empathy)) {
            ++rank;
        }
        break;
    default:
        break;
    }
    if (_game.isTSL() && skill == SkillType::Stealth) {
        if (_attributes.hasSpell(SpellType::MasterForceCamouflage)) {
            rank += 8;
        } else if (_attributes.hasSpell(SpellType::ImprovedForceCamouflage)) {
            rank += 4;
        }
    }
    return rank;
}

void Creature::runEndDialogScript() {
    _game.scriptRunner().run(_onEndDialogue, id());
}

void Creature::updateRegeneration(float dt) {
    if (!isPartyMember() || isDead() || isTemporarilyDead() || currentHitPoints() <= 0) {
        return;
    }
    if (_game.isTSL()) {
        updateHitPointRegeneration(dt);
        updateForcePointRegeneration(dt);
    } else {
        updateK1HitPointRegeneration(dt);
        updateK1ForcePointRegeneration(dt);
    }
}

void Creature::updateK1HitPointRegeneration(float dt) {
    const int maximum = maxHitPoints();
    const int row = !_combatState.active || _combatState.activationType == CombatActivation::Indirect
                        ? RegenerationRates::kOutOfCombatRow
                        : RegenerationRates::kInCombatRow;
    const float rate = _services.game.combatTables.regeneration().healthRegen[row];
    const float pointsPerSecond = std::max(
        0.0001f,
        static_cast<float>(maximum) * rate / 100.0f);

    _hitPointAccumulator += pointsPerSecond * dt;
    const float rounded = _hitPointAccumulator < 0.0f
                              ? std::ceil(_hitPointAccumulator)
                              : std::floor(_hitPointAccumulator);
    const int whole = static_cast<int>(rounded);
    _hitPointAccumulator -= whole;

    const int current = currentHitPointsWithoutTemporary();
    setCurrentHitPoints(std::min(maximum, current + whole));
}

void Creature::updateHitPointRegeneration(float dt) {
    if (maxHitPoints() == currentHitPointsWithoutTemporary() ||
        _game.party().isHealthRegenerationDisabled())
        return;
    const auto &rates = _services.game.combatTables.regeneration();
    const float timePerPoint = rates.timePerHitPoint;
    float rate = !_combatState.active || _combatState.activationType == CombatActivation::Indirect
                     ? rates.outOfCombatHitPointBase
                     : rates.inCombatHitPointBase;
    rate += getEffectiveAbilityModifier(Ability::Constitution) * rates.constitutionBonus;
    const auto skill = racialType() == RacialType::Droid ? SkillType::Repair : SkillType::TreatInjury;
    rate += getUnopposedSkillRank(skill) * rates.skillRankBonus;
    if (hasEffectiveFeat(FeatType::RegenerateVitalityPoints)) {
        rate += 0.5f;
    }
    for (const auto &effect : effects()) {
        // Each vitality regeneration modifier doubles the rate, whatever its amount.
        if (effect.type() == EffectType::VPRegenModifier) {
            rate += rate;
        }
    }
    const float change = dt * std::max(0.0001f, 1.0f / std::max(0.0001f, timePerPoint)) * rate;
    if (change != 0.0f) {
        _hitPointAccumulator += change;
        const int whole = static_cast<int>(std::floor(_hitPointAccumulator));
        _hitPointAccumulator -= whole;
        const int value = std::max(1, std::min(maxHitPoints(), currentHitPointsWithoutTemporary() + whole));
        setCurrentHitPoints(value);
    }
}

void Creature::updateK1ForcePointRegeneration(float dt) {
    const int maximum = maxForcePoints();
    if (maximum <= 0) {
        return;
    }

    const int row = !_combatState.active || _combatState.activationType == CombatActivation::Indirect
                        ? RegenerationRates::kOutOfCombatRow
                        : RegenerationRates::kInCombatRow;
    const float rate = _services.game.combatTables.regeneration().forceRegen[row];
    const int totalMaximum = maximum + _temporaryForcePoints;
    const float pointsPerSecond = std::max(
        0.0001f,
        static_cast<float>(totalMaximum) * rate / 100.0f);

    _forcePointAccumulator += pointsPerSecond * dt;
    const float rounded = _forcePointAccumulator < 0.0f
                              ? std::ceil(_forcePointAccumulator)
                              : std::floor(_forcePointAccumulator);
    const int whole = static_cast<int>(rounded);
    _forcePointAccumulator -= whole;

    const int current = _currentForce + _temporaryForcePoints;
    _currentForce = std::min(totalMaximum, current + whole);
}

void Creature::updateForcePointRegeneration(float dt) {
    const auto area = spatialArea();
    if (!area) {
        return;
    }
    const int maximum = maxForcePoints();
    if (maximum <= 0) {
        return;
    }
    const int totalMaximum = maximum + _temporaryForcePoints;
    const int roomRating = area->get().getRoomForceRating(position());
    const auto &rates = _services.game.combatTables.regeneration();
    const float time = rates.forcePointTime;
    float rate;
    // Niman needs a lightsaber in the right hand.
    const auto right = getEquippedItem(InventorySlots::rightWeapon);
    const bool lightsaber = right && right->isLightsaber();
    if (!_combatState.active || _combatState.activationType == CombatActivation::Indirect) {
        rate = rates.outOfCombatForcePointBase;
    } else if (_currentForm == CombatForm::SaberVINiman && lightsaber) {
        rate = rates.outOfCombatForcePointBase * 0.3f;
    } else if (_currentForm == CombatForm::ForceIIIAffinity) {
        rate = rates.outOfCombatForcePointBase * 0.6f;
    } else {
        rate = rates.inCombatForcePointBase;
    }
    rate += getEffectiveAbilityModifier(Ability::Wisdom) * rates.wisdomBonus;
    if (hasEffectiveFeat(FeatType::RegenerateForcePoints)) {
        rate += 0.25f;
    }
    for (const auto &effect : effects()) {
        if (effect.type() == EffectType::FPRegenModifier) {
            rate += static_cast<float>(effect.integerParameter(0)) / 100.0f;
        }
    }
    if (_currentForm == CombatForm::ForceIChannel) {
        rate += 0.5f;
    }
    const float roomChange = roomRating * rate / 100.0f;
    if (_goodEvil <= 40) {
        rate -= roomChange;
    } else if (_goodEvil >= 60) {
        rate += roomChange;
    }
    const float change = dt * (rate / std::max(0.0001f, time) * totalMaximum);
    if (change != 0.0f) {
        _forcePointAccumulator += change;
        const float rounded = _forcePointAccumulator < 0.0f
                                  ? std::ceil(_forcePointAccumulator)
                                  : std::floor(_forcePointAccumulator);
        const int whole = static_cast<int>(rounded);
        _forcePointAccumulator -= whole;
        const int current = _currentForce + _temporaryForcePoints;
        // The result is written to ordinary FP; the separate temporary pool is
        // not debited or refilled by this update.
        _currentForce = std::min(totalMaximum, std::max(0, current + whole));
    }
}

void Creature::setCurrentHitPoints(int hitPoints) {
    _currentHitPoints = hitPoints;
    updateDeathFromCurrentHitPoints();
}

void Creature::initializeGeneratedVitality() {
    _hitPoints = _attributes.getAggregateHitDie();
    // A generated character's levels record only their hit die: they grant
    // no Force points, and the base Force points start at zero. Force points
    // come from later level-ups.
    _levelStats.clear();
    for (const auto &[clazz, level] : _attributes.classLevels()) {
        for (int i = 0; i < level; ++i) {
            _levelStats.push_back({clazz->hitdie(), 0});
        }
    }
    refreshVitalityConstitution();
    _currentHitPoints = maxHitPoints();
    _forcePoints = 0;
    _currentForce = maxForcePoints();
    updateDeathFromCurrentHitPoints();
}

void Creature::applyLevelUp(CreatureAttributes attributes, CreatureClass &clazz) {
    const int oldMaximumForce = maxForcePoints();
    const int forceGain = clazz.forcedie();

    _levelStats.push_back({clazz.hitdie(), forceGain});
    _forcePoints += forceGain;

    attributes.addClassLevels(&clazz, 1);
    _attributes = std::move(attributes);

    setMaxHitPoints(hitPoints() + clazz.hitdie());
    setCurrentHitPoints(maxHitPoints());

    const int forceIncrease = maxForcePoints() - oldMaximumForce;
    _currentForce += forceIncrease;
    // A creature able to level up again chimes again.
    _levelUpChimeArmed = true;
}

void Creature::addClass(CreatureClass &clazz) {
    _attributes.addClassLevels(&clazz, 0);
    updatePureGoodEvilPowers();
}

void Creature::damage(
    int amount,
    const std::shared_ptr<Object> &damager) {
    if (!_dead && amount >= 0) {
        // Direct damage has no typed packet. Publish a complete observation
        // here; effect damage has already published one before retaliation.
        std::array<int, 15> amounts;
        amounts.fill(-1);
        if (amount != std::numeric_limits<int>::max()) {
            amounts.back() = amount;
        }
        setLastDamager(damager);
        setLastDamageAmounts(amounts);
    }
    applyHitPointDamage(amount, damager);
}

void Creature::applyHitPointDamage(
    int amount,
    const std::shared_ptr<Object> &damager,
    std::optional<DamageReaction> reaction) {
    if (_dead) {
        return;
    }

    if (amount < 0) {
        // Heal instead of damage.
        int previousHitPoints = _currentHitPoints;
        _currentHitPoints = std::min(maxHitPoints(), _currentHitPoints - amount);
        _game.floatingText().addHeal(*this, _currentHitPoints - previousHitPoints);
        return;
    }

    bool deathEffect = amount == std::numeric_limits<int>::max();
    // Attribution belongs to this application, even when a nested hit has
    // replaced the public last-damage observation.
    const uint32_t damagerId = damager ? damager->id() : script::kObjectInvalid;
    if (deathEffect) {
        consumeTemporaryHitPoints(_temporaryHitPoints);
        if (_dead) {
            return; // A removal callback may already have killed us.
        }
        _currentHitPoints = 0; // special case for Death effect
    } else {
        const int totalBefore = currentHitPoints();
        const int allowedDamage = isMinOneHP()
                                      ? std::min(amount, std::max(0, totalBefore - 1))
                                      : amount;
        const int remainingDamage = consumeTemporaryHitPoints(allowedDamage);
        if (_dead) {
            return;
        }
        _currentHitPoints -= remainingDamage;
        const int adjustedAmount = allowedDamage;
        if (amount > 0) {
            _game.floatingText().addDamage(
                *this, amount, adjustedAmount, damagerId);
        }
    }

    // The reaction comes after the vitality change and before the damaged script.
    if (reaction) {
        _game.combat().reactToDamage(*this, damager, *reaction);
    }
    runDamagedScript();

    if (_immortal || currentHitPoints() > 0) {
        return;
    }

    (void)applyDeathEffect(damager, false);
}

int Creature::getReputationToward(const Creature &target) const {
    const bool sourceParty = _game.party().isMember(*this);
    if (&target == this || (sourceParty && _game.party().isMember(target))) {
        return IReputes::kMaxReputation;
    }
    return target.getReputationFrom(sourceParty ? Faction::Player : faction());
}

int Creature::getReputationFrom(Faction sourceFaction) const {
    if (_game.party().isMember(*this)) {
        return _services.game.reputes.getReputation(Faction::Player, sourceFaction);
    }
    return _services.game.reputes.getReputation(sourceFaction, faction());
}

void Creature::joinEncounter(const Encounter &encounter) {
    _encounterId = encounter.id();
    _actsAsPC = 0;
}

void Creature::leaveEncounter() {
    if (auto encounter = _game.getObjectById<Encounter>(_encounterId)) {
        encounter->removeSpawnedCreature(_challengeRating);
    }
    _leftEncounter = true;
}

// An area of effect whose object is not in the area is placed there anew and
// the effect takes the new object.
void Creature::placeCarriedAreasOfEffect(Area &area) {
    auto self = _game.getObjectById<Creature>(_id);
    for (auto &effect : _effects) {
        if (effect.type() != EffectType::AreaOfEffect) {
            continue;
        }
        auto carried = effect.boundObjectParameter(0);
        if (carried && area.isObjectResident(*carried)) {
            continue;
        }
        auto areaOfEffect = area.spawnAreaOfEffect(effect, _position, objectFacingFromScript(0.0f), self);
        effect.objectParameters[0] = areaOfEffect->id();
        effect.objectParameterObjects[0] = std::static_pointer_cast<Object>(areaOfEffect);
    }
}

void Creature::setOrientationLock(uint32_t objectId, bool force) {
    if (!force && (isDead() || isTemporarilyDead())) {
        return;
    }
    if (objectId == script::kObjectInvalid && _orientationLock != script::kObjectInvalid && _orientationLock != _id) {
        auto target = _game.getObjectById(_orientationLock);
        if (target && target->type() != ObjectType::Module && target->type() != ObjectType::Area) {
            turnToward(*target);
        }
    }
    _orientationLock = objectId;
}

void Creature::dropOrientationLock() {
    if (isDead() || isTemporarilyDead()) {
        return;
    }
    _orientationLock = script::kObjectInvalid;
}

void Creature::setIsInConversation(bool isInConversation) {
    const bool leaving = this->isInConversation() && !isInConversation;
    Object::setIsInConversation(isInConversation);
    if (leaving) {
        setOrientationLock(script::kObjectInvalid);
    }
}

// Facings are yaws within (-pi, pi].
static float wrapFacing(float facing) {
    facing = std::remainder(facing, glm::two_pi<float>());
    return facing <= -glm::pi<float>() ? facing + glm::two_pi<float>() : facing;
}

static float facingToward(const glm::vec3 &from, const glm::vec3 &to) {
    return -std::atan2(to.x - from.x, to.y - from.y);
}

void Creature::setFacing(float facing) {
    _presentationFacing = _desiredFacing = _movementFacing = wrapFacing(facing);
    Object::setFacing(facing);
}

glm::quat Creature::presentedOrientation() const {
    return glm::quat(glm::vec3(0.0f, 0.0f, _presentationFacing));
}

void Creature::turnTo(float facing) {
    _orientation = glm::quat(glm::vec3(0.0f, 0.0f, facing));
    _desiredFacing = _movementFacing = wrapFacing(facing);
    rotateStep(_desiredFacing);
}

void Creature::turnToward(const glm::vec3 &point) {
    if (point == _position) {
        return;
    }
    turnTo(facingToward(_position, point));
}

void Creature::turnToward(const Object &object) {
    if (object.id() != _id) {
        turnToward(object.position());
    }
}

void Creature::turnAwayFrom(const Object &object) {
    if (object.id() == _id || object.position() == _position) {
        return;
    }
    turnTo(facingToward(object.position(), _position));
}

void Creature::turnAlongStep(const glm::vec2 &step) {
    if (_movementType != MovementType::None) {
        rotateStep(_movementFacing);
    }
    turnTo(-std::atan2(step.x, step.y));
}

void Creature::setDesiredFacing(float facing) {
    _desiredFacing = wrapFacing(facing);
}

void Creature::setDesiredFacingToward(const glm::vec3 &point) {
    const glm::vec3 offset {point - _position};
    if (glm::dot(offset, offset) > 1e-5f) {
        setDesiredFacing(facingToward(_position, point));
    }
}

Creature::AnimationUpdater Creature::animationUpdater() const {
    auto &party = _game.party();
    const bool leader = party.getLeader().get() == this;
    // In a conversation the leader is driven only when it neither speaks nor
    // listens; in TSL not at all. Nor is it driven after an engaged swing.
    if (leader && !_engagedExchange && !hasOrdinaryActionsPending() && !isPlayingDialogAnimation() &&
        !(_game.isTSL() ? _game.isConversationActive() : _game.isConversationSpeakerOrListener(*this))) {
        return AnimationUpdater::Drive;
    }
    if (!leader && party.isMember(*this) && currentSerializedActionId() == kFollowLeaderActionId) {
        return AnimationUpdater::Follow;
    }
    return AnimationUpdater::Default;
}

int Creature::followSlot() const {
    auto &party = _game.party();
    for (int slot = 1; slot < kPartyFollowSlots; ++slot) {
        if (party.getMember(slot).get() == this) {
            return slot;
        }
    }
    return -1;
}

// Each frame the model turns. A following party member turns toward the
// facing its follow slot keeps. Anyone else standing still turns toward the
// facing it wants, which a lock points at the locked object, and moving
// other than by an action toward its movement. The controlled leader acting
// freely turns only as its facing is set.
void Creature::updateTurning() {
    switch (animationUpdater()) {
    case AnimationUpdater::Drive:
        return;
    case AnimationUpdater::Follow: {
        const int slot = followSlot();
        if (slot != -1) {
            const glm::vec3 forward {_game.party().followerFacing(slot)};
            rotateStep(-std::atan2(forward.x, forward.y));
        }
        return;
    }
    case AnimationUpdater::Default:
        break;
    }
    if (_movementType != MovementType::None && hasOrdinaryActionsPending()) {
        return;
    }
    if (_orientationLock != script::kObjectInvalid) {
        if (auto target = _game.getObjectById(_orientationLock)) {
            setDesiredFacingToward(target->position());
        }
    }
    rotateStep(_movementType == MovementType::None ? _desiredFacing : _movementFacing);
}

// In combat, with no move ordered, a creature trying to attack or cast at an
// object it regards as hostile turns its facing at once.
bool Creature::isHostileCombatTurn() const {
    if (!_combatState.active) {
        return false;
    }
    if (std::any_of(_actions.nodes.begin(), _actions.nodes.end(),
                    [](const OrdinaryActionQueue::Node &node) { return node->actionId == kMoveToPointActionId; })) {
        return false;
    }
    auto hostile = [this](const std::shared_ptr<Object> &target) {
        if (!target) {
            return false;
        }
        if (auto *creature = dyn_cast<Creature>(target.get())) {
            return getReputationToward(*creature) <= 10;
        }
        return getObjectReputation(*this, *target, _game) <= 10;
    };
    return hostile(_combatState.attemptedAttackTarget.resolve()) || hostile(_combatState.attemptedSpellTarget.resolve());
}

// One frame's turn of the model toward a facing. A dead creature, or one
// held by a state, does not turn. In hostile combat the facing itself turns,
// by up to 900 degrees a millisecond, and the model with it. Otherwise the
// model turns at 2.2 to 4 radians a second, faster the farther off it is
// (reaching full speed sooner running), and settles once within a step.
void Creature::rotateStep(float target) {
    static constexpr float kCombatTurnSpeed = 900.0f; // degrees per millisecond
    if (isDead() || isDebilitated()) {
        return;
    }
    target = wrapFacing(target);
    const float milliseconds = static_cast<float>(_frameMilliseconds);
    if (isHostileCombatTurn()) {
        const float cap = kCombatTurnSpeed * milliseconds;
        const float turn = glm::clamp(glm::degrees(wrapFacing(target - getFacing())), -cap, cap);
        const float facing = wrapFacing(getFacing() + glm::radians(turn));
        _orientation = glm::quat(glm::vec3(0.0f, 0.0f, facing));
        _presentationFacing = _desiredFacing = _movementFacing = facing;
        updateTransform();
        return;
    }
    const float theta = wrapFacing(target - _presentationFacing);
    const float dot = std::cos(theta);
    const float sign = theta >= 0.0f ? 1.0f : -1.0f;
    // The turn animation compares the yaws as they are, without wrapping.
    if (dot < 1.0f && std::abs(glm::degrees(_presentationFacing) - glm::degrees(target)) > 15.0f) {
        playTurnAnimation(sign);
    }
    float speed = (1.0f - dot) * 3.0f;
    if (currentAnimationId() == kRunAnimationId) {
        speed += speed;
    }
    speed = speed <= 0.55f ? 0.55f : std::min(speed, 1.0f);
    const float step = milliseconds * speed * 4000.0f / 1000000.0f;
    const float facing = static_cast<double>(dot) >= std::cos(static_cast<double>(step))
                             ? target
                             : wrapFacing(_presentationFacing + sign * step);
    if (facing == _presentationFacing) {
        return;
    }
    _presentationFacing = facing;
    updateTransform();
}

// Standing in a pause, ready, listen or talk pose, a creature turning plays
// a turn to its left or right, cutting a head-turn fidget first. Only models
// with the character animations turn so from poses other than ready.
void Creature::playTurnAnimation(float sign) {
    auto standing = [](int id) {
        return id == kPauseAnimationId || id == 10030 || (id >= 10038 && id <= 10042) || id == 10154 || id == 10155;
    };
    int current = currentAnimationId();
    const bool headTurn = current == kHeadTurnLeftAnimationId || current == kHeadTurnRightAnimationId;
    if (!headTurn && current != kReadyAnimationId && !standing(current)) {
        return;
    }
    if (headTurn) {
        abortFireForgetAnimation();
        current = currentAnimationId();
    }
    if (current != kReadyAnimationId && (!standing(current) || _modelType == ModelType::Creature)) {
        return;
    }
    if (sign < 0.0f) {
        addFireForgetAnimation("turnright", false, AnimationSource {kTurnRightAnimationId});
    } else {
        addFireForgetAnimation("turnleft", false, AnimationSource {kTurnLeftAnimationId});
    }
}

void Creature::removeEffectsOnDeath() {
    const auto &keptTypes = _services.game.combatTables.deathKeptEffectTypes();
    std::vector<uint64_t> refused;
    for (;;) {
        auto found = std::find_if(_effects.begin(), _effects.end(), [&](const EffectInstance &record) {
            return std::find(refused.begin(), refused.end(), record.applicationOrder) == refused.end() &&
                   !isEffectPreservedOnDeath(record, keptTypes, _game.isTSL());
        });
        if (found == _effects.end()) {
            break;
        }
        const auto id = found->id;
        const auto order = found->applicationOrder;
        removeEffectsById(id); // package removal can also remove exempt siblings
        if (findEffectApplication(order)) {
            refused.push_back(order);
        }
    }
}

void Creature::onDestroyabilityChanged() {
    if (_destroyable && _dead && !_game.party().isMember(*this) &&
        _game.party().actualPlayer().get() != this)
        _game.postObjectDestruction(*this, 3.0f);
}

bool Creature::applyDeathEffect(const std::shared_ptr<Object> &damager,
                                bool noFadeAway, std::optional<std::reference_wrapper<const EffectInstance>> operation) {
    if (_dead || _immortal || plotFlag()) {
        return false;
    }
    if (_minOneHP) {
        _currentHitPoints = 1;
        return false;
    }
    setOrientationLock(script::kObjectInvalid, true);
    clearAllActions(true);
    // Death ends a running round without its end-of-round script.
    if (_game.combat().ownsRound(*this)) {
        _game.combat().endRound(*this, 0);
    }
    _game.combat().cancelActions(*this);
    _game.setLastPartyMemberTempKilled(*this);
    if (operation && shouldCheckDeathImmunity(operation->get().spellId, operation->get().category())) {
        auto creator = operation->get().boundCreator();
        if (hasEffectImmunity(ImmunityType::Death, asCreature(creator))) {
            addLinkImmunityFeedback(_game, creator, *this, ImmunityType::Death);
            return false;
        }
    }
    const bool partyDeath = _game.party().isMember(*this);
    // A standalone Death effect is a new observation; HP depletion is not.
    // Keep a newer nested hit intact while attributing death to this source.
    if (operation) {
        std::array<int, 15> amounts;
        amounts.fill(-1);
        setLastDamager(damager);
        setLastDamageAmounts(amounts);
    }
    setLastKiller(damager ? damager->id() : script::kObjectInvalid);
    // K2's third Death parameter suppresses both XP and fade-away. K1 has
    // no third-parameter XP gate.
    if (!_game.isTSL() || !noFadeAway) {
        applyDeathExperience(*this, damager, _name, _game, _services);
    }
    // A creature outside the party joins the neutral faction when it dies.
    if (!partyDeath) {
        setFaction(Faction::Neutral);
    }
    finishCombatRound();
    _combatState.active = false;
    _combatState.activationType = CombatActivation::None;
    setClientCombatMode(false);
    _combatState.expiryHeld = false;
    _combatState.attackTarget.reset();
    _combatState.roundTarget.reset();
    _combatState.attackAction = ActionType::QueueEmpty;
    _combatState.combatFeat = FeatType::Invalid;
    runDeathScript();
    // A party member is never dead: it lies downed, at zero vitality.
    _dead = !partyDeath;
    if (!_deathPose) {
        _deathPose = isInFallenLoop()       ? DeathPose::Dead3
                     : randomInt(0, 1) == 1 ? DeathPose::Dead1
                                            : DeathPose::Dead;
    }
    // The dead stop listening and lose every AI capability.
    _listening = false;
    _effectAIStateMask = kAIStateNone;
    _currentHitPoints = std::min(-11, static_cast<int>(_currentHitPoints));
    if (!partyDeath) {
        if (!_livingName) {
            _livingName = _name;
        }
        _name = _services.resource.strings.getText(kStrRefRemains);
    }
    // The death cry reaches the leader within 30 m of the same area.
    if (isHeardByLeader()) {
        playSound(SoundSetEntry::Dead);
    }
    // The dead pose leads in from the loop the creature stood in: it stops
    // first, and no queued clip or stored loop outlasts the death. Dying into
    // a die clip ends the running one-shot at once; the fall into the third
    // dead pose waits for it.
    setMovementType(MovementType::None);
    const int leftLoopId = _storedLoop ? _storedLoop->source.id : -1;
    if (_deathPose != DeathPose::Dead3) {
        _oneShot.reset();
    }
    flushFireForgetQueue();
    _storedLoop.reset();
    playDieTransition(leftLoopId);
    removeEffectsOnDeath();
    _currentHitPoints = std::min(-11, static_cast<int>(_currentHitPoints));
    _dead = !partyDeath;
    const auto leader = _game.party().getLeader();
    if (leader.get() == this) {
        _game.party().changeToNextLivingMember(true);
        // The controlled creature's death closes any popup it had open.
        _game.closeMessagePopup();
    }
    // A party member's death asks for its tutorial window while the party has company.
    if (partyDeath && _game.party().getSize() >= 2) {
        _game.requestTutorialWindow(kPartyMemberDiedTutorial);
    }
    if (partyDeath) {
        // Losing one companion pauses play only while someone still stands.
        for (int index = 0; index < _game.party().getSize(); ++index) {
            const auto member = _game.party().getMember(index);
            if (member && member->currentHitPoints() > 0) {
                _game.requestAutoPause(AutoPauseReason::PartyKilled);
                break;
            }
        }
    }
    // A fallen party member tells the module, whose player-death script runs;
    // a spectacular death of a destroyable creature outside the party shows
    // its visual.
    if (partyDeath) {
        _game.queueScriptEvent(*_game.module(), *this, Event(kPlayerDeathEvent));
    } else if (operation && operation->get().integerParameter(0) != 0 && (_destroyable || !_game.isTSL())) {
        applyEffect(_game.newEffect<VisualEffect>(kSpectacularDeathVisual, false, _services), DurationType::Instant);
        // A spectacular death leaves the creature's encounter at once.
        if (isEncounterCreature()) {
            leaveEncounter();
        }
    }
    // Destroyability is tested when the destruction is delivered. A death
    // leaves a destruction an earlier death queued pending.
    if (!partyDeath && !noFadeAway) {
        _game.postObjectDestruction(*this, _destroyObjectDelay);
    }
    return true;
}

void Creature::applyHealingEffect(int amount, const std::shared_ptr<Object> &creator, bool quiet) {
    if (isDead() || isTemporarilyDead()) {
        return;
    }
    const int current = currentHitPointsWithoutTemporary();
    const int maximum = maxHitPoints();
    const int sum = current + amount;
    if (sum > maximum) {
        amount = maximum - current;
    }
    _game.floatingText().addHeal(*this, amount);
    // The effect uses the direct HP setter; it is not a damage event.
    Object::setCurrentHitPoints(currentHitPointsWithoutTemporary() + amount);
    if (!_game.isTSL() || !quiet) {
        addHitPointHealingFeedback(_game, creator, *this, amount);
    }
    // Heal restarts this live, sorted walk after each temporary Wounding package.
    for (size_t index = 0; index < _effects.size();) {
        const auto record = _effects[index];
        if (record.serializedType > SavedEffectTypes::wounding) {
            break;
        }
        if (record.serializedType == SavedEffectTypes::wounding && record.durationType() == DurationType::Temporary) {
            if (!removeEffectsById(record.id)) {
                break; // A reentrant removal may own it already.
            }
            index = 0;
        } else {
            ++index;
        }
    }
}

bool Creature::applyResurrectionEffect(int hpPercent) {
    if (!_raiseable) {
        return false;
    }
    // Skip the write when the combined pool is already positive.
    // Writing that combined value to the ordinary pool would credit temp HP twice.
    if (currentHitPoints() <= 0) {
        Object::setCurrentHitPoints(getResurrectionHitPoints(
            _game.isTSL(), currentHitPoints(), maxHitPoints(), hpPercent));
    }
    // The raised choose a new dead pose when they next die. They get up from
    // the one they lay in as the pause takes over from its loop. A party
    // member is never dead.
    _dead = currentHitPoints() <= 0 && !_game.party().isMember(*this);
    if (currentHitPoints() > 0) {
        _deathPose.reset();
    }
    // A destruction its death queued still comes.
    clearAllActions(true);
    _game.combat().cancelActions(*this);
    finishCombatRound();
    _combatState.active = false;
    _combatState.activationType = CombatActivation::None;
    setClientCombatMode(false);
    _combatState.expiryHeld = false;
    _combatState.attackTarget.reset();
    _combatState.roundTarget.reset();
    _combatState.attackAction = ActionType::QueueEmpty;
    _combatState.combatFeat = FeatType::Invalid;
    // The raised listen again, take commands and regain their AI capabilities.
    _listening = true;
    _effectAIStateMask = kAIStateUnrestricted;
    setCommandable(true);
    resumeStateDrivenAnimation();
    if (!_dead && _livingName) {
        _name = *_livingName;
        _livingName.reset();
    }
    removeResurrectionEffects(_effects, [this](EffectId id) { removeEffectsById(id); });
    _animDirty = true;
    return true;
}

// The party always runs at the top priority; combat raises the lower
// priorities to 2, and leaving combat drops 2 to 1.
void Creature::updateAILevel() {
    if (_isPC == 1 || isPartyMember()) {
        _aiLevel = 4;
    } else if (_combatState.active && _aiLevel <= 1) {
        _aiLevel = 2;
    } else if (!_combatState.active && _aiLevel == 2) {
        _aiLevel = 1;
    }
}

void Creature::setScriptAILevel(int level) {
    _savedAILevel = _aiLevel;
    _aiLevel = level >= 0 && level <= 4 ? level : 3;
}

void Creature::resetScriptAILevel() {
    if (_savedAILevel >= 0 && _savedAILevel <= 4) {
        _aiLevel = _savedAILevel;
    }
}

bool Creature::throttlesPerceptionPass() {
    if (_aiLevel != 0) {
        return false;
    }
    if (_perceptionThrottle++ <= 148) {
        return true;
    }
    _perceptionThrottle = randomInt(0, 49);
    return false;
}

void Creature::updateCombat(float dt) {
    updateAILevel();
    if (_combatState.active) {
        // The expiry test precedes subtraction. A timer exhausted by this
        // update leaves combat on the next update unless activation refreshes it.
        if (_combatState.expiryTimer.elapsed()) {
            setCombatState(false, _combatState.activationType);
        } else if (!_combatState.expiryHeld) {
            _combatState.expiryTimer.update(dt);
        }
    }
    // A directly engaged combatant cannot stay hidden.
    if (_combatState.active && _combatState.activationType == CombatActivation::Direct) {
        setStealthMode(false);
    }
    // TSL combat decisions use a separate ScriptEndRound counter. Activation,
    // expiry holds, and individual round completion do not reset it.
    if (_game.isTSL() && _combatState.active) {
        if (_combatState.decisionScriptTime <= 0.0f) {
            if (!isDead()) {
                runEndRoundScript();
            }
            _combatState.decisionScriptTime = 3.0f;
        }
        _combatState.decisionScriptTime -= dt;
    }
    // In TSL a living creature in combat state keeps its hand items lit.
    if (_game.isTSL() && _combatState.active && currentHitPoints() > 0) {
        setLightsabersPowered(true, true);
    }
    _lightsaberIdlePowerDownTimer.update(dt);
    if (_lightsaberIdlePowerDownPending &&
        !_combatState.active &&
        _lightsaberIdlePowerDownTimer.elapsed()) {
        _lightsaberIdlePowerDownPending = false;
        setLightsabersPowered(false, true);
    }
}

void Creature::clearAllActions(bool force, bool evenUncommandable) {
    if (!isCommandable() && !evenUncommandable) {
        return;
    }
    Object::clearAllActions(force, true);
    setMovementType(MovementType::None);
    _useApproach.reset();
    _attemptedMovementTarget.reset();
    resetBlockingCreature();
    // Clearing the actions also ends a push or leap carrying the creature.
    endForcedMove();
}

void Creature::clearOrdersUnlessBusy() {
    if (queueState(_actions) <= kLastFreeQueueState) {
        clearAllActions(true);
    }
}

void Creature::teardownActions() {
    Object::teardownActions();
    setMovementType(MovementType::None);
    _useApproach.reset();
}

void Creature::playAnimation(AnimationType type, AnimationProperties properties) {
    playAnimation(type, std::move(properties), AnimationSource());
}

void Creature::playAnimation(AnimationType type, AnimationProperties properties, AnimationSource source) {
    if (isAnimationLooping(type)) {
        properties.flags |= AnimationFlags::loop;
    }

    std::string animName(getAnimationName(type));
    if (animName.empty()) {
        return;
    }

    playAnimation(animName, std::move(properties), source);
}

void Creature::playAnimation(const std::string &name, AnimationProperties properties, AnimationSource source) {
    // A world-space animation of an animated cutscene holds the creature.
    if (holdsCutsceneAnimation()) {
        return;
    }
    bool fireForget = !(properties.flags & AnimationFlags::loop);
    // A clip the model lacks shows nothing and leaves the current one running.
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (model && !model->model().getAnimation(name)) {
        return;
    }

    doPlayAnimation(fireForget, [&]() {
        if (!fireForget && !storeLoop(name, properties, false, source)) {
            return;
        }
        if (model) {
            model->playAnimation(name, nullptr, properties);
            presentEquippedItems(name);
        }
        setAnimationSource(source);
    });
}

bool Creature::doPlayAnimation(bool fireForget, const std::function<void()> &callback) {
    if (!_sceneNode || _movementType != MovementType::None) {
        return false;
    }

    callback();
    markAnimationChosen();

    if (fireForget) {
        // A clip played directly takes the place of a queued one-shot.
        _oneShot.reset();
        _animFireForget = true;
    } else {
        // A new loop replaces the injured pause.
        _injuredPause = false;
        _injuredIdle = false;
    }
    return true;
}

bool Creature::playAnimation(const std::shared_ptr<Animation> &anim, AnimationProperties properties) {
    // A world-space animation of an animated cutscene holds the creature.
    if (holdsCutsceneAnimation()) {
        return false;
    }
    bool fireForget = !(properties.flags & AnimationFlags::loop);

    return doPlayAnimation(fireForget, [&]() {
        if (!fireForget && !storeLoop(anim->name(), properties, false, AnimationSource())) {
            return;
        }
        auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
        if (model) {
            model->playAnimation(*anim, nullptr, properties);
            presentEquippedItems(anim->name());
        }
        setAnimationSource(AnimationSource());
    });
}

// A loop the conversation gives plays at once. A one-shot first makes the
// pause the loop, ends the running one-shot, empties the queue and starts at
// once; when it ends the creature returns to the pause.
bool Creature::playExternalAnimation(const std::shared_ptr<Animation> &anim, AnimationProperties properties,
                                     AnimationSource source) {
    // A world-space animation of an animated cutscene holds the creature.
    if (holdsCutsceneAnimation()) {
        return false;
    }
    const bool fireForget = !(properties.flags & AnimationFlags::loop);
    if (fireForget) {
        showDialogPause();
        abortFireForgetAnimation();
        flushFireForgetQueue();
    }
    properties.flags |= AnimationFlags::retargetRoot;
    bool started = doPlayAnimation(fireForget, [&]() {
        auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
        if (model) {
            // The one-shot played again starts over. The ended one is still
            // the model's clip when no pause went on in its place (the stored
            // loop is state-driven, or the pause names no clip the model has),
            // and the model would keep a clip it already runs going on.
            const auto base = model->baseAnimationChannel();
            if (fireForget && base && boost::iequals(base->get().anim->name(), anim->name())) {
                model->removeAnimation(anim->name());
            }
            model->playAnimation(*anim, nullptr, properties);
            presentEquippedItems(anim->name());
        }
        // A clip the conversation owns counts as a dialog animation.
        setAnimationSource(source);
    });
    if (started) {
        // A pending state-driven refresh must not install an idle or
        // locomotion channel over the clip before its first render update.
        _animDirty = false;
    }
    return started;
}

void Creature::playOverlayAnimation(AnimationType type) {
    std::string animName(getAnimationName(type));
    if (animName.empty()) {
        return;
    }
    playOverlayAnimation(animName);
}

void Creature::playOverlayAnimation(const std::string &clip) {
    // A world-space animation of an animated cutscene holds the creature.
    if (holdsCutsceneAnimation()) {
        return;
    }
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model) {
        return;
    }
    // Deliberately not routed through doPlayAnimation: that refuses to play
    // anything while the creature is moving, which is the one case an overlay
    // exists to cover. _animFireForget is left alone too, so
    // updateModelAnimation goes on driving locomotion underneath the layer,
    // later poses and locomotion replace only what lies beneath it, and the
    // layer erases itself once it has finished.
    model->playAnimation(
        clip,
        nullptr,
        AnimationProperties::fromFlags(
            AnimationFlags::overlay |
            AnimationFlags::layer |
            AnimationFlags::fireForget |
            AnimationFlags::propagate));
}

// Any clip replaces what the creature was last given.
void Creature::markAnimationChosen() {
    _damageFlinchHeld = false;
    _castAnimation.reset();
}

// The one-shot waits in the fire-and-forget queue, lasting its length at the
// speed given. The one-shot already shown is not queued again, and the
// animations that play as overlays go over the loop.
void Creature::playFireForgetAnimation(const std::string &clip, AnimationSource source, float speed) {
    if (source.id >= 0 && source.id == currentAnimationId()) {
        return;
    }
    addFireForgetAnimation(clip, playsAsOverlay(source.id, _game.isTSL()), source, speed);
}

// A dialog loop plays at once. A dialog one-shot makes the pause the loop
// first, then waits in the fire-and-forget queue.
void Creature::playDialogAnimation(AnimationType type, bool looping, AnimationSource source) {
    playDialogAnimation(getAnimationName(type), looping, source);
}

void Creature::playDialogAnimation(const std::string &clip, bool looping, AnimationSource source) {
    if (clip.empty()) {
        return;
    }
    if (looping) {
        playAnimation(clip, AnimationProperties::fromFlags(AnimationFlags::loop), source);
        return;
    }
    showDialogPause();
    addFireForgetAnimation(clip, playsAsOverlay(source.id, _game.isTSL()), source);
}

// The pause becomes the loop, unless it shows already.
void Creature::showDialogPause() {
    if (currentAnimationId() != kPauseAnimationId) {
        playScriptAnimation(getScriptAnimation(kPauseAnimationId), 1.0f);
    }
}

// While play is paused the draw waits. While the player drives the leader the
// draw plays as a layer over the movement, and waits on; otherwise it is
// queued for a second and a half at its natural speed.
void Creature::playWeaponDraw() {
    // A model without the draw clip draws nothing.
    const std::string clip = getAnimationName(CombatAnimation::Draw, getWieldType(), 1);
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model || clip.empty() || !model->model().getAnimation(clip)) {
        return;
    }
    if (_game.isPaused()) {
        _weaponDrawPending = true;
        return;
    }
    if (_game.module()->isPlayerMoving()) {
        playOverlayAnimation(getWeaponDrawOverlayAnimation());
        return;
    }
    addFireForgetAnimation(clip, kWeaponDrawMilliseconds, 1.0f, false, AnimationSource {kWeaponDrawAnimationId});
    _weaponDrawPending = false;
}

// The flourish is the draw of the weapon classes that have one, played as a
// layer while the player drives the leader.
void Creature::playWeaponFlourish() {
    if (_game.module()->isPlayerMoving()) {
        playOverlayAnimation(getWeaponDrawOverlayAnimation());
        return;
    }
    std::string clip;
    switch (getWieldType()) {
    case CreatureWieldType::SingleSword:
    case CreatureWieldType::DoubleBladedSword:
    case CreatureWieldType::DualSwords:
        clip = getAnimationName(CombatAnimation::Draw, getWieldType(), 1);
        break;
    case CreatureWieldType::BlasterPistol:
    case CreatureWieldType::DualPistols:
    case CreatureWieldType::BlasterRifle:
    case CreatureWieldType::HeavyWeapon:
        if (_game.isTSL()) {
            clip = getAnimationName(CombatAnimation::Draw, getWieldType(), 1);
        }
        break;
    default:
        break;
    }
    // A weapon class without a flourish, or a model without its clip, holds
    // the queue for as long and shows nothing.
    addFireForgetAnimation(clip, kWeaponDrawMilliseconds, 1.0f, false, AnimationSource {kWeaponFlourishAnimationId});
}

// A living creature that can flourish powers its lightsabers and enters combat
// state; alive or dead, the flourish then plays. In TSL a flourish waits for
// the weapon-switch cooldown and starts it, the player's is refused in
// restricted areas, and stun batons and empty hands do not flourish; in KotOR
// only the melee weapon classes flourish.
void Creature::flourishWeapons(bool playerCommand) {
    const bool tsl = _game.isTSL();
    if (tsl && isSwitchWeaponsCoolingDown()) {
        return;
    }
    if (!_dead && !isTemporarilyDead()) {
        if (tsl && playerCommand && _game.module()->area()->playerRestrictMode()) {
            return;
        }
        switch (getWieldType()) {
        case CreatureWieldType::StunBaton:
        case CreatureWieldType::HandToHand:
        case CreatureWieldType::HandToHandComplex:
            return;
        case CreatureWieldType::SingleSword:
        case CreatureWieldType::DoubleBladedSword:
        case CreatureWieldType::DualSwords:
            break;
        default:
            if (!tsl) {
                return;
            }
            break;
        }
        setLightsabersPowered(true, true);
        setCombatState(true, CombatActivation::Indirect, false);
    }
    playWeaponFlourish();
    if (tsl) {
        startSwitchWeaponsCooldown();
    }
}

// The melee weapon classes, and in TSL the ranged ones, draw as a layer.
std::string Creature::getWeaponDrawOverlayAnimation() const {
    if (_modelType == ModelType::Creature) {
        return "";
    }
    const CreatureWieldType wield = getWieldType();
    switch (wield) {
    case CreatureWieldType::SingleSword:
    case CreatureWieldType::DoubleBladedSword:
    case CreatureWieldType::DualSwords:
        return formatCombatAnimation("g%df%d", wield, 1);
    case CreatureWieldType::BlasterPistol:
    case CreatureWieldType::DualPistols:
    case CreatureWieldType::BlasterRifle:
    case CreatureWieldType::HeavyWeapon:
        return _game.isTSL() ? formatCombatAnimation("g%df%d", wield, 1) : "";
    default:
        return "";
    }
}

bool Creature::isPlayingOneShotAnimation() const {
    if (_oneShot) {
        return true;
    }
    // A clip played directly runs on the model it was played on.
    if (!_animFireForget) {
        return false;
    }
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    const auto base = model->baseAnimationChannel();
    return base && !base->get().finished;
}

void Creature::resumeStateDrivenAnimation() {
    // A world-space animation of an animated cutscene holds the creature.
    if (holdsCutsceneAnimation()) {
        return;
    }
    _oneShot.reset();
    _animFireForget = false;
    _animDirty = true;
    updateModelAnimation();
}

bool Creature::isInjured() const {
    return !_disableInjuredAnim &&
           static_cast<float>(currentHitPoints()) / static_cast<float>(maxHitPoints()) < 0.2f;
}

// Combat state always shows the ready loop, and the leader looking around in
// free-look never shows the injured pause. A running one-shot finishes first.
void Creature::showPauseReadyAnimation(bool pauseEnd) {
    _castAnimation.reset();
    _injuredPause = pauseEnd && !_combatState.active && isInjured() && !isFreeLookLeader(_game, *this);
    refreshPauseAnimation();
}

void Creature::showReadyAnimation() {
    _castAnimation.reset();
    _injuredPause = false;
    playAnimation(AnimationType::LoopingReady, AnimationProperties(), AnimationSource {kReadyAnimationId});
}

void Creature::refreshPauseAnimation() {
    _injuredIdle = false;
    _damageFlinchHeld = false;
    _animDirty = true;
    updateModelAnimation();
}

// A module transition saves and spawns the party again, which regenerates
// each Force shield's protection: its pool is full again. The party keeps its
// live effects here, so only the pool, the generated state that differs, is
// rebuilt.
void Creature::refillForceShieldPools() {
    if (std::none_of(_effects.begin(), _effects.end(),
                     [](const EffectInstance &record) { return record.serializedType == SavedEffectTypes::forceShield; }))
        return;
    const auto &tables = _services.game.combatTables;
    for (const auto &root : _effects) {
        if (root.serializedType != SavedEffectTypes::forceShield) {
            continue;
        }
        const auto shield = forceShieldDefinition(tables.forceShield(root.integerParameter(0)), _appearance);
        for (auto &child : _effects) {
            if (child.id == root.id && child.serializedType == SavedEffectTypes::damageResistance && child.skipOnLoad) {
                child.setIntegerParameter(2, shield.amount);
            }
        }
    }
}

void Creature::holdCastAnimation(std::string loop) {
    // An unchanged cast animation is not sent again.
    if (_castAnimation == loop) {
        return;
    }
    // It replaces the pose the creature was given, and a loop the idle.
    _injuredPause = false;
    _damageFlinchHeld = false;
    if (!loop.empty()) {
        _injuredIdle = false;
    }
    _castAnimation = std::move(loop);
    _animDirty = true;
    updateModelAnimation();
}

void Creature::releaseCastAnimation() {
    if (!_castAnimation) {
        return;
    }
    _castAnimation.reset();
    _animDirty = true;
}

// Every flinch row plays as a layer; a clip the model lacks shows nothing.
// Hands with no flinch row take the pose again each time, unless it still
// shows. The flinch replaces what the creature was last given.
void Creature::playDamageFlinch() {
    // A world-space animation of an animated cutscene holds the creature.
    if (holdsCutsceneAnimation()) {
        return;
    }
    const std::string name = getDamageFlinchAnimation();
    if (name.empty()) {
        if (!showsDamageFlinchPose()) {
            holdDamageFlinchPose();
        }
    } else {
        if (_damageFlinchHeld) {
            return;
        }
        addFireForgetAnimation(name, true);
    }
    _castAnimation.reset();
    _damageFlinchHeld = true;
}

// The flinch pose shows the pause clip at once, cutting the running one-shot,
// and neither loops nor runs out: the queue waits behind it, and it holds
// until another animation is chosen. The loop to return to and the queue stay.
// The time left of a cut queued one-shot stays too, without counting down, and
// a new loop then waits for the pose to be ended otherwise.
void Creature::holdDamageFlinchPose() {
    // A world-space animation of an animated cutscene holds the creature.
    if (holdsCutsceneAnimation()) {
        return;
    }
    static const std::string kPoseClip("pause1");
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model || !model->model().getAnimation(kPoseClip)) {
        return;
    }
    if (_oneShot) {
        _oneShot = RunningOneShot {kPoseClip, kDamageFlinchAnimationId, false, false, _oneShot->remainingMilliseconds};
    }
    _animFireForget = false;
    model->playAnimation(kPoseClip, nullptr,
                         AnimationProperties::fromFlags(AnimationFlags::loopBlend | AnimationFlags::propagate));
    presentEquippedItems(kPoseClip);
    setAnimationSource(AnimationSource {kDamageFlinchAnimationId});
}

bool Creature::showsDamageFlinchPose() const {
    return _animationSource.id == kDamageFlinchAnimationId;
}

bool Creature::showsPauseReadyAnimation() const {
    return _movementType == MovementType::None && !liesDead() && !_injuredPause && !_damageFlinchHeld && !_castAnimation &&
           !isPlayingOneShotAnimation() &&
           getActiveAnimationName() == getPauseAnimation(_injuredIdle && !isFreeLookLeader(_game, *this));
}

// The ready loop follows the creature's state. Any other animation ID shows
// the animations.2da row it names on the creature's kind of model, the rows
// below 10000 directly, or else its cut clip; a row that is not
// fire-and-forget plays as a loop. An ID with neither shows the pause row,
// except 10098, which shows nothing.
Creature::ScriptAnimation Creature::getScriptAnimation(int id) const {
    ScriptAnimation result;
    const bool characterModel = _modelType != ModelType::Creature;
    if (id == kReadyAnimationId) {
        result.clip = getAnimationName(AnimationType::LoopingReady);
        result.loop = true;
    } else {
        const int row = id < kPauseAnimationId ? id : scriptAnimationRow(id, characterModel);
        if (row >= 0) {
            result.clip = _services.game.animations.getNameById(static_cast<uint32_t>(row));
        }
        // An ID whose row names no clip may name a cut clip; one that names
        // neither shows the pause, except 10098.
        if (result.clip.empty()) {
            result.clip = cutClipName(id);
        }
        if (result.clip.empty() && id != kNoPauseFallbackAnimationId) {
            result.clip = _services.game.animations.getNameById(
                static_cast<uint32_t>(scriptAnimationRow(kPauseAnimationId, characterModel)));
        }
        result.loop = !isFireForgetCutClip(id) &&
                      (row < 0 || !_services.game.animations.isFireForgetById(static_cast<uint32_t>(row)));
    }
    if (id >= 0) {
        result.source = AnimationSource {id, _services.game.animations.isDialogAnimation(id)};
    }
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    auto anim = model && !result.clip.empty() ? model->model().getAnimation(result.clip) : nullptr;
    if (anim) {
        result.length = anim->length();
    }
    return result;
}

// A loop plays at once; a one-shot waits in the fire-and-forget queue.
void Creature::playScriptAnimation(const ScriptAnimation &animation, float speed) {
    if (animation.clip.empty()) {
        return;
    }
    if (!animation.loop) {
        markAnimationChosen();
        playFireForgetAnimation(animation.clip, animation.source, speed);
        return;
    }
    AnimationProperties properties;
    properties.speed = speed;
    properties.flags |= AnimationFlags::loop;
    playAnimation(animation.clip, std::move(properties), animation.source);
}

// A creature idling in its plain pause glances or fidgets now and then: first
// after one to two and a half minutes, then every fifteen to thirty seconds.
// The time goes on during a conversation, but no fidget starts. Models with
// the creature animations have fewer fidgets.
void Creature::updateFidget() {
    if (!_sceneNode) {
        return;
    }
    if (currentAnimationId() != kPauseAnimationId) {
        _fidgetTime = 0;
        return;
    }
    _fidgetTime += _frameMilliseconds;
    if (_fidgetTime < _fidgetDelay || _game.isConversationActive()) {
        return;
    }
    struct Fidget {
        const char *clip;
        int id;
    };
    static const std::array<Fidget, 7> kCharacterFidgets {{{"hturnl", kHeadTurnLeftAnimationId},
                                                           {"hturnr", kHeadTurnRightAnimationId},
                                                           {"pausesh", kPauseScratchHeadAnimationId},
                                                           {"pausebrd", kPauseBoredAnimationId},
                                                           {"pause2", kFidgetPauseAnimationId},
                                                           {"pause2", kFidgetPauseAnimationId},
                                                           {"pause2", kFidgetPauseAnimationId}}};
    static const std::array<Fidget, 5> kCreatureFidgets {{{"chturnl", kHeadTurnLeftAnimationId},
                                                          {"chturnr", kHeadTurnRightAnimationId},
                                                          {"cpause2", kFidgetPauseAnimationId},
                                                          {"cpause2", kFidgetPauseAnimationId},
                                                          {"cpause2", kFidgetPauseAnimationId}}};
    const Fidget &fidget = _modelType == ModelType::Creature
                               ? kCreatureFidgets[randomInt(0, static_cast<int>(kCreatureFidgets.size()) - 1)]
                               : kCharacterFidgets[randomInt(0, static_cast<int>(kCharacterFidgets.size()) - 1)];
    addFireForgetAnimation(fidget.clip, false, AnimationSource {fidget.id});
    _fidgetTime = 0;
    _fidgetDelay = 15000 + 5000 * randomInt(0, 3);
}

bool Creature::equip(const std::string &resRef) {
    std::shared_ptr<Item> item;
    if (isPresentationOnly()) {
        item = _game.newPresentationItem();
        item->loadFromBlueprint(resRef);
    } else {
        item = _game.newItemFromBlueprint(resRef);
    }

    bool equipped = false;

    if (item->isEquippable(InventorySlots::body)) {
        equipped = equip(InventorySlots::body, item);
    } else if (item->isEquippable(InventorySlots::rightWeapon)) {
        equipped = equip(InventorySlots::rightWeapon, item);
    }

    if (!equipped && item->isRuntimeLive()) {
        _game.destroyRuntimeObjectGraph(item);
    }

    return equipped;
}

// The creaturespeed.2da rows of a creature that cannot move and of one that
// moves as its appearance does.
static constexpr int kImmobileMovementRate = 1;
static constexpr int kDefaultMovementRate = 7;
// Every AI state but moving.
static constexpr int kImmobileAIStateMask = Creature::kAIStateUnrestricted & Creature::kAIStateMaskRooted;

void Creature::setMovementRate(int row) {
    if (row == kDefaultMovementRate) {
        row = _services.game.creatureSpeeds.find(_appearanceMoveRate);
    }
    _walkRate = row;
    const auto &speed = _services.game.creatureSpeeds.get(row);
    _walkSpeed = speed.walkRate;
    _runSpeed = speed.runRate;
    if (row == kImmobileMovementRate) {
        _effectAIStateMask = kImmobileAIStateMask;
    }
}

void Creature::applyDisguiseAppearance(int appearance, bool place) {
    if (_disguised) {
        return;
    }
    _appearanceBeforeDisguise = _appearance;
    _appearance = appearance;
    _disguised = true;
    loadAppearanceProperties();
    setMovementRate(kDefaultMovementRate);
    if (_sceneNode) {
        updateModel();
    }
    if (place) {
        standWhereAppearanceFits();
    }
}

void Creature::removeDisguiseAppearance() {
    if (!_disguised) {
        return;
    }
    _appearance = _appearanceBeforeDisguise;
    loadAppearanceProperties();
    setMovementRate(kDefaultMovementRate);
    if (_sceneNode) {
        updateModel();
    }
    _disguised = false;
    standWhereAppearanceFits();
}

// Handmaiden wears her own looks: one in her clothing, a darker one in it once
// she has turned to the dark side, and in any other armour the appearance
// whose models fit the player's armour.
static constexpr char kHandmaidenTag[] = "handmaiden";
static constexpr char kHandmaidenClothingVariation = 'b';
static constexpr int kHandmaidenAppearance = 454;
static constexpr int kHandmaidenDarkAppearance = 669;
static constexpr int kHandmaidenArmouredAppearance = 638;
static constexpr int kHandmaidenDarkAlignment = 40;
// Body variations past L use the L models.
static constexpr char kLastArmourModelVariation = 'l';

void Creature::updateArmourAppearance(const std::shared_ptr<Item> &armour) {
    if (!_game.isTSL()) {
        return;
    }
    char variation = 'a';
    if (armour && !armour->baseBodyVariation().empty()) {
        variation = std::min(armour->baseBodyVariation().front(), kLastArmourModelVariation);
    }
    if (armour && boost::iequals(_tag, kHandmaidenTag)) {
        if (variation == kHandmaidenClothingVariation) {
            const int appearance = _goodEvil > kHandmaidenDarkAlignment ? kHandmaidenAppearance : kHandmaidenDarkAppearance;
            if (_appearance != appearance) {
                changeAppearance(appearance);
            }
            return;
        }
        if (_appearance != kHandmaidenArmouredAppearance) {
            changeAppearance(kHandmaidenArmouredAppearance);
        }
    }
    // A variation before A has no model column.
    if (variation < 'a') {
        return;
    }
    const int appearance = _armourAppearances[variation - 'a'];
    if (appearance != 0 && appearance != _appearance) {
        changeAppearance(appearance);
    }
}

void Creature::changeAppearance(int appearance) {
    // A disguise holds the appearance as it is.
    if (_disguised) {
        return;
    }
    _appearance = appearance;
    loadAppearanceProperties();
    setMovementRate(kDefaultMovementRate);
    if (_sceneNode) {
        updateModel();
    }
    standWhereAppearanceFits();
}

// A creature whose appearance has just changed stands, if it can, where its
// new size fits within this distance.
static constexpr float kAppearanceChangePlacementRadius = 20.0f;

void Creature::standWhereAppearanceFits() {
    // Outside an area the creature stays where it is.
    const auto area = spatialArea();
    if (!area) {
        return;
    }
    if (auto spot = area->get().computeSafeLocation(_position, kAppearanceChangePlacementRadius, *this, true)) {
        setPosition(*spot);
        area->get().determineObjectRoom(*this);
    }
}

void Creature::updateDisguise() {
    // Presentation-only snapshots cannot admit effects. Live equipment instead
    // uses the same Disguise records as scripts, avoiding a second override.
    if (!isPresentationOnly()) {
        return;
    }
    int disguiseAppearance = -1;
    for (auto &[slot, item] : _equipment) {
        if (item->hasDisguise()) {
            disguiseAppearance = item->disguiseAppearance();
            break;
        }
    }
    if (disguiseAppearance >= 0) {
        if (!_disguised) {
            _appearanceBeforeDisguise = _appearance;
            _disguised = true;
        }
        _appearance = disguiseAppearance;
    } else if (_disguised) {
        _appearance = _appearanceBeforeDisguise;
        _disguised = false;
    }
}

bool Creature::canEquip(
    int slot,
    const std::shared_ptr<Item> &item) const {
    if (!item || (!item->isRuntimeLive() && !item->isPresentationOnly()) ||
        !item->isEquippable(getEquipabilitySlot(slot))) {
        return false;
    }
    if (std::find(_items.begin(), _items.end(), item) != _items.end()) {
        // Inventory -> equipment is a transfer. Callers must first remove or
        // split the candidate so one object never has two ownership edges.
        return false;
    }
    for (const auto &[equippedSlot, equipped] : _equipment) {
        if (equipped == item && equippedSlot != slot) {
            return false;
        }
    }
    auto previous = getEquippedItem(slot);
    if (previous != item && item->isHeld()) {
        return false;
    }
    if (previous && previous != item) {
        return false;
    }
    return true;
}

bool Creature::equip(int slot, const std::shared_ptr<Item> &item) {
    if (!canEquip(slot, item) || isActiveAreaOwnedItem(_game, item)) {
        return false;
    }
    if (getEquippedItem(slot) == item) {
        return true;
    }
    auto replacementEffects = effectsWithoutEquippedSource(std::nullopt);
    appendEquippedItemEffects(replacementEffects, slot, item);

    _equipment[slot] = item;
    item->setEquipped(true);
    item->setOwner(_id);
    replaceEffectState(std::move(replacementEffects));

    updateEquipmentPresentation();
    if (_sceneNode && _combatState.active &&
        (slot == InventorySlots::rightWeapon ||
         slot == InventorySlots::leftWeapon)) {
        setLightsabersPowered(true, true);
    }

    return true;
}

bool Creature::canReplaceEquipment(
    int slot,
    const std::shared_ptr<Item> &item,
    const Object &displacedReceiver) const {
    auto previous = getEquippedItem(slot);
    if (!previous || previous == item) {
        return canEquip(slot, item);
    }
    if (!item || (!item->isRuntimeLive() && !item->isPresentationOnly()) ||
        !item->isEquippable(getEquipabilitySlot(slot)) ||
        std::find(_items.begin(), _items.end(), item) != _items.end() ||
        item->isHeld()) {
        return false;
    }
    for (const auto &[equippedSlot, equipped] : _equipment) {
        if (equipped == item && equippedSlot != slot) {
            return false;
        }
    }
    if ((!previous->isRuntimeLive() && !previous->isPresentationOnly()) ||
        (!displacedReceiver.isRuntimeLive() &&
         !displacedReceiver.isPresentationOnly()) ||
        (isPresentationOnly() != displacedReceiver.isPresentationOnly())) {
        return false;
    }
    return true;
}

bool Creature::replaceEquipment(
    int slot,
    const std::shared_ptr<Item> &item,
    Object &displacedReceiver) {
    auto previous = getEquippedItem(slot);
    if (!previous || previous == item) {
        return equip(slot, item);
    }
    if (!canReplaceEquipment(slot, item, displacedReceiver) ||
        isActiveAreaOwnedItem(_game, item)) {
        return false;
    }
    auto replacementEffects = effectsWithoutEquippedSource(std::cref(*previous));
    appendEquippedItemEffects(replacementEffects, slot, item);

    // All ordinary recoverable validation is complete. The remainder is one
    // synchronous ownership move; only exceptional allocation failure remains.
    // A hand item held by a script stays as it is when it leaves the hand.
    if (!_handPowerHeld) {
        if (slot == InventorySlots::rightWeapon || slot == InventorySlots::leftWeapon) {
            updatePoweredHands(*previous, false);
        }
        previous->powerDown(_position, true);
    }
    previous->setEquipped(false);
    previous->clearOwner();
    _equipment[slot] = item;
    item->setEquipped(true);
    item->setOwner(_id);
    displacedReceiver.addItem(previous);
    replaceEffectState(std::move(replacementEffects));
    updateEquipmentPresentation();
    if (previous->itemType() == Item::kStealthUnitItemType && _stealthMode) {
        toggleStealth();
    }
    return true;
}

std::shared_ptr<Item> Creature::takeEquippedItem(
    const std::shared_ptr<Item> &item, bool updatePresentation) {
    auto equipped = std::find_if(
        _equipment.begin(), _equipment.end(),
        [&item](const auto &entry) { return entry.second == item; });
    if (equipped == _equipment.end()) {
        return nullptr;
    }

    auto result = equipped->second;
    auto replacementEffects = effectsWithoutEquippedSource(std::cref(*result));
    // A hand item held by a script stays as it is when it leaves the hand.
    if (!_handPowerHeld) {
        if (equipped->first == InventorySlots::rightWeapon || equipped->first == InventorySlots::leftWeapon) {
            updatePoweredHands(*result, false);
        }
        result->powerDown(_position, true);
    }
    result->setEquipped(false);
    result->clearOwner();
    _equipment.erase(equipped);
    replaceEffectState(std::move(replacementEffects));
    if (updatePresentation) {
        updateEquipmentPresentation();
    }
    // Taking off a stealth unit uses the skill again, as the creature now is.
    if (result->itemType() == Item::kStealthUnitItemType && _stealthMode) {
        toggleStealth();
    }
    return result;
}

bool Creature::moveEquippedItemTo(
    const std::shared_ptr<Item> &item,
    Object &receiver) {
    if (!item ||
        (!receiver.isRuntimeLive() && !receiver.isPresentationOnly()) ||
        (isPresentationOnly() != receiver.isPresentationOnly())) {
        return false;
    }
    auto taken = takeEquippedItem(item);
    if (!taken) {
        return false;
    }
    receiver.addItem(taken);
    return true;
}

void Creature::updateEquipmentPresentation() {
    int prevAppearance = _appearance;
    updateDisguise();
    if (_appearance != prevAppearance) {
        // Refresh appearance-derived state so the in-place model swap below picks
        // up the disguise (or restored) model; rebuilding the scene node here would
        // orphan it from the area scene graph.
        loadAppearanceProperties();
    }
    if (_sceneNode) {
        updateModel();
    }
}

// Only the first sixteen equipment slots leave items in a body bag.
static constexpr int kBodyBagEquipmentSlots = 16;

std::vector<std::shared_ptr<Item>> Creature::dropableItems() const {
    std::vector<std::shared_ptr<Item>> result;
    for (const auto &[slot, item] : _equipment) {
        if (slot < kBodyBagEquipmentSlots && item->isDropable()) {
            result.push_back(item);
        }
    }
    for (const auto &item : _items) {
        if (item->isDropable()) {
            result.push_back(item);
        }
    }
    return result;
}

void Creature::stripHandWeapons() {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    for (const auto *hand : {&g_rightHandNode, &g_leftHandNode}) {
        if (auto weapon = model->getAttachment(*hand)) {
            weapon->setEnabled(false);
        }
    }
    // Stripped weapons stay away whatever the body shows next.
    _equipmentHidden = false;
}

std::shared_ptr<Item> Creature::getEquippedItem(int slot) const {
    auto equipped = _equipment.find(slot);
    return equipped != _equipment.end() ? equipped->second : nullptr;
}

void Creature::setFaction(Faction faction) {
    if (faction == _faction) {
        return;
    }
    _faction = faction;
    _factionJoinOrder = _game.nextFactionJoinOrder();
}
void Creature::setMovementType(MovementType type) {
    if (_movementType == type) {
        return;
    }

    _movementType = type;
    _animDirty = true;
    // A walk or run ends the running one-shot, though a layer goes on over
    // it; the queue waits until the creature stands again.
    _oneShot.reset();
    _animFireForget = false;
    _damageFlinchHeld = false;
    _castAnimation.reset();
    if (type != MovementType::None) {
        _injuredPause = _injuredIdle = false;
    }
}

glm::vec3 Creature::getSelectablePosition() const {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model) {
        return _position;
    }
    if (liesDead()) {
        return model->getWorldCenterOfAABB();
    }
    auto headModel = static_cast<ModelSceneNode *>(model->getAttachment(g_headHookNode));
    if (headModel) {
        auto talkDummy = headModel->getNodeByName(g_talkDummyNode);
        return talkDummy ? talkDummy->origin() : headModel->getWorldCenterOfAABB();
    } else {
        auto talkDummy = model->getNodeByName(g_talkDummyNode);
        return talkDummy ? talkDummy->origin() : model->getWorldCenterOfAABB();
    }
}

void Creature::setAttemptedSpellTarget(uint32_t id) {
    _combatState.attemptedSpellTarget = _game.getObjectById(id);
}
float Creature::maxCleaveRange(std::optional<std::reference_wrapper<const Creature>> target) const {
    auto weapon = getEquippedItem(InventorySlots::rightWeapon);
    if (weapon && weapon->isRanged()) {
        return 22.0f;
    }
    return target ? _hitRadius + target->get()._hitRadius + 4.1f : 4.0f;
}

static int maxCharacterLevel(bool tsl) {
    return tsl ? 50 : 20;
}

// The experience table's row is the level; experience and the table's
// amounts compare unsigned.
bool Creature::isLevelUpPending() const {
    if (isDead() || isTemporarilyDead()) {
        return false;
    }
    const int level = _attributes.getAggregateLevel();
    return level < maxCharacterLevel(_game.isTSL()) &&
           static_cast<uint32_t>(_xp) >= _services.game.combatTables.experienceThreshold(level);
}

// Only a creature whose IsPC is exactly 1 walks the table; any other keeps
// its level.
int Creature::potentialLevel() const {
    const auto &tables = _services.game.combatTables;
    int level = getSpellLevel(false);
    if (_isPC != 1) {
        return level;
    }
    while (level < maxCharacterLevel(_game.isTSL()) &&
           static_cast<uint32_t>(_xp) >= tables.experienceThreshold(level)) {
        ++level;
    }
    return level;
}

std::optional<uint32_t> Creature::getNeededXP() const {
    const int level = _attributes.getAggregateLevel();
    if (level > maxCharacterLevel(_game.isTSL())) {
        return std::nullopt;
    }
    return _services.game.combatTables.experienceThreshold(level);
}

int Creature::experienceToNextLevel() const {
    const auto &tables = _services.game.combatTables;
    const auto xp = static_cast<uint32_t>(_xp);
    if (_game.isTSL()) {
        const int level = std::min(getSpellLevel(false), maxCharacterLevel(true));
        return static_cast<int>(tables.experienceThreshold(level) - xp);
    }
    for (int row = 0; row <= maxCharacterLevel(false); ++row) {
        const uint32_t threshold = tables.experienceThreshold(row);
        if (xp < threshold) {
            return static_cast<int>(threshold - xp);
        }
    }
    return static_cast<int>(0u - xp);
}

void Creature::runSpawnScript() {
    // The creation script fires at most once per creature rather than once
    // per area attachment. A party member carried through an ordinary module
    // transition is the same creature: the destination area takes it in
    // without recreating it, and its OnSpawn does not run a second time. The
    // flag is set whether or not a script was authored.
    if (_spawnScriptFired) {
        return;
    }
    _spawnScriptFired = true;
    // Just before its creation script, a balanced creature outside the party
    // records the player character's level and scales its hit points to it,
    // then heals to the new maximum. The scaled total is what gets saved, so a
    // restored creature is never scaled twice.
    if (isAutoBalanceEligible(_game.isTSL(), isPartyMember(), _autoBalanceContext.multiplierSet)) {
        _autoBalanceContext.playerLevelAtSpawn = _game.getGlobalNumber("G_PC_LEVEL");
        _autoBalancePlayerLevelAtSpawnSet = true;
        const AutoBalanceRow row = _services.game.combatTables.autoBalance(_autoBalanceContext.multiplierSet);
        const int factor = getAutoBalanceVitalityFactor(_autoBalanceContext.playerLevelAtSpawn, row.vitalityMultiplier);
        _hitPoints = static_cast<int16_t>(factor * hitPoints());
        setCurrentHitPoints(maxHitPoints());
    }
    if (!_onSpawn.empty()) {
        _game.scriptRunner().run(_onSpawn, _id);
    }
    // The heartbeat follows at once, whatever the throttle, and its interval
    // starts over. Outside TSL only a party member or an encounter's creature
    // gets it; any other creature counts as spawned straight after its
    // creation script, so its first due heartbeat only starts the interval.
    if (_game.isTSL() || isPartyMember() || isEncounterCreature()) {
        _heartbeatThrottle = 0;
        runHeartbeat();
        _heartbeatInterval = randomInt(kMinHeartbeatIntervalMilliseconds, kMaxHeartbeatIntervalMilliseconds);
        _stateSupportTimer.reset(_heartbeatInterval / 1000.0f);
    } else {
        startUnstampedHeartbeat();
    }
}

void Creature::runBlockedScript(uint32_t blockingDoorId) {
    // The creature keeps the door until its next blocked event; every run
    // asking for it, this one's continuations included, sees the latest.
    _lastBlockingDoorId = blockingDoorId;
    if (_onBlocked.empty()) {
        return;
    }
    _game.scriptRunner().run(_onBlocked, _id);
}

void Creature::refreshCombatDecisionTimer() {
    if (_game.isTSL()) {
        _combatState.decisionScriptTime = 3.0f;
    }
}

void Creature::runEndRoundScript() {
    if (!_onEndRound.empty()) {
        _game.scriptRunner().run(_onEndRound, _id);
    }
}

void Creature::runConversationScript() {
    // A creature with no conversation script, or "default", runs the fallback
    // one, which stays its own.
    if (_onDialogue.empty() || _onDialogue == "default") {
        _onDialogue = kFallbackConversationScript;
    }
    _game.scriptRunner().run(_onDialogue, _id);
}

// An addition that would lower the experience, or pass the most it holds,
// is refused. A total held past the most, read from a save, takes no
// positive addition.
void Creature::giveXP(int amount) {
    if (amount < 0) {
        return;
    }
    const auto &classes = _attributes.classLevels();
    int behind = 0;
    if (classes.size() >= 2) {
        int favored = race().favoredClass;
        if (favored == kNoFavoredClass) {
            const auto highest = std::max_element(classes.begin(), classes.end(),
                                                  [](const auto &a, const auto &b) { return a.second < b.second; });
            favored = static_cast<int>(highest->first->type());
        }
        int highestLevel = 0;
        for (const auto &[clazz, level] : classes) {
            if (static_cast<int>(clazz->type()) != favored) {
                highestLevel = std::max(highestLevel, level);
            }
        }
        for (const auto &[clazz, level] : classes) {
            if (static_cast<int>(clazz->type()) != favored && highestLevel - level >= 2) {
                ++behind;
            }
        }
    }
    behind = std::min(behind, 5);
    const auto kept = static_cast<int>(int64_t {amount} * (5 - behind) / 5);
    if (kept > 0 && (_xp < 0 || kept > kMaxExperience - _xp)) {
        return;
    }
    setXP(_xp + kept);
}

void Creature::setXP(int xp) {
    bool wasLevelUpPending = isLevelUpPending();
    _xp = xp;

    if (isLevelUpPending()) {
        _game.notifyLevelUpAvailable(*this, !wasLevelUpPending);
    }
}

void Creature::playSound(SoundSetEntry entry, bool positional) {
    if (!_soundSet) {
        return;
    }
    auto maybeSound = _soundSet->find(entry);
    if (maybeSound == _soundSet->end()) {
        return;
    }
    if (entry != SoundSetEntry::Dead && (isDead() || isTemporarilyDead())) {
        return;
    }
    std::optional<glm::vec3> position;
    if (positional) {
        position = _position;
    }
    // The creature speaks with one voice at a time.
    if (_audioSourceVoice) {
        _audioSourceVoice->stop();
    }
    _audioSourceVoice = _services.audio.mixer.play(
        maybeSound->second,
        AudioType::Sound,
        1.0f,
        false,
        std::move(position));
}

void Creature::runAttackedScript(uint32_t attackerId) {
    if (_onAttacked.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onAttacked,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastAttacker, Variable::ofObject(attackerId)}});
}

void Creature::runDamagedScript() {
    // HP has already changed, but the death lifecycle may not have run yet.
    if (_onDamaged.empty() || currentHitPoints() <= 0 ||
        _game.party().getLeader().get() == this) {
        return;
    }
    _game.scriptRunner().run(
        _onDamaged,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastAttacker, Variable::ofObject(getLastDamager())},
         {script::ArgKind::LastDamager, Variable::ofObject(getLastDamager())}});
}

void Creature::runDeathScript() {
    if (_onDeath.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onDeath,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastAttacker, Variable::ofObject(getLastDamager())},
         {script::ArgKind::LastDamager, Variable::ofObject(getLastDamager())}});
}

// The wield type is the weapon class of the hands, with empty hands under the
// complex unarmed feat shown as their own type.
CreatureWieldType Creature::getWieldType() const {
    const int weaponClass = getReadyWeaponClass();
    if (weaponClass == static_cast<int>(CreatureWieldType::HandToHand) &&
        hasEffectiveFeat(FeatType::ComplexUnarmedAnims)) {
        return CreatureWieldType::HandToHandComplex;
    }
    return static_cast<CreatureWieldType>(weaponClass);
}

bool Creature::hasAssassinateWeaponPresentation() const {
    // Use the live model's weapon class: a pistol, a pair of pistols or a
    // rifle. Heavy weapons are class 9 and do not qualify.
    if (!std::dynamic_pointer_cast<scene::ModelSceneNode>(_sceneNode)) {
        return false;
    }
    const int weaponClass = getReadyWeaponClass();
    return weaponClass >= static_cast<int>(CreatureWieldType::BlasterPistol) &&
           weaponClass <= static_cast<int>(CreatureWieldType::BlasterRifle);
}

// The head talks the line's lip sync once, as a layer over whatever the body
// shows; a line without lip sync moves no lips. A new line starts over.
void Creature::startTalking(const std::shared_ptr<LipAnimation> &animation) {
    if (_talking && _lipAnimation == animation) {
        return;
    }
    removeHeadTalk(false);
    _lipAnimation = animation;
    _talking = true;
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (model && _lipAnimation) {
        model->playAnimation(getHeadTalkAnimation(), _lipAnimation, AnimationProperties::fromFlags(kHeadTalkFlags));
    }
}

// The talking head blends out.
void Creature::stopTalking() {
    if (!_talking && !_lipAnimation) {
        return;
    }
    removeHeadTalk(true);
    _lipAnimation.reset();
    _talking = false;
}

void Creature::removeHeadTalk(bool fade) {
    auto body = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!body) {
        return;
    }
    const std::string clip = getHeadTalkAnimation();
    auto head = static_cast<ModelSceneNode *>(body->getAttachment(g_headHookNode));
    for (auto *model : {body.get(), head}) {
        if (!model) {
            continue;
        }
        if (fade) {
            model->fadeOutLayer(clip);
        } else {
            model->removeAnimation(clip);
        }
    }
}

void Creature::setObjectSeen(const std::shared_ptr<Object> &object, bool seen) {
    if (seen) {
        _perception.seen[object->id()] = object;
    } else {
        _perception.seen.erase(object->id());
    }
}

void Creature::setObjectHeard(const std::shared_ptr<Object> &object, bool heard) {
    if (heard) {
        _perception.heard[object->id()] = object;
    } else {
        _perception.heard.erase(object->id());
    }
}

void Creature::setObjectInvisible(const std::shared_ptr<Object> &object, bool invisible) {
    if (invisible) {
        _perception.invisible[object->id()] = object;
    } else {
        _perception.invisible.erase(object->id());
    }
}

void Creature::forgetPerceived(uint32_t id) {
    _perception.seen.erase(id);
    _perception.heard.erase(id);
    _perception.invisible.erase(id);
}

// Each change is its own notice: the perceived creature and exactly one kind
// of change stay on the creature for the perception routines.
void Creature::runOnNotice(const std::shared_ptr<Object> &object, PerceptionEvent event) {
    // A notice is an event to the creature, which runs its creation script
    // first if it has not yet.
    runSpawnScript();
    _savedReferences["LastPerceived"] = object;
    _lastPerception = event;
    if (_onNotice.empty()) {
        return;
    }
    _game.scriptRunner().run(_onNotice, _id);
}

// Blinded, or in darkness without Ultravision.
bool Creature::isBlind() const {
    return (_visibilityCounterBits & kBlindnessCounter) != 0 ||
           (_visibilityCounterBits & (kDarknessCounter | kUltravisionCounter)) == kDarknessCounter;
}

float Creature::spotRange() const {
    if (_combatState.active && _game.party().getLeader().get() != this) {
        return _services.game.spells.range(kCombatPerceptionRangeRow).primary.value_or(0.0f);
    }
    return _perception.sightRange;
}

float Creature::listenRange() const {
    if (_combatState.active && _game.party().getLeader().get() != this) {
        return _services.game.spells.range(kCombatPerceptionRangeRow).secondary.value_or(0.0f);
    }
    return _perception.hearingRange;
}

void Creature::setPerceptionRangeRow(int row) {
    _pendingPerceptionRangeRow = row;
    _pendingPerceptionRangeByAppearance = false;
}

// A creature's own default row defers to its appearance's row.
void Creature::resolvePerceptionRanges() {
    if (!_pendingPerceptionRangeRow) {
        return;
    }
    int row = *_pendingPerceptionRangeRow;
    const bool byAppearance = _pendingPerceptionRangeByAppearance;
    _pendingPerceptionRangeRow.reset();
    _pendingPerceptionRangeByAppearance = false;
    if (byAppearance) {
        row = _appearancePerceptionRange;
    }
    // A row outside the ranges table gives no ranges.
    const auto range = _services.game.spells.findRange(row);
    if (!range) {
        return;
    }
    _perception.sightRange = range->get().primary.value_or(0.0f);
    _perception.hearingRange = range->get().secondary.value_or(0.0f);
}

// Stealth rolls are rerolled only while stealthed; awareness rolls always.
void Creature::refreshPerceptionRolls() {
    if (_perceptionRolls.age < kPerceptionRollInterval) {
        return;
    }
    if (_stealthMode) {
        _perceptionRolls.hide = randomInt(11, 20);
        _perceptionRolls.moveSilently = randomInt(11, 20);
    }
    _perceptionRolls.spot = randomInt(1, 20);
    _perceptionRolls.listen = randomInt(1, 20);
    _perceptionRolls.age = 0.0f;
}

bool Creature::isStationaryForDetection() const {
    return _movementType == MovementType::None;
}

float Creature::maxAttackRange(const Object &target, bool choreographed, bool enemySearch) const {
    auto weapon = getEquippedItem(InventorySlots::rightWeapon);
    if (weapon && weapon->isRanged()) {
        if (enemySearch) {
            return kRangedEnemySearchRange;
        }
        float range = weapon->attackRange() == 0.0f ? 30.0f : weapon->attackRange();
        if (_game.isTSL()) {
            const int awareness = getUnopposedSkillRank(SkillType::Awareness);
            if (awareness > 0) {
                range += static_cast<float>(awareness);
            }
        }
        return range;
    }
    if (!_game.isTSL()) {
        return desiredAttackRange(target, choreographed) + 0.5f;
    }
    if (choreographed) {
        return kChoreographedAttackRange + 0.1f;
    }
    return desiredAttackRange(target) + 0.5f;
}

float Creature::desiredAttackRange(const Object &target, bool choreographed) const {
    if (auto *creature = dyn_cast<Creature>(&target)) {
        if (choreographed) {
            return _game.isTSL() ? kChoreographedAttackRange
                                 : kChoreographedAttackLead + _creaturePersonalSpace +
                                       creature->_creaturePersonalSpace + kChoreographedAttackTail;
        }
        return _hitRadius + 1.6f + creature->_hitRadius;
    }
    if (target.type() == ObjectType::Door) {
        return 2.0f * _personalSpace;
    }
    if (target.type() == ObjectType::Placeable) {
        return useRange(target).range;
    }
    return 1.5f;
}

bool Creature::isImmobile() const {
    return _runDistance < kImmobileRunSpeed;
}

// Spotting a creature: an unstealthed one within spot range is always seen; a
// stealthed one is seen when awareness and the spot roll beat its stealth and
// hide roll, with the running, combat, stillness, facing and distance terms.
bool Creature::detectsBySight(const Creature &target, bool invisible) const {
    if (isBlind() || invisible) {
        return false;
    }
    const float spot = spotRange();
    const glm::vec3 offset {target._position - _position};
    if (glm::dot(offset, offset) > spot * spot &&
        !(_game.isTSL() && target.forceAlwaysUpdate())) {
        return false;
    }
    if (hasVisibilityCounter(kTrueSeeingCounter)) {
        return true;
    }
    if (!target._stealthMode) {
        return true;
    }
    const int rank = target.getSkillRankVersus(SkillType::Stealth, *this);
    if (rank == 0) {
        return true;
    }

    const int hide = target._perceptionRolls.hide;
    const int aware = getSkillRankVersus(SkillType::Awareness, target);
    int total = aware + _perceptionRolls.spot - (rank + hide);
    const int run = _movementType == MovementType::Run ? -5 : 0;
    total += run;
    int facing = 0;
    auto leader = _game.party().getLeader();
    const glm::vec3 toTarget {target._position - _position};
    const float yaw = getFacing();
    const glm::vec3 forward {-std::sin(yaw), std::cos(yaw), 0.0f};
    const float cosine = glm::dot(glm::length(toTarget) > 0.0f ? glm::normalize(toTarget) : toTarget, forward);
    // An NPC is worse at spotting the controlled creature behind it: TSL
    // has a rear blind spot, KotOR a rear arc.
    if (leader.get() != this && leader.get() == &target) {
        if (_game.isTSL()) {
            const float angle = glm::degrees(std::acos(std::clamp(cosine, -1.0f, 1.0f)));
            if (angle >= std::max(0.0f, 180.0f - _blindSpot / 2.0f)) {
                facing = -10;
            }
        } else if (cosine <= -0.707f) {
            facing = -5;
        }
    }
    total += facing;
    const int combat = _combatState.active ? -10 : 0;
    total += combat;
    const int targetStill = target.isStationaryForDetection() ? 5 : 0;
    const int observerStill = isStationaryForDetection() ? 5 : 0;
    total += observerStill - targetStill;
    const float third = glm::length(toTarget) / 3.0f;
    const int distance = third > 2.0f ? static_cast<int>(third - 2.0f) : 0;
    // TSL counts distance in the spotter's favour; KotOR against it.
    total += _game.isTSL() ? distance : -distance;
    if (total <= 0) {
        return false;
    }

    if (leader.get() == this || leader.get() == &target) {
        reportStealthDetection(target, _perceptionRolls.spot, hide, aware, rank,
                               observerStill, targetStill, -distance, facing, run, combat);
    }
    return true;
}

// Hearing a creature: within listen range and not walled off, an unstealthed
// one is always heard; a stealthed one when awareness and the listen roll beat
// its stealth and move-silently roll, with the area, occlusion, running,
// combat, stillness, distance and size terms.
bool Creature::detectsBySound(const Creature &target, bool invisible) const {
    auto area = _game.module()->area();
    const glm::vec3 lift {0.0f, 0.0f, 1.5f};
    const glm::vec3 eye {_position + lift};
    const glm::vec3 targetEye {target._position + lift};
    const glm::vec3 offset {targetEye - eye};
    const float distance2 = glm::dot(offset, offset);
    if (invisible || isBlind()) {
        const float reach = maxAttackRange(target);
        if (distance2 > reach * reach) {
            return false;
        }
    }
    const float listen = listenRange();
    if (distance2 > listen * listen) {
        return false;
    }
    const auto occlusion = area->soundOcclusion(*this, target, eye, targetEye);
    if (!occlusion) {
        return false;
    }
    if (!target._stealthMode) {
        return true;
    }
    const int rank = target.getSkillRankVersus(SkillType::Stealth, *this);
    if (rank == 0) {
        return true;
    }

    const int aware = getSkillRankVersus(SkillType::Awareness, target);
    int total = *occlusion - (rank + target._perceptionRolls.moveSilently) + aware + _perceptionRolls.listen +
                area->modListenCheck();
    if (_movementType == MovementType::Run) {
        total -= 5;
    }
    if (_combatState.active) {
        total -= 10;
    }
    if (target.isStationaryForDetection()) {
        total -= 5;
    }
    if (isStationaryForDetection()) {
        total += 5;
    }
    total -= static_cast<int>(glm::distance(_position, target._position) / 3.0f);
    switch (target.size()) {
    case CreatureSize::Tiny:
        total -= 8;
        break;
    case CreatureSize::Small:
        total -= 4;
        break;
    case CreatureSize::Large:
        total += 4;
        break;
    case CreatureSize::Huge:
        total += 8;
        break;
    default:
        break;
    }
    return total > 0;
}

bool Creature::hasDetectedTarget(const Object &target) const {
    if (target.type() != ObjectType::Creature || isPartyMember()) {
        return true;
    }
    return _perception.sees(target.id()) && !_perception.isInvisible(target.id());
}

// The spotter's side reads the two totals, then how each side's total is made.
void Creature::reportStealthDetection(const Creature &target, int spotRoll, int hideRoll, int aware, int rank,
                                      int observerStill, int targetStill, int distance, int facing, int run, int combat) const {
    auto text = [this](int strref) {
        return _game.getFeedbackText(strref);
    };
    const int observerTotal = aware + spotRoll + observerStill + distance + facing + run + combat;
    const int hiderTotal = rank + hideRoll + targetStill;
    _game.setCustomToken(0, _name);
    _game.setCustomToken(1, target.name());
    _game.setCustomToken(2, std::to_string(observerTotal));
    _game.setCustomToken(3, std::to_string(hiderTotal));
    _game.messageLog().add(MessageLog::kFeedbackMessageType, MessageLog::Style::Combat, text(kStealthSpottedStrRef));

    _game.setCustomToken(0, _name);
    _game.setCustomToken(1, std::to_string(observerTotal));
    _game.setCustomToken(2, std::to_string(spotRoll));
    _game.setCustomToken(3, std::to_string(aware));
    std::string observerLine = text(kStealthObserverStrRef);
    const std::array<std::pair<int, int>, 5> terms {{
        {observerStill, kStealthStationaryStrRef},
        {distance, kStealthDistanceStrRef},
        {facing, kStealthFacingStrRef},
        {run, kStealthRunningStrRef},
        {combat, kStealthCombatStrRef},
    }};
    for (const auto &[value, strref] : terms) {
        if (value == 0) {
            continue;
        }
        _game.setCustomToken(0, std::to_string(value));
        observerLine += " + " + text(strref);
    }
    _game.messageLog().add(MessageLog::kFeedbackMessageType, MessageLog::Style::Normal, observerLine);

    _game.setCustomToken(0, target.name());
    _game.setCustomToken(1, std::to_string(hiderTotal));
    _game.setCustomToken(2, std::to_string(hideRoll));
    _game.setCustomToken(3, std::to_string(rank));
    std::string hiderLine = text(kStealthHiderStrRef);
    // The hider's stillness is listed when the spotter stands still.
    if (observerStill != 0) {
        _game.setCustomToken(0, std::to_string(targetStill));
        hiderLine += " + " + text(kStealthStationaryStrRef);
    }
    _game.messageLog().add(MessageLog::kFeedbackMessageType, MessageLog::Style::Normal, hiderLine);
}

uint32_t Creature::lastPerceivedId() const {
    auto object = savedReference("LastPerceived");
    return object ? object->id() : script::kObjectInvalid;
}

// The other creature's own place in its list comes across with the list.
void Creature::takePerception(Creature &other) {
    _perception.seen = std::move(other._perception.seen);
    _perception.heard = std::move(other._perception.heard);
    _perception.invisible = std::move(other._perception.invisible);
    other._perception.clear();
    forgetPerceived(_id);
    if (auto previous = _game.getObjectById(other.id())) {
        setObjectSeen(previous, true);
        setObjectHeard(previous, true);
    }
}

// Saved bits: 0 seen, 1 heard, 3 invisible.
void Creature::restoreSavedPerception(const std::vector<uint8_t> &data) {
    for (size_t index = 0; index < data.size(); ++index) {
        auto object = savedReference("Perception/" + std::to_string(index));
        if (!object || object.get() == this) {
            continue;
        }
        setObjectSeen(object, (data[index] & 0x1) != 0);
        setObjectHeard(object, (data[index] & 0x2) != 0);
        setObjectInvisible(object, (data[index] & 0x8) != 0);
    }
}

/** The local number a TSL party member clears whenever it is set out of combat. */
static constexpr int kCombatLocalNumber = 8;

void Creature::setCombatState(bool active, CombatActivation activationType, bool holdExpiry) {
    // Faction 5 cannot enter combat. The TSL expiry-hold input does not lock a round.
    if (faction() == Faction::Neutral) {
        active = false;
    }
    // In TSL a party member set out of combat clears its local number 8.
    if (_game.isTSL() && !active && isPartyMember()) {
        setLocalNumber(kCombatLocalNumber, 0);
    }
    const bool wasActive = _combatState.active;
    // Entering combat state ends any head look.
    if (active && !wasActive) {
        lookAt(nullptr, 0.0f);
    }
    // A creature drawn directly into combat may shout a battle cry that the
    // controlled creature hears within 30 m of the same area.
    if (active && !wasActive && activationType == CombatActivation::Direct &&
        !isPartyMember() && _game.party().player().get() != this && randomInt(0, 9) <= 2) {
        playBattleCry();
    }
    _combatState.active = active;
    _combatState.expiryHeld = active && _game.isTSL() && holdExpiry;
    const bool drawnBladesLit = _lightsaberIdlePowerDownPending;
    _lightsaberIdlePowerDownPending = false;

    if (active) {
        _combatState.expiryTimer.reset(8.0f);
        // A direct activation stays direct until the state-zero transition.
        if (_combatState.activationType != CombatActivation::Direct) {
            _combatState.activationType = activationType;
        }
    } else {
        _combatState.expiryTimer.reset(0.0f);
        _combatState.activationType = CombatActivation::None;
        _combatState.attackTarget.reset();
        _combatState.roundTarget.reset();
        _combatState.attemptedAttackTarget.reset();
        _combatState.attemptedSpellTarget.reset();
        spellScriptContext().clearActiveTarget();
        _combatState.attackAction = ActionType::QueueEmpty;
        _combatState.combatFeat = FeatType::Invalid;
        _lastHostileTarget.reset();
        _lastAttackAction = -1;
        _lastCombatFeat = FeatType::Invalid;
        _combatState.roundSpell.reset();
        _lastForcePowerUsed = -1;
        setLastHostileActor(script::kObjectInvalid, true);
        // Leaving combat state clears the current round's attack records.
        if (auto action = _currentCombatAction.lock()) {
            action->combatAction().retireCombatRound();
        }
        // It also drops the round's scheduled work and ends a running round
        // without its end-of-round script.
        _game.combat().discardEquipment(*this);
        // The leader's continuation attack goes with the round's scheduled work.
        for (const auto &node : actions().nodes) {
            auto *continuation = node->action ? dyn_cast<AttackObjectAction>(node->action.get()) : nullptr;
            if (!continuation || !continuation->isPendingRoundContinuation()) {
                continue;
            }
            continuation->cancel(node->action, *this);
            continuation->markCancelled();
        }
        if (_game.combat().ownsRound(*this)) {
            _game.combat().endRound(*this, 0);
        }
    }

    // Entering combat state does not enter client combat mode; that follows
    // the member's own attempted target. A direct exit still leaves it.
    if (!active && activationType == CombatActivation::Direct) {
        setClientCombatMode(false);
    }
    // Only a change of combat state powers the hand items on or off; leaving
    // it again puts out only the blades a weapon draw lit.
    if (!active || !wasActive) {
        _animDirty = true;
        if (active != wasActive || drawnBladesLit) {
            setLightsabersPowered(active, true);
        }
    }
}

void Creature::applyFuryState(int spellId) {
    _furyDamageBonus = 0;
    switch (spellId) {
    case static_cast<int>(SpellType::Fury):
    case static_cast<int>(SpellType::WookieeRageI):
        _furySpellState = 7;
        break;
    case static_cast<int>(SpellType::ImprovedFury):
    case static_cast<int>(SpellType::WookieeRageII):
        _furySpellState = 8;
        break;
    case static_cast<int>(SpellType::MasterFury):
    case static_cast<int>(SpellType::WookieeRageIII):
        _furySpellState = 9;
        break;
    default:
        break;
    }
}

void Creature::clearFuryState() {
    _furyDamageBonus = kNoFuryDamageBonus;
    _furySpellState = 0;
}

void Creature::incrementFuryDamageBonus() {
    if (!_game.isTSL()) {
        return;
    }
    if (_furyDamageBonus != kNoFuryDamageBonus && _furyDamageBonus <= 5) {
        ++_furyDamageBonus;
    }
}

void Creature::setLightsabersPowered(bool powered, bool animate) {
    if (_handPowerHeld) {
        return;
    }
    powerHandItems(powered, animate);
}

void Creature::overrideLightsabers(int override, bool powered, bool animate) {
    // A KotOR script holds the hands only with exactly TRUE, and lets go with FALSE.
    if (!_game.isTSL()) {
        _handPowerHeld = override == 1;
    }
    if (override == 0) {
        return;
    }
    powerHandItems(powered, animate);
}

void Creature::putOutItemLeavingSlot(int slot) {
    if (_handPowerHeld) {
        return;
    }
    powerSlotItem(slot, false, false);
}

void Creature::resetHandPower() {
    _handPowerHeld = false;
    powerHandItems(false, false);
    _poweredHands = PoweredHands {};
}

void Creature::powerHandItems(bool powered, bool animate) {
    powerSlotItem(InventorySlots::rightWeapon, powered, animate);
    powerSlotItem(InventorySlots::leftWeapon, powered, animate);
}

// Without the transition a lightsaber goes straight to lit or out, its hum
// starting or stopping, with no power-up or power-down clip or sound.
void Creature::powerSlotItem(int slot, bool powered, bool animate) {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model) {
        return;
    }
    // Every item counts, lit or not, lightsaber or not, whatever its slot.
    auto item = getEquippedItem(slot);
    if (item) {
        updatePoweredHands(*item, powered);
    }

    // Only a hand holds a lightsaber that lights.
    if (slot != InventorySlots::rightWeapon && slot != InventorySlots::leftWeapon) {
        return;
    }
    const auto &attachment = slot == InventorySlots::rightWeapon ? g_rightHandNode : g_leftHandNode;
    auto weapon = static_cast<ModelSceneNode *>(model->getAttachment(attachment));
    if (!weapon || weapon->model().classification() != MdlClassification::lightsaber) {
        return;
    }

    const auto activeAnimation = weapon->activeAnimationName();
    if ((powered && (activeAnimation == "powerup" || activeAnimation == "powered")) ||
        (!powered && (activeAnimation == "powerdown" || activeAnimation == "off"))) {
        return;
    }
    weapon->playAnimation(animate ? (powered ? "powerup" : "powerdown") : (powered ? "powered" : "off"));
    if (item) {
        powered ? item->powerUp(_position, animate) : item->powerDown(_position, animate);
    }
}

// A hand counts while the lightsaber in it is lit. Any other item powered on
// or off, whichever hand it is in, keeps the hands powered until a lightsaber
// is powered on or off again.
void Creature::updatePoweredHands(const Item &item, bool on) {
    // Hands count only while the creature has a model.
    if (!_sceneNode) {
        return;
    }
    const bool right = getEquippedItem(InventorySlots::rightWeapon).get() == &item;
    const bool left = !right && getEquippedItem(InventorySlots::leftWeapon).get() == &item;
    // A lit item powered on again counts only its own hand; an unlit item
    // powered off counts none.
    if (on) {
        if (item.isPowered()) {
            _poweredHands.right = right;
            _poweredHands.left = left;
        } else {
            _poweredHands.right = _poweredHands.right || right;
            _poweredHands.left = _poweredHands.left || left;
        }
    } else if (item.isPowered()) {
        _poweredHands.right = _poweredHands.right && !right;
        _poweredHands.left = _poweredHands.left && !left;
    } else {
        _poweredHands.right = false;
        _poweredHands.left = false;
    }
    const int itemType = item.itemType();
    _poweredHands.notLightsaber = itemType < Item::kDoubleLightsaberItemType || itemType > Item::kLightsaberItemType;
}

void Creature::updateLightsaberSoundPositions() {
    for (int slot : {InventorySlots::rightWeapon, InventorySlots::leftWeapon}) {
        auto item = getEquippedItem(slot);
        if (item) {
            item->updatePoweredSoundPosition(_position);
        }
    }
}

std::shared_ptr<Item> Creature::getEquippedOffhandWeapon() const {
    auto right = getEquippedItem(InventorySlots::rightWeapon);
    if (right && right->weaponWield() == WeaponWield::DoubleBladedSword) {
        return right;
    }
    return getEquippedItem(InventorySlots::leftWeapon);
}

bool Creature::isTwoWeaponFighting() const {
    return static_cast<bool>(getOffhandAttackWeapon());
}

std::shared_ptr<Item> Creature::getOffhandAttackWeapon() const {
    auto main = getEquippedItem(InventorySlots::rightWeapon);
    if (!main) {
        return nullptr;
    }

    int relativeSize = static_cast<int>(main->weaponSize()) -
                       static_cast<int>(_size);
    if (relativeSize > 0) {
        return main->weaponWield() == WeaponWield::DoubleBladedSword
                   ? main
                   : nullptr;
    }

    auto offhand = getEquippedItem(InventorySlots::leftWeapon);
    if (!offhand ||
        offhand->weaponType() == WeaponType::None ||
        offhand->weaponWield() == WeaponWield::None) {
        return nullptr;
    }
    return offhand;
}

int Creature::currentCombatAttackType() const {
    const auto action = _currentCombatAction.lock();
    return action ? action->combatAction().currentCombatAttackType() : 0;
}

AttackResultType Creature::currentCombatAttackResult() const {
    const auto action = _currentCombatAction.lock();
    return action ? action->combatAction().currentCombatAttackResult() : AttackResultType::Invalid;
}

void Creature::beginCombatAttack(Object &targetObject, FeatType feat) {
    _roundActionKind = kAttackRoundAction;
    interruptActivities();
    _combatState.attackTarget = _game.getObjectById(targetObject.id());
    _combatState.attackAction = ActionType::AttackObject;
    _combatState.combatFeat = feat;
}

void Creature::setAttemptedAttackTarget(uint32_t target) {
    _combatState.attemptedAttackTarget = _game.getObjectById(target);
}

void Creature::setOrderedAttackTarget(uint32_t target) {
    _combatState.orderedAttackTarget = _game.getObjectById(target);
}

void Creature::recordQueuedAttack(Object &target) {
    if (auto *creature = dyn_cast<Creature>(&target)) {
        const auto leader = _game.party().getLeader();
        if (creature->actsAsPCValue() == 1 || leader.get() == creature) {
            creature->_incomingAttacker = SavedObjectReference::fromRuntimeId(id());
            _game.bindSavedObjectReference(creature->_incomingAttacker);
        }
    }
    // Accepted ordinary attack submission broadcasts the actor's own ID,
    // including when the target is a door or placeable. Intent is initialized
    // afterward and does not replace the intent of an already queued attack.
    broadcastCombatState(id());
    // A party member taking up an attack shouts a battle cry one time in ten.
    if (isPartyMember() && randomInt(0, 9) == 0) {
        playBattleCry();
    }
    if (!_combatState.attemptedAttackTarget.resolve()) {
        setAttemptedAttackTarget(target.id());
    }
    // A plot door or placeable told it is attacked fails to open for the
    // attacker.
    if ((isa<Door>(&target) || isa<Placeable>(&target)) && target.plotFlag()) {
        signalFailToOpen(target, *this);
    }
}

uint32_t Creature::getFirstAttacker() {
    // A creature outside the area has no attackers to find; the walk it holds
    // is kept as it is.
    auto area = _game.module()->area();
    if (!area->isObjectResident(*this)) {
        return script::kObjectInvalid;
    }
    return _attackerList.first(area->objects(), [this](const auto &object) {
            const auto creature = dyn_cast<Creature>(object);
            return creature && creature->getAttackTarget().get() == this; }, [](const auto &object) { return object->id(); });
}

void Creature::setClientCombatMode(bool active) {
    _clientCombatMode = active;
    _game.syncClientCombatMode();
}

void Creature::removeCombatInvisibilityEffects() {
    std::vector<EffectId> packages;
    for (const auto &effect : effects()) {
        if ((effect.type() == EffectType::Invisibility &&
             effect.integerParameter(0) == static_cast<int>(InvisibilityType::Normal)) ||
            effect.type() == EffectType::Sanctuary) {
            if (std::find(packages.begin(), packages.end(), effect.id) == packages.end()) {
                packages.push_back(effect.id);
            }
        }
    }
    for (auto id : packages) {
        removeEffectsById(id);
    }
}

void Creature::removeMindTrickEffects() {
    if (!_game.isTSL()) {
        return;
    }
    // The live list is walked by position, which moves on after a removal:
    // a record that a removal shifts into the freed position is passed over.
    for (size_t index = 0; index < effects().size(); ++index) {
        const auto &effect = effects()[index];
        if (effect.serializedType > SavedEffectTypes::setState) {
            break;
        }
        if (effect.serializedType != SavedEffectTypes::setState) {
            continue;
        }
        const auto state = static_cast<CreatureState>(effect.integerParameter(0));
        if (state != CreatureState::MindTrick && state != CreatureState::DroidScramble) {
            continue;
        }
        const EffectId id = effect.id;
        removeEffectsById(id);
    }
}

void Creature::broadcastCombatState(uint32_t opponentId) {
    auto opponent = _game.getObjectById<Creature>(opponentId);
    if (!opponent) {
        return;
    }
    const auto attemptedAttack = opponent->_combatState.attemptedAttackTarget.resolve();
    const auto attemptedSpell = opponent->_combatState.attemptedSpellTarget.resolve();
    auto activationType = [&](const Creature &member) {
        return attemptedAttack.get() == &member || attemptedSpell.get() == &member
                   ? CombatActivation::Direct
                   : CombatActivation::Indirect;
    };
    if (opponent.get() == this || getReputationToward(*opponent) <= 10) {
        setCombatState(true, activationType(*this), false);
    }
    // A plot creature does not rouse its faction.
    if (plotFlag()) {
        return;
    }
    // The faction pass includes this creature, which is always within range.
    if (getReputationToward(*opponent) <= 10) {
        setCombatState(true, activationType(*this), false);
        setExcitedState(1);
    }
    auto area = _game.module()->area();
    if (!area->isObjectResident(*this)) {
        return;
    }
    // Snapshot membership before any presentation or script can change the area.
    const auto members = area->getObjectsByType(ObjectType::Creature);
    const bool alwaysInRange = _game.isTSL() && forceAlwaysUpdate();
    for (const auto &object : members) {
        auto member = std::static_pointer_cast<Creature>(object);
        if (!member->isRuntimeLive() || member.get() == this || member->faction() != faction()) {
            continue;
        }
        const float range = isPartyMember() ? 30.0f : member->perception().sightRange;
        if (!alwaysInRange && getSquareDistanceTo(*member) > range * range) {
            continue;
        }
        if (member->getReputationToward(*opponent) <= 10) {
            member->setCombatState(true, activationType(*member), false);
            member->setExcitedState(1);
        }
    }
}

static std::shared_ptr<Item> attackWeapon(const Creature &attacker, int weaponAttackType) {
    switch (weaponAttackType) {
    case 1:
    case 6:
        return attacker.getEquippedItem(InventorySlots::rightWeapon);
    case 2:
        return attacker.getEquippedItem(InventorySlots::leftWeapon);
    case 3:
        return attacker.getEquippedItem(InventorySlots::cWeaponL);
    case 4:
        return attacker.getEquippedItem(InventorySlots::cWeaponR);
    case 5:
        return attacker.getEquippedItem(InventorySlots::cWeaponB);
    case 7:
    case 8:
        return attacker.getEquippedItem(InventorySlots::hands);
    default:
        return nullptr;
    }
}

void Creature::recordAttackWeapon(int weaponAttackType) {
    const auto weapon = attackWeapon(*this, weaponAttackType);
    _lastWeaponUsed = weapon ? weapon->id() : script::kObjectInvalid;
}

void Creature::receiveAttackEvent(std::optional<std::reference_wrapper<const AttackHistory>> history, uint32_t attackerId,
                                  std::optional<std::reference_wrapper<const AttackEventFields>> fields) {
    // Capture borrowed values before callbacks can retire their owning round.
    if (history) {
        _receivedAttack = history->get();
    }
    const int weaponType = fields ? fields->get().weaponAttackType : 0;
    auto attacker = _game.getObjectById<Creature>(attackerId);
    const auto weapon = attacker ? attackWeapon(*attacker, weaponType) : nullptr;
    _savedReferences["LastAttacker"] = _game.getObjectById(attackerId);
    setLastHostileActor(attackerId);
    if (attacker) {
        attacker->broadcastCombatState(id());
        attacker->removeCombatInvisibilityEffects();
        attacker->_lastWeaponUsed = weapon ? weapon->id() : script::kObjectInvalid;
    }
    if (!isRuntimeLive()) {
        return;
    }
    broadcastCombatState(attackerId);
    // Neither a dead creature nor an incapacitated party member reacts.
    if (!isDead() && !isTemporarilyDead()) {
        runAttackedScript(attackerId);
        if (isRuntimeLive()) {
            removeMindTrickEffects();
        }
    }
}

void Creature::receiveSpellCastAt(uint32_t casterId, int spell, int harmful) {
    // A creature records a creature caster as itself and an area of effect as
    // the creature that created it; any other caster is recorded as no object.
    auto casterObject = _game.getObjectById(casterId);
    if (auto areaOfEffect = casterObject ? dyn_cast<AreaOfEffect>(casterObject.get()) : nullptr) {
        casterObject = areaOfEffect->creator();
    }
    if (casterObject && !isa<Creature>(casterObject.get())) {
        casterObject.reset();
    }
    casterId = casterObject ? casterObject->id() : script::kObjectInvalid;
    setLastSpellCast(casterId, spell, harmful);
    if (harmful == 1) {
        setLastHostileActor(casterId);
    }
    if (!isDead() && !isTemporarilyDead()) {
        _game.scriptRunner().run(getOnSpellCastAt(), {{script::ArgKind::Caller, Variable::ofObject(_id)}});
    }
    if (!isRuntimeLive() || harmful == 0) {
        return;
    }
    auto caster = _game.getObjectById<Creature>(casterId);
    if (!caster) {
        return;
    }
    // A harmful spell engages both sides and excites the caster's spell row.
    caster->broadcastCombatState(id());
    caster->setExcitedState(2);
    caster->removeCombatInvisibilityEffects();
    if (!isRuntimeLive()) {
        return;
    }
    broadcastCombatState(casterId);
    setExcitedState(1);
}

bool Creature::updateClientCombatMode(std::optional<std::reference_wrapper<const Object>> storedTarget, bool holdDeadTarget) {
    if (!isPartyMember()) {
        return false;
    }
    const auto attack = _combatState.attemptedAttackTarget.resolve();
    const auto spell = _combatState.attemptedSpellTarget.resolve();
    const bool attacking = attack && !attack->isDead();
    if (!_clientCombatMode) {
        const bool casting = spell && spell.get() != this && spell != attack && !spell->isDead();
        if (attacking || casting) {
            setClientCombatMode(true);
        }
        return false;
    }
    auto leader = _game.party().getLeader();
    auto isHostile = [&](const Creature &creature) {
        return leader && _services.game.reputes.getIsEnemy(creature, *leader);
    };
    if (holdDeadTarget) {
        return false;
    }
    if (storedTarget) {
        // A hostile stored target keeps the mode even once it is dead, as does
        // a locked door or placeable.
        const auto &target = storedTarget->get();
        if (auto creature = asCreature(target); creature && isHostile(creature->get())) {
            return false;
        }
        if (auto *door = dyn_cast<Door>(&target); door && door->isLocked()) {
            return false;
        }
        if (auto *placeable = dyn_cast<Placeable>(&target); placeable && placeable->isLocked()) {
            return false;
        }
    }
    if (attacking) {
        return false;
    }
    if (spell && spell != attack && !spell->isDead()) {
        return false;
    }
    const auto incoming = _incomingAttacker.boundObject();
    if (incoming && incoming != attack && incoming != spell && !incoming->isDead()) {
        auto *incomingCreature = dyn_cast<Creature>(incoming.get());
        if (!incomingCreature || isHostile(*incomingCreature)) {
            return false;
        }
    }
    setClientCombatMode(false);
    return true;
}

void Creature::cancelCombat(int runEndRound) {
    // An attack its round has taken up ends with the round below.
    clearAllActions();
    setCombatState(false);
    setLastHostileActor(script::kObjectInvalid, true);
    discardHostileActionGroups();
    _game.combat().endRound(*this, runEndRound);
    _combatState.attackTarget.reset();
    _combatState.roundTarget.reset();
    setClientCombatMode(false);
}

// The farthest an object can be seen in an area.
static constexpr float kMaxObjectVisibleDistance = 250.0f;

void Creature::surrenderToEnemies(bool retainOwnEffects) {
    cancelCombat();
    clearAllEffects(retainOwnEffects);
    if (auto area = spatialArea()) {
        const ObjectList creatures = area->get().getObjectsByType(ObjectType::Creature);
        for (const auto &object : creatures) {
            auto &other = static_cast<Creature &>(*object);
            if (&other == this ||
                getSquareDistanceTo(other) > kMaxObjectVisibleDistance * kMaxObjectVisibleDistance ||
                getReputationToward(other) > 10)
                continue;
            other.cancelCombat();
            other.clearAllEffects(retainOwnEffects);
        }
    }
    setFaction(Faction::Neutral);
}

void Creature::pacify() {
    auto area = spatialArea();
    if (!area) {
        return;
    }
    const ObjectList creatures = area->get().getObjectsByType(ObjectType::Creature);
    for (const auto &object : creatures) {
        auto &other = static_cast<Creature &>(*object);
        if (other.getAttemptedAttackTarget() != _id && other.getAttackTarget().get() != this &&
            other.attemptedSpellTarget().get() != this &&
            other.spellScriptContext().activeTarget().get() != this &&
            other.getLastHostileActor() != _id)
            continue;
        // Leaving combat state also forgets the last hostile actor.
        other.setCombatState(false);
        other.clearHostileActionsAgainst(*this);
    }
}

void Creature::refreshBodyFuel() {
    if (_bodyFuel) {
        _currentForce = maxForcePoints();
    }
}

void Creature::finishCombatRound() {
    const auto incoming = _incomingAttacker.boundObject();
    if (!incoming || incoming->isDead()) {
        _incomingAttacker = SavedObjectReference {};
    }
    _attackerList.clear();
    if (auto target = _combatState.attackTarget.resolve()) {
        _lastHostileTarget = target;
    } else {
        _lastHostileTarget.reset();
    }
    // A round without an action, as when leaving combat ends one, leaves -1.
    _lastAttackAction = _combatState.attackAction == ActionType::QueueEmpty ? -1 : static_cast<int>(_combatState.attackAction);
    if (_combatState.combatFeat != FeatType::Invalid) {
        _lastCombatFeat = _combatState.combatFeat;
    }
    // A spell round leaves its power as the last one used.
    if (_combatState.roundSpell) {
        _lastForcePowerUsed = *_combatState.roundSpell;
    }
    _combatState.roundSpell.reset();
}

void Creature::adjustModifiedAttacks(int amount) {
    _modifiedAttacks += amount;
    // KotOR saturates this value on writes. TSL retains the raw value and caps
    // its contribution when constructing on-hand attacks.
    if (!_game.isTSL()) {
        _modifiedAttacks = std::clamp(_modifiedAttacks, 0, 2);
    }
}

bool Creature::applyAssuredHit() {
    if (_assuredHit) {
        return false;
    }
    _assuredHit = true;
    return true;
}

bool Creature::applyAssuredDeflection(int returnDamage) {
    if (_assuredDeflection || _assuredReturn) {
        return false;
    }
    _assuredDeflection = true;
    _assuredReturn = returnDamage != 0;
    return true;
}

Alignment Creature::alignment() const {
    if (_goodEvil <= 40) {
        return Alignment::DarkSide;
    }
    if (_goodEvil >= 60) {
        return Alignment::LightSide;
    }
    return Alignment::Neutral;
}

namespace {

constexpr int kMaxGoodEvil = 100;
constexpr char kPlayerAlignmentGlobal[] = "G_PC_Align_Val";

// Mastery of a side of the Force strengthens influence by the level held in
// its prestige class.
float influenceFactor(int classLevel) {
    if (classLevel > 18) {
        return 2.0f;
    }
    if (classLevel > 14) {
        return 1.8f;
    }
    if (classLevel > 10) {
        return 1.6f;
    }
    if (classLevel > 6) {
        return 1.4f;
    }
    if (classLevel >= 1) {
        return 1.2f;
    }
    return 1.0f;
}

// Influence above 50 pulls the companion's own alignment toward the player
// character's side, influence below 50 pushes it away; the further the player
// character is from balance, the stronger the pull.
int influenceAlignment(int base, int influence, int charismaBonus, float factor, int playerAlignment) {
    const int total = influence + charismaBonus;
    const float lean = total > 100 ? 50.0f : (total < 0 ? -50.0f : static_cast<float>(total - 50));
    const float pull = factor * lean / 50.0f;
    const float push = static_cast<float>(playerAlignment - 50) / 50.0f * pull;
    const float value = push <= 0.0f
                            ? static_cast<float>(base) * (1.0f + push)
                            : static_cast<float>(base) + static_cast<float>(100 - base) * push;
    if (value > 100.0f) {
        return 100;
    }
    if (value < 0.0f) {
        return 0;
    }
    return static_cast<int>(value);
}

} // namespace

void Creature::modifyAlignment(int shift, bool dontModifyNPCs) {
    const auto clampGoodEvil = [](int value) {
        return std::clamp(value, 0, kMaxGoodEvil);
    };
    const auto shifted = [&]() {
        return clampGoodEvil(_goodEvil + shift);
    };
    Party &party = _game.party();
    if (!_game.isTSL()) {
        _goodEvil = shifted();
    } else if (_playerCreated) {
        _goodEvil = shifted();
        if (!dontModifyNPCs) {
            _game.setGlobalNumber(kPlayerAlignmentGlobal, _goodEvil);
            for (int npc = 0; npc < static_cast<int>(Party::kK2NpcCount); ++npc) {
                if (auto companion = party.getAvailableMember(npc)) {
                    companion->recomputeInfluenceAlignment();
                }
            }
        }
    } else if (party.isMember(*this)) {
        _baseCNPCAlignment = clampGoodEvil(_baseCNPCAlignment + shift);
        recomputeInfluenceAlignment();
    } else {
        _goodEvil = shifted();
    }
    updatePureGoodEvilPowers();
}

void Creature::updatePureGoodEvilPowers() {
    if (_goodEvil == kMaxGoodEvil) {
        removePureAlignmentPowers(false);
        addPureAlignmentPowers(true);
    } else if (_goodEvil == 0) {
        removePureAlignmentPowers(true);
        addPureAlignmentPowers(false);
    } else {
        removePureAlignmentPowers(true);
        removePureAlignmentPowers(false);
    }
}

void Creature::removePureAlignmentPowers(bool good) {
    const EffectType type = good ? EffectType::PureGoodPowers : EffectType::PureEvilPowers;
    const auto found = std::find_if(effects().begin(), effects().end(),
                                    [type](const EffectInstance &effect) { return effect.type() == type; });
    if (found != effects().end()) {
        removeEffectsById(found->id);
    }
}

void Creature::addPureAlignmentPowers(bool good) {
    removePureAlignmentPowers(good);
    auto powers = std::make_shared<PureAlignmentPowersEffect>(good);
    auto instance = powers->saveFacingInstance();
    instance.effect = powers;
    instance.creatorId = id();
    _game.bindEffectCreator(instance);
    instance.setDuration(DurationType::Innate, 0.0f);
    instance.restoring = false;
    applyEffect(std::move(instance));
}

void Creature::recomputeInfluenceAlignment() {
    if (!_game.isTSL()) {
        return;
    }
    // Kreia's alignment is her own, whatever her influence.
    if (boost::iequals(_tag, "kreia")) {
        _goodEvil = std::clamp(_baseCNPCAlignment, 0, kMaxGoodEvil);
        return;
    }
    // Influence belongs to a companion's roster slot, and its base to the
    // companion's row of influence.2da; a creature with neither has none.
    Party &party = _game.party();
    const auto identity = party.rosterIdentity(*this);
    if (!identity || identity->kind != RosterKind::Npc) {
        return;
    }
    const auto baseInfluence = party.baseInfluence(_tag);
    if (!baseInfluence) {
        return;
    }

    const int playerAlignment = _game.getGlobalNumber(kPlayerAlignmentGlobal);
    std::shared_ptr<Creature> player;
    for (int index = 0; index < 3 && !player; ++index) {
        auto member = party.getMember(index);
        if (member && member->isPlayerCreated()) {
            player = std::move(member);
        }
    }
    if (!player) {
        player = party.player();
    }
    float factor = 1.0f;
    int charismaBonus = 0;
    if (player) {
        charismaBonus = 3 * player->getEffectiveAbilityModifier(Ability::Charisma);
        const auto &attributes = player->attributes();
        if (player->hasEffectiveFeat(FeatType::DarkSideCorruption)) {
            if (player->goodEvil() <= 40) {
                factor = influenceFactor(attributes.getClassLevel(ClassType::SithLord));
            }
        } else if (player->hasEffectiveFeat(FeatType::LightSideEnlightenment) && player->goodEvil() >= 60) {
            factor = influenceFactor(attributes.getClassLevel(ClassType::JediMaster));
        }
    }

    const int npc = identity->slot;
    auto influence = party.influence(npc);
    // Influence never set starts at the companion's base influence.
    if (!influence) {
        influence = *baseInfluence;
        party.setInfluence(npc, *influence);
    }
    _goodEvil = influenceAlignment(_baseCNPCAlignment, *influence, charismaBonus, factor, playerAlignment);
    updatePureGoodEvilPowers();
}

bool Creature::isEffectLinkImmune(const Effect &effect) const {
    if (const auto *link = dynamic_cast<const LinkEffectsEffect *>(&effect)) {
        return isEffectLinkImmune(*link->childEffect()) ||
               isEffectLinkImmune(*link->parentEffect());
    }
    EffectInstance record = effect.saveFacingInstance();
    int state = record.serializedType == SavedEffectTypes::setState ? record.integerParameter(0) : 0;
    auto row = getEffectImmunityRow(_game.isTSL(), record.serializedType, state);
    if (!row) {
        return false;
    }
    // An immunity the table has no column or no value for reads as none, as
    // a failed table lookup leaves it.
    const int count = immunityTypeCount(_game.isTSL());
    for (int immunity : _services.game.combatTables.gameEffectImmunities(*row)) {
        // Immunity is judged without an opposing creature; the creator is
        // only told of it.
        const auto type = static_cast<ImmunityType>(immunity);
        if (immunity < count && hasEffectImmunity(type)) {
            addLinkImmunityFeedback(_game, record.boundCreator(), *this, type);
            return true;
        }
    }
    return false;
}

bool Creature::hasEffectImmunity(
    ImmunityType immunityType, std::optional<std::reference_wrapper<const Creature>> creator) const {
    for (const EffectInstance &applied : effects()) {
        if (!applied.hasLiveRuntimeSource()) {
            continue;
        }
        if (applied.type() == EffectType::Immunity &&
            (applied.integerParameter(0) == static_cast<int>(immunityType) ||
             applied.integerParameter(0) == static_cast<int>(ImmunityType::All)) &&
            applied.appliesVersus(creator)) {
            return true;
        }
    }
    return false;
}

int Creature::getAbilityEffectModifier(Ability ability) const {
    AbilityEffectReducer reducer(_game.isTSL());
    for (const EffectInstance &effect : effects()) {
        if (!effect.hasLiveRuntimeSource() ||
            effect.integerParameter(0, -1) != static_cast<int>(ability))
            continue;
        if (effect.type() == EffectType::AbilityIncrease) {
            reducer.addIncrease(getEffectSourceKey(effect), effect.integerParameter(1));
        } else if (effect.type() == EffectType::AbilityDecrease) {
            reducer.addDecrease(getEffectSourceKey(effect), effect.integerParameter(1));
        }
    }
    return reducer.total();
}

const Race &Creature::race() const {
    return _services.game.races.get(_race);
}

void Creature::setRacialType(RacialType race) {
    // Only a race of the race table can be set.
    _services.game.races.get(race);
    _race = race;
}

int Creature::racialAbilityAdjustment(Ability ability) const {
    return race().abilityAdjustments[static_cast<int>(ability)];
}

int Creature::getEffectiveAbilityScore(Ability ability) const {
    return getAbilityScoreFromParts(_attributes.getAbilityScore(ability),
                                    getAbilityEffectModifier(ability), racialAbilityAdjustment(ability));
}

int Creature::getEffectiveAbilityModifier(Ability ability) const {
    return getAbilityModifierFromScore(getEffectiveAbilityScore(ability));
}

// A live bonus feat effect gives the creature its feat.
static bool isLiveBonusFeat(const EffectInstance &effect) {
    return effect.hasLiveRuntimeSource() && effect.type() == EffectType::BonusFeat;
}

bool Creature::hasEffectiveFeat(FeatType feat) const {
    if (_attributes.hasFeat(feat)) {
        return true;
    }
    return std::any_of(
        effects().begin(), effects().end(),
        [feat](const EffectInstance &effect) {
            return isLiveBonusFeat(effect) && effect.integerParameter(0) == static_cast<int>(feat);
        });
}

std::set<FeatType> Creature::effectFeats() const {
    std::set<FeatType> result;
    for (const auto &effect : effects()) {
        if (isLiveBonusFeat(effect)) {
            result.insert(static_cast<FeatType>(effect.integerParameter(0)));
        }
    }
    return result;
}

int Creature::sneakAttackDice() const {
    return getHighestOwnedFeatRank(*this, FeatType::SneakAttack1d6, 10);
}

void Creature::appendEquippedItemEffects(
    std::deque<EffectInstance> &effects,
    int slot,
    const std::shared_ptr<Item> &item, bool onlyDeferredEffects) const {
    if (!item || !equippedItemPropertiesAreActive(slot)) {
        return;
    }

    auto append = [&](std::shared_ptr<Effect> effect) {
        if (!effect) {
            return;
        }
        effect->setSaveFacingCreator(item);
        EffectInstance instance = effect->saveFacingInstance();
        instance.effect = std::move(effect);
        instance.id = _game.allocateEffectId();
        instance.setDurationType(DurationType::Equipped);
        instance.creatorId = item->id();
        instance.exposed = 1;
        // Item-property effects retain load mode. Equipped duration does not
        // bypass application checks.
        instance.restoring = isRestoringSavedRuntime();
        effects.push_back(std::move(instance));
    };

    for (const Item::PropertyEntry &property : item->properties()) {
        if (!item->isPropertyActive(property)) {
            continue;
        }
        const auto propertyType = property.type();
        const bool armorClass = propertyType == ItemProperty::AcBonus ||
                                propertyType == ItemProperty::AcBonusVsAlignmentGroup ||
                                propertyType == ItemProperty::AcBonusVsDamageType ||
                                propertyType == ItemProperty::AcBonusVsRacialGroup ||
                                propertyType == ItemProperty::DecreasedAc;
        const bool deferred = armorClass || propertyType == ItemProperty::Disguise;
        if (onlyDeferredEffects && !deferred) {
            continue;
        }
        // While a save loads, the armour class and disguise effects wait for
        // the saved effects; onEffectsRestored adds them.
        if (!onlyDeferredEffects && deferred && isRestoringSavedRuntime()) {
            continue;
        }
        switch (propertyType) {
        case ItemProperty::SniperBonus:
            if (_game.isTSL()) {
                item->enableSniperBonus();
            }
            break;
        case ItemProperty::RapidShotBonus:
            if (_game.isTSL()) {
                item->enableRapidShotBonus();
            }
            break;
        case ItemProperty::Doorcutting:
            if (_game.isTSL()) {
                item->enableDoorCutting();
            }
            break;
        case ItemProperty::Doorsabering:
            if (_game.isTSL()) {
                item->enableDoorSabering();
            }
            break;
        case ItemProperty::AcBonus:
        case ItemProperty::AcBonusVsAlignmentGroup:
        case ItemProperty::AcBonusVsDamageType:
        case ItemProperty::AcBonusVsRacialGroup: {
            int amount = getCostTableValue(_services, kBonusCostTable, property.costValue, CostColumn::Value, 0);
            if (amount == 0) {
                break;
            }
            const int selector = propertyType == ItemProperty::AcBonusVsDamageType
                                     ? property.subtype
                                     : kPhysicalDamageTypeFlags;
            auto effect = _game.newEffect<ACIncreaseEffect>(amount, item->acBonusType(), selector);
            if (propertyType == ItemProperty::AcBonusVsRacialGroup) {
                effect->setVersusRacialType(property.subtype);
            } else if (propertyType == ItemProperty::AcBonusVsAlignmentGroup &&
                       property.subtype >= 1 && property.subtype <= 3)
                effect->setVersusAlignment(0, property.subtype);
            append(std::move(effect));
            break;
        }
        case ItemProperty::DecreasedAc: {
            int amount = getCostTableValue(_services, kDecreaseCostTable, property.costValue, CostColumn::Value, 0);
            if (amount != 0)
                append(_game.newEffect<ACDecreaseEffect>(
                    -amount,
                    static_cast<ACBonus>(property.subtype), kPhysicalDamageTypeFlags));
            break;
        }
        case ItemProperty::AbilityBonus: {
            int amount = getItemPropertyValue(
                _services, property, CostColumn::Value, 0);
            if (amount > 0) {
                append(_game.newEffect<AbilityIncreaseEffect>(
                    getPropertyAbility(property.subtype), amount));
            }
            break;
        }
        case ItemProperty::DecreasedAbilityScore: {
            // The cost table holds the decrease as a negative value.
            const int amount = -costCell(_services.game.combatTables.costTableAt(kDecreasedAbilityCostTable),
                                         property.costValue, CostColumn::Value, 0);
            if (amount != 0) {
                append(_game.newEffect<AbilityDecreaseEffect>(
                    getPropertyAbility(property.subtype), amount));
            }
            break;
        }
        case ItemProperty::SkillBonus:
            if (property.costValue != 0)
                append(_game.newEffect<SkillIncreaseEffect>(
                    static_cast<SkillType>(property.subtype), property.costValue));
            break;
        case ItemProperty::DecreasedSkillModifier: {
            const int amount = getCostTableValue(_services, kDecreaseCostTable, property.costValue, CostColumn::Value, 0);
            if (amount != 0)
                append(_game.newEffect<SkillDecreaseEffect>(
                    static_cast<SkillType>(property.subtype), -amount));
            break;
        }
        case ItemProperty::Disguise:
            append(_game.newEffect<DisguiseEffect>(property.subtype));
            break;
        case ItemProperty::BonusFeat:
            append(_game.newEffect<BonusFeatEffect>(
                static_cast<FeatType>(property.subtype)));
            break;
        case ItemProperty::Immunity: {
            // The subtype is a row of iprp_immunity; a subtype past its ten
            // rows grants no immunity.
            static constexpr std::array<ImmunityType, 10> immunities {
                ImmunityType::SneakAttack, ImmunityType::AbilityDecrease,
                ImmunityType::MindSpells, ImmunityType::Poison, ImmunityType::Disease,
                ImmunityType::Fear, ImmunityType::Knockdown, ImmunityType::Paralysis,
                ImmunityType::CriticalHit, ImmunityType::Death};
            if (property.subtype >= immunities.size()) {
                break;
            }
            const ImmunityType immunity = immunities[property.subtype];
            // Immunity to ability decrease also covers negative levels, which
            // are granted first.
            if (immunity == ImmunityType::AbilityDecrease) {
                append(_game.newEffect<ImmunityEffect>(ImmunityType::NegativeLevel));
            }
            append(_game.newEffect<ImmunityEffect>(immunity));
            break;
        }
        case ItemProperty::FreedomOfMovement:
            for (auto immunity : {ImmunityType::Paralysis, ImmunityType::Slow,
                                  ImmunityType::Entangle, ImmunityType::MovementSpeedDecrease})
                append(_game.newEffect<ImmunityEffect>(immunity));
            break;
        case ItemProperty::SpecialWalk: {
            // Unlike the other item effects, the walk and its speed limit are magical.
            auto walk = _game.newEffect<WalkAnimationEffect>(property.subtype);
            walk->setCategory(EffectCategory::Magical);
            append(std::move(walk));
            auto limit = _game.newEffect<LimitMovementSpeedEffect>();
            limit->setCategory(EffectCategory::Magical);
            append(std::move(limit));
            break;
        }
        case ItemProperty::Light:
            // The wearer is lit by one fixed visual, whatever the property's
            // parameters.
            append(_game.newEffect<LightEffect>(kItemLightVisualEffect));
            append(_game.newEffect<VisualEffectMarkerEffect>(kItemLightVisualEffect));
            break;
        // The deflection amount is the raw cost value.
        case ItemProperty::BlasterBoltDeflectIncrease:
            if (property.costValue != 0) {
                append(_game.newEffect<BlasterDeflectionIncreaseEffect>(property.subtype, property.costValue));
            }
            break;
        case ItemProperty::BlasterBoltDeflectDecrease:
            if (property.costValue != 0) {
                append(_game.newEffect<BlasterDeflectionDecreaseEffect>(property.subtype, property.costValue));
            }
            break;
        case ItemProperty::ImmunityDamageType: {
            int amount = getItemPropertyValue(
                _services, property, CostColumn::Value, 0);
            if (amount > 0) {
                append(_game.newEffect<DamageImmunityIncreaseEffect>(
                    getItemPropertyDamageType(_services, property.subtype),
                    amount));
            }
            break;
        }
        case ItemProperty::DamageVulnerability: {
            if (plotFlag()) {
                break;
            }
            int amount = getCostTableValue(
                _services,
                kVulnerabilityCostTable,
                property.costValue,
                CostColumn::Value,
                0);
            if (amount > 0) {
                append(_game.newEffect<DamageImmunityDecreaseEffect>(
                    getItemPropertyDamageType(_services, property.subtype),
                    amount));
            }
            break;
        }
        case ItemProperty::DamageResistance: {
            int amount = getCostTableValue(
                _services,
                kResistanceCostTable,
                property.costValue,
                CostColumn::Amount,
                0);
            if (amount > 0) {
                append(_game.newEffect<DamageResistanceEffect>(
                    getItemPropertyDamageType(_services, property.subtype),
                    amount,
                    0));
            }
            break;
        }
        case ItemProperty::DamageReduction: {
            int amount = getCostTableValue(
                _services,
                kReductionCostTable,
                property.costValue,
                CostColumn::Amount,
                0);
            if (amount > 0) {
                append(_game.newEffect<DamageReductionEffect>(
                    amount,
                    getDamageReductionPower(_services, property.subtype),
                    0));
            }
            break;
        }
        case ItemProperty::ImprovedForceResistance: {
            const int amount = costCell(_services.game.combatTables.costTableAt(kImprovedForceResistanceCostTable),
                                        property.costValue, CostColumn::Value, 0);
            if (amount != 0) {
                append(_game.newEffect<ForceResistanceIncreaseEffect>(amount));
            }
            break;
        }
        case ItemProperty::Regeneration:
        case ItemProperty::RegenerationForcePoints:
            if (property.costValue != 0) {
                append(_game.newEffect<RegenerateEffect>(property.costValue, 6000, property.propertyName));
            }
            break;
        case ItemProperty::TrueSeeing:
            append(_game.newEffect<TrueSeeingEffect>());
            break;
        default:
            break;
        }
    }
}

std::deque<EffectInstance> Creature::effectsWithoutEquippedSource(
    const OptionalItem &source) const {
    std::deque<EffectInstance> result;
    for (const EffectInstance &effect : effects()) {
        auto creator = effect.boundCreator();
        if (source && effect.durationType() == DurationType::Equipped &&
            creator.get() == &source->get()) {
            continue;
        }
        result.push_back(effect);
    }
    return result;
}

std::deque<EffectInstance> Creature::rebuildEquippedItemEffects(
    const std::map<int, std::shared_ptr<Item>> &equipment) const {
    std::deque<EffectInstance> result;
    for (const EffectInstance &effect : effects()) {
        if (effect.durationType() != DurationType::Equipped) {
            result.push_back(effect);
        }
    }
    std::set<const Item *> seen;
    for (const auto &[slot, item] : equipment) {
        if (item && seen.insert(item.get()).second) {
            appendEquippedItemEffects(result, slot, item);
        }
    }
    return result;
}

// TSL party leadership terms of the targeted attack modifier. The shorter
// helpers used by touch attacks and the character sheet carry none of them.
void Creature::addPartyLeadershipTerms(AttackBonusBreakdown &result, std::optional<std::reference_wrapper<const Creature>> target) const {
    if (!_game.isTSL()) {
        return;
    }
    // Party leadership: a light side holder of Inspire Followers lifts
    // the party's attacks, the player character's Charisma lifts its
    // followers' attacks, and a dark side holder of Crush Opposition
    // lowers attacks on the party. The holders are the creature in
    // question when it is the player character, and the party's NPCs.
    const auto leadership = [this](const Creature &subject, SpellType high, SpellType low, bool lightSide) {
        const auto aligned = [lightSide](const Creature &holder) {
            return lightSide ? holder.goodEvil() > 59 : holder.goodEvil() < 41;
        };
        int value = 0;
        const auto consider = [&](const Creature &holder) {
            if (!aligned(holder)) {
                return false;
            }
            if (holder.attributes().hasSpell(high)) {
                value = 2;
                return true;
            }
            if (holder.attributes().hasSpell(low)) {
                value = 1;
            }
            return false;
        };
        if (subject.isPC() && consider(subject)) {
            return value;
        }
        for (const auto &member : _game.party().members()) {
            if (member.creature && !member.creature->isPC() && consider(*member.creature)) {
                return value;
            }
        }
        return value;
    };
    if (isPartyMember())
        result.inspireFollowersBonus =
            leadership(*this, SpellType::InspireFollowersVI, SpellType::InspireFollowersIII, true);
    if (isPartyMember() && !isPC()) {
        for (const auto &member : _game.party().members()) {
            if (member.creature && member.creature->isPC()) {
                result.leaderCharismaBonus = member.creature->getEffectiveAbilityModifier(Ability::Charisma);
                break;
            }
        }
    }
    if (target && target->get().isPartyMember())
        result.crushOppositionPenalty =
            -leadership(target->get(), SpellType::CrushOppositionVI, SpellType::CrushOppositionIII, false);
}

AttackBonusBreakdown Creature::getAttackBonusBreakdown(
    std::optional<std::reference_wrapper<const Creature>> target,
    const std::shared_ptr<Item> &weapon,
    bool offHand,
    std::optional<AttackBonus> handType) const {

    AttackBonusBreakdown result;

    int strengthModifier = getEffectiveAbilityModifier(Ability::Strength);
    int dexterityModifier = getEffectiveAbilityModifier(Ability::Dexterity);

    if (weapon && weapon->isRanged()) {
        result.dexterityModifier = dexterityModifier;
    } else if (weapon && dexterityModifier > strengthModifier) {
        bool finesse = qualifiesForWeaponFinesse(
            _game.isTSL(),
            weapon->isLightsaber(),
            weapon->weaponWield(),
            hasEffectiveFeat(FeatType::FinesseLightsabers),
            hasEffectiveFeat(FeatType::FinesseMeleeWeapons));

        if (finesse) {
            result.dexterityModifier = dexterityModifier;
        } else {
            result.strengthModifier = strengthModifier;
        }
    } else {
        result.strengthModifier = strengthModifier;
    }

    const bool creatureWeapon = isCreatureWeaponAttack(*this, weapon);
    // The kind of attack hand-specific modifiers must be for.
    const auto attackType = handType                                                       ? *handType
                            : creatureWeapon                                               ? AttackBonus::CreatureWeapon
                            : weapon                                                       ? (offHand ? AttackBonus::Offhand : AttackBonus::Onhand)
                            : !_game.isTSL() && isEmptyCreatureWeaponAttack(*this, weapon) ? AttackBonus::Onhand
                                                                                           : AttackBonus::Unarmed;
    int modifierBonus = 0;
    int modifierPenalty = 0;
    EffectModifierReducer miscModifierReducer;

    for (const auto &applied : effects()) {
        if (!applied.hasLiveRuntimeSource()) {
            continue;
        }
        if (!applied.appliesVersus(target)) {
            continue;
        }
        auto modifierType = static_cast<AttackBonus>(
            applied.integerParameter(1));
        switch (applied.type()) {
        case EffectType::AttackIncrease: {
            int bonus = applied.integerParameter(0);
            if (bonus > 0 &&
                attackModifierApplies(modifierType, attackType)) {
                if (modifierType == AttackBonus::Misc) {
                    miscModifierReducer.addIncrease(
                        getEffectSourceKey(applied),
                        static_cast<int>(AttackBonus::Misc),
                        bonus);
                } else {
                    modifierBonus += bonus;
                }
            }
            break;
        }
        case EffectType::AttackDecrease: {
            int penalty = applied.integerParameter(0);
            if (penalty > 0 &&
                attackModifierApplies(modifierType, attackType)) {
                if (modifierType == AttackBonus::Misc) {
                    miscModifierReducer.addDecrease(
                        getEffectSourceKey(applied),
                        static_cast<int>(AttackBonus::Misc),
                        penalty);
                } else {
                    modifierPenalty += penalty;
                }
            }
            break;
        }
        default:
            break;
        }
    }

    for (const auto &[slot, item] : _equipment) {
        if (!item || !equippedItemAppliesToAttack(slot, *item, attackType)) {
            continue;
        }

        int itemBonus = 0;
        int itemPenalty = 0;
        for (const auto &property : item->properties()) {
            if (!item->isPropertyActive(property)) {
                continue;
            }

            int modifier = 0;
            auto propertyType = property.type();
            switch (propertyType) {
            case ItemProperty::EnhancementBonus:
            case ItemProperty::EnhancementBonusVsAlignmentGroup:
            case ItemProperty::EnhancementBonusVsRacialGroup:
            case ItemProperty::AttackBonus:
            case ItemProperty::AttackBonusVsAlignmentGroup:
            case ItemProperty::AttackBonusVsRacialGroup: {
                if (!attackPropertyApplies(
                        propertyType,
                        property.subtype,
                        target)) {
                    continue;
                }
                int value = getCostTableValue(
                    _services,
                    kMeleeCostTable,
                    property.costValue,
                    CostColumn::Value,
                    0);
                if (value <= 0) {
                    continue;
                }
                modifier = value;
                break;
            }
            // Decreased Attack Modifier (41) has no gameplay effect.
            case ItemProperty::AttackPenalty: {
                int value = getCostTableValue(
                    _services,
                    kDecreaseCostTable,
                    property.costValue,
                    CostColumn::Value,
                    0);
                if (value >= 0) {
                    continue;
                }
                modifier = value;
                break;
            }
            default:
                continue;
            }

            if (isHandSpecificAttackModifierSlot(slot)) {
                addAttackModifier(modifier, modifierBonus, modifierPenalty);
            } else if (modifier > 0) {
                itemBonus = std::max(itemBonus, modifier);
            } else {
                itemPenalty = std::max(itemPenalty, -modifier);
            }
        }
        EffectSourceKey source {EffectSourceKind::Item, item->runtimeIncarnation()};
        miscModifierReducer.addIncrease(source, static_cast<int>(AttackBonus::Misc), itemBonus);
        miscModifierReducer.addDecrease(source, static_cast<int>(AttackBonus::Misc), itemPenalty);
    }

    int attackEffectCap = getAttackEffectModifierCap(_game.isTSL());
    modifierBonus += miscModifierReducer.totalIncrease(attackEffectCap);
    modifierPenalty += miscModifierReducer.totalDecrease(attackEffectCap);
    result.effectBonus = std::min(modifierBonus, attackEffectCap) -
                         std::min(modifierPenalty, attackEffectCap);

    if (weapon &&
        weapon->weaponFocusFeat() != FeatType::Invalid &&
        hasEffectiveFeat(weapon->weaponFocusFeat())) {
        result.weaponFocusBonus = 1;
    }

    result.targetingBonus = getTargetingAttackBonus(
        _game.isTSL(),
        weapon && weapon->isRanged(),
        getHighestOwnedFeatRank(*this,
                                FeatType::Targeting1,
                                10));
    result.superiorWeaponFocusBonus =
        getSuperiorWeaponFocusLightsaberBonus(
            _game.isTSL(),
            weapon && weapon->isLightsaber(),
            getHighestOwnedFeatRank(*this,
                                    FeatType::SuperiorWeaponFocusLightsaber1,
                                    3));

    auto rightHandWeapon = getEquippedItem(InventorySlots::rightWeapon);
    auto targetRightHandWeapon = target
                                     ? target->get().getEquippedItem(
                                           InventorySlots::rightWeapon)
                                     : nullptr;
    result.formBonus = target
                           ? getLightsaberFormAttackBonus(
                                 _game.isTSL(),
                                 rightHandWeapon && rightHandWeapon->isLightsaber(),
                                 currentForm(),
                                 targetRightHandWeapon && targetRightHandWeapon->isLightsaber())
                           : 0;

    if (_game.isTSL() && target) {
        bool rank1 = hasEffectiveFeat(FeatType::DualStrike);
        bool rank2 = hasEffectiveFeat(FeatType::ImprovedDualStrike);
        bool rank3 = hasEffectiveFeat(FeatType::MasterDualStrike);
        bool qualifies = qualifiesForDualStrike(true, isPartyMember(), rank1, rank2, rank3);
        bool matchingAlly = false;
        if (qualifies) {
            for (const auto &member : _game.party().members()) {
                if (member.creature && member.creature.get() != this &&
                    member.creature->_combatState.attemptedAttackTarget.resolve().get() == &target->get()) {
                    matchingAlly = true;
                    break;
                }
            }
        }
        result.dualStrikeBonus = getDualStrikeAttackBonus(
            qualifies, matchingAlly, rank1, rank2, rank3);
    }

    if (target) {
        getSituationalAttackBonuses(
            *this,
            target->get(),
            weapon,
            result.closeProximityRangedBonus,
            result.meleeOnRangedBonus);
    }

    const auto twoWeapon = getTwoWeaponAttackPenalty(weapon, offHand);
    result.smallOffhandBonus = twoWeapon.smallOffhandBonus;
    result.dualWieldPenalty = -twoWeapon.penalty - result.smallOffhandBonus;

    if (!offHand) {
        result.duelingBonus = getDuelingBonus();
        switch (result.duelingBonus) {
        case 3:
            result.duelingFeat = FeatType::MasterDueling;
            break;
        case 2:
            result.duelingFeat = FeatType::ImprovedDueling;
            break;
        case 1:
            result.duelingFeat = FeatType::Dueling;
            break;
        default:
            break;
        }
    }

    result.baseAttackBonus = _attributes.getAggregateAttackBonus();
    if (isAutoBalanceEligible(
            _game.isTSL(),
            isPartyMember(),
            _autoBalanceContext.multiplierSet)) {
        const auto row = _services.game.combatTables.autoBalance(
            _autoBalanceContext.multiplierSet);
        result.baseAttackBonus += getAutoBalanceLevelBonus(
            _autoBalanceContext.playerLevelAtSpawn,
            row.toHitMultiplier);
    }
    return result;
}

bool Creature::projectileDefenseEligible(const Creature &shooter, int damageFlags,
                                         const std::shared_ptr<Item> &weapon, bool allowShield, bool &shieldHit, bool &canReturn) const {
    canReturn = true;
    shieldHit = false;
    // A party member knocked out at no vitality defends against nothing.
    const auto state = static_cast<CreatureState>(_effectState);
    const bool canAct = !isDead() && !isTemporarilyDead() &&
                        (_game.isTSL()
                             ? (state == CreatureState::None || state == CreatureState::Confusion ||
                                state == CreatureState::DroidConfused) &&
                                   combatStance() != CombatStance::Meditative
                             : state == CreatureState::None);
    const bool facingShot = std::cos(getFacing() - shooter.getFacing()) <= 0.0f;
    const auto main = getEquippedItem(InventorySlots::rightWeapon);
    const auto off = getEquippedItem(InventorySlots::leftWeapon);
    const bool saber = (main && main->isLightsaber()) || (off && off->isLightsaber());
    const bool defense = hasEffectiveFeat(FeatType::JediDefense) ||
                         hasEffectiveFeat(FeatType::AdvancedJediDefense) || hasEffectiveFeat(FeatType::MasterJediDefense);
    if (defense && canAct) {
        if (!_game.isTSL()) {
            return saber && !_throwParryBlocked && facingShot;
        }
        if (saber && !_throwParryBlocked && facingShot &&
            (damageFlags & static_cast<int>(DamageType::Blaster)))
            return true;
    }
    // A missed shot may strike a shield without passing an opposed defense roll.
    if (allowShield) {
        shieldHit = hasForceShield();
        const auto ammunition = weapon ? weapon->ammunitionType() : nullptr;
        if (shieldHit && ammunition && ammunition->shieldHit) {
            return true;
        }
    }
    if (!_game.isTSL() || !canAct || _throwParryBlocked || !facingShot) {
        return false;
    }
    const bool redirect = _attributes.hasSpell(SpellType::ForceRedirection);
    canReturn = redirect;
    return redirect || _attributes.hasSpell(SpellType::ForceRepulsion);
}

bool Creature::canParryRangedWeapon(const Creature &shooter, int damageFlags, bool &canReturn) const {
    bool shieldHit = false;
    return projectileDefenseEligible(shooter, damageFlags, nullptr, false, shieldHit, canReturn);
}

AttackResultType Creature::resolveRangedMiss(const Creature &shooter, const std::shared_ptr<Item> &weapon) const {
    bool shieldHit = false;
    bool canReturn = false;
    // An empty hand has no blaster damage flags and no ammunition.
    if (!projectileDefenseEligible(shooter, weapon ? weapon->damageFlags() : 0, weapon, true, shieldHit, canReturn)) {
        return AttackResultType::Invalid;
    }
    return shieldHit ? AttackResultType::ShieldHit : AttackResultType::Parried;
}

AttackResultType Creature::resolveRangedDefense(const Creature &shooter, int damageFlags, int attackTotal,
                                                DeflectionBreakdown &record) const {
    bool canReturn = false;
    if (!canParryRangedWeapon(shooter, damageFlags, canReturn)) {
        return AttackResultType::Invalid;
    }
    if (_assuredDeflection) {
        return _assuredReturn ? AttackResultType::Deflected : AttackResultType::Parried;
    }
    const auto weapon = getEquippedItem(InventorySlots::rightWeapon);
    // The record keeps each part of the roll; a feat, power or form part is
    // written only when it applies.
    record.roll = randomInt(1, 20);
    int bonuses = 0;
    if (hasEffectiveFeat(FeatType::MasterJediDefense)) {
        record.jediDefenseFeat = FeatType::MasterJediDefense;
        record.jediDefenseBonus = 6;
        bonuses += record.jediDefenseBonus;
    } else if (hasEffectiveFeat(FeatType::AdvancedJediDefense)) {
        record.jediDefenseFeat = FeatType::AdvancedJediDefense;
        record.jediDefenseBonus = 3;
        bonuses += record.jediDefenseBonus;
    }
    if (_game.isTSL()) {
        if (_attributes.hasSpell(SpellType::ForceRedirection)) {
            record.redirectionBonus = 3;
            bonuses += record.redirectionBonus;
        }
        // The class feat and the lightsaber form need a lightsaber in the
        // right hand; a saber in the left hand only admits the defense.
        const bool saber = weapon && weapon->isLightsaber();
        if (saber && hasEffectiveFeat(FeatType::Deflect)) {
            record.deflectFeatBonus = (_attributes.getClassLevel(ClassType::JediWeaponMaster) + 1) / 2;
            bonuses += record.deflectFeatBonus;
        }
        int formBonus = 0;
        if (saber) {
            switch (_currentForm) {
            case CombatForm::SaberIIMakashi:
                formBonus = -5;
                break;
            case CombatForm::SaberIIISoresu:
                formBonus = 4;
                break;
            case CombatForm::SaberIVAtaru:
                formBonus = -4;
                break;
            case CombatForm::SaberVShien:
                formBonus = 2;
                break;
            case CombatForm::SaberVINiman:
                formBonus = 1;
                break;
            default:
                break;
            }
        }
        if (formBonus != 0) {
            record.form = _currentForm;
            record.formBonus = formBonus;
            bonuses += formBonus;
        }
        // The shooter's highest Precise Shot rank takes two points a rank.
        if (const int preciseShot = getHighestOwnedFeatRank(shooter, FeatType::PreciseShot, 5)) {
            record.shooterFeat = static_cast<FeatType>(static_cast<int>(FeatType::PreciseShot) + preciseShot - 1);
            record.shooterFeatPenalty = -2 * preciseShot;
            bonuses += record.shooterFeatPenalty;
        }
    }
    record.baseAttackBonus = getAttackBonusBreakdown(std::nullopt, weapon, false).baseAttackBonus;
    record.dexterity = getEffectiveAbilityModifier(Ability::Dexterity);
    record.effects = 0;
    for (const auto &effect : effects()) {
        // The deflection amount is the effect's second integer. KotOR adds a
        // decrease's amount as it adds an increase's.
        if (effect.serializedType == SavedEffectTypes::blasterDeflectionIncrease) {
            record.effects += effect.integerParameter(1);
        } else if (effect.serializedType == SavedEffectTypes::blasterDeflectionDecrease) {
            record.effects += _game.isTSL() ? -effect.integerParameter(1) : effect.integerParameter(1);
        }
    }
    const int total = record.roll + bonuses + record.baseAttackBonus + record.dexterity + record.effects;
    record.total = total;
    record.attackTotal = attackTotal;
    if (total < attackTotal) {
        return AttackResultType::Invalid;
    }
    return canReturn && total >= attackTotal + 6 ? AttackResultType::Deflected : AttackResultType::Parried;
}

AttackResultType Creature::resolveTouchDeflection(const Creature &target) {
    const auto action = _currentCombatAction.lock();
    const auto attacks = action ? action->combatAction().combatAttacks() : std::nullopt;
    const auto score = attacks ? attacks->get().rangedRecordScore() : std::nullopt;
    if (!score) {
        return AttackResultType::Invalid;
    }
    // The record's weapon attack type picks the hand whose weapon is tested.
    const int hand = attacks->get().recordWeaponAttackType();
    const auto weapon = hand == 1   ? getEquippedItem(InventorySlots::rightWeapon)
                        : hand == 2 ? getEquippedItem(InventorySlots::leftWeapon)
                                    : nullptr;
    const auto result = target.resolveRangedDefense(*this, weapon ? weapon->damageFlags() : 0, *score,
                                                    attacks->get().recordDeflection());
    if (result != AttackResultType::Invalid) {
        attacks->get().setRecordResult(result);
    }
    return result;
}

int Creature::getAttackBonus(bool offHand) const {
    auto weapon = offHand
                      ? getOffhandAttackWeapon()
                      : getEquippedItem(InventorySlots::rightWeapon);
    return getAttackBonusBreakdown(std::nullopt, weapon, offHand).total();
}

AttackBonusBreakdown Creature::getHandAttackBonusBreakdown(
    bool ranged,
    bool offHand,
    bool doubleBladed,
    bool meleeTwoWeapon) const {

    auto right = getEquippedItem(InventorySlots::rightWeapon);
    const bool tsl = _game.isTSL();
    const auto handType = offHand && (tsl || !ranged) ? AttackBonus::Offhand
                          : tsl && !right             ? AttackBonus::Unarmed
                                                      : AttackBonus::Onhand;
    if (ranged) {
        if (right && !right->isRanged()) {
            return {};
        }
        auto weapon = offHand ? getEquippedItem(InventorySlots::leftWeapon) : right;
        AttackBonusBreakdown result = getAttackBonusBreakdown(std::nullopt, weapon, offHand, handType);
        // A ranged attack always uses Dexterity, and targeting needs no weapon.
        result.strengthModifier = 0;
        result.dexterityModifier = getEffectiveAbilityModifier(Ability::Dexterity);
        result.targetingBonus = getTargetingAttackBonus(
            _game.isTSL(),
            true,
            getHighestOwnedFeatRank(*this, FeatType::Targeting1, 10));
        if (!offHand) {
            const auto twoWeapon = getTwoWeaponAttackPenalty(weapon, false, true);
            result.smallOffhandBonus = twoWeapon.smallOffhandBonus;
            result.dualWieldPenalty = -twoWeapon.penalty - result.smallOffhandBonus;
        }
        return result;
    }
    auto weapon = offHand && !doubleBladed ? getEquippedItem(InventorySlots::leftWeapon) : right;
    if (weapon && weapon->isRanged()) {
        return {};
    }
    AttackBonusBreakdown result = getAttackBonusBreakdown(std::nullopt, weapon, offHand, handType);
    if (!meleeTwoWeapon) {
        result.dualWieldPenalty = 0;
        result.smallOffhandBonus = 0;
    }
    return result;
}

int Creature::getTouchAttackBonus(bool ranged) const {
    // A melee touch ignores two-weapon fighting.
    return getHandAttackBonusBreakdown(ranged, false, false, false).total();
}

static bool isArmorClassEffect(const EffectInstance &effect) {
    return effect.type() == EffectType::ACIncrease || effect.type() == EffectType::ACDecrease;
}

static ArmorClassEffectData armorClassEffectData(const EffectInstance &effect) {
    return {effect.integerParameter(0), effect.integerParameter(1),
            effect.integerParameter(2, static_cast<int>(RacialType::All)),
            effect.integerParameter(3), effect.integerParameter(4),
            effect.integerParameter(5, kPhysicalDamageTypeFlags),
            effect.type() == EffectType::ACDecrease};
}

// The line telling the controlled creature that a natural, armour, shield or
// deflection bonus of the same kind is already in place: this string reference
// plus the kind's armour class bonus type.
static constexpr int kArmorClassBonusPresentStrRef = 1501;

void Creature::addArmorClassEffect(const EffectInstance &effect) {
    if (!isArmorClassEffect(effect)) {
        return;
    }
    const auto data = armorClassEffectData(effect);
    // An unconditional increase of a kind the creature already has tells it
    // so, whether or not the new bonus is larger.
    if (!data.decrease && data.category > ArmorClassEffectData::kDodge &&
        data.category <= ArmorClassEffectData::kDeflection && data.unqualified() &&
        _armorClassCache.increases[data.category] > 0 && _game.party().getLeader().get() == this) {
        _game.addFeedbackMessage(kArmorClassBonusPresentStrRef + data.category);
    }
    _armorClassCache.add(data, _game.isTSL());
}

void Creature::removeArmorClassEffect(const EffectInstance &effect) {
    if (!isArmorClassEffect(effect)) {
        return;
    }
    const auto removed = armorClassEffectData(effect);
    const auto maximum = remainingArmorClassMaximum(_armorClassCursor, effects().size(), removed, [&](size_t i) { return effects()[i].serializedType; }, [&](size_t i) { return armorClassEffectData(effects()[i]); }, [&](size_t i) { return effects()[i].applicationOrder == effect.applicationOrder; });
    _armorClassCache.remove(removed, _game.isTSL(), maximum);
}

void Creature::updateArmorClassEffectCursor() {
    _armorClassCursor.update(effects().size(), [&](size_t i) { return effects()[i].serializedType; });
}

int Creature::defenseDexterityModifier() const {
    int dexterityModifier = getEffectiveAbilityModifier(Ability::Dexterity);
    auto armor = getEquippedItem(InventorySlots::body);
    int armorMaxDexterityBonus = armor
                                     ? getEffectiveArmorMaxDexterityBonus(_game.isTSL(), armor->maxDexterityBonus(),
                                                                          armor->maxDexterityBonusAdjustment())
                                     : -1;
    // Only armour drawn as the body model with an armour class limits Dexterity.
    if (isDebilitated(true)) {
        dexterityModifier = std::min(dexterityModifier, 0);
    } else if (armor && armor->bodyArmorClass() > 0 && armorMaxDexterityBonus >= 0) {
        dexterityModifier = std::min(
            dexterityModifier,
            armorMaxDexterityBonus);
    }
    return dexterityModifier;
}

DefenseBreakdown Creature::getDefenseBreakdown(std::optional<std::reference_wrapper<const Creature>> attacker, int damageFlags, bool touch) const {
    int dexterityModifier = defenseDexterityModifier();
    auto armor = getEquippedItem(InventorySlots::body);
    int armorDefense = armor ? armor->baseDefense() : 0;

    ConditionalArmorClass conditional(_armorClassCache);
    bool attackerSeen = true;
    if (attacker) {
        for (size_t i = _armorClassCursor.position; i < effects().size(); ++i) {
            const auto &effect = effects()[i];
            if (effect.serializedType != SavedEffectTypes::acIncrease && effect.serializedType != SavedEffectTypes::acDecrease) {
                break;
            }
            if (!isArmorClassEffect(effect) || !effect.hasLiveRuntimeSource()) {
                continue;
            }
            conditional.add(armorClassEffectData(effect), static_cast<int>(attacker->get().racialType()),
                            static_cast<int>(attacker->get().alignment()), damageFlags);
        }
        const bool invisible = attacker->get().isInvisibleTo(*this);
        const bool seen = _perception.sees(attacker->get().id());
        attackerSeen = !invisible && seen;
        if (invisible) {
            dexterityModifier = std::min(dexterityModifier, 0);
        } else if (!seen) {
            dexterityModifier = 0;
        }
    }
    const auto ac = armorClassParts(_armorClassCache, conditional, _naturalAC, armorDefense,
                                    0, attacker.has_value(), attackerSeen, touch);
    DefenseBreakdown breakdown;
    breakdown.armor = ac.armour;
    breakdown.dexterity = dexterityModifier;
    breakdown.classDefense = _attributes.getAggregateDefenseBonus(_game.isTSL());
    if (isAutoBalanceEligible(
            _game.isTSL(),
            isPartyMember(),
            _autoBalanceContext.multiplierSet)) {
        const auto row = _services.game.combatTables.autoBalance(
            _autoBalanceContext.multiplierSet);
        breakdown.classDefense += getAutoBalanceLevelBonus(
            _autoBalanceContext.playerLevelAtSpawn,
            row.armorClassMultiplier);
    }
    breakdown.natural = ac.natural;
    breakdown.dodgeAndDeflection = ac.shield + ac.dodgeAndDeflection;
    breakdown.feat = getDuelingBonus();
    // Total Defense, the lightsaber form and Battle Precognition answer an
    // attacker; the general Defense has none of them.
    if (attacker) {
        breakdown.stance = getTotalDefenseBonus(
            _game.isTSL(),
            isInTotalDefense(),
            getHighestTotalDefenseClassLevel(_attributes));
        const bool attackerIsCombatTarget =
            _combatState.attemptedAttackTarget.resolve().get() == &attacker->get() ||
            _combatState.attackTarget.resolve().get() == &attacker->get();
        auto rightHandWeapon = getEquippedItem(InventorySlots::rightWeapon);
        breakdown.form = getLightsaberFormDefenseBonus(
            _game.isTSL(),
            rightHandWeapon && rightHandWeapon->isLightsaber(),
            currentForm(),
            attackerIsCombatTarget);
        // Battle Precognition adds a positive Wisdom modifier while no armour
        // with base Defense is worn.
        if (_game.isTSL() && _attributes.hasSpell(SpellType::BattlePrecognition) && armorDefense == 0) {
            const int wisdom = getEffectiveAbilityModifier(Ability::Wisdom);
            if (wisdom > 0) {
                breakdown.dodgeAndDeflection += wisdom;
            }
        }
    }
    breakdown.debilitationPenalty = isDebilitated(true) ? -4 : 0;
    breakdown.total = 10 + breakdown.armor + breakdown.dexterity +
                      breakdown.classDefense + breakdown.natural + breakdown.dodgeAndDeflection +
                      breakdown.feat + breakdown.stance + breakdown.form + breakdown.debilitationPenalty;
    return breakdown;
}

int Creature::getDefense() const {
    return getDefense(std::nullopt, 0);
}

static int conditioningBonus(const Creature &creature) {
    if (creature.hasEffectiveFeat(FeatType::LightningReflexes)) {
        return 3;
    }
    if (creature.hasEffectiveFeat(FeatType::IronWill)) {
        return 2;
    }
    return creature.hasEffectiveFeat(FeatType::GreatFortitude) ? 1 : 0;
}

static int getClassSavingThrow(
    const CreatureAttributes &attributes,
    int requestedSave) {

    const SavingThrows result = attributes.getAggregateSavingThrows();

    switch (requestedSave) {
    case kFortitudeSavingThrow:
        return result.fortitude;
    case kReflexSavingThrow:
        return result.reflex;
    case kWillSavingThrow:
        return result.will;
    default:
        return 0;
    }
}

int Creature::getSavingThrowEffectBonus(
    SavingThrow savingThrow, SavingThrowType savingThrowType, const OptionalObject &versus) const {
    const int save = static_cast<int>(savingThrow);

    const auto versusCreature = versus ? asCreature(versus->get()) : std::nullopt;
    EffectModifierReducer modifierReducer;
    for (const EffectInstance &applied : effects()) {
        if (!applied.hasLiveRuntimeSource() || !applied.appliesVersus(versusCreature)) {
            continue;
        }
        if (applied.type() != EffectType::SavingThrowIncrease &&
            applied.type() != EffectType::SavingThrowDecrease)
            continue;
        int effectSave = applied.integerParameter(1);
        auto effectType = static_cast<SavingThrowType>(applied.integerParameter(2));
        if (!savingThrowModifierApplies(effectSave, effectType, save, savingThrowType)) {
            continue;
        }
        auto source = getEffectSourceKey(applied);
        int value = applied.integerParameter(0);
        if (applied.type() == EffectType::SavingThrowIncrease) {
            modifierReducer.addIncrease(source, 0, value);
        } else {
            modifierReducer.addDecrease(source, 0, value);
        }
    }

    std::set<uint64_t> visitedItems;
    for (const auto &[slot, item] : _equipment) {
        if (!item ||
            slot == InventorySlots::rightWeapon2 ||
            slot == InventorySlots::leftWeapon2 ||
            !visitedItems.insert(item->runtimeIncarnation()).second) {
            continue;
        }

        EffectSourceKey source {EffectSourceKind::Item, item->runtimeIncarnation()};
        for (const Item::PropertyEntry &property : item->properties()) {
            if (!item->isPropertyActive(property)) {
                continue;
            }

            auto propertyType =
                property.type();
            if (!savingThrowPropertyApplies(
                    propertyType,
                    property.subtype,
                    save)) {
                continue;
            }

            if (propertyType == ItemProperty::ImprovedSavingThrow ||
                propertyType == ItemProperty::ImprovedSavingThrowSpecific) {
                const int value = costCell(_services.game.combatTables.costTableAt(kImprovedSavingThrowCostTable),
                                           property.costValue, CostColumn::Value, 0);
                modifierReducer.addIncrease(source, 0, value);
            } else {
                // The cost table holds the decrease as a negative value.
                const int value = getCostTableValue(_services, kDecreaseCostTable, property.costValue, CostColumn::Value, 0);
                modifierReducer.addDecrease(source, 0, -value);
            }
        }
    }

    const int survival = getSurvivalSavingThrowBonus(
        _game.isTSL(), hasEffectiveFeat(FeatType::Survival), currentHitPoints(), maxHitPoints());
    const int cap = _game.isTSL() ? 60 : 20;
    return getSavingThrowEffectTotal(_game.isTSL(),
                                     modifierReducer.totalIncrease(cap), modifierReducer.totalDecrease(cap), survival);
}

int Creature::getSavingThrowBase(SavingThrow savingThrow) const {
    const int save = static_cast<int>(savingThrow);
    int abilityModifier = 0;
    int baseBonus = 0;
    switch (save) {
    case kFortitudeSavingThrow:
        abilityModifier =
            getEffectiveAbilityModifier(Ability::Constitution);
        baseBonus = _fortBonus;
        break;
    case kReflexSavingThrow:
        abilityModifier = getEffectiveAbilityModifier(Ability::Dexterity);
        baseBonus = _refBonus;
        break;
    case kWillSavingThrow:
        abilityModifier = getEffectiveAbilityModifier(Ability::Wisdom);
        baseBonus = _willBonus;
        break;
    default:
        break;
    }

    int autoBalanceBonus = 0;
    if (isAutoBalanceEligible(
            _game.isTSL(),
            isPartyMember(),
            _autoBalanceContext.multiplierSet)) {
        const auto row = _services.game.combatTables.autoBalance(
            _autoBalanceContext.multiplierSet);
        autoBalanceBonus = getAutoBalanceLevelBonus(
            _autoBalanceContext.playerLevelAtSpawn,
            row.savingThrowMultiplier);
    }

    const int base = getBaseSavingThrowBonus(getClassSavingThrow(_attributes, save),
                                             conditioningBonus(*this), autoBalanceBonus);
    return getDerivedSavingThrowBonus(base, abilityModifier, baseBonus);
}

void Creature::modifySavingThrowBonus(SavingThrow save, int delta) {
    switch (save) {
    case SavingThrow::Fortitude:
        _fortBonus += delta;
        break;
    case SavingThrow::Reflex:
        _refBonus += delta;
        break;
    case SavingThrow::Will:
        _willBonus += delta;
        break;
    default:
        break;
    }
}

int Creature::getSavingThrow(SavingThrow save) const {
    return getSavingThrowStat(getSavingThrowBase(save),
                              getSavingThrowEffectBonus(save, SavingThrowType::All, std::nullopt));
}

SavingThrowBreakdown Creature::getSavingThrowBreakdown(
    SavingThrow save, SavingThrowType type, std::optional<std::reference_wrapper<const Object>> versus) const {
    const int base = getSavingThrowBase(save);
    const int effects = getSavingThrowEffectBonus(save, type, versus);
    const int survival = getSurvivalSavingThrowBonus(
        _game.isTSL(), hasEffectiveFeat(FeatType::Survival), currentHitPoints(), maxHitPoints());
    int room = 0;
    if (_game.isTSL()) {
        room = getRoomSavingThrowModifier(true, _goodEvil,
                                          _game.module()->area()->getRoomForceRating(position()));
    }
    return {base, getSavingThrowModifier(_game.isTSL(), effects, room, survival)};
}

SavingThrowResult Creature::getSavingThrowResult(
    int total, int dc, SavingThrowType type, std::optional<std::reference_wrapper<const Object>> versus) const {
    return resolveSavingThrow(total, dc, [&] {
        int userType = -1;
        if (type == SavingThrowType::Death && spellCast() != SpellType::All) {
            auto spell = _services.game.spells.get(spellCast());
            if (spell) {
                userType = spell->userType;
            }
        }
        const auto immunity = savingThrowImmunity(type, userType);
        return immunity && hasEffectImmunity(*immunity, versus ? asCreature(versus->get()) : std::nullopt);
    });
}

PhysicalDamageBonus Creature::getPhysicalDamageBonus(
    const std::shared_ptr<Item> &weapon,
    bool offHand) const {

    int strengthModifier = getEffectiveAbilityModifier(Ability::Strength);
    int abilityModifier = strengthModifier;
    const bool ranged = weapon && weapon->isRanged();

    // A ranged attack adds no Strength, except a TSL off-hand shot. KotOR adds
    // the whole Strength modifier to every melee hit. TSL halves a positive
    // modifier for the off hand, and raises it by half for a single or
    // double-bladed weapon held with the other hand empty.
    if (!_game.isTSL()) {
        if (ranged) {
            abilityModifier = 0;
        }
    } else if (offHand) {
        if (strengthModifier > 0) {
            abilityModifier = strengthModifier / 2;
        }
    } else if (ranged) {
        abilityModifier = 0;
    } else if (strengthModifier > 0) {
        const auto right = getEquippedItem(InventorySlots::rightWeapon);
        if (right && !getEquippedItem(InventorySlots::leftWeapon) &&
            (right->weaponWield() == WeaponWield::SingleSword ||
             right->weaponWield() == WeaponWield::DoubleBladedSword))
            abilityModifier = 3 * strengthModifier / 2;
    }

    int specialization = 0;
    if (weapon &&
        weapon->weaponSpecializationFeat() != FeatType::Invalid &&
        hasEffectiveFeat(weapon->weaponSpecializationFeat())) {
        specialization = 2;
    }

    PhysicalDamageBonus result {abilityModifier, strengthModifier, specialization};
    if (_game.isTSL() && _furyDamageBonus > 0) {
        result.furyDamage = _furyDamageBonus;
    }
    if (_game.isTSL()) {
        bool ranged = weapon && weapon->isRanged();
        result.combatFeatDamage = getCombatDamageFeatBonus(true, ranged,
                                                           getHighestOwnedFeatRank(*this, FeatType::IncreaseCombatDamage1, 3),
                                                           getHighestOwnedFeatRank(*this, FeatType::IncreaseMeleeDamage1, 3));
        result.preciseShotDamage = getPreciseShotDamageBonus(true, ranged,
                                                             getHighestOwnedFeatRank(*this, FeatType::PreciseShot, 5));
        auto rightHand = getEquippedItem(InventorySlots::rightWeapon);
        result.formDamage = getLightsaberFormDamageBonus(true,
                                                         rightHand && rightHand->isLightsaber(), currentForm());
        if (!weapon) {
            // Only unarmed attacks roll these; the caller drops them otherwise.
            result.unarmedDice209 = getHighestOwnedFeatRank(*this, FeatType::UnarmedDamageThreeRanks1, 3);
            result.unarmedDice212 = getHighestOwnedFeatRank(*this, FeatType::UnarmedDamageEightRanks1, 8);
        }
    }
    return result;
}

int Creature::getPhysicalDamageAutoBalanceFactor() const {
    if (!isAutoBalanceEligible(
            _game.isTSL(),
            isPartyMember(),
            _autoBalanceContext.multiplierSet)) {
        return 1;
    }

    const auto row = _services.game.combatTables.autoBalance(
        _autoBalanceContext.multiplierSet);
    return getAutoBalanceDamageFactor(
        _autoBalanceContext.playerLevelAtSpawn,
        row.damageMultiplier);
}

int Creature::getMassiveCriticalDamage(
    const std::shared_ptr<Item> &weapon,
    bool criticalHit) const {

    if (!criticalHit) {
        return 0;
    }

    // An unarmed hit takes the property of the gloves worn.
    const auto sourceItem = weapon ? weapon : getEquippedItem(InventorySlots::hands);
    if (!sourceItem) {
        return 0;
    }

    // KotOR takes the first Massive Criticals property; TSL adds them all.
    int damage = 0;
    for (const auto &property : sourceItem->properties()) {
        if (!sourceItem->isPropertyActive(property) ||
            property.type() != ItemProperty::MassiveCriticals) {
            continue;
        }

        // Massive Criticals selects dice by NumDice, not the increase-code
        // threshold. Keep that property contract separate from bonus effects.
        const auto cost = _services.game.combatTables.damageCost(property.costValue);
        const int numDice = cost.numDice;
        DamageModifier modifier {property.costValue, numDice, 0, property.costValue,
                                 weapon ? getPrimaryDamageType(sourceItem->damageFlags()) : DamageType::Bludgeoning};
        modifier.usesDice = numDice != 0;
        if (modifier.usesDice) {
            modifier.flat = 0;
            modifier.die = cost.die;
        }
        damage += rollDamageModifier(modifier, 1);
        if (!_game.isTSL()) {
            break;
        }
    }

    return damage;
}

/** An attack's rolled damage modifiers. */
struct RolledDamageModifiers {
    /** The hand-specific increases and decreases by damage type. */
    std::map<int, int> typedIncreases;
    std::map<int, int> typedDecreases;
    int miscellaneousIncrease {0};
    int miscellaneousDecrease {0};
    /** The hand-specific dice at their lowest face less at their highest. */
    int lowestDifference {0};
};

/**
 * Rolls the damage modifiers of one attack of \p attackHand against
 * \p target. Hand-specific dice roll once a critical multiple, \p dice
 * choosing a rolled or fixed face; hand-specific flat amounts and every
 * decrease count once, and a decrease kept for a hand counts its flat value.
 * A miscellaneous amount, its dice rolled once at their highest face for a
 * fixed roll, counts towards the miscellaneous totals.
 */
static RolledDamageModifiers rollDamageModifiers(
    const Creature &creature,
    const OptionalCreature &target,
    DamageBonusHand attackHand,
    int criticalMultiplier,
    DamageDiceRoll dice) {

    auto &services = creature.services();
    const bool tsl = creature.game().isTSL();
    const auto merged = mergeDamageModifiers(
        services, tsl, collectDamageModifiers(creature, target, attackHand),
        [](const DamageModifierEntry &entry) { return entry.applies && entry.flags != 0; });

    RolledDamageModifiers result;
    for (const auto &entry : merged.handIncreases) {
        const auto type = getDamageModifierType(tsl, entry.flags);
        const auto modifier = getDamageModifier(services, entry.code, type);
        result.typedIncreases[static_cast<int>(type)] += rollHandDamageModifier(modifier, criticalMultiplier, dice);
        if (modifier.usesDice && dice == DamageDiceRoll::Highest) {
            result.lowestDifference += modifier.numDice - modifier.numDice * modifier.die;
        }
    }
    for (const auto &[type, code] : merged.handDecreases) {
        if (code > 0) {
            result.typedDecreases[type] = code;
        }
    }
    auto rollMiscellaneous = [&](const DamageModifierEntry &entry) {
        const auto modifier = getDamageModifier(services, entry.code, getDamageModifierType(tsl, entry.flags));
        return rollDamageModifier(modifier, 1, dice == DamageDiceRoll::Rolled ? DamageDiceRoll::Rolled : DamageDiceRoll::Highest);
    };
    for (const auto &entry : merged.miscellaneousIncreases) {
        result.miscellaneousIncrease += rollMiscellaneous(entry);
    }
    for (const auto &entry : merged.miscellaneousDecreases) {
        result.miscellaneousDecrease += rollMiscellaneous(entry);
    }
    return result;
}

/** The cap on the summed damage increases, and on the decreases, of an attack's total. */
static int getDamageModifierTotalCap(bool tsl) {
    return tsl ? kMaximumElementalDamageBonus : kMaximumDamageEffectModifier;
}

int Creature::addPhysicalDamageModifiers(
    DamagePacket &damage,
    DamageBreakdown &breakdown,
    std::optional<std::reference_wrapper<const Creature>> target,
    const std::shared_ptr<Item> &weapon,
    bool offHand,
    int criticalMultiplier,
    DamageDiceRoll dice,
    std::optional<AttackBonus> handType) const {

    const bool emptyCreatureWeapons = !_game.isTSL() && isEmptyCreatureWeaponAttack(*this, weapon);
    const auto attackHand = handType                                ? static_cast<DamageBonusHand>(*handType)
                            : emptyCreatureWeapons                  ? DamageBonusHand::MainHand
                            : !weapon                               ? DamageBonusHand::Unarmed
                            : isCreatureWeaponAttack(*this, weapon) ? DamageBonusHand::CreatureWeapon
                            : offHand                               ? DamageBonusHand::OffHand
                                                                    : DamageBonusHand::MainHand;
    const auto rolled = rollDamageModifiers(*this, target, attackHand, criticalMultiplier, dice);

    // The hand-specific amounts join the hit by type. KotOR's miscellaneous
    // amounts never reach the hit; TSL adds them to its base damage.
    auto addDamage = [&](int amount, DamageType type) {
        damage.addBonus(amount, type);
        breakdown.addRawDamage(amount, type);
    };
    for (const auto &[type, amount] : rolled.typedIncreases) {
        if (amount > 0) {
            addDamage(amount, static_cast<DamageType>(type));
        }
    }
    for (const auto &[type, amount] : rolled.typedDecreases) {
        addDamage(-amount, static_cast<DamageType>(type));
    }
    int miscellaneous = 0;
    if (_game.isTSL()) {
        miscellaneous = rolled.miscellaneousIncrease - rolled.miscellaneousDecrease;
        damage.addPhysicalBase(miscellaneous, getPrimaryDamageType(damage.damageFlags()));
        breakdown.otherSpecialBonus += miscellaneous;
    }
    return miscellaneous;
}

bool Creature::isWeaponEffective(std::optional<std::reference_wrapper<Object>> versus, bool offHand) const {
    // Nothing mitigates a hit without a target, and a hit deals at least one point.
    if (!versus) {
        return true;
    }
    if (versus->get().plotFlag()) {
        return false;
    }
    return getMaximumWeaponDamage(versus, offHand) + getMaximumElementalDamageBonus(versus->get()) > 0;
}

int Creature::getMaximumWeaponDamage(std::optional<std::reference_wrapper<Object>> versus, bool offHand) const {
    // The attack weapon is the right-hand weapon, else the left creature
    // weapon when both hands are empty and a creature weapon is worn, else
    // the gloves. The off hand uses whatever the left hand holds.
    const auto rightHand = getEquippedItem(InventorySlots::rightWeapon);
    const auto leftHand = getEquippedItem(InventorySlots::leftWeapon);
    const bool creatureWeapons = !rightHand && !leftHand &&
                                 (getEquippedItem(InventorySlots::cWeaponL) ||
                                  getEquippedItem(InventorySlots::cWeaponR) ||
                                  getEquippedItem(InventorySlots::cWeaponB));
    const auto attackWeapon = creatureWeapons ? getEquippedItem(InventorySlots::cWeaponL)
                              : rightHand     ? rightHand
                                              : getEquippedItem(InventorySlots::hands);
    const auto weapon = offHand && leftHand ? leftHand : attackWeapon;
    const bool unarmed = !weapon ||
                         weapon->itemType() == Item::kGauntletsItemType ||
                         weapon->itemType() == Item::kForearmBandsItemType;
    const auto damageWeapon = unarmed ? nullptr : weapon;
    const bool tsl = _game.isTSL();

    // Weapon dice are taken at their highest; unarmed damage is still rolled.
    int base;
    if (unarmed) {
        base = randomInt(1, getOrdinaryUnarmedDamageDie(tsl, size())) * getPhysicalDamageAutoBalanceFactor();
    } else {
        const auto dice = creatureWeapons ? weapon->monsterDamageDice()
                          : !rightHand    ? std::make_pair(1, getOrdinaryUnarmedDamageDie(tsl, size()))
                                          : std::make_pair(weapon->numDice(), weapon->dieToRoll());
        base = weapon->hasActiveProperty(ItemProperty::NoDamage) ? 0 : dice.first * dice.second;
    }

    const auto versusCreature = versus ? asCreature(versus->get()) : std::nullopt;
    const int damageFlags = attackWeapon ? attackWeapon->damageFlags() : static_cast<int>(DamageType::Bludgeoning);
    // Of the item and effect modifiers only the miscellaneous amount counts,
    // with every die at its highest face.
    DamagePacket modifiers;
    DamageBreakdown breakdown;
    modifiers.setDamageFlags(damageFlags);
    const int miscellaneous = addPhysicalDamageModifiers(
        modifiers, breakdown, versusCreature, damageWeapon, offHand, 1, DamageDiceRoll::Highest);

    const int total = base + getPhysicalDamageBonus(damageWeapon, offHand).total() + miscellaneous;
    // Nothing mitigates a hit without a target, and a hit deals at least one point.
    if (!versus) {
        return std::max(1, total);
    }
    DamagePacket damage(calculateDamagePower(versusCreature, damageWeapon, offHand));
    damage.addPhysicalBase(total, getPrimaryDamageType(damageFlags));
    damage.setDamageFlags(damageFlags);
    damage.setCutsDoors(weapon && weapon->cutsDoors());
    damage.resolvePhysical(versus->get(), *this, true);
    return damage.resolvedDamage();
}

void Creature::evaluateWithEquipment(
    std::map<int, std::shared_ptr<Item>> equipment,
    const std::function<void()> &evaluate) {
    // The creature answers from the given equipment while the evaluation
    // runs; its effects, items and presentation stay as they are.
    struct Restore {
        std::map<int, std::shared_ptr<Item>> &current;
        std::map<int, std::shared_ptr<Item>> &held;
        ~Restore() { current.swap(held); }
    };
    _equipment.swap(equipment);
    Restore restore {_equipment, equipment};
    evaluate();
}

int Creature::getMaximumElementalDamageBonus(Object &versus) const {
    const bool tsl = _game.isTSL();
    const auto versusCreature = asCreature(versus);
    // KotOR counts the main hand's modifiers; TSL those of the main hand
    // with a right-hand weapon, else those of an unarmed attack.
    const auto attackHand = !tsl || getEquippedItem(InventorySlots::rightWeapon) ? DamageBonusHand::MainHand
                                                                                 : DamageBonusHand::Unarmed;
    // A modifier without a physical type counts whoever it is against and
    // whichever attack it is for; one that mixes in a physical type must apply.
    const auto merged = mergeDamageModifiers(
        _services, tsl, collectDamageModifiers(*this, versusCreature, attackHand),
        [](const DamageModifierEntry &entry) {
            const int physical = static_cast<int>(DamageType::Physical);
            return entry.flags != 0 && ((entry.applies && entry.flags > physical) || (entry.flags & physical) == 0);
        });

    // Every die counts at its highest face. Typed amounts meet immunity and
    // resistance of their own type, miscellaneous ones those of base damage.
    std::map<int, int> typedIncreases;
    for (const auto &entry : merged.handIncreases) {
        const auto type = getDamageModifierType(tsl, entry.flags);
        typedIncreases[static_cast<int>(type)] +=
            rollDamageModifier(getDamageModifier(_services, entry.code, type), 1, DamageDiceRoll::Highest);
    }
    int increase = 0;
    for (const auto &[type, amount] : typedIncreases) {
        if (amount > 0) {
            increase += simulateDamageBonusMitigation(versus, *this, type, amount);
        }
    }
    // Only a miscellaneous increase of a type other than physical counts, and
    // in KotOR only one of dice.
    for (const auto &entry : merged.miscellaneousIncreases) {
        const auto type = getDamageModifierType(tsl, entry.flags);
        const auto modifier = getDamageModifier(_services, entry.code, type);
        if (static_cast<int>(type) < static_cast<int>(DamageType::Universal) || (!tsl && !modifier.usesDice)) {
            continue;
        }
        increase += simulateDamageBonusMitigation(
            versus, *this, kBaseDamageFlag, rollDamageModifier(modifier, 1, DamageDiceRoll::Highest));
    }
    // Only the hand-specific decreases count.
    int decrease = 0;
    for (const auto &[type, code] : merged.handDecreases) {
        if (code > 0) {
            decrease += code;
        }
    }
    const int cap = getDamageModifierTotalCap(tsl);
    return std::min(increase, cap) - std::min(decrease, cap);
}

void Creature::getDamageRange(
    const std::shared_ptr<Item> &weapon,
    bool offHand,
    bool includeModifiers,
    int &minimum,
    int &maximum) const {

    const bool tsl = _game.isTSL();
    const int strength = getEffectiveAbilityModifier(Ability::Strength);
    const PhysicalDamageBonus bonus = getPhysicalDamageBonus(weapon, offHand);
    if (!weapon) {
        // An unarmed hit adds the whole Strength modifier and, in TSL, the
        // unarmed feat dice.
        const auto lowest = rollUnarmedFeatDamage(
            bonus.unarmedDice209, bonus.unarmedDice212, [](int) { return 1; });
        const auto highest = rollUnarmedFeatDamage(
            bonus.unarmedDice209, bonus.unarmedDice212, [](int die) { return die; });
        minimum = 1 + strength + lowest.total();
        maximum = getOrdinaryUnarmedDamageDie(tsl, size()) + strength + highest.total();
    } else {
        // A ranged weapon shows no Strength. TSL halves Strength for the off
        // hand and raises a positive modifier by half for a single or
        // double-bladed weapon while the left hand is empty.
        int ability = 0;
        if (!weapon->isRanged()) {
            ability = strength;
            if (tsl && offHand) {
                ability = strength / 2;
            } else if (tsl && strength > 0 && !getEquippedItem(InventorySlots::leftWeapon) &&
                       (weapon->weaponWield() == WeaponWield::SingleSword ||
                        weapon->weaponWield() == WeaponWield::DoubleBladedSword)) {
                ability = 3 * strength / 2;
            }
        }
        const int flat = ability + bonus.weaponSpecialization + bonus.combatFeatDamage + bonus.preciseShotDamage;
        minimum = weapon->numDice() + flat;
        maximum = weapon->numDice() * weapon->dieToRoll() + flat;
    }

    if (includeModifiers) {
        // The item and effect modifiers of one hit against no target, every
        // die at its highest face: the hand-specific and miscellaneous
        // increases, capped, less the decreases, capped. The lowest bound
        // takes the hand-specific dice at their lowest face. They are those of
        // the equipped hand, whichever weapon is shown; in KotOR the main
        // hand's whatever it holds.
        const auto handWeapon = offHand ? getEquippedOffhandWeapon() : getEquippedItem(InventorySlots::rightWeapon);
        const auto attackHand = offHand              ? DamageBonusHand::OffHand
                                : !tsl || handWeapon ? DamageBonusHand::MainHand
                                                     : DamageBonusHand::Unarmed;
        const auto rolled = rollDamageModifiers(*this, std::nullopt, attackHand, 1, DamageDiceRoll::Highest);
        int increase = rolled.miscellaneousIncrease;
        for (const auto &[type, amount] : rolled.typedIncreases) {
            if (amount > 0) {
                increase += amount;
            }
        }
        int decrease = rolled.miscellaneousDecrease;
        for (const auto &[type, amount] : rolled.typedDecreases) {
            decrease += amount;
        }
        const int cap = getDamageModifierTotalCap(tsl);
        const int total = std::min(increase, cap) - std::min(decrease, cap);
        minimum += total + rolled.lowestDifference;
        maximum += total;
    }

    // Each bound shows at least one point.
    minimum = std::max(minimum, 1);
    maximum = std::max(maximum, 1);
}

bool Creature::hasSilentMove() const {
    if (!_game.isTSL()) {
        return false;
    }
    for (const auto &[slot, item] : _equipment) {
        if (!item || slot == InventorySlots::rightWeapon2 || slot == InventorySlots::leftWeapon2) {
            continue;
        }
        for (const auto &property : item->properties()) {
            if (property.type() == ItemProperty::DampenSound &&
                item->isPropertyActive(property))
                return true;
        }
    }
    return false;
}

void Creature::onEventSignalled(const std::string &name) {
    if (name == "hit") {
        presentSwingHit();
        return;
    }
    if (name == "swingshort" || name == "swinglong" || name == "swingtwirl") {
        playSwingSound(name, 3);
        return;
    }
    if (name == "clash") {
        playSwingSound(name, 2);
        return;
    }
    if (name == "hitparry") {
        playSwingSound("parry", 2);
        return;
    }
    if (name == "draw_weapon") {
        setLightsabersPowered(true, true);
        if (!_combatState.active) {
            _lightsaberIdlePowerDownPending = true;
            _lightsaberIdlePowerDownTimer.reset(8.0f);
        }
        return;
    }
    if (name == "snd_hitground") {
        playFallSound();
        return;
    }
    if (_footstepType == -1 || name != "snd_footstep") {
        return;
    }
    // A creature outside an area makes no step.
    const auto area = spatialArea();
    if (!area) {
        return;
    }
    // A stealthed creature steps softly.
    std::shared_ptr<FootstepTypeSounds> sounds(_services.game.footstepSounds.get(_stealthMode ? 0 : _footstepType));
    if (!sounds) {
        return;
    }
    // A step sounds one of three variants of the sound of the surface under
    // the creature, named by the surface; a surface without a name is silent.
    const Surface &surface = _services.game.surfaces.getSurface(area->get().getSurfaceMaterial(_position));
    if (surface.name.empty()) {
        return;
    }
    const auto cell = sounds->cells.find(boost::to_lower_copy(surface.name) + std::to_string(randomInt(0, 2)));
    auto clip = cell != sounds->cells.end() ? _services.resource.audioClips.get(cell->second) : nullptr;
    // TSL's Dampen Sound silences a stealthed creature's steps.
    if (clip && _stealthMode && hasSilentMove()) {
        return;
    }
    if (clip) {
        _audioSourceFootstep = _services.audio.mixer.play(
            std::move(clip),
            AudioType::Sound,
            1.0f,
            false,
            _position);
        // The step's pitch strays at random by the footstep type's offset: on
        // one step in two up by as much, on the other down by half as much.
        const float variance = std::clamp(sounds->pitchOffset, 0.0f, 1.0f);
        if (_audioSourceFootstep && variance > 0.0f) {
            const bool up = randomInt(0, 1) == 1;
            const float stray = randomFloat(0.0f, variance);
            _audioSourceFootstep->setPitch(up ? 1.0f + stray : 1.0f - 0.5f * stray);
        }
    }
}

void Creature::giveGold(int amount) {
    _gold += amount;
}

void Creature::takeGold(int amount) {
    _gold -= amount;
}

glm::vec3 Creature::computeSteeringForce(const Uniwalk &uni, const glm::vec3 &next, float dt) {
    glm::vec3 desiredForce = glm::normalize(next - _position);
    glm::vec3 keepoutForce = computeKeepoutForce(uni, _position);

    // If we're not making progress - move in a random direction and
    // hope. If we wander off too far, the path will be recalculated.
    const bool noProgress = _previousPosition && glm::length2(_position - *_previousPosition) < 0.0001;
    if (!_stuckTimer.elapsed() || noProgress) {
        // Try to unstuck for some time even if we're moving
        // again. Otherwise desiredForce kicks in again next frame.
        if (_stuckTimer.elapsed()) {
            _stuckTimer.reset(1.0f);
            _stuckForce =
                glm::normalize(glm::vec3 {
                    randomFloat(-1.0f, 1.0f),
                    randomFloat(-1.0f, 1.0f),
                    randomFloat(-1.0f, 1.0f),
                });
        } else {
            _stuckTimer.update(dt);
        }
        desiredForce = glm::vec3 {0.0f, 0.0f, 0.0f};
    } else {
        _stuckForce = glm::vec3 {0.0f, 0.0f, 0.0f};
    }
    _previousPosition = _position;

    glm::vec3 combinedForce = desiredForce + 0.1f * keepoutForce + _stuckForce;

    drawdebug::pushId("computeSteeringForce");
    drawdebug::pushId(_id);
    drawdebug::clear();

    if (isShowPathEnabled()) {
        drawdebug::line(_position, _position + desiredForce, 0x00BFFFFF, 0.02);
        drawdebug::line(_position, _position + keepoutForce, 0xDDA0DDFF, 0.02);
        drawdebug::line(_position, _position + _stuckForce, 0xF08080FF, 0.02);
        drawdebug::line(_position, _position + combinedForce, 0xADFF2FFF, 0.02);
    }

    drawdebug::popId();
    drawdebug::popId();

    return combinedForce;
}

// The next pace of a walk that went at \p pace: full speed until the ground
// left is within half a second of travel, then the square root of the share
// of that stretch still left, less the frame's time; never more than \p gain a
// second above the pace it had, and never below a tenth.
static float nextWalkPace(float pace, float remaining, float speed, float dt, float gain) {
    const float brake = speed * kWalkBrakeTime;
    float target = 1.0f;
    if (brake >= remaining) {
        const float fraction = remaining / brake;
        target = (fraction < kWalkBrakeEndFraction ? kMinimumWalkPace : std::sqrt(fraction)) - dt;
    }
    if (pace <= 1.0f) {
        target = std::min(target, pace + gain * dt);
    }
    return std::max(kMinimumWalkPace, target);
}

// The ground a path walk has left from a point: the leg to the path point
// being walked to, then the legs between the points after it, on the ground
// plan. With \p heightCounts the first leg also spans the point's height, as
// if the path lay at zero height.
static float remainingPathLength(const std::vector<glm::vec3> &points, uint32_t next, const glm::vec3 &from, bool heightCounts) {
    const glm::vec2 first {glm::vec2(points[next]) - glm::vec2(from)};
    float length = glm::length(glm::vec3(first, heightCounts ? from.z : 0.0f));
    for (size_t i = next + 1; i < points.size(); ++i) {
        length += glm::distance(glm::vec2(points[i]), glm::vec2(points[i - 1]));
    }
    return length;
}

bool Creature::navigateTo(const glm::vec3 &dest, bool run, float distance, float dt,
                          std::optional<std::reference_wrapper<const Object>> pathTarget,
                          bool straight) {
    _navigationFailed = false;
    const bool clicksTrigger = pathTarget && clicksTriggerInReach(pathTarget->get());
    if (isMovementRestricted() && !clicksTrigger) {
        setMovementType(MovementType::None);
        return false;
    }

    auto module = _game.module();
    if (!module || !module->area()) {
        // Navigation without a module does not make sense. This is only useful
        // for unit tests.
        return true;
    }

    Area &area = *module->area();
    Pathfinder &pf = area.pathfinder();

    // A creature within reach of the destination but without a clear line to
    // it, past a wall or anything that blocks sight, walks all the way onto
    // it, and keeps to that until the walk ends. The line is tested just above
    // the ground.
    if (!_path || !_walksOntoPoint) {
        const glm::vec3 lift {0.0f, 0.0f, kGroundLineHeight};
        _walksOntoPoint = distance > 0.0f && glm::distance2(_position, dest) < distance * distance &&
                          (area.isWalkLineBlocked(*this, _position, dest) ||
                           !area.isEyeLineClear(_position + lift, dest + lift, std::nullopt, std::nullopt));
    }
    if (_walksOntoPoint) {
        distance = 0.0f;
    }

    // A walk heading for a trigger that is not a mine ends once the walker is
    // within use range of it, as it sets out or on the way, and the trigger
    // hears that as a click.
    if (clicksTrigger) {
        setMovementType(MovementType::None);
        clearPath();
        static constexpr int kClickedEvent = static_cast<int>(SavedSignalEventType::Clicked);
        auto clicked = _game.getObjectById(pathTarget->get().id());
        _game.queueScriptEvent(*clicked, *this, Event(kClickedEvent, {}, {}, {}, {_game.getObjectById(_id)}));
        return true;
    }

    // Stop if we reached the destination.
    float distToDest2 = getSquareDistanceTo(glm::vec2(dest));
    if (distToDest2 <= distance * distance) {
        setMovementType(Creature::MovementType::None);
        clearPath();
        return true;
    }

    float eps2 = std::min(0.5f * 0.5f, distance * distance);
    if (_path && getSquareDistanceTo(getLastPathPoint(pf, *_path)) < eps2) {
        // Reached the last point, but not reached the destination. Find another
        // path.
        releasePath(pf, *_path);
        _path = std::nullopt;
    }

    // A walk onto a point that sets out within a tenth of a metre of it ends
    // at once, the creature stepping onto the point if it fits there.
    if (!_path && distance <= kNoMoveRange && getSquareDistanceTo(dest) <= kArrivalSnapDistance2) {
        if (area.isSafeLocationPoint(dest, *this)) {
            setPosition(dest);
            area.determineObjectRoom(*this);
        }
        setMovementType(MovementType::None);
        return true;
    }

    if (_path && !updatePath(pf, *_path, position())) {
        // Lost the path and cannot recalculate.
        releasePath(pf, *_path);
        _path = std::nullopt;
        _navigationFailed = true;
        return false;
    }

    // Advance on path.
    if (_path) {
        const glm::vec3 nextPoint = getNextPathPoint(pf, *_path);
        const glm::vec3 lastPoint = getLastPathPoint(pf, *_path);
        // A walk onto its point heads straight for the end of its path once
        // that is the next point, and ends on the step that reaches it.
        const bool endsOnPoint = distance <= kNoMoveRange && nextPoint == lastPoint;
        const glm::vec2 toLast {glm::vec2(lastPoint) - glm::vec2(_position)};
        if (endsOnPoint && glm::dot(toLast, toLast) <= kNoMoveRange * kNoMoveRange) {
            setMovementType(MovementType::None);
            clearPath();
            return true;
        }
        // With the clock stopped the walk keeps its path but takes no step.
        if (dt <= 0.0f) {
            return false;
        }
        glm::vec3 dir;
        glm::vec3 stepLimit = dest;
        if (endsOnPoint) {
            dir = glm::vec3(glm::normalize(toLast), 0.0f);
        } else if (followsPathPoints(pf, *_path)) {
            // Explicit points are walked straight, stopping on each on the
            // way; the last is walked to as the end of any path is.
            dir = glm::vec3(glm::normalize(glm::vec2(nextPoint - _position)), 0.0f);
            if (nextPoint != lastPoint) {
                stepLimit = glm::vec3(nextPoint.x, nextPoint.y, _position.z);
            }
        } else {
            glm::vec3 steeringForce = computeSteeringForce(pf.uni, nextPoint, dt);
            _pathVelocity += steeringForce * dt;

            float maxSpeed = 0.3f;
            float speed = glm::min(glm::length(_pathVelocity), maxSpeed);
            _pathVelocity = glm::normalize(_pathVelocity) * speed;

            dir = glm::normalize(_pathVelocity);
        }

        const float moveSpeed = run && !isRunLimited() ? runSpeed() : walkSpeed();
        // The ground left from a point along the path is never less than the
        // straight line to its end, so it is only measured along the path
        // once that line comes within the braking stretch.
        const glm::vec2 end {lastPoint};
        uint32_t next = 0;
        std::vector<glm::vec3> points;
        const auto remaining = [&](const glm::vec3 &from, bool heightCounts) {
            const float straight = glm::length(glm::vec3(end - glm::vec2(from), heightCounts ? from.z : 0.0f));
            if (straight > moveSpeed * kWalkBrakeTime) {
                return straight;
            }
            if (points.empty()) {
                points = pathPoints(pf, *_path, next);
            }
            return remainingPathLength(points, next, from, heightCounts);
        };
        // A walk picks up speed from the pace its last walk ended at and slows
        // toward the end of its path; each step goes at the mean of the pace
        // it starts and ends at. A party member following the leader keeps an
        // even pace, and a creature that cannot move keeps its pace.
        float pacedStep = moveSpeed * dt;
        const auto current = getCurrentAction();
        if (moveSpeed > 0.0f && !(current && current->type() == ActionType::FollowLeader)) {
            const float pace = nextWalkPace(_walkPace, remaining(_position, true), moveSpeed, dt, kWalkPaceGain);
            pacedStep *= (_walkPace + pace) * 0.5f;
            _walkPace = pace;
        }
        const float step = std::min(pacedStep, endsOnPoint ? glm::length(toLast) : getDistanceTo(stepLimit));
        const glm::vec3 stepEnd {_position.x + dir.x * step, _position.y + dir.y * step, _position.z};
        const glm::vec2 stepDir {glm::normalize(glm::vec2(dir))};
        const auto test = area.testDirectLine(*this, _position, stepEnd);
        const Area::DirectLine line = test.result;
        const auto &door = test.door;

        // A wall in the way of this step ends the walk where the creature
        // stands. When the wall is a closed door, the creature runs its
        // blocked script, unless the player controls it.
        if (line == Area::DirectLine::Blocked) {
            _previousPosition.reset();
            setMovementType(MovementType::None);
            clearPath();
            if (door && door->get().state() == DoorState::Closed && !door->get().isOpening() &&
                _game.party().getLeader().get() != this) {
                runBlockedScript(door->get().id());
            }
            return true;
        }

        // A creature in the way of this step is first pushed aside, when this
        // creature can push it; a step that is then clear is walked.
        // Otherwise the creature is walked into.
        if (line == Area::DirectLine::CreatureBlocked) {
            auto other = _game.getObjectById<Creature>(test.blocker->get().id());
            if (!pushAside(*other, stepEnd, _position, stepDir) ||
                area.findBlockingCreature(*this, _position, stepEnd)) {
                const Bump bump = bumpIntoCreature(other, pf, stepDir);
                // No step is taken this frame, so the next has none to compare
                // with for progress.
                _previousPosition.reset();
                if (bump == Bump::WalkEnded) {
                    setMovementType(MovementType::None);
                    clearPath();
                    return true;
                }
                return false;
            }
        }

        // The creature walks the step it tested. Its walk and run clips keep
        // a pace of their own, which follows each step it actually takes.
        const glm::vec3 before {_position};
        advanceOnPath(_position + dir * step, dir, run, distance, dt);
        if (_position != before) {
            _clipPace = nextWalkPace(_clipPace, remaining(_position, false), moveSpeed, dt, kClipPaceGain);
        }
        if (endsOnPoint && glm::distance2(glm::vec2(_position), glm::vec2(lastPoint)) <= kNoMoveRange * kNoMoveRange) {
            setMovementType(MovementType::None);
            clearPath();
            return true;
        }
        return false;
    }

    // Find a path and start following it.
    _avoidance.bumps = 0;
    _path = createPath(pf, position(), dest);
    if (!_path) {
        _navigationFailed = true;
        return false;
    }
    if (straight) {
        setPathPoints(pf, *_path, {_position, dest}, 1);
    }
    _pathVelocity = {0.0f, 0.0f, 0.0f};
    _attemptedMovementTarget = pathTarget ? _game.getObjectById(pathTarget->get().id()) : nullptr;
    resetBlockingCreature();

    return navigateTo(dest, run, distance, dt, pathTarget, straight);
}

Creature::Bump Creature::bumpIntoCreature(const std::shared_ptr<Creature> &blocker, Pathfinder &pf, const glm::vec2 &stepDir) {
    // Every blocked step counts, and the sixth since the path was planned
    // ends the walk.
    if (_avoidance.bumps++ >= kMaxPathBumps) {
        return Bump::WalkEnded;
    }
    // Walking into the object the path heads for is arriving.
    if (blocker->id() == attemptedMovementTarget()) {
        return Bump::WalkEnded;
    }
    // A creature already looked at, still where it stood, ends the walk.
    if (blocker->id() == blockingCreature() &&
        glm::distance2(_avoidance.blockerPosition, blocker->position()) <= kSameBlockerDistance2) {
        return Bump::WalkEnded;
    }

    bool side = _avoidance.side;
    const Avoidance avoidance = plotPathAroundCreature(blocker, pf, stepDir, side);
    if (avoidance == Avoidance::Failed) {
        // No way round at all. When this is the player's character, the
        // player is told.
        resetBlockingCreature();
        _game.party().setCombatMessage(*this, kStrRefCannotGetAround);
        return Bump::WalkEnded;
    }
    // The walker goes round the next creature the other way first. The
    // creature walked into takes the walker's new preference when it faces
    // against the walker, the opposite one otherwise.
    _avoidance.side = side;
    const float facing = blocker->getFacing();
    const glm::vec2 blockerDir {-glm::sin(facing), glm::cos(facing)};
    blocker->_avoidance.side = glm::dot(stepDir, blockerDir) < 0.0f ? side : !side;
    return avoidance == Avoidance::EndInHex ? Bump::WalkEnded : Bump::Detoured;
}

Creature::Avoidance Creature::plotPathAroundCreature(const std::shared_ptr<Creature> &blocker, Pathfinder &pf, const glm::vec2 &stepDir, bool &side) {
    _avoidance.blocker = blocker;
    _avoidance.blockerPosition = blocker->position();

    // A creature the walker regards as an enemy makes the walker run its
    // path-failure script. Unless the script took away the walker's
    // commandability, there is no way round it.
    if (_services.game.reputes.getIsEnemy(*blocker, *this)) {
        _avoidance.foundEnemy = blocker;
        _game.scriptRunner().run(kPathFailScript, _id);
        if (isCommandable()) {
            return Avoidance::Failed;
        }
    }

    // The creature is ringed by a hexagon whose edges keep both personal
    // spaces and a little more from its centre. Each corner stands on the
    // ground, the room under it looked for around the blocker's height, or at
    // zero height over no room.
    Area &area = *_game.module()->area();
    const float blockerHeight = blocker->position().z;
    const auto groundAt = [&area, blockerHeight](const glm::vec2 &point) {
        return area.groundHeight(glm::vec3(point, blockerHeight), Area::GroundFace::Locating);
    };
    const float inradius = blocker->creaturePersonalSpace() + creaturePersonalSpace() + kAvoidanceSpace;
    const AvoidanceHex hex = computeAvoidanceHex(blocker->position(), glm::vec3(stepDir, 0.0f), inradius, groundAt);

    assert(_path);
    uint32_t next = 0;
    std::vector<glm::vec3> points = pathPoints(pf, *_path, next);
    // A walk that ends inside the hexagon stops here.
    const glm::vec3 &last = points.back();
    if (isPointInAvoidanceHex(hex, glm::vec3(last.x, last.y, 0.0f))) {
        return Avoidance::EndInHex;
    }
    AvoidanceCrossing entry;
    AvoidanceCrossing exit;
    if (!findAvoidanceEntryAndExit(hex, points, next, entry, exit)) {
        return Avoidance::Failed;
    }

    // Both ways round are tried, the preferred one first. A clear way scores
    // two, one where a creature stands scores one, one blocked otherwise
    // nothing; a tie goes to the first. The object the path heads for never
    // stands in the way.
    const auto pathTarget = _attemptedMovementTarget.resolve();
    const auto targetCreature = asCreature(pathTarget);
    struct Way {
        std::vector<glm::vec3> points;
        std::optional<std::reference_wrapper<const Creature>> standing;
        int score {0};
    };
    const auto planWay = [&](bool left) {
        Way way;
        way.points = findAvoidanceWay(hex, entry, exit, left);
        for (auto &point : way.points) {
            point.z = groundAt(glm::vec2(point));
        }
        for (size_t i = 1; i < way.points.size(); ++i) {
            const auto test = area.testDirectLine(*this, way.points[i - 1], way.points[i], targetCreature);
            if (test.result == Area::DirectLine::Clear) {
                continue;
            }
            if (test.result == Area::DirectLine::CreatureBlocked) {
                way.standing = test.blocker;
            }
            way.score = way.standing ? 1 : 0;
            return way;
        }
        way.score = 2;
        return way;
    };
    const Way first = planWay(side);
    side = !side;
    const Way second = planWay(side);
    const Way *chosen = nullptr;
    if (first.score < second.score) {
        chosen = &second;
    } else if (first.score > 0) {
        chosen = &first;
    }

    if (!chosen) {
        _avoidance.foundEnemy.reset();
        _game.scriptRunner().run(kPathFailScript, _id);
        return Avoidance::Failed;
    }
    insertAvoidanceWay(points, next, entry.index, exit.index, chosen->points);
    setPathPoints(pf, *_path, std::move(points), next);
    // A hostile creature standing on the way round is found too, and the
    // walker runs its path-failure script, but walks on.
    if (chosen->standing && _services.game.reputes.getIsEnemy(chosen->standing->get(), *this)) {
        _avoidance.foundEnemy = _game.getObjectById<Creature>(chosen->standing->get().id());
        _game.scriptRunner().run(kPathFailScript, _id);
    }
    return Avoidance::Planned;
}

bool Creature::canPushAside(const Creature &other) const {
    if (!_game.isTSL() || _game.globalFade().dialogPending()) {
        return false;
    }
    if (_game.party().getLeader().get() != this) {
        return false;
    }
    return other.isPartyMember() || other.isPuppet();
}

bool Creature::pushAside(Creature &other, glm::vec3 a, glm::vec3 b, const glm::vec2 &moverDir) {
    static constexpr float kDirectionEpsilon = 1e-9f;
    if (!canPushAside(other)) {
        return false;
    }

    // The spot looked for stands off the line through a and b, on the side
    // the other creature already stands, far enough out for both creatures
    // and a little more. A creature right on the line goes along +x.
    Area &area = *_game.module()->area();
    const glm::vec3 standing {other.position()};
    const glm::vec3 line {b - a};
    const float length2 = glm::dot(line, line);
    const float along = length2 > 0.0f ? glm::dot(standing - a, line) / length2 : 0.0f;
    const glm::vec3 nearest {a + line * along};
    const glm::vec3 offset {standing - nearest};
    const float offsetLength = glm::length(offset);
    const glm::vec3 away = offsetLength < kDirectionEpsilon ? glm::vec3(1.0f, 0.0f, 0.0f) : offset / offsetLength;
    const float reach = other.creaturePersonalSpace() + kPushAsideSpace + creaturePersonalSpace();

    // This creature stands on its line, nearest the other, while the spot is
    // chosen, and goes back where it was once done.
    const glm::vec3 saved {_position};
    setPosition(nearest);
    bool handled = false;
    const auto spot = area.computeSafeLocation(nearest + away * reach, kPushAsideSpotRadius, other, true);
    if (spot && area.testDirectLine(other, standing, *spot).result != Area::DirectLine::Blocked) {
        // A creature already set on a move is left to it.
        const bool moving = std::any_of(other.actions().nodes.begin(), other.actions().nodes.end(),
                                        [](const OrdinaryActionQueue::Node &node) {
                                            return node->actionId == kMoveToPointActionId || node->actionId == kMoveToObjectActionId;
                                        });
        if (!moving) {
            other.clearAllActions(true);
            other.addActionOnTop(_game.newAction<MoveToPointAction>(*spot, true, 0.0f));
            other.planStraightPath(*spot);
        }
        handled = true;
    } else if (auto behind = area.computeSafeLocationInDirection(b, -glm::vec3(moverDir, 0.0f), kPushBehindRadius, other, true)) {
        other.setPosition(*behind);
        area.determineObjectRoom(other);
        handled = true;
    }
    setPosition(saved);
    return handled;
}

void Creature::planStraightPath(const glm::vec3 &point) {
    clearPath();
    Pathfinder &pf = _game.module()->area()->pathfinder();
    _avoidance.bumps = 0;
    _path = createPath(pf, _position, point);
    if (!_path) {
        return;
    }
    setPathPoints(pf, *_path, {_position, point}, 1);
    _attemptedMovementTarget.reset();
    resetBlockingCreature();
}

Creature::UseRange Creature::useRange(const Object &target, bool ignorePreciseUse) const {
    UseRange use {target.position(), _personalSpace};
    switch (target.type()) {
    case ObjectType::Creature: {
        const auto &creature = static_cast<const Creature &>(target);
        use.range = _game.isTSL()
                        ? kCreatureUseRange
                        : _creaturePersonalSpace + creature._creaturePersonalSpace + kCreatureUseSpacing;
        break;
    }
    case ObjectType::Trigger: {
        const auto &trigger = static_cast<const Trigger &>(target);
        if (trigger.isAreaTransition()) {
            break;
        }
        use.point = trigger.nearestPoint(_position);
        use.range += kTriggerUseSpacing;
        break;
    }
    case ObjectType::Placeable: {
        const auto &placeable = static_cast<const Placeable &>(target);
        use.point = placeable.nearestActionPoint(_position);
        auto area = spatialArea();
        if (placeable.isPreciseUse() && !ignorePreciseUse && area && area->get().isSafeLocationPoint(use.point, *this)) {
            use.range = kPreciseUseRange;
        } else {
            use.range += kObjectUseSpacing;
        }
        if (placeable.isCorpse()) {
            use.range += kCorpseUseSpacing;
        }
        break;
    }
    case ObjectType::Door: {
        const auto &door = static_cast<const Door &>(target);
        use.point = door.nearestActionPoint(_position, false);
        // The point stands on the ground of the door's area, or at zero height
        // over no room.
        auto doorArea = door.spatialArea();
        use.point.z = doorArea ? doorArea->get().groundHeight(use.point, Area::GroundFace::Locating) : 0.0f;
        auto area = spatialArea();
        // In TSL, while a door saber is anywhere in the queue, every door is
        // used from exactly a metre.
        const bool doorSaberQueued = _game.isTSL() && std::any_of(actions().begin(), actions().end(), [](const auto &action) {
                                         return action->type() == ActionType::DoorSaber;
                                     });
        if (doorSaberQueued) {
            use.range = kDoorSaberUseRange;
        } else if (door.isPreciseUse() && door.isLocked() && !ignorePreciseUse && area &&
                   area->get().isSafeLocationPoint(use.point, *this)) {
            use.range = kPreciseUseRange;
        } else {
            use.range += kObjectUseSpacing;
        }
        break;
    }
    default:
        break;
    }
    return use;
}

bool Creature::isInUseRange(const Object &target, float extra) const {
    auto area = target.spatialArea();
    if (!area || !sharesSpatialArea(target)) {
        return false;
    }
    if (auto *trigger = dyn_cast<Trigger>(&target); trigger && trigger->isAreaTransition()) {
        return trigger->isIn(_position);
    }
    const UseRange use = useRange(target);
    const glm::vec3 lift {0.0f, 0.0f, kUseLineHeight};
    if (!area->get().isEyeLineClear(_position + lift, use.point + lift, *this, target)) {
        return false;
    }
    const glm::vec2 offset {glm::vec2(use.point) - glm::vec2(_position)};
    const float reach = use.range + extra + kUseRangeAllowance;
    return glm::dot(offset, offset) <= reach * reach;
}

void Creature::UseApproach::follow(const UseRange &current) {
    if (fixed) {
        return;
    }
    if (current.range == use.range && glm::length(current.point - use.point) <= kUsePointTolerance) {
        return;
    }
    use = current;
    fixed = true;
}

bool Creature::navigateToUse(const Object &target, float extra, float dt, bool run, bool followsPoint, bool targetsPath) {
    std::optional<std::reference_wrapper<const Object>> pathTarget;
    if (targetsPath) {
        pathTarget = target;
    }
    if (isInUseRange(target, extra)) {
        setMovementType(MovementType::None);
        clearPath();
        _useApproach.reset();
        return true;
    }
    const UseRange use = useRange(target);
    if (target.type() != ObjectType::Door && target.type() != ObjectType::Placeable) {
        navigateTo(use.point, run, use.range + extra, dt, pathTarget);
        return false;
    }
    if (!_useApproach || _useApproach->target != target.id()) {
        _useApproach = UseApproach {target.id(), use, !followsPoint};
    } else {
        _useApproach->follow(use);
    }
    // Arriving ends the walk; the next one sets out afresh.
    if (navigateTo(_useApproach->use.point, run, _useApproach->use.range + extra, dt, pathTarget)) {
        _useApproach.reset();
    }
    return false;
}

void Creature::advanceOnPath(const glm::vec3 &dest, const glm::vec3 &dir, bool run, float distance, float dt) {
    // A creature limited from running walks the whole way.
    run = run && !isRunLimited();
    setMovementType(run ? Creature::MovementType::Run : Creature::MovementType::Walk);
    _game.module()->area()->moveCreature(
        _game.getObjectById<Creature>(_id), dir, run, dt, getDistanceTo(dest));
}

void Creature::clearPath() {
    if (!_path) {
        return;
    }

    Pathfinder &pf = _game.module()->area()->pathfinder();
    releasePath(pf, *_path);
    _path = std::nullopt;
}

std::string Creature::getAnimationName(AnimationType anim) const {
    std::string result;
    switch (anim) {
    case AnimationType::LoopingPause:
        return getPauseAnimation(false);
    case AnimationType::LoopingPause2:
        return getFirstIfCreatureModel("cpause2", "pause2");
    case AnimationType::LoopingListen:
        return "listen";
    case AnimationType::LoopingMeditate:
        return "meditate";
    case AnimationType::LoopingTalkNormal:
        return "tlknorm";
    case AnimationType::LoopingTalkPleading:
        return "tlkplead";
    case AnimationType::LoopingTalkForceful:
        return "tlkforce";
    case AnimationType::LoopingTalkLaughing:
        return getFirstIfCreatureModel("", "tlklaugh");
    case AnimationType::LoopingTalkSad:
        return "tlksad";
    case AnimationType::LoopingPauseTired:
        return "pausetrd";
    case AnimationType::LoopingFlirt:
        return "flirt";
    case AnimationType::LoopingUseComputer:
        return "usecomplp";
    case AnimationType::LoopingDance:
        return "dance";
    case AnimationType::LoopingDance1:
        return "dance1";
    case AnimationType::LoopingHorror:
        return "horror";
    case AnimationType::LoopingDeactivate:
        return getFirstIfCreatureModel("", "deactivate");
    case AnimationType::LoopingSpasm:
        return getFirstIfCreatureModel("cspasm", "spasm");
    case AnimationType::LoopingSleep:
        return "sleep";
    case AnimationType::LoopingProne:
        return "prone";
    case AnimationType::LoopingPause3:
        return getFirstIfCreatureModel("", "pause3");
    case AnimationType::LoopingWeld:
        return "weld";
    case AnimationType::LoopingDead:
        return getDeadAnimation();
    case AnimationType::LoopingTalkInjured:
        return "talkinj";
    case AnimationType::LoopingListenInjured:
        return "listeninj";
    case AnimationType::LoopingTreatInjured:
        return "treatinjlp";
    case AnimationType::LoopingUnlockDoor:
        return "unlockdr";
    case AnimationType::LoopingClosed:
        return "closed";
    case AnimationType::LoopingStealth:
        return "stealth";
    case AnimationType::FireForgetHeadTurnLeft:
        return getFirstIfCreatureModel("chturnl", "hturnl");
    case AnimationType::FireForgetHeadTurnRight:
        return getFirstIfCreatureModel("chturnr", "hturnr");
    case AnimationType::FireForgetSalute:
        return "salute";
    case AnimationType::FireForgetBow:
        return "bow";
    case AnimationType::FireForgetGreeting:
        return "greeting";
    case AnimationType::FireForgetTaunt:
        return getFirstIfCreatureModel("ctaunt", "taunt");
    case AnimationType::FireForgetDiveRoll:
        // Row 567 of K2 animations.2da, the one animation the shipped scripts
        // ever ask PlayOverlayAnimation for. K1 has neither the clip nor the
        // constant.
        return getFirstIfCreatureModel("cdiveroll", "diveroll");
    case AnimationType::FireForgetVictory1:
        return getFirstIfCreatureModel("cvictory", "victory");
    case AnimationType::FireForgetInject:
        return "inject";
    case AnimationType::FireForgetUseComputer:
        return "usecomp";
    case AnimationType::FireForgetPersuade:
        return "persuade";
    case AnimationType::FireForgetActivate:
        return "activate";
    case AnimationType::LoopingChoke:
        return "choke";
    case AnimationType::FireForgetTreatInjured:
        return "treatinj";
    case AnimationType::FireForgetOpen:
        return "open";
    case AnimationType::LoopingReady:
        return getReadyAnimation();

    case AnimationType::LoopingWorship:
    case AnimationType::LoopingGetLow:
    case AnimationType::LoopingGetMid:
    case AnimationType::LoopingPauseDrunk:
    case AnimationType::LoopingDeadProne:
    case AnimationType::LoopingKneelTalkAngry:
    case AnimationType::LoopingKneelTalkSad:
    case AnimationType::LoopingCheckBody:
    case AnimationType::LoopingSitAndMeditate:
    case AnimationType::LoopingSitChair:
    case AnimationType::LoopingSitChairDrink:
    case AnimationType::LoopingSitChairPazak:
    case AnimationType::LoopingSitChairComp1:
    case AnimationType::LoopingSitChairComp2:
    case AnimationType::LoopingRage:
    case AnimationType::LoopingChokeWorking:
    case AnimationType::LoopingMeditateStand:
    case AnimationType::FireForgetPauseScratchHead:
    case AnimationType::FireForgetPauseBored:
    case AnimationType::FireForgetVictory2:
    case AnimationType::FireForgetVictory3:
    case AnimationType::FireForgetThrowHigh:
    case AnimationType::FireForgetThrowLow:
    case AnimationType::FireForgetCustom01:
    case AnimationType::FireForgetForceCast:
    case AnimationType::FireForgetScream:
    default:
        debug("CreatureAnimationResolver: unsupported animation type: " + std::to_string(static_cast<int>(anim)));
        return "";
    }
}

std::string Creature::getDieAnimation() const {
    if (_modelType == ModelType::Creature) {
        return "cdie";
    }
    return _deathPose == DeathPose::Dead1 ? "die1" : "die";
}

// Animation IDs of the die clips of the first two dead poses.
static constexpr int kDieAnimationId = 10221;
static constexpr int kDie1AnimationId = 10222;

// Each dead pose is led into from the loop the creature stood in, as any loop
// is. The first two go off with the appearance's death visual, and their die
// clip, when that is the lead-in, plays at once. Any other lead-in waits
// behind the dead pose, which is the loop from now on, so it survives; the
// pose shows once a running one-shot ends, and the lead-in follows it.
void Creature::playDieTransition(int leftLoopId) {
    const int deadId = _deathPose == DeathPose::Dead1   ? kDead1AnimationId
                       : _deathPose == DeathPose::Dead3 ? kDead3AnimationId
                                                        : kDeadAnimationId;
    if (_deathPose != DeathPose::Dead3) {
        presentDeathVisual();
        const LoopTransition transition = loopTransition(deadId, leftLoopId, currentAnimationId(), _modelType != ModelType::Creature);
        if (transition.clip == kDieAnimationId || transition.clip == kDie1AnimationId) {
            playAnimation(getDieAnimation());
            return;
        }
    }
    _animFireForget = false;
    storeLoop(getDeadAnimation(), AnimationProperties(), true, AnimationSource {deadId});
    _animDirty = true;
    updateModelAnimation();
    queueLoopTransition(deadId, leftLoopId, 1.0f);
}

// In TSL a character model's knock-down fall waits in the one-shot queue. KotOR
// shows no fall, nor does a creature model.
void Creature::showKnockdownFall(int animationId) {
    if (!_game.isTSL() || _modelType == ModelType::Creature) {
        return;
    }
    addFireForgetAnimation("g1y1", false, AnimationSource {animationId});
}

// The visual is presentation only. It goes off where its node on the body
// stands, or a metre above the feet when the body has no such node. A
// creature with no body, or outside an area, shows none.
void Creature::presentDeathVisual() {
    if (!_deathVisual) {
        return;
    }
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model || !_spatialArea) {
        return;
    }
    auto *node = model->getNodeByName(_deathVisualNode);
    _spatialArea->presentVisualAt(*_deathVisual, node ? node->origin() : _position + glm::vec3(0.0f, 0.0f, 1.0f));
}

std::string Creature::getFirstIfCreatureModel(std::string creatureAnim, std::string elseAnim) const {
    return _modelType == Creature::ModelType::Creature ? std::move(creatureAnim) : std::move(elseAnim);
}

std::string Creature::getDeadAnimation() const {
    if (_modelType == ModelType::Creature) {
        return "cdead";
    }
    if (_deathPose == DeathPose::Dead1) {
        return "dead1";
    }
    if (_deathPose == DeathPose::Dead3) {
        return "dead3";
    }
    return "dead";
}

// Caught in a whirlwind, lying prone or in a knock-back loop.
bool Creature::isInFallenLoop() const {
    const auto active = getActiveAnimationName();
    return active == "whirlwind" || active == "prone" || active == "ckdbcklp";
}

// The damage flinch follows the hands' weapon class with its own rows: the
// double-bladed class shows the dual flinch and the dual class the
// double-bladed one, heavy weapons the rifle one, and empty hands the creature
// flinch (TSL complex unarmed its own). A stun baton or a class-0 hand has
// none and holds the pause clip as a pose instead.
std::string Creature::getDamageFlinchAnimation() const {
    if (_modelType == ModelType::Creature) {
        return "cdamages";
    }
    int weaponClass = getReadyWeaponClass();
    switch (weaponClass) {
    case 0:
    case 1:
        return "";
    case 3:
        weaponClass = 4;
        break;
    case 4:
        weaponClass = 3;
        break;
    case 8:
        return _game.isTSL() && hasEffectiveFeat(FeatType::ComplexUnarmedAnims) ? "g10d1" : "cdamages";
    case 9:
        weaponClass = 7;
        break;
    default:
        break;
    }
    return getAnimationName(CombatAnimation::Damage, static_cast<CreatureWieldType>(weaponClass), 1);
}

std::string Creature::getReadyAnimation() const {
    if (_modelType == Creature::ModelType::Creature) {
        return "creadyr";
    }
    int weaponClass = getReadyWeaponClass();
    // Class 0 has no ready pose of its own and shows the pause instead.
    if (weaponClass == static_cast<int>(CreatureWieldType::None)) {
        return "pause1";
    }
    // Heavy weapons stand ready with the rifle pose, and empty hands with the
    // complex unarmed feat use its own.
    if (weaponClass == 9) {
        weaponClass = 7;
    } else if (weaponClass == 8 && _game.isTSL() && hasEffectiveFeat(FeatType::ComplexUnarmedAnims)) {
        weaponClass = 10;
    }
    return str(boost::format("g%dr1") % weaponClass);
}

bool Creature::showsClassZeroReady() const {
    return _animationSource.id == kReadyAnimationId && _modelType != ModelType::Creature &&
           getReadyWeaponClass() == static_cast<int>(CreatureWieldType::None);
}

std::string Creature::getPauseAnimation(bool injured) const {
    // Combat state, or an attack or cast under way, shows the ready loop.
    if (_combatState.active || isAttackingOrCasting(_actions)) {
        return getReadyAnimation();
    }
    if (_modelType == Creature::ModelType::Creature) {
        return "cpause1";
    }
    return injured ? "pauseinj" : "pause1";
}

// The weapon class of the hands: empty hands are 8 and a left item alone is 0.
// With both hands filled only the left item's wield counts; otherwise the
// right item's wield does. A wield with no class gives 0.
int Creature::getReadyWeaponClass() const {
    const auto right = getEquippedItem(InventorySlots::rightWeapon);
    const auto left = getEquippedItem(InventorySlots::leftWeapon);
    if (!right) {
        return left ? 0 : 8;
    }
    if (left) {
        switch (left->weaponWield()) {
        case WeaponWield::BlasterPistol:
            return 6;
        case WeaponWield::SingleSword:
            return 4;
        default:
            return 0;
        }
    }
    switch (right->weaponWield()) {
    case WeaponWield::StunBaton:
        return 1;
    case WeaponWield::SingleSword:
        return 2;
    case WeaponWield::DoubleBladedSword:
        return 3;
    case WeaponWield::BlasterPistol:
        return 5;
    case WeaponWield::BlasterRifle:
        return 7;
    case WeaponWield::HeavyWeapon:
        return 9;
    default:
        return 0;
    }
}

std::optional<int> Creature::getRelativeWeaponSize(const Item &weapon) const {
    if (_size == CreatureSize::Invalid ||
        weapon.weaponSize() == CreatureSize::Invalid) {
        return std::nullopt;
    }

    int relativeSize = static_cast<int>(weapon.weaponSize()) -
                       static_cast<int>(_size);
    if (relativeSize < -2 || relativeSize > 1) {
        return std::nullopt;
    }
    return relativeSize;
}

Creature::TwoWeaponPenalty Creature::getTwoWeaponAttackPenalty(
    const std::shared_ptr<Item> &weapon,
    bool offHand,
    bool rangedSheet) const {

    auto mainHand = getEquippedItem(InventorySlots::rightWeapon);
    if (!mainHand || mainHand->weaponType() == WeaponType::None) {
        return {};
    }

    auto offHandWeapon = getOffhandAttackWeapon();
    bool doubleBladed = offHandWeapon == mainHand &&
                        mainHand->weaponWield() == WeaponWield::DoubleBladedSword;
    if (!offHandWeapon || offHandWeapon->weaponType() == WeaponType::None) {
        return {};
    }

    int superiorRank = getHighestOwnedFeatRank(*this,
                                               FeatType::SuperiorWeaponFocusTwoWeapon1,
                                               3);

    if (offHand) {
        if (!isAttackWeapon(weapon, *offHandWeapon)) {
            return {};
        }

        int penalty = 10;
        if (hasEffectiveFeat(FeatType::AdvancedDoubleWeaponFighting)) {
            penalty = 2;
        } else if (hasEffectiveFeat(FeatType::DoubleWeaponFighting)) {
            penalty = 4;
        } else if (hasEffectiveFeat(FeatType::Ambidexterity)) {
            penalty = 6;
        }
        return {penalty - getSuperiorTwoWeaponPenaltyReduction(_game.isTSL(), true, superiorRank), 0};
    }

    if (!isAttackWeapon(weapon, *mainHand)) {
        return {};
    }

    // An attack eases the penalty for an off-hand weapon one size below the
    // wielder's, or a double-bladed weapon.
    bool balanced = doubleBladed;
    if (!balanced) {
        // On the ranged sheet a main-hand size outside the span counts as
        // smaller.
        if (rangedSheet) {
            const auto size = getRelativeWeaponSize(*mainHand);
            balanced = !size || *size <= -1;
        } else {
            balanced = getRelativeWeaponSize(*offHandWeapon) == -1;
        }
    }

    const int smallOffhandBonus = balanced ? 2 : 0;
    int penalty = balanced ? 4 : 6;
    if (hasEffectiveFeat(FeatType::AdvancedDoubleWeaponFighting)) {
        penalty -= 4;
    } else if (hasEffectiveFeat(FeatType::DoubleWeaponFighting)) {
        penalty -= 2;
    }
    return {penalty - getSuperiorTwoWeaponPenaltyReduction(_game.isTSL(), false, superiorRank), smallOffhandBonus};
}

int Creature::getDuelingBonus() const {
    auto mainHand = getEquippedItem(InventorySlots::rightWeapon);
    bool leftHandEquipped = static_cast<bool>(
        getEquippedItem(InventorySlots::leftWeapon));
    if (!qualifiesForDueling(
            _game.isTSL(),
            static_cast<bool>(mainHand),
            mainHand ? mainHand->weaponWield() : WeaponWield::None,
            leftHandEquipped)) {
        return 0;
    }

    if (hasEffectiveFeat(FeatType::MasterDueling)) {
        return 3;
    }
    if (hasEffectiveFeat(FeatType::ImprovedDueling)) {
        return 2;
    }
    return hasEffectiveFeat(FeatType::Dueling) ? 1 : 0;
}

// A character walks and runs by the weapon class of its hands: a sword, a
// double-bladed weapon or a pair of melee weapons only while the hands are
// powered, a rifle or heavy weapon always, in or out of combat.
std::string Creature::getWalkAnimation() const {
    if (_modelType == Creature::ModelType::Creature) {
        return "cwalk";
    }

    switch (static_cast<CreatureWieldType>(getReadyWeaponClass())) {
    case CreatureWieldType::SingleSword:
        return _poweredHands.any() ? "walkss" : "walk";
    case CreatureWieldType::DoubleBladedSword:
        return _poweredHands.any() ? "walkst" : "walk";
    case CreatureWieldType::DualSwords:
        return _poweredHands.any() ? "walkds" : "walk";
    case CreatureWieldType::BlasterRifle:
    case CreatureWieldType::HeavyWeapon:
        return "walkrf";
    default:
        return "walk";
    }
}

// Creature models have no sneaking walk of their own and keep their walk.
std::string Creature::getStealthWalkAnimation() const {
    return getFirstIfCreatureModel("cwalk", "stealth");
}

std::string Creature::getRunAnimation() const {
    if (_modelType == Creature::ModelType::Creature) {
        return "crun";
    }

    switch (static_cast<CreatureWieldType>(getReadyWeaponClass())) {
    case CreatureWieldType::SingleSword:
        return _poweredHands.any() ? "runss" : "run";
    case CreatureWieldType::DoubleBladedSword:
        return _poweredHands.any() ? "runst" : "run";
    case CreatureWieldType::DualSwords:
        return _poweredHands.any() ? "runds" : "run";
    case CreatureWieldType::BlasterRifle:
    case CreatureWieldType::HeavyWeapon:
        return "runrf";
    default:
        return "run";
    }
}

std::string Creature::getHeadTalkAnimation() const {
    return "talk";
}

std::string Creature::getAnimationName(CombatAnimation anim, CreatureWieldType wield, int variant) const {
    switch (anim) {
    case CombatAnimation::Draw:
        // Heavy weapons draw as rifles; empty hands draw only in KotOR, with
        // the equip clip. Hands without a weapon class have no draw.
        if (wield == CreatureWieldType::None) {
            return "";
        }
        if (wield == CreatureWieldType::HeavyWeapon) {
            wield = CreatureWieldType::BlasterRifle;
        }
        if (wield == CreatureWieldType::HandToHand || wield == CreatureWieldType::HandToHandComplex) {
            return getFirstIfCreatureModel("", _game.isTSL() ? "" : "equip");
        }
        return getFirstIfCreatureModel("", formatCombatAnimation("g%dw%d", wield, 1));
    case CombatAnimation::Ready:
        return getFirstIfCreatureModel("creadyr", formatCombatAnimation("g%dr%d", wield, 1));
    case CombatAnimation::Attack:
        return getFirstIfCreatureModel("g0a1", formatCombatAnimation("g%da%d", wield, variant));
    case CombatAnimation::Damage:
        return getFirstIfCreatureModel("cdamages", formatCombatAnimation("g%dd%d", wield, variant));
    case CombatAnimation::Dodge:
        return getFirstIfCreatureModel("cdodgeg", formatCombatAnimation("g%dg%d", wield, variant));
    case CombatAnimation::MeleeAttack:
        return getFirstIfCreatureModel("m0a1", formatCombatAnimation("m%da%d", wield, variant));
    case CombatAnimation::MeleeDamage:
        return getFirstIfCreatureModel("cdamages", formatCombatAnimation("m%dd%d", wield, variant));
    case CombatAnimation::MeleeDodge:
        return getFirstIfCreatureModel("cdodgeg", formatCombatAnimation("m%dg%d", wield, variant));
    case CombatAnimation::CinematicMeleeAttack:
        return formatCombatAnimation("c%da%d", wield, variant);
    case CombatAnimation::CinematicMeleeDamage:
        return formatCombatAnimation("c%dd%d", wield, variant);
    case CombatAnimation::CinematicMeleeParry:
        return formatCombatAnimation("c%dp%d", wield, variant);
    case CombatAnimation::BlasterAttack:
        return getFirstIfCreatureModel("b0a1", formatCombatAnimation("b%da%d", wield, variant));
    default:
        return "";
    }
}

std::string Creature::getActiveAnimationName() const {
    auto model = std::dynamic_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model) {
        return "";
    }

    const auto base = model->baseAnimationChannel();
    return base ? base->get().anim->name() : "";
}

std::shared_ptr<ModelSceneNode> Creature::buildModel() {
    std::string modelName(getBodyModelName());
    if (modelName.empty()) {
        return nullptr;
    }
    std::shared_ptr<Model> model(_services.resource.models.get(modelName));
    if (!model) {
        return nullptr;
    }
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    auto sceneNode = sceneGraph.newModel(*model, ModelUsage::Creature);
    sceneNode->setDrawDistance(_game.options().graphics.drawDistance);
    sceneNode->setAnimationEventListener(*this);

    return sceneNode;
}

// The Force Sight glow of a creature's body: blue and more opaque toward
// good, red toward evil, pale and faint around neutral.
static glm::vec4 forceGlowColor(int goodEvil) {
    if (goodEvil > 60) {
        const float t = static_cast<float>(goodEvil - 60) / 40.0f;
        return glm::vec4(0.01f * t + 0.65f * (1.0f - t), 0.05f * t + 0.65f * (1.0f - t), 1.0f, t);
    }
    if (goodEvil >= 50) {
        const float t = static_cast<float>(50 - goodEvil) / 10.0f;
        const float pale = 0.65f * (1.0f - t) + t;
        return glm::vec4(pale, pale, 0.9f, t);
    }
    if (goodEvil > 40) {
        const float t = static_cast<float>(goodEvil - 40) / 10.0f;
        const float pale = 0.65f * (1.0f - t) + t;
        return glm::vec4(1.0f, pale, pale, t);
    }
    const float t = static_cast<float>(40 - goodEvil) / 40.0f;
    return glm::vec4(1.0f, 0.05f * t + 0.65f * (1.0f - t), 0.01f * t + 0.65f * (1.0f - t), t);
}

void Creature::finalizeModel(ModelSceneNode &body) {
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);

    // Under Force Sight the body glows in the colour of the alignment it has
    // when the model is built; a droid's body is drawn see-through instead.
    if (_race == RacialType::Droid) {
        body.setForceSightStyle(ModelSceneNode::ForceSightStyle::Translucent);
    } else {
        body.setForceSightStyle(ModelSceneNode::ForceSightStyle::Glow, forceGlowColor(_goodEvil));
    }

    // Body texture

    if (!_envmap.empty()) {
        if (_envmap == "default") {
            body.setEnvironmentMap(&_services.graphics.textureRegistry.get(TextureName::defaultCubemapRgb));
        } else {
            body.setEnvironmentMap(_services.resource.textures.get(_envmap, TextureUsage::EnvironmentMap).get());
        }
    }
    std::string bodyTextureName(getBodyTextureName());
    if (!bodyTextureName.empty()) {
        std::shared_ptr<Texture> texture(_services.resource.textures.get(bodyTextureName, TextureUsage::MainTex));
        if (texture) {
            body.setMainTexture(texture.get());
        }
    }

    // Mask

    std::shared_ptr<Model> maskModel;
    std::string maskModelName(getMaskModelName());
    if (!maskModelName.empty()) {
        maskModel = _services.resource.models.get(maskModelName);
    }

    // Head

    std::string headModelName(getHeadModelName());
    if (!headModelName.empty()) {
        std::shared_ptr<Model> headModel(_services.resource.models.get(headModelName));
        if (headModel) {
            std::shared_ptr<ModelSceneNode> headSceneNode(sceneGraph.newModel(*headModel, ModelUsage::Creature));
            body.attach(g_headHookNode, *headSceneNode);
            if (maskModel) {
                auto maskSceneNode = sceneGraph.newModel(*maskModel, ModelUsage::Equipment);
                headSceneNode->attach(g_maskHookNode, *maskSceneNode);
            }
        }
    }

    // Right weapon

    std::string rightWeaponModelName(getWeaponModelName(InventorySlots::rightWeapon));
    if (!rightWeaponModelName.empty()) {
        std::shared_ptr<Model> weaponModel(_services.resource.models.get(rightWeaponModelName));
        if (weaponModel) {
            std::shared_ptr<ModelSceneNode> weaponSceneNode(sceneGraph.newModel(*weaponModel, ModelUsage::Equipment));
            body.attach(g_rightHandNode, *weaponSceneNode);
            if (weaponModel->classification() == MdlClassification::lightsaber) {
                weaponSceneNode->playAnimation(_combatState.active ? "powered" : "off");
            }
        }
    }

    // Left weapon

    std::string leftWeaponModelName(getWeaponModelName(InventorySlots::leftWeapon));
    if (!leftWeaponModelName.empty()) {
        std::shared_ptr<Model> weaponModel(_services.resource.models.get(leftWeaponModelName));
        if (weaponModel) {
            std::shared_ptr<ModelSceneNode> weaponSceneNode(sceneGraph.newModel(*weaponModel, ModelUsage::Equipment));
            body.attach(g_leftHandNode, *weaponSceneNode);
            if (weaponModel->classification() == MdlClassification::lightsaber) {
                weaponSceneNode->playAnimation(_combatState.active ? "powered" : "off");
            }
        }
    }
}

CreaturePresentation Creature::presentation() const {
    if (_presentation) {
        return *_presentation;
    }
    CreaturePresentation result;
    result.gender = static_cast<int>(_gender);
    result.appearance = _appearance;
    auto body = getEquippedItem(InventorySlots::body);
    if (body && !body->baseBodyVariation().empty()) {
        auto variation = boost::to_lower_copy(body->baseBodyVariation());
        result.bodyVariation = variation.front() - 'a';
        result.textureVariation = body->textureVariation();
    }
    return result;
}

void Creature::setPresentation(const CreaturePresentation &presentation) {
    if (!isPresentationOnly()) {
        throw std::logic_error("Visual tuples require a presentation-only creature");
    }
    if (presentation.appearance < 0 || presentation.appearance > 65535 ||
        presentation.gender < 0 || presentation.gender > static_cast<int>(Gender::None) ||
        presentation.bodyVariation < 0 || presentation.bodyVariation >= 26 ||
        presentation.textureVariation < 0 || presentation.textureVariation > 255) {
        throw std::invalid_argument("Invalid creature presentation");
    }
    _presentation = presentation;
    _appearance = presentation.appearance;
    _gender = static_cast<Gender>(presentation.gender);
}

std::string Creature::getBodyModelName() const {
    std::string column;

    if (_modelType == Creature::ModelType::Character) {
        column = "model";

        std::shared_ptr<Item> bodyItem(getEquippedItem(InventorySlots::body));
        if (_presentation) {
            column += static_cast<char>('a' + _presentation->bodyVariation);
        } else if (bodyItem) {
            std::string baseBodyVar(bodyItem->baseBodyVariation());
            column += baseBodyVar;
        } else {
            column += "a";
        }

    } else {
        column = "race";
    }

    std::shared_ptr<TwoDA> appearance(_services.resource.twoDas.get("appearance"));
    if (!appearance) {
        throw ResourceNotFoundException("appearance 2DA not found");
    }

    std::string modelName(appearance->getString(_appearance, column));
    boost::to_lower(modelName);

    return modelName;
}

std::string Creature::getBodyTextureName() const {
    std::string column;
    std::shared_ptr<Item> bodyItem(getEquippedItem(InventorySlots::body));

    if (_modelType == Creature::ModelType::Character) {
        column = "tex";

        if (_presentation) {
            column += static_cast<char>('a' + _presentation->bodyVariation);
        } else if (bodyItem) {
            std::string baseBodyVar(bodyItem->baseBodyVariation());
            column += baseBodyVar;
        } else {
            column += "a";
        }
    } else {
        column = "racetex";
    }

    std::shared_ptr<TwoDA> appearance(_services.resource.twoDas.get("appearance"));
    if (!appearance) {
        throw ResourceNotFoundException("appearance 2DA not found");
    }

    std::string texName(boost::to_lower_copy(appearance->getString(_appearance, column)));
    if (texName.empty()) {
        return "";
    }

    if (_modelType == Creature::ModelType::Character) {
        bool texFound = false;
        if (_presentation || bodyItem) {
            auto variation = _presentation ? _presentation->textureVariation : bodyItem->textureVariation();
            std::string tmp(str(boost::format("%s%02d") % texName % variation));
            std::shared_ptr<Texture> texture(_services.resource.textures.get(tmp, TextureUsage::MainTex));
            if (texture) {
                texName = std::move(tmp);
                texFound = true;
            }
        }
        if (!texFound) {
            texName += "01";
        }
    }

    return texName;
}

std::string Creature::getHeadModelName() const {
    if (_modelType != Creature::ModelType::Character) {
        return "";
    }
    std::shared_ptr<TwoDA> appearance(_services.resource.twoDas.get("appearance"));
    if (!appearance) {
        throw ResourceNotFoundException("appearance 2DA not found");
    }
    int headIdx = appearance->getInt(_appearance, "normalhead", -1);
    if (headIdx == -1) {
        return "";
    }
    std::shared_ptr<TwoDA> heads(_services.resource.twoDas.get("heads"));
    if (!heads) {
        throw ResourceNotFoundException("heads 2DA not found");
    }

    std::string modelName(heads->getString(headIdx, "head"));
    boost::to_lower(modelName);

    return modelName;
}

std::string Creature::getMaskModelName() const {
    std::shared_ptr<Item> headItem(getEquippedItem(InventorySlots::head));
    if (!headItem) {
        return "";
    }

    std::string modelName(boost::to_lower_copy(headItem->itemClass()));
    modelName += str(boost::format("_%03d") % headItem->modelVariation());

    return modelName;
}

std::string Creature::getWeaponModelName(int slot) const {
    std::shared_ptr<Item> bodyItem(getEquippedItem(slot));
    if (!bodyItem) {
        return "";
    }

    std::string modelName(bodyItem->itemClass());
    boost::to_lower(modelName);

    modelName += str(boost::format("_%03d") % bodyItem->modelVariation());

    return modelName;
}

void Creature::deserialize(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    std::string templateRes;
    std::shared_ptr<Gff> utc;
    if (!identityContext.isSerializedState() &&
        gff.readResRef(templateRes, "TemplateResRef")) {
        utc = findTemplate(_services.resource.gffs, templateRes);
        if (utc) {
            deserializeAll(
                *utc,
                SerializedIdentityContext::templateResource(templateRes));
        }
    }
    deserializeAll(gff, identityContext);

    // Vitality is rebuilt from the record before equipment is worn, so worn
    // Constitution, Wisdom and Charisma move the rebuilt pools.
    restoreSerializedVitality();
    if (utc) {
        deserializeOwnedItemsAndEquipment(
            *utc, SerializedIdentityContext::templateResource(templateRes), true);
    }
    deserializeOwnedItemsAndEquipment(gff, identityContext, false);
    countLoadedItems();
    // A reloaded corpse lies, without dying again, in the dead pose it was
    // saved in; one saved in no dead pose lies in the second.
    if (_dead) {
        int32_t animation = kDead1AnimationId;
        gff.readInt(animation, "Animation");
        _deathPose = animation == kDeadAnimationId    ? DeathPose::Dead
                     : animation == kDead3AnimationId ? DeathPose::Dead3
                                                      : DeathPose::Dead1;
    }

    updateTransform();
    loadAppearance();
}

void Creature::deserializeAll(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    if (_game.isTSL()) {
        if (gff.has("CurrentForm")) {
            _currentForm = static_cast<CombatForm>(gff.getUint("CurrentForm"));
        }
        // Both are saved as byte fields.
        uint8_t value;
        if (gff.readByte(value, "MultiplierSet")) {
            _autoBalanceContext.multiplierSet = value;
        }
        if (gff.readByte(value, "PCLevelAtSpawn")) {
            _autoBalanceContext.playerLevelAtSpawn = value;
            _autoBalancePlayerLevelAtSpawnSet = true;
        }
    }

    Object::deserialize(gff, identityContext);

    // IsPC is read before hit points are rebuilt. A player character stays
    // incapacitated and resumable through 0..-9 HP and is dead only at -10 HP.
    gff.readByte(_isPC, "IsPC");
    _actsAsPC = _isPC;
    gff.readBool(_playerCreated, "PlayerCreated");
    // Only the value 1 is stealth.
    setStealthMode(gff.getUint("StealthMode", _stealthMode ? 1 : 0) == 1);
    gff.readInt(_assignedPuppet, "AssignedPup");

    // index into racialtypes.2da
    gff.readEnum(_race, "Race");
    setRacialType(_race);

    // index into subrace.2da
    gff.readEnum(_subrace, "SubraceIndex");

    // index into appearance.2da
    gff.readEnum(_appearance, "Appearance_Type");

    // Savegames keep the visible disguise appearance in Appearance_Type and
    // the normal appearance separately. Restore this before equipped items
    // are loaded, so equipping a saved disguise does not overwrite it.
    _disguised = false;
    _appearanceBeforeDisguise = 0;
    bool disguised;
    uint16_t appearanceBeforeDisguise;
    if (gff.readBool(disguised, "PM_IsDisguised") &&
        disguised &&
        gff.readWord(appearanceBeforeDisguise, "PM_Appearance")) {
        _disguised = true;
        _appearanceBeforeDisguise = appearanceBeforeDisguise;
    }

    // in dex into gender.2da
    gff.readEnum(_gender, "Gender");

    // index into portrait.2da
    gff.readWord(_portraitId, "PortraitId");

    // index into repute.2da; a player character belongs to the player faction
    gff.readEnum(_faction, "FactionID");
    if (_isPC) {
        _faction = Faction::Player;
    }
    _factionJoinOrder = _game.nextFactionJoinOrder();

    gff.readBool(_disarmable, "Disarmable");
    gff.readBool(_noPermDeath, "NoPermDeath");
    if (identityContext.isSerializedState()) {
        // Missing BYTE fields default to one during loading.
        // These defaults differ from freshly constructed object flags.
        _destroyable = _raiseable = _selectableWhenDead = true;
        gff.readBool(_destroyable, "IsDestroyable");
        gff.readBool(_raiseable, "IsRaiseable");
        gff.readBool(_selectableWhenDead, "DeadSelectable");
    }
    gff.readBool(_notReorienting, "NotReorienting");
    gff.readByte(_bodyVariation, "BodyVariation");
    gff.readByte(_textureVar, "TextureVar");
    gff.readBool(_partyInteract, "PartyInteract");

    // index into creaturespeed.2da
    gff.readInt(_walkRate, "WalkRate");
    uint8_t movementRate;
    if (gff.readByte(movementRate, "MovementRate")) {
        _walkRate = movementRate;
    }
    // The default rate is the appearance's, which the record has just named.
    loadAppearanceMoveRate(*getRequiredTwoDA(_services.resource.twoDas, "appearance"));
    setMovementRate(_walkRate);
    gff.readBool(_listening, "Listening");
    for (const auto &expression : gff.getList("ExpressionList")) {
        const auto pattern = expression->getString("ExpressionString");
        const int32_t number = expression->getInt("ExpressionId");
        setListenPattern(pattern, number);
        // A saved pattern that does not parse here matches nothing, and is
        // saved again as it was loaded.
        auto entry = std::find_if(_listenExpressions.begin(), _listenExpressions.end(),
                                  [number](const ListenExpression &value) { return value.number == number; });
        if (entry != _listenExpressions.end() && !entry->parsed) {
            entry->pattern = pattern;
        }
    }

    gff.readByte(_naturalAC, "NaturalAC");
    // The pools and bonuses are saved as short fields.
    const bool hasForcePoints = gff.readShort(_forcePoints, "ForcePoints");
    // A record without current Force points starts at its own base Force points.
    int currentForce = _forcePoints;
    if (gff.readShort(currentForce, "CurrentForce") || hasForcePoints) {
        _currentForce = currentForce;
    }
    if (_game.isTSL()) {
        // A record without the bonus has none.
        int8_t furyDamageBonus = kNoFuryDamageBonus;
        gff.readChar(furyDamageBonus, "FuryDamageBonus");
        _furyDamageBonus = furyDamageBonus;
    }
    _temporaryHitPointsRestored = gff.readInt(_temporaryHitPoints, "TemporaryHP");
    _temporaryForcePointsRestored = gff.readInt(_temporaryForcePoints, "TemporaryFP");
    gff.readInt(_bonusForcePoints, "BonusForcePoints");
    gff.readShort(_refBonus, "refbonus");
    gff.readShort(_willBonus, "willbonus");
    gff.readShort(_fortBonus, "fortbonus");
    uint8_t goodEvil;
    if (gff.readByte(goodEvil, "GoodEvil")) {
        _goodEvil = goodEvil;
    }
    if (_game.isTSL()) {
        // A field label holds sixteen characters, so the base alignment's is
        // cut short. A record without a base alignment, or with the unset
        // value, takes its good/evil value.
        static constexpr int8_t kUnsetBaseAlignment = -1;
        int8_t base = kUnsetBaseAlignment;
        gff.readChar(base, "BaseCNPCAlignmen");
        _baseCNPCAlignment = base == kUnsetBaseAlignment ? _goodEvil : base;
    }
    gff.readFloat(_challengeRating, "ChallengeRating");
    int aiStyle = static_cast<int>(_aiStyle);
    if (gff.readInt(aiStyle, "AIState")) {
        _aiStyle = static_cast<NPCAIStyle>(aiStyle);
    }
    uint32_t experience {0};
    if (gff.readDword(experience, "Experience")) {
        _xp = static_cast<int>(experience);
    }
    gff.readInt(_joiningXP, "JoiningXP");

    gff.readResRef(_onNotice, "ScriptOnNotice");
    gff.readResRef(_onSpellAt, "ScriptSpellAt");
    gff.readResRef(_onAttacked, "ScriptAttacked");
    gff.readResRef(_onDamaged, "ScriptDamaged");
    gff.readResRef(_onDisturbed, "ScriptDisturbed");
    gff.readResRef(_onEndRound, "ScriptEndRound");
    gff.readResRef(_onEndDialogue, "ScriptEndDialogu");
    gff.readResRef(_onDialogue, "ScriptDialogue");
    gff.readResRef(_onSpawn, "ScriptSpawn");
    gff.readResRef(_onDeath, "ScriptDeath");
    gff.readResRef(_onBlocked, "ScriptOnBlocked");

    // Only a saved creature record carries CreatnScrptFird; a blueprint leaves
    // the flag clear so a freshly materialized creature still spawns once.
    uint8_t spawnScriptFired = 0;
    if (gff.readByte(spawnScriptFired, "CreatnScrptFird")) {
        _spawnScriptFired = spawnScriptFired != 0;
    }
    // The record does not keep the heartbeat stamp.
    startUnstampedHeartbeat();

    deserializeName(gff);
    deserializeSoundSet(gff);
    gff.readByte(_bodyBagId, "BodyBag");
    deserializeAttributes(gff);
    deserializePerception(gff);
}

void Creature::deserializeName(const resource::Gff &gff) {
    resource::LocString firstName = _firstName;
    resource::LocString lastName = _lastName;
    gff.readLocString(firstName, "FirstName", _services.resource.strings);
    gff.readLocString(lastName, "LastName", _services.resource.strings);
    setFirstAndLastName(std::move(firstName), std::move(lastName));
}

void Creature::setFirstAndLastName(resource::LocString firstName, resource::LocString lastName) {
    _firstName = std::move(firstName);
    _lastName = std::move(lastName);
    _name = _firstName.str();
    const std::string &last = _lastName.str();
    if (!_name.empty() && !last.empty()) {
        _name += ' ';
    }
    _name += last;
}

void Creature::deserializeSoundSet(const resource::Gff &gff) {
    gff.readWord(_soundSetId, "SoundSetFile");
    if (_soundSetId == 0xffff) {
        return;
    }

    std::shared_ptr<TwoDA> soundSetTable(_services.resource.twoDas.get("soundset"));
    if (!soundSetTable) {
        return;
    }
    std::string soundSetResRef(soundSetTable->getString(_soundSetId, "resref"));
    if (!soundSetResRef.empty()) {
        _soundSet = _services.resource.soundSets.get(soundSetResRef);
    }
}

void Creature::deserializeAttributes(const resource::Gff &gff) {
    // Only a player character's record gives its level history.
    if (_isPC) {
        _levelStats.clear();
        for (const auto &level : gff.getList("LvlStatList")) {
            // Both are saved as byte fields.
            uint8_t hitDie = 0;
            uint8_t forcePoints = 0;
            level->readByte(hitDie, "LvlStatHitDie");
            level->readByte(forcePoints, "LvlStatForce");
            _levelStats.push_back({hitDie, forcePoints});
        }
    }
    CreatureAttributes &attributes = _attributes;
    {
        uint8_t value;
        if (gff.readByte(value, "Str")) {
            attributes.setAbilityScore(Ability::Strength, value);
        }
        if (gff.readByte(value, "Dex")) {
            attributes.setAbilityScore(Ability::Dexterity, value);
        }
        if (gff.readByte(value, "Con")) {
            attributes.setAbilityScore(Ability::Constitution, value);
        }
        if (gff.readByte(value, "Int")) {
            attributes.setAbilityScore(Ability::Intelligence, value);
        }
        if (gff.readByte(value, "Wis")) {
            attributes.setAbilityScore(Ability::Wisdom, value);
        }
        if (gff.readByte(value, "Cha")) {
            attributes.setAbilityScore(Ability::Charisma, value);
        }
    }

    for (const auto &clazz : gff.getList("ClassList")) {
        deserializeClass(*clazz);
    }
    _spellLikeAbilities.clear();
    for (const auto &record : gff.getList("SpecAbilityList")) {
        // The spell is saved as a word field, its readiness and caster level as bytes.
        uint16_t spell = 0;
        uint8_t flags = 0;
        uint8_t casterLevel = 0;
        record->readWord(spell, "Spell");
        record->readByte(flags, "SpellFlags");
        record->readByte(casterLevel, "SpellCasterLevel");
        _spellLikeAbilities.push_back({spell, flags, casterLevel});
    }

    int skillType = 0;
    for (const auto &skill : gff.getList("SkillList")) {
        attributes.setSkillRank(
            static_cast<SkillType>(skillType++), skill->getUint("Rank"));
    }

    // Only a feat the feat table defines is held.
    for (const auto &feat : gff.getList("FeatList")) {
        auto featType = static_cast<FeatType>(feat->getUint("Feat"));
        if (_services.game.feats.get(featType)) {
            _attributes.addFeat(featType);
        }
    }
}

void Creature::deserializeClass(const resource::Gff &gff) {
    auto clazz = _services.game.classes.get(
        static_cast<ClassType>(gff.getInt("Class")));
    if (!clazz) {
        return;
    }

    int16_t level;
    if (gff.readShort(level, "ClassLevel")) {
        _attributes.addClassLevels(clazz.get(), level);
    }

    for (const auto &spell : gff.getList("KnownList0")) {
        auto spellType = static_cast<SpellType>(spell->getUint("Spell"));
        if (canLearnPower(spellType, clazz->type())) {
            _attributes.addSpell(spellType, clazz->type());
        }
    }
}

void Creature::deserializePerception(const resource::Gff &gff) {
    gff.readFloat(_blindSpot, "BlindSpot");
    // A player character perceives at the player range whatever its record
    // says. Others perceive at their range row, by default the one their
    // appearance names; a row outside the table gives no ranges.
    if (_isPC) {
        setPerceptionRangeRow(kPlayerPerceptionRangeRow);
        return;
    }
    gff.readByte(_perceptionId, "PerceptionRange");
    setPerceptionRangeRow(_perceptionId);
    _pendingPerceptionRangeByAppearance = _perceptionId == kAppearancePerceptionRange;
}

std::vector<std::shared_ptr<Object>> Creature::ownedRuntimeObjects() const {
    auto result = Object::ownedRuntimeObjects();
    std::set<const Object *> seen;
    for (const auto &object : result) {
        if (object) {
            seen.insert(object.get());
        }
    }
    for (const auto &[_, item] : _equipment) {
        if (item && seen.insert(item.get()).second) {
            result.push_back(item);
        }
    }
    return result;
}

// An equipped item on load takes the slot its entry names when this creature
// may wear it there, judged against the items loaded before it: a weapon
// finding both hands of its set empty goes to the main hand, and one finding
// only the off hand held is refused. A refused item goes to the inventory,
// and refused body armour is remembered for the party's clothing.
std::optional<int> Creature::admitLoadedEquipment(
    const Item &item, uint32_t slotMask,
    const std::map<int, std::shared_ptr<Item>> &loaded, bool &bodyRefused, bool reportRefusal) {
    const auto slot = equipmentSlotFromMask(slotMask);
    if (!slot) {
        return std::nullopt;
    }
    EquipmentCandidateDecision decision;
    evaluateWithEquipment(loaded, [&]() { decision = evaluateEquipmentCandidate(*this, *slot, &item); });
    if (decision.valid) {
        return decision.action == EquipmentCandidateAction::Equip ? decision.actualSlot : *slot;
    }
    if (reportRefusal) {
        if (auto strRef = equipmentRefusalFeedback(decision.reason)) {
            _game.addFeedbackMessage(*strRef);
        }
    }
    if (*slot == InventorySlots::body) {
        bodyRefused = true;
    }
    return std::nullopt;
}

void Creature::forceEquipClothing() {
    if (!_bodyRefusedOnLoad) {
        return;
    }
    _bodyRefusedOnLoad = false;
    static constexpr char kClothing[] = "g_a_clothes01";
    // The party's own clothing is taken first; otherwise a new, droppable set.
    auto self = _game.getObjectById(_id);
    auto repository = self ? _game.party().sharedInventoryReceiver(self) : nullptr;
    std::shared_ptr<Item> clothing;
    if (repository) {
        std::shared_ptr<Item> found;
        for (const auto &item : repository->items()) {
            if (item && boost::iequals(item->tag(), kClothing)) {
                found = item;
                break;
            }
        }
        if (found) {
            clothing = takeEquipmentCandidate(_game, *repository, found);
        }
    }
    if (!clothing) {
        clothing = _game.newItemFromBlueprint(kClothing);
        if (!clothing) {
            return;
        }
        clothing->setDropable(true);
    }
    if (!equip(InventorySlots::body, clothing)) {
        if (repository) {
            repository->addItem(clothing);
        }
        return;
    }
    // The member has already joined, so the clothing breaks a no-armour forfeit.
    _game.finishEquip(*this, InventorySlots::body, clothing);
}

void Creature::countLoadedItems() {
    std::vector<std::shared_ptr<Item>> counted;
    for (const auto &item : _items) {
        if (_game.party().isCountedItem(*item)) {
            counted.push_back(item);
        }
    }
    if (counted.empty()) {
        return;
    }
    std::vector<std::shared_ptr<Object>> obsolete(counted.begin(), counted.end());
    std::vector<std::shared_ptr<Item>> kept;
    ItemAttributes keptAttributes;
    _game.replaceRuntimeObjectGraph(
        obsolete,
        [&]() {
            for (const auto &item : _items) {
                if (std::find(counted.begin(), counted.end(), item) != counted.end()) {
                    continue;
                }
                kept.push_back(item);
                keptAttributes.addItem(item, _services.game);
            }
        },
        [&]() noexcept {
            for (const auto &item : counted) {
                item->clearOwner();
            }
            _items = std::move(kept);
            _itemAttributes = std::move(keptAttributes);
        });
    for (const auto &item : counted) {
        _game.party().acquireCountedItem(*this, *item);
    }
}

void Creature::deserializeOwnedItemsAndEquipment(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    bool placedTemplate) {
    const bool replaceItems = gff.has("ItemList");
    const bool replaceEquipment = gff.has("Equip_ItemList");
    if (!replaceItems && !replaceEquipment) {
        return;
    }
    bool bodyRefused = false;

    if (!identityContext.isSerializedState()) {
        std::vector<std::shared_ptr<Object>> obsolete;
        if (replaceItems) {
            obsolete.insert(obsolete.end(), _items.begin(), _items.end());
        }
        if (replaceEquipment) {
            for (const auto &[_, item] : _equipment) {
                obsolete.push_back(item);
            }
        }
        std::vector<std::shared_ptr<Item>> replacementItems =
            replaceItems ? std::vector<std::shared_ptr<Item>> {} : _items;
        std::map<int, std::shared_ptr<Item>> replacementEquipment =
            replaceEquipment
                ? std::map<int, std::shared_ptr<Item>> {}
                : _equipment;
        ItemAttributes replacementAttributes;
        std::deque<EffectInstance> replacementEffects;
        // An entry is left out when the template it names does not exist. The
        // template of a creature an area places names its list's template in
        // each entry and takes only the entry's Dropable flag, unset when absent.
        auto loadEntry = [&](const Gff &entry, const std::string &templateField) -> std::shared_ptr<Item> {
            if (placedTemplate) {
                std::string resRef;
                if (!entry.readResRef(resRef, templateField) || !_services.resource.gffs.get(resRef, ResType::Uti)) {
                    return nullptr;
                }
                auto item = _game.newItemFromBlueprint(resRef);
                uint8_t dropable = 0;
                entry.readByte(dropable, "Dropable");
                item->setDropable((dropable & 1) != 0);
                return item;
            }
            if (Item::namesMissingTemplate(entry, _services.resource.gffs)) {
                return nullptr;
            }
            return _game.newOwnedItem(entry, identityContext);
        };
        _game.replaceRuntimeObjectGraph(
            obsolete,
            [&]() {
                // Refused equipment enters the inventory before the carried items.
                if (replaceEquipment) {
                    for (const auto &itemGff : gff.getList("Equip_ItemList")) {
                        auto item = loadEntry(*itemGff, "EquippedRes");
                        if (!item) {
                            continue;
                        }
                        const auto slot = admitLoadedEquipment(*item, itemGff->type(), replacementEquipment, bodyRefused, false);
                        if (!slot || replacementEquipment.count(*slot) != 0) {
                            item->setOwner(_id);
                            appendOwnedItemCandidate(
                                replacementItems, item, true);
                            continue;
                        }
                        item->setOwner(_id);
                        item->setEquipped(true);
                        _game.recordEquippedOnLoad(*slot, item);
                        replacementEquipment.emplace(*slot, std::move(item));
                    }
                }
                // Carried items keep their own stacks.
                if (replaceItems) {
                    for (const auto &itemGff : gff.getList("ItemList")) {
                        auto item = loadEntry(*itemGff, "InventoryRes");
                        if (!item) {
                            continue;
                        }
                        item->setOwner(_id);
                        replacementItems.push_back(std::move(item));
                    }
                }
                for (const auto &item : replacementItems) {
                    replacementAttributes.addItem(item, _services.game);
                }
                if (replaceEquipment) {
                    replacementEffects =
                        rebuildEquippedItemEffects(replacementEquipment);
                }
            },
            [&]() noexcept {
                _bodyRefusedOnLoad = bodyRefused;
                _items = std::move(replacementItems);
                _equipment = std::move(replacementEquipment);
                _itemAttributes = std::move(replacementAttributes);
                if (replaceEquipment) {
                    replaceEffectState(std::move(replacementEffects));
                }
            });
        // Body armour read with the creature is put on like any other.
        if (auto armour = replaceEquipment ? getEquippedItem(InventorySlots::body) : nullptr) {
            updateArmourAppearance(armour);
        }
        return;
    }

    std::vector<std::shared_ptr<Object>> obsolete;
    if (replaceItems) {
        obsolete.insert(obsolete.end(), _items.begin(), _items.end());
    }
    if (replaceEquipment) {
        for (const auto &[_, item] : _equipment) {
            obsolete.push_back(item);
        }
    }
    std::vector<std::shared_ptr<Item>> replacementItems =
        replaceItems ? std::vector<std::shared_ptr<Item>> {} : _items;
    std::map<int, std::shared_ptr<Item>> replacementEquipment =
        replaceEquipment
            ? std::map<int, std::shared_ptr<Item>> {}
            : _equipment;
    ItemAttributes replacementAttributes;
    std::deque<EffectInstance> replacementEffects;
    _game.replaceRuntimeObjectGraph(
        obsolete,
        [&]() {
            // Refused equipment enters the inventory before the carried items
            // and joins a stack it matches.
            if (replaceEquipment) {
                for (const auto &itemGff : gff.getList("Equip_ItemList")) {
                    auto item = _game.newOwnedItem(*itemGff, identityContext);
                    item->captureSaveRecord(
                        *itemGff,
                        identityContext,
                        {SaveRecordOriginKind::EquippedItem, std::to_string(_id)});

                    // A saved player character is told why it may no longer wear an item.
                    const auto slot = admitLoadedEquipment(*item, itemGff->type(), replacementEquipment, bodyRefused, _isPC);
                    if (!slot) {
                        item->setOwner(_id);
                        appendOwnedItemCandidate(
                            replacementItems, item, false);
                        continue;
                    }
                    if (replacementEquipment.count(*slot) != 0) {
                        throw ValidationException(
                            "Multiple saved items occupy equipment slot " +
                            std::to_string(*slot));
                    }
                    item->setOwner(_id);
                    item->setEquipped(true);
                    _game.recordEquippedOnLoad(*slot, item);
                    replacementEquipment.emplace(*slot, std::move(item));
                }
            }

            // Carried items keep their own stacks.
            if (replaceItems) {
                for (const auto &itemGff : gff.getList("ItemList")) {
                    auto item = _game.newOwnedItem(*itemGff, identityContext);
                    item->captureSaveRecord(
                        *itemGff,
                        identityContext,
                        {SaveRecordOriginKind::ContainedItem, std::to_string(_id)});
                    item->setOwner(_id);
                    replacementItems.push_back(std::move(item));
                }
            }
            for (const auto &item : replacementItems) {
                replacementAttributes.addItem(item, _services.game);
            }
            if (replaceEquipment) {
                replacementEffects =
                    rebuildEquippedItemEffects(replacementEquipment);
            }
        },
        [&]() noexcept {
            _bodyRefusedOnLoad = bodyRefused;
            if (replaceEquipment) {
                for (auto &[_, item] : _equipment) {
                    if (item) {
                        item->setEquipped(false);
                        item->clearOwner();
                    }
                }
            }
            if (replaceItems) {
                for (auto &item : _items) {
                    if (item) {
                        item->clearOwner();
                    }
                }
            }
            _items = std::move(replacementItems);
            _equipment = std::move(replacementEquipment);
            _itemAttributes = std::move(replacementAttributes);
            if (replaceEquipment) {
                replaceEffectState(std::move(replacementEffects));
            }
        });
    uint32_t previousAppearance = _appearance;
    updateDisguise();
    if (_appearance != previousAppearance) {
        loadAppearanceProperties();
    }
    // Body armour read with the creature is put on like any other.
    if (auto armour = replaceEquipment ? getEquippedItem(InventorySlots::body) : nullptr) {
        updateArmourAppearance(armour);
    }
}

bool Creature::isPartyMember() const {
    return _game.party().isMember(*this);
}

void Creature::applyDamageEffect(
    int amount,
    const std::shared_ptr<Object> &damager,
    std::optional<DamageReaction> reaction) {

    if (_dead) {
        return;
    }

    if (amount == 0) {
        if (reaction) {
            _game.combat().reactToDamage(*this, damager, *reaction);
        }
        runDamagedScript();
        return;
    }

    applyHitPointDamage(amount, damager, reaction);
}

int Creature::selectMeleeAttackVariant(bool cinematic) {
    int variant;
    if (cinematic) {
        do {
            variant = randomInt(0, 4);
        } while (variant == _lastMeleeAttackVariant);
    } else {
        variant = randomInt(0, 1);
    }

    _lastMeleeAttackVariant = variant;
    return variant + 1;
}

int Creature::getDefense(std::optional<std::reference_wrapper<const Creature>> attacker, int damageFlags) const {
    return getDefenseBreakdown(attacker, damageFlags).total;
}

DamagePower Creature::calculateDamagePower(
    std::optional<std::reference_wrapper<const Creature>> target,
    const std::shared_ptr<Item> &weapon,
    bool offHand,
    std::optional<AttackBonus> handType) const {

    int effectBonus = getAttackBonusBreakdown(
                          target,
                          weapon,
                          offHand,
                          handType)
                          .effectBonus;
    // The power is the low byte of the bonus, so a net penalty becomes a
    // power above every reduction's.
    return static_cast<DamagePower>(static_cast<uint8_t>(effectBonus));
}

void Creature::getDamageResistanceFeatBonuses(
    int damage,
    DamageResolution &resolution) const {

    int percentageRank = 0;
    if (_game.isTSL()) {
        percentageRank = std::max(
            getHighestOwnedFeatRank(*this, FeatType::IgnorePain1, 3),
            getHighestOwnedFeatRank(*this, FeatType::InnerStrength1, 3));
    }
    auto reduction = getResistanceFeatReduction(_game.isTSL(), damage, percentageRank,
                                                hasEffectiveFeat(FeatType::ImprovedToughness),
                                                hasEffectiveFeat(FeatType::WookieEndurance),
                                                _game.isTSL() && hasEffectiveFeat(FeatType::ImprovedWookieEndurance),
                                                _game.isTSL() && hasEffectiveFeat(FeatType::MasterWookieEndurance));
    resolution.percentageResistanceBonus = reduction.percentage;
    resolution.improvedToughnessBonus = reduction.improvedToughness;
    resolution.wookieeEnduranceBonus = reduction.endurance;
}

float Creature::movementRate(bool applyMobility) const {
    return getMovementRateFactor(_movementRate, _game.isTSL(), applyMobility,
                                 _game.isTSL() && applyMobility && hasEffectiveFeat(FeatType::Mobility));
}

// A walk slowed to a crawl is no walk at all. A creature sneaking where the
// player's side knows of it walks at its appearance's own pace, whatever
// hastens or slows it.
float Creature::walkSpeed() const {
    const float speed = _walkSpeed * movementRate(true);
    if (speed <= kMinimumWalkSpeed) {
        return 0.0f;
    }
    if (movesStealthily() && isClientPresent()) {
        return _stealthWalkSpeed;
    }
    return speed;
}

// However slowed, a run covers at least a metre a second.
float Creature::runSpeed() const {
    return std::max(kMinimumRunSpeed, _runSpeed * movementRate(true));
}

int Creature::maxForcePoints() const {
    const bool tsl = _game.isTSL();
    if (racialType() == RacialType::Droid) {
        return 0;
    }
    bool jedi = false;
    bool mastery = false;
    ClassType lastClass = ClassType::Invalid;
    for (const auto &[clazz, classLevel] : _attributes.classLevels()) {
        lastClass = clazz->type();
        jedi |= isForceUsingClass(lastClass, tsl);
        mastery |= lastClass == ClassType::JediConsular ||
                   (tsl && (lastClass == ClassType::JediMaster || lastClass == ClassType::SithLord));
    }
    const int level = _attributes.getAggregateLevel();
    int bonuses = hasEffectiveFeat(FeatType::ForceSensitive) ? 40 : 0;
    if (tsl) {
        bonuses += _bonusForcePoints;
    }
    if (mastery && hasEffect(EffectType::PureEvilPowers)) {
        bonuses += 50;
    }
    // A creature without Force points reads no ability modifier.
    const auto wisdom = [this]() { return getEffectiveAbilityModifier(Ability::Wisdom); };
    const auto charisma = [this]() { return getEffectiveAbilityModifier(Ability::Charisma); };
    if (_isPC && _game.party().controlledNpc() == -1) {
        // The player character's level history counts when its last class
        // uses the Force, whatever its earlier classes.
        if (!isForceUsingClass(lastClass, tsl)) {
            return 0;
        }
        const int modifier = wisdom() + (tsl ? 0 : charisma());
        int total = bonuses;
        for (int i = 0; i < level; ++i) {
            // A level without a recorded entry grants no Force points.
            const int gain = static_cast<size_t>(i) < _levelStats.size() ? _levelStats[i].forcePoints : 0;
            if (gain != 0) {
                total += std::max(1, gain + modifier);
            }
        }
        // In TSL a negative total gives no Force points; KotOR keeps it.
        if (tsl && total < 0) {
            return 0;
        }
        return total;
    }
    if (!jedi) {
        return 0;
    }
    return forcePointMaximum(_forcePoints, level, wisdom(), tsl ? 0 : charisma(), tsl, bonuses);
}

void Creature::setBonusForcePoints(int amount) {
    if (_game.isTSL()) {
        _bonusForcePoints = amount;
    }
}

// A party member's cost follows its alignment and, in TSL, its Charisma; the
// power menus count those terms for everyone.
ForcePointCostTerms Creature::spellForcePointCostTerms(const Spell &spell, bool partyModifiers) const {
    ForcePointCostTerms terms;
    terms.base = spell.forcePointCost;
    if (partyModifiers && (spell.alignment == 'G' || spell.alignment == 'E')) {
        terms.alignmentMultiplier = _services.game.combatTables.forceCostMultiplier(
            std::clamp(_goodEvil / 10, 0, 10), spell.alignment == 'G');
    }
    terms.tsl = _game.isTSL();
    terms.charismaModifier = partyModifiers ? getEffectiveAbilityModifier(Ability::Charisma) : 0;
    terms.alignment = _goodEvil;
    terms.roomRating = terms.tsl ? _game.module()->area()->getRoomForceRating(position()) : 0;
    terms.form = _currentForm;
    return terms;
}

int Creature::adjustedSpellForcePointCost(const Spell &spell, bool partyModifiers) const {
    return adjustedForcePointCost(spellForcePointCostTerms(spell, partyModifiers));
}

PowerMenuStatus Creature::powerMenuStatus(const Spell &spell, std::optional<std::reference_wrapper<const Object>> target) const {
    // This is the client menu's read-only status, not casting admission or
    // payment. Menus count the party cost modifiers for every subject.
    PowerMenuStatus status;
    // A power offered against a mine reads Force points alone, counts a
    // missing Force Body as its first level, and skips the range and
    // equipment checks.
    const bool mine = target && target->get().type() == ObjectType::Trigger;
    const auto costTerms = spellForcePointCostTerms(spell, true);
    const int cost = adjustedForcePointCost(costTerms);
    const int pool = _bodyFuel && !mine ? currentHitPoints() : currentForce();
    const int forceBody = mine ? std::max(0, forceBodyLevel()) : forceBodyLevel();
    if (_game.isTSL() && forceBody != -1) {
        const int percentage = static_cast<uint32_t>(forceBody) < 3
                                   ? 50 - 10 * forceBody
                                   : 0;
        const int lifeShare = cost * percentage / 100;
        if (static_cast<uint32_t>(cost - lifeShare) > static_cast<uint32_t>(pool)) {
            status.disallow(PowerUnavailableReason::InsufficientForce);
        } else if (currentHitPoints() - lifeShare <= 0) {
            status.disallow(PowerUnavailableReason::InsufficientVitality);
        }
    } else if (static_cast<uint32_t>(cost) > static_cast<uint32_t>(pool)) {
        status.disallow(PowerUnavailableReason::InsufficientForce);
    }
    if (mine) {
        return status;
    }

    // Personal menus use a small positive squared distance. Target menus use
    // 3-D center distance, without casting's personal-space extension or maximum-range rule.
    float distanceSquared = 0.0001f;
    if (target) {
        const auto difference = target->get().position() - position();
        distanceSquared = glm::dot(difference, difference);
    }
    // A power whose range tag names no range takes the minimum that working
    // out its cost leaves behind.
    const auto minimumRange = spell.minimumRange ? spell.minimumRange : unrangedPowerMinimumRange(costTerms);
    if (minimumRange && *minimumRange * *minimumRange >= distanceSquared) {
        status.disallow(PowerUnavailableReason::MinimumRange);
    }

    const uint32_t itemMask = forceItemMask();
    if (spell.isForbiddenByEquipment(itemMask)) {
        status.disallow(PowerUnavailableReason::ForbiddenEquipment);
    } else if (status.available() && !spell.hasRequiredEquipment(itemMask)) {
        status.disallow(PowerUnavailableReason::RequiredEquipment);
    }
    return status;
}

// An equipped item's power is ready while its use property has a use left
// (unlimited uses always count), and is refused by the equipment masks as a
// Force power is; the last refusal names the reason.
static PowerMenuStatus equipmentPowerStatus(const Creature &creature, const Item &item, const Spell &spell) {
    PowerMenuStatus status;
    if (const auto property = item.firstUseProperty()) {
        const int uses = item.properties()[*property].costValue;
        if (uses != Item::kNoChargesPerUse && uses != Item::kUnlimitedUse && !item.hasSpellUse(*property)) {
            status.disallow(PowerUnavailableReason::InsufficientForce);
        }
    }
    const uint32_t itemMask = creature.forceItemMask();
    if (spell.isForbiddenByEquipment(itemMask)) {
        status.disallow(PowerUnavailableReason::ForbiddenEquipment);
    }
    if (!spell.hasRequiredEquipment(itemMask)) {
        status.disallow(PowerUnavailableReason::RequiredEquipment);
    }
    return status;
}

// The powers a menu walks: each class's known powers, class by class in the
// creature's class order, in the order that class learned them.
static std::vector<SpellType> knownSpellsInClassOrder(const CreatureAttributes &attributes) {
    std::vector<SpellType> result;
    for (const auto &[clazz, level] : attributes.classLevels()) {
        const auto &known = attributes.spellsForClass(clazz->type());
        result.insert(result.end(), known.begin(), known.end());
    }
    return result;
}

static constexpr const char *kCurrentImplantGlobal = "CANDEROUS_CUR_IMPLANT";
static constexpr const char *kPreviousImplantGlobal = "CANDEROUS_P_IMPLANT";
static constexpr auto kImplantSwitchingFeat = FeatType::ImplantSwitching;

// The kinds of target a hostile power menu names, which a power's exclusion
// bits refuse.
static constexpr uint32_t kHumanTargetKind = 1;
static constexpr uint32_t kDroidTargetKind = 2;
static constexpr uint32_t kNonCreatureTargetKind = 4;

// The forms, which have a menu of their own.
static bool isFormSpell(int id) {
    return id >= static_cast<int>(SpellType::FormSaberIShiiCho) && id <= static_cast<int>(SpellType::FormForceIVMastery);
}

int Creature::implantMode() const {
    return _game.getGlobalNumber(kCurrentImplantGlobal);
}

void Creature::switchImplantMode(int mode) {
    _game.setGlobalNumber(kPreviousImplantGlobal, _game.getGlobalNumber(kCurrentImplantGlobal));
    _game.setGlobalNumber(kCurrentImplantGlobal, mode);
    _game.scriptRunner().run("a_swapimplant", _id);
}

std::vector<ContextAction> Creature::powerMenuActions(std::optional<std::reference_wrapper<const Object>> target, bool hostileMenu) const {
    std::vector<ContextAction> actions;
    // No Force power is offered while the area restricts the player; a hostile
    // menu then offers nothing at all.
    const bool restricted = _game.module()->area()->playerRestrictMode();
    if (restricted && hostileMenu) {
        return actions;
    }
    const bool tsl = _game.isTSL();
    // TSL: the personal menu first offers the implant modes of the
    // implant-switching feat. An implant mode outside 1-4 becomes 1.
    if (tsl && !hostileMenu && _attributes.hasFeat(kImplantSwitchingFeat)) {
        const int mode = implantMode();
        if (mode < 1 || mode > 4) {
            _game.setGlobalNumber(kCurrentImplantGlobal, 1);
        }
        for (const auto &implant : kImplantModes) {
            actions.emplace_back(implant);
        }
    }
    // A hostile menu names its target's kind.
    uint32_t targetKind = 0;
    bool beast = false;
    const auto targetCreature = target ? asCreature(target->get()) : std::nullopt;
    if (hostileMenu && target) {
        if (!targetCreature) {
            targetKind = kNonCreatureTargetKind;
        } else if (targetCreature->get().racialType() == RacialType::Droid) {
            targetKind = kDroidTargetKind;
        } else if (targetCreature->get().racialType() == RacialType::Human) {
            targetKind = kHumanTargetKind;
            beast = targetCreature->get().subrace() == Subrace::Beast;
        }
    }
    const auto global = [&](const char *name) { return _game.getGlobalNumber(name); };
    // TSL: against a droid, the droid-confusing power the leader's feats grant
    // comes first and is always ready. The feats have no name in the feat list yet.
    if (tsl && targetKind == kDroidTargetKind) {
        std::shared_ptr<Spell> power;
        FeatType grantingFeat = FeatType::Invalid;
        if (_attributes.hasFeat(FeatType::DroidConfusion) && global("000_Droid_Conf_Active") == 0) {
            power = _services.game.spells.get(SpellType::DroidConfusion);
            grantingFeat = FeatType::DroidConfusion;
        } else if (_attributes.hasFeat(FeatType::DroidTrick)) {
            power = _services.game.spells.get(SpellType::DroidTrick);
            grantingFeat = FeatType::DroidTrick;
        }
        if (power) {
            actions.emplace_back(power);
            actions.back().feat = grantingFeat;
        }
    }
    // TSL: the personal menu then offers the Wookiee Rage of the highest rank
    // held, always ready.
    if (tsl && !hostileMenu) {
        static constexpr std::array<std::pair<FeatType, SpellType>, 3> kWookieeRage {{
            {FeatType::WookieeRageIII, SpellType::WookieeRageIII},
            {FeatType::WookieeRageII, SpellType::WookieeRageII},
            {FeatType::WookieeRageI, SpellType::WookieeRageI},
        }};
        for (const auto &[feat, spell] : kWookieeRage) {
            if (!_attributes.hasFeat(feat)) {
                continue;
            }
            actions.emplace_back(_services.game.spells.get(spell));
            actions.back().feat = feat;
            break;
        }
    }
    if (restricted) {
        return actions;
    }
    // A droid's hostile menu offers the powers of its equipped items instead of
    // Force powers: each item whose first use property casts a spell the
    // target's kind does not exclude, in slot order, without merging tiers.
    if (hostileMenu && racialType() == RacialType::Droid) {
        static constexpr int kEquipmentPowerSlots = 15;
        std::vector<SpellType> candidates;
        std::vector<std::shared_ptr<Item>> sources;
        for (const auto &[slot, item] : _equipment) {
            if (slot >= kEquipmentPowerSlots || !item) {
                continue;
            }
            const auto property = item->firstUseProperty();
            if (!property) {
                continue;
            }
            const auto &entry = item->properties()[*property];
            if (entry.type() != ItemProperty::ActivateItem) {
                continue;
            }
            candidates.push_back(static_cast<SpellType>(entry.subtype));
            sources.push_back(item);
        }
        const auto admitted = [&](const Spell &spell) { return (spell.exclusion & targetKind) == 0; };
        for (const auto &spell : _services.game.spells.getActionSpells(candidates, true, admitted, false)) {
            // The entry belongs to the first item granting the power.
            const auto source = sources[std::find(candidates.begin(), candidates.end(), spell->type) - candidates.begin()];
            actions.emplace_back(source, spell);
            actions.back().equipmentPower = true;
            actions.back().availability = equipmentPowerStatus(*this, *source, *spell);
        }
        return actions;
    }
    const auto offered = [&](const Spell &spell) {
        if (!tsl) {
            return (spell.exclusion & targetKind) == 0;
        }
        const SpellType type = spell.type;
        const int id = static_cast<int>(type);
        if (isFormSpell(id)) {
            return false;
        }
        // The mind powers come as beast powers against a beast and as the
        // others against anything else.
        if ((type == SpellType::MindTrick || type == SpellType::Confusion) && beast) {
            return false;
        }
        if ((type == SpellType::BeastTrick || type == SpellType::BeastConfusion) && !beast) {
            return false;
        }
        if ((spell.exclusion & targetKind) != 0) {
            return false;
        }
        // A confusion already at work is not offered again.
        if (type == SpellType::Confusion && global("000_Human_Conf_Active") > 0) {
            return false;
        }
        if (type == SpellType::DroidConfusion && global("000_Droid_Conf_Active") > 0) {
            return false;
        }
        if (type == SpellType::BeastConfusion && global("000_Beast_Conf_Active") > 0) {
            return false;
        }
        // Crush Opposition answers the dark side, Inspire Followers the light.
        if (id >= static_cast<int>(SpellType::CrushOppositionI) && id <= static_cast<int>(SpellType::CrushOppositionVI) &&
            _goodEvil > 40)
            return false;
        if (id >= static_cast<int>(SpellType::InspireFollowersI) && id <= static_cast<int>(SpellType::InspireFollowersVI) &&
            _goodEvil < 60)
            return false;
        return true;
    };
    for (const auto &spell : _services.game.spells.getActionSpells(knownSpellsInClassOrder(_attributes),
                                                                   hostileMenu, offered, tsl)) {
        actions.emplace_back(spell);
        actions.back().availability = powerMenuStatus(*spell, target);
    }
    return actions;
}

std::vector<ContextAction> Creature::formMenuActions() const {
    std::vector<ContextAction> actions;
    if (_game.module()->area()->playerRestrictMode()) {
        return actions;
    }
    const auto isForm = [](const Spell &spell) {
        return isFormSpell(static_cast<int>(spell.type));
    };
    for (const auto &spell : _services.game.spells.getActionSpells(knownSpellsInClassOrder(_attributes),
                                                                   false, isForm, true)) {
        actions.emplace_back(spell);
        actions.back().form = true;
    }
    return actions;
}

std::vector<ContextAction> Creature::behaviorMenuActions() {
    std::vector<ContextAction> actions;
    switch (_aiStyle) {
    case NPCAIStyle::PartyAggro:
    case NPCAIStyle::PartyRanged:
    case NPCAIStyle::PartyStationary:
    case NPCAIStyle::PartySupport:
        break;
    default:
        _aiStyle = NPCAIStyle::PartyAggro;
        break;
    }
    // Every behaviour row but the defensive one is offered. The support row
    // (the fifth) is Jedi support for a creature whose last class is a Jedi
    // class, and grenadier support under another name for anyone else.
    static constexpr int kSupportRow = 4;
    static constexpr uint32_t kGrenadierSupportName = 126053;
    const bool jedi = isForceUsingClass(_attributes.getEffectiveClass(), true);
    const auto &scripts = _game.party().aiScripts();
    for (int row = 0; row < static_cast<int>(scripts.size()); ++row) {
        int nameStrRef = scripts[row].nameStrRef;
        const auto style = static_cast<NPCAIStyle>(scripts[row].state);
        if (style == NPCAIStyle::PartyDefense) {
            continue;
        }
        if (row == kSupportRow && !jedi) {
            nameStrRef = kGrenadierSupportName;
        }
        BehaviorEntry entry {style, static_cast<uint32_t>(nameStrRef), ""};
        switch (style) {
        case NPCAIStyle::PartyAggro:
            entry.icon = "ib_aggressive";
            break;
        case NPCAIStyle::PartyRanged:
            entry.icon = "ib_ranged";
            break;
        case NPCAIStyle::PartyStationary:
            entry.icon = "ib_stationary";
            break;
        case NPCAIStyle::PartySupport:
            entry.icon = jedi ? "ib_jedisupport" : "ib_grenadier";
            break;
        default:
            break;
        }
        actions.emplace_back(std::move(entry));
    }
    return actions;
}

int Creature::spellCastingClass() const {
    // Only the first two class positions cast from the power menu.
    static constexpr size_t kMenuCastingClassCount = 2;
    const auto &classes = _attributes.classLevels();
    for (size_t index = 0; index < classes.size() && index < kMenuCastingClassCount; ++index) {
        if (isForceUsingClass(classes[index].first->type(), _game.isTSL())) {
            return static_cast<int>(index);
        }
    }
    return kUnselectedCastingClass;
}

int Creature::spellLikeAbilityCasterLevel(int spellId) const {
    for (const auto &ability : _spellLikeAbilities)
        if (ability.spell == spellId) {
            return ability.casterLevel;
        }
    return kUnspecifiedCasterLevel;
}

int Creature::adjustedClassLevel(int classIndex) const {
    // A class position the creature does not have, as a script may ask, has no level.
    const auto &classes = _attributes.classLevels();
    if (classIndex < 0 || static_cast<size_t>(classIndex) >= classes.size()) {
        return 0;
    }
    int drained = 0;
    for (const auto &effect : effects())
        if (effect.serializedType == SavedEffectTypes::negativeLevel && effect.integerParameter(1, -1) == classIndex) {
            drained += effect.integerParameter(0);
        }
    return std::max(0, classes[classIndex].second - drained);
}

int Creature::spellCasterLevel(const Spell &spell, bool itemOrCheat) const {
    if (!itemOrCheat) {
        const auto &classes = _attributes.classLevels();
        for (size_t index = 0; index < classes.size(); ++index) {
            const auto &[clazz, level] = classes[index];
            if (!isForceUsingClass(clazz->type(), _game.isTSL())) {
                continue;
            }
            const auto required = spell.getClassLevelRequirement(clazz->type());
            if (level > 0 && required && *required >= 0 && *required != 0xff) {
                return adjustedClassLevel(static_cast<int>(index));
            }
        }
        return -1;
    }
    return spell.innateCasterLevel();
}

bool Creature::readySpellLikeAbility(SpellType spell, int &casterLevel) const {
    for (const auto &ability : _spellLikeAbilities) {
        if (ability.spell == static_cast<int>(spell) && ability.flags == 1) {
            casterLevel = ability.casterLevel;
            return true;
        }
    }
    return false;
}

uint32_t Creature::forceItemMask() const {
    uint32_t mask = 0;
    for (const auto &[slot, item] : _equipment) {
        if (item) {
            mask |= item->forceItemMask();
        }
    }
    return mask;
}

void Creature::consumeSpellLikeAbility(SpellType spell, int casterLevel) {
    // Readiness belongs to the selected spell/level entry, not the first
    // occurrence of the spell or the level returned by a later query.
    for (auto &ability : _spellLikeAbilities) {
        if (ability.spell != static_cast<int>(spell) ||
            ability.flags == 0 || ability.casterLevel != casterLevel)
            continue;
        ability.flags = 0;
        return;
    }
}

int Creature::forceBodyLevel() const {
    if (_game.isTSL()) {
        for (const auto &effect : effects())
            if (effect.serializedType == SavedEffectTypes::forceBody) {
                return effect.integerParameter(0, -1);
            }
    }
    return -1;
}

bool Creature::canPaySpellForcePointCost(const Spell &spell) const {
    return canPayForcePointCharge(forcePointCharge(adjustedSpellForcePointCost(spell, isPartyMember()), forceBodyLevel()),
                                  _currentForce + _temporaryForcePoints, _currentHitPoints + _temporaryHitPoints);
}

int Creature::recordedSpellForcePointCost(const Spell &spell) const {
    return forcePointCharge(adjustedSpellForcePointCost(spell, isPartyMember()), forceBodyLevel()).spentForce;
}

void Creature::paySpellForcePointCost(const Spell &spell) {
    ForcePointPools pools {_currentForce, _temporaryForcePoints, _currentHitPoints, _temporaryHitPoints};
    payForcePointCharge(pools, forcePointCharge(adjustedSpellForcePointCost(spell, isPartyMember()), forceBodyLevel()));
    const int spentTemporaryHitPoints = _temporaryHitPoints - pools.temporaryHitPoints;
    _currentForce = pools.force;
    _temporaryForcePoints = pools.temporaryForce;
    _currentHitPoints = pools.hitPoints;
    consumeTemporaryHitPoints(spentTemporaryHitPoints);
}

void Creature::damageForcePoints(int amount) {
    _currentForce = damagedForcePointPool(_currentForce, _temporaryForcePoints, amount);
}

void Creature::addCurrentForcePoints(int amount) {
    _currentForce += amount;
}

void Creature::healForcePoints(int amount) {
    _currentForce = healedForcePointPool(_currentForce, _temporaryForcePoints, amount, maxForcePoints());
    if (_game.isTSL()) {
        addForceHealingFeedback(_game, *this, amount);
    }
}

void Creature::regenerateForcePoints(int amount) {
    // Force points restored by a Heal effect count the temporary points toward
    // the maximum, are written to the ordinary points, and always report the
    // gain, unlike healForcePoints.
    const int current = _currentForce + _temporaryForcePoints;
    const int maximum = maxForcePoints();
    int result = current + amount;
    if (result > maximum) {
        amount = maximum - current;
        result = maximum;
    }
    _currentForce = result;
    addForceHealingFeedback(_game, *this, amount);
}

void Creature::addTemporaryHitPoints(int amount, bool restoring) {
    if (!restoring || !_temporaryHitPointsRestored) {
        _temporaryHitPoints += amount;
    }
}

int Creature::consumeTemporaryHitPoints(int amount) {
    amount = std::max(0, amount);
    const int remainder = consumeTemporaryResource(_temporaryHitPoints, amount);
    int spent = amount - remainder;
    std::vector<EffectId> depleted;
    // Commit every grant debit before removing packages: removal can execute
    // callbacks, remove siblings, or add effects and invalidate collection refs.
    for (auto &effect : _effects) {
        if (spent == 0) {
            break;
        }
        if (effect.type() != EffectType::TemporaryHitpoints ||
            std::find(_removingEffectApplications.begin(), _removingEffectApplications.end(),
                      effect.applicationOrder) != _removingEffectApplications.end())
            continue;
        const int remaining = std::max(0, effect.integerParameter(0));
        const int debit = std::min(spent, remaining);
        if (debit == 0) {
            continue;
        }
        effect.setIntegerParameter(0, remaining - debit);
        spent -= debit;
        if (remaining == debit && std::find(depleted.begin(), depleted.end(), effect.id) == depleted.end()) {
            depleted.push_back(effect.id);
        }
    }
    for (const auto id : depleted) {
        removeEffectsById(id);
    }
    return remainder;
}

void Creature::removeTemporaryHitPoints(int amount) {
    const bool wasAlive = !_dead && currentHitPoints() > 0;
    // The pool does not fall below zero.
    _temporaryHitPoints = std::max(0, _temporaryHitPoints - amount);
    // The creature bleeds as the loss kills it.
    if (wasAlive && currentHitPoints() <= 0 && !_immortal) {
        applyBloodVisual(*this);
        (void)applyDeathEffect(nullptr, false);
    }
}

void Creature::addTemporaryForcePoints(int amount, bool restoring) {
    if (!restoring || !_temporaryForcePointsRestored) {
        _temporaryForcePoints += amount;
    }
}

void Creature::removeTemporaryForcePoints(int amount) {
    // Unlike temporary HP, removal subtracts the original grant even if spent.
    _temporaryForcePoints -= amount;
}

void Creature::multiplyMovementRate(float multiplier) {
    _movementRate = clampMovementRate(_movementRate * multiplier);
}

void Creature::recomputeMovementRate(uint64_t removingId) {
    float rate = 1.0f;
    for (const auto &effect : effects()) {
        // Removal excludes all records with the removed group ID.
        if (effect.id == removingId) {
            continue;
        }
        if (!effect.hasLiveRuntimeSource()) {
            continue;
        }
        if (effect.serializedType == SavedEffectTypes::movementSpeedIncrease) {
            rate += effect.integerParameter(0) / 100.0f;
        } else if (effect.serializedType == SavedEffectTypes::movementSpeedDecrease) {
            rate -= effect.integerParameter(0) / 100.0f;
        }
    }
    _movementRate = clampMovementRate(rate);
}

void Creature::beginStateImmobilization() {
    if (!_movementTypeBeforeStateImmobilization) {
        _movementTypeBeforeStateImmobilization = _movementType;
    }
    setMovementType(MovementType::None);
}

void Creature::restoreMovementAfterState() {
    if (!_movementTypeBeforeStateImmobilization) {
        return;
    }

    MovementType previous = *_movementTypeBeforeStateImmobilization;
    _movementTypeBeforeStateImmobilization.reset();
    if (liesDead() || isImmobilizingState(static_cast<CreatureState>(_effectState))) {
        setMovementType(MovementType::None);
        return;
    }
    setMovementType(previous);
}

void Creature::setInternalStateEffect(
    int state,
    int ambientState,
    uint64_t effectId) {

    _effectState = state;
    _effectAmbientState = ambientState;
    _internalStateEffectId = effectId;
    // A state makes the creature's heartbeat due at once.
    _stateSupportTimer.reset(0.0f);
    _animDirty = true;
}

void Creature::clearInternalStateEffect(uint64_t effectId) {
    if (_internalStateEffectId != effectId) {
        return;
    }
    _internalStateEffectId = 0;
    _effectState = 0;
    _effectAmbientState = 0;
    _activeStateRootId = 0;
    resumeStateDrivenAnimation();
    restoreMovementAfterState();
}

void Creature::addEffectIcon(int iconId) {
    ++_effectIconCounts[iconId];
}

void Creature::removeEffectIcon(int iconId) {
    auto it = _effectIconCounts.find(iconId);
    if (it == _effectIconCounts.end()) {
        return;
    }
    if (--it->second == 0) {
        _effectIconCounts.erase(it);
    }
}

void Creature::onStateRootApplied(const EffectInstance &pending) {
    if (pending.integerParameter(0) > _effectState) {
        rebuildStateEffects(pending);
    }
}

void Creature::rebuildStateEffects(std::optional<std::reference_wrapper<const EffectInstance>> pending, uint64_t removingOrder) {
    std::optional<EffectInstance> winner;
    for (const auto &record : effects()) {
        if (record.applicationOrder == removingOrder || record.serializedType != SavedEffectTypes::setState ||
            !stateHasConsumer(static_cast<CreatureState>(record.integerParameter(0))))
            continue;
        if (!winner || record.integerParameter(0) > winner->integerParameter(0)) {
            winner = record;
        }
    }
    if (pending && (!winner || pending->get().integerParameter(0) > winner->integerParameter(0))) {
        winner = pending->get();
    }
    // A removal hands the state on; an application, a restored one included,
    // lays its own.
    const bool removal = !pending;
    const auto state = winner ? static_cast<CreatureState>(winner->integerParameter(0)) : CreatureState::None;
    _internalStateEffectId = 0;
    _effectState = static_cast<int>(state);
    _effectAmbientState = getStateAmbientCode(state, _game.isTSL());
    _activeStateRootId = winner ? winner->id : kUnassignedEffectId;
    for (size_t index = 0; index < _effects.size();) {
        const auto record = _effects[index];
        if (record.serializedType > SavedEffectTypes::setStateInternal) {
            break;
        }
        if (record.serializedType != SavedEffectTypes::setStateInternal) {
            ++index;
            continue;
        }
        const auto removed = removeEffectsById(record.id);
        // An application advances after erasure; a removal consumes the shifted entry.
        if (!removal || removed == 0) {
            ++index;
        }
    }
    if (!winner) {
        restoreMovementAfterState();
        return;
    }
    // The state that remains after a removal takes command back at once,
    // whichever state it is.
    if (removal) {
        setCommandable(false);
    }
    auto internal = makeInternalStateInstance(*winner, removal);
    internal.id = _game.allocateEffectId();
    setInternalStateEffect(static_cast<int>(state), getStateAmbientCode(state, _game.isTSL()), internal.id);
    _activeStateRootId = winner->id;
    if (!applyEffect(internal)) {
        clearInternalStateEffect(internal.id);
        return;
    }
    // Only a state that holds the AI sets the pose afresh; any other leaves
    // the creature the animation it is playing.
    if (stateAppliesPose(static_cast<int>(state), _game.isTSL())) {
        resumeStateDrivenAnimation();
    }
}

void Creature::onInternalStateRemoved(EffectId id, bool setsPose) {
    if (_internalStateEffectId == id) {
        _internalStateEffectId = kUnassignedEffectId;
    }
    _effectAmbientState = getStateAmbientCode(static_cast<CreatureState>(_effectState), _game.isTSL());
    if (setsPose) {
        resumeStateDrivenAnimation();
    }
}

void Creature::restrictAIState(int mask) {
    _effectAIStateMask &= mask;
    if (!canMove()) {
        setMovementType(MovementType::None);
    }
}

void Creature::recomputeAIStateEffects(int pendingMask, uint64_t removingOrder) {
    int mask = pendingMask;
    for (const auto &effect : effects()) {
        if (effect.applicationOrder != removingOrder && effect.serializedType == SavedEffectTypes::setAIState) {
            mask &= effect.integerParameter(0);
        }
    }
    _effectAIStateMask = mask;
    if (!canMove()) {
        setMovementType(MovementType::None);
    }
}

// Equipped-item effects are never saved: loading wears the items again. The
// armour class and disguise effects of a worn item follow the saved effects,
// so they are added once those are back.
void Creature::onEffectsRestored() {
    std::set<const Item *> visited;
    for (const auto &[slot, item] : _equipment) {
        if (!item || !visited.insert(item.get()).second) {
            continue;
        }
        std::deque<EffectInstance> deferred;
        appendEquippedItemEffects(deferred, slot, item, true);
        for (auto &effect : deferred) {
            applyEffect(std::move(effect));
        }
    }
    // The AI state and the laid state follow from the restored effects. An
    // Immobile creature's missing legs come from its movement rate, which
    // loads before its effects.
    recomputeAIStateEffects(_walkRate == kImmobileMovementRate ? kImmobileAIStateMask : kAIStateUnrestricted);
    std::optional<std::reference_wrapper<const EffectInstance>> winner;
    for (const auto &effect : effects()) {
        if (effect.serializedType == SavedEffectTypes::setState &&
            (!winner || effect.integerParameter(0) > winner->get().integerParameter(0)))
            winner = std::cref(effect);
    }
    if (!winner) {
        return;
    }
    const int state = winner->get().integerParameter(0);
    for (const auto &effect : effects()) {
        if (effect.serializedType == SavedEffectTypes::setStateInternal && effect.integerParameter(0) == state) {
            _activeStateRootId = winner->get().id;
            setInternalStateEffect(state, getStateAmbientCode(static_cast<CreatureState>(state), _game.isTSL()), effect.id);
            if (isImmobilizingState(static_cast<CreatureState>(state))) {
                beginStateImmobilization();
            }
            break;
        }
    }
}

void Creature::resolveDamageShields(Creature &attacker) {
    auto owner = _game.getObjectById(id());
    // Advance the live index even when a callback removes the current shield;
    // appended shields participate in this same walk. Keep no record reference
    // across either application, since those callbacks may mutate the deque.
    for (size_t index = 0; index < _effects.size(); ++index) {
        const auto &shield = _effects[index];
        if (shield.type() != EffectType::DamageShield || !shield.hasLiveRuntimeSource()) {
            continue;
        }
        const int amount = shield.integerParameter(0) +
                           rollDamageShieldContribution(_services, shield.integerParameter(1));
        const int flags = shield.integerParameter(2);
        auto effect = DamageEffect::fromShieldRetaliation(amount, flags);
        effect->setSaveFacingCreator(owner);
        attacker.applyEffect(effect, DurationType::Instant);
        auto visual = _game.newEffect<VisualEffect>(0, false, _services);
        visual->setSaveFacingCreator(owner);
        attacker.applyEffect(visual, DurationType::Instant);
    }
}

int Creature::getSpellLevel(bool applyNegativeLevels) const {
    int level = 0;
    size_t classIndex = 0;
    for (const auto &[clazz, classLevel] : _attributes.classLevels()) {
        int drained = 0;
        if (applyNegativeLevels)
            for (const auto &effect : effects()) {
                if (effect.serializedType == SavedEffectTypes::negativeLevel && effect.integerParameter(1, -1) == static_cast<int>(classIndex)) {
                    drained += effect.integerParameter(0);
                }
            }
        level += std::max(0, classLevel - drained);
        ++classIndex;
    }
    // A creature flagged as a player character is never balanced. The
    // player's level, recorded at spawn or read now, is a signed byte, and
    // the level is a byte.
    const bool balanced = !_isPC && isAutoBalanceEligible(_game.isTSL(), isPartyMember(),
                                                          _autoBalanceContext.multiplierSet);
    const float multiplier = balanced
                                 ? _services.game.combatTables.autoBalance(_autoBalanceContext.multiplierSet).levelMultiplier
                                 : 0.0f;
    const int spawnLevel = static_cast<int8_t>(_autoBalanceContext.playerLevelAtSpawn);
    const int globalLevel = balanced && spawnLevel == 0
                                ? static_cast<int8_t>(_game.getGlobalNumber("G_PC_LEVEL"))
                                : 0;
    return calculateSpellLevel(level, balanced, spawnLevel, globalLevel, multiplier) & 0xff;
}

int Creature::getSpellSaveDC(int spellId) const {
    return calculateSpellSaveDC(_game.isTSL(), spellId, getSpellLevel(),
                                getEffectiveAbilityModifier(Ability::Wisdom), getEffectiveAbilityModifier(Ability::Charisma),
                                hasEffectiveFeat(FeatType::ForceFocusSense), hasEffectiveFeat(FeatType::ForceFocusAdvanced),
                                hasEffectiveFeat(FeatType::ForceFocusMastery));
}

void Creature::playForceResistedAnimation() {
    markAnimationChosen();
    playFireForgetAnimation("fblock", AnimationSource {kForceResistedAnimationId});
}

void Creature::beginForcePush(const glm::vec3 &destination) {
    clearPath();
    _forcedMove = ForcedMove {destination, kForcePushSpeed, true};
    setMovementType(MovementType::Run);
}

void Creature::beginLeap(const glm::vec3 &destination) {
    endForcedMove();
    const float distance = glm::length(glm::vec2(destination) - glm::vec2(position()));
    _forcedMove = ForcedMove {destination, distance / kLeapDuration, false};
}

void Creature::endForcedMove() {
    const bool pushed = isForcePushed();
    _forcedMove.reset();
    if (pushed) {
        setMovementType(MovementType::None);
    }
}

void Creature::updateForcedMove(float dt) {
    if (!_forcedMove) {
        return;
    }
    if (isDead() || isTemporarilyDead()) {
        endForcedMove();
        return;
    }
    auto area = _game.module()->area();
    glm::vec2 delta = glm::vec2(_forcedMove->destination) - glm::vec2(position());
    float distance = glm::length(delta);
    if (distance == 0.0f) {
        endForcedMove();
        return;
    }
    float step = std::min(distance, _forcedMove->speed * dt);
    if (step <= 0.0f) {
        return;
    }
    const bool pushed = _forcedMove->pushed;
    // Carried, the creature keeps facing where it was turned.
    bool moved = area->moveCreatureByDistance(
        _game.getObjectById<Creature>(id()), delta / distance, step, Area::MoveFacing::Keep);
    if (!moved || step == distance) {
        endForcedMove();
    } else if (pushed) {
        setMovementType(MovementType::Run);
    }
}

void Creature::updateStateHeartbeat(float dt) {
    _stateSupportTimer.update(dt);
    if (!_stateSupportTimer.elapsed()) {
        return;
    }
    // A due heartbeat of a level-0 creature waits a number of updates set by
    // its interval; the counter runs at every level.
    if (++_heartbeatThrottle < (_heartbeatInterval >> 6) && _aiLevel == 0) {
        return;
    }
    _heartbeatThrottle = 0;
    _heartbeatInterval = randomInt(kMinHeartbeatIntervalMilliseconds, kMaxHeartbeatIntervalMilliseconds);
    _stateSupportTimer.reset(_heartbeatInterval / 1000.0f);
    runHeartbeat();
}

void Creature::runHeartbeat() {
    if (_dead) {
        return;
    }
    const auto state = static_cast<CreatureState>(_effectState);
    // A confused TSL creature takes no command from its own heartbeat.
    if (_game.isTSL() && (state == CreatureState::Confusion || state == CreatureState::DroidConfused)) {
        setCommandable(false);
    }
    if (!_onHeartbeat.empty()) {
        _game.scriptRunner().run(_onHeartbeat, _id);
    }
    int row;
    switch (state) {
    case CreatureState::Confusion:
    case CreatureState::DroidConfused:
        row = 3;
        break;
    case CreatureState::Fear:
        row = 2;
        break;
    default:
        return;
    }
    const auto &script = _services.game.combatTables.stateScript(row);
    if (!script.empty()) {
        _game.scriptRunner().run(script, _id);
    }
    // Whatever the state's script commanded, the creature stays out of command.
    setCommandable(false);
}

void Creature::forceHeartbeat() {
    if (!_onHeartbeat.empty()) {
        _game.scriptRunner().run(_onHeartbeat, _id);
    }
    _stateSupportTimer.reset(_heartbeatInterval / 1000.0f);
}

Creature::EffectStackCounts Creature::effectStackCounts() const {
    EffectStackCounts result;
    for (const auto &[iconId, count] : _effectIconCounts) {
        const auto good = _services.game.combatTables.effectIcon(iconId).good;
        if (count <= 0 || !good) {
            continue;
        }
        if (*good) {
            ++result.positive;
        } else {
            ++result.negative;
        }
    }
    return result;
}

// Talent selection

namespace {

// A talent category of four four-bit fields; a field of all ones takes any value.
bool categoryMatches(int requested, int actual) {
    constexpr unsigned categoryCount = 4;
    constexpr unsigned bitsPerCategory = 4;
    constexpr uint32_t wildcard = 0xfu;
    for (unsigned category = 0; category < categoryCount; ++category) {
        const unsigned shift = category * bitsPerCategory;
        const uint32_t selected = (static_cast<uint32_t>(requested) >> shift) & wildcard;
        const uint32_t value = (static_cast<uint32_t>(actual) >> shift) & wildcard;
        if (selected != wildcard && selected != value) {
            return false;
        }
    }
    return true;
}

// A talent without exclusions passes, and so does one whose exclusions and the
// requested inclusions are both empty; the two are not intersected.
bool inclusionMatches(uint32_t exclusion, int inclusion) {
    return exclusion == 0 || (static_cast<uint32_t>(inclusion) | exclusion) == 0;
}

// The challenge rating every ranked talent is first measured against.
constexpr int kLowestChallengeRating = -100;

// An invalid talent has type Talent::kNoType and id -1.
constexpr int kInvalidTalentId = -1;

struct Candidate {
    TalentType type;
    int id;
    int classIndex {kUnselectedCastingClass};
    uint32_t item {script::kObjectInvalid};
    int property {-1};
    int casterLevel {kUnspecifiedCasterLevel};
};

struct CandidatePool {
    std::vector<Candidate> entries;
    int bestCR {kLowestChallengeRating};

    void add(Candidate candidate, int cr, bool ranked) {
        if (ranked) {
            if (cr < bestCR) {
                return;
            }
            if (cr > bestCR) {
                entries.clear();
                bestCR = cr;
            }
        }
        entries.push_back(std::move(candidate));
    }

    // Known powers are ranked against the lowest challenge rating alone: a
    // power above it replaces those gathered so far, and the rating to beat
    // never rises.
    void addKnownPower(Candidate candidate, int cr, bool ranked) {
        if (ranked) {
            if (cr < kLowestChallengeRating) {
                return;
            }
            if (cr > kLowestChallengeRating) {
                entries.clear();
            }
        }
        entries.push_back(std::move(candidate));
    }

    std::shared_ptr<Talent> choose(Game &game) const {
        if (entries.empty()) {
            return nullptr;
        }
        const auto &entry = entries[randomInt(0, static_cast<int>(entries.size()) - 1)];
        return game.newTalent(entry.type, entry.id, entry.classIndex,
                              entry.item, entry.property, entry.casterLevel);
    }
};

// The share of a power's cost Force Body takes from vitality at its level.
int bodyCostShare(int cost, int level) {
    int percent = 0;
    switch (level) {
    case 0:
        percent = 50;
        break;
    case 1:
        percent = 40;
        break;
    case 2:
        percent = 30;
        break;
    default:
        break;
    }
    return cost * percent / 100;
}

} // namespace

// The uses left that a feat reads as having when its uses are unlimited.
static constexpr int kUnlimitedFeatUses = 100;

int Creature::featRemainingUses(FeatType feat) const {
    const auto definition = _services.game.feats.get(feat);
    if (!definition || !hasEffectiveFeat(feat)) {
        return 0;
    }
    const int maximum = definition->usesPerDay;
    // Only a learned feat with uses per day counts its uses; any other feat
    // the creature has is unlimited.
    if (maximum == 0 || !_attributes.hasFeat(feat)) {
        return kUnlimitedFeatUses;
    }
    const int spent = _attributes.spentFeatUses(feat);
    // The uses left are a byte: spending past the maximum wraps round to a
    // large count, which reads as unlimited.
    const int remaining = static_cast<uint8_t>(maximum - spent);
    return remaining < kUnlimitedFeatUses || maximum == spent ? remaining : kUnlimitedFeatUses;
}

void Creature::spendFeatUse(FeatType feat) {
    const auto definition = _services.game.feats.get(feat);
    if (!definition || !_attributes.hasFeat(feat)) {
        return;
    }
    if (definition->usesPerDay != 0) {
        _attributes.spendFeatUse(feat);
    }
}

FeatType Creature::useFeat(FeatType feat) {
    if (!hasEffectiveFeat(feat)) {
        return FeatType::Invalid;
    }
    // A held rank gives way to the next rank of its chain while that is held.
    for (auto successor = _services.game.feats.get(feat)->successor;
         successor != FeatType::Invalid && hasEffectiveFeat(successor);
         successor = _services.game.feats.get(feat)->successor) {
        feat = successor;
    }
    if (feat == FeatType::GuardStance || feat == FeatType::AdvancedGuardStance || feat == FeatType::MasterGuardStance) {
        _guardStanceFeat = inGuardStance() ? FeatType::Invalid : feat;
    }
    return isPhysicalAttackFeat(feat) ? feat : FeatType::Invalid;
}

// Under Force Body a known power is affordable when the Force points cover the
// vitality share; asking for uses left compares the rest of the cost instead.
bool Creature::talentPowerAffordable(const Spell &spell) const {
    const int cost = adjustedSpellForcePointCost(spell, isPartyMember());
    const int body = _game.isTSL() ? forceBodyLevel() : -1;
    if (body == -1) {
        return cost <= currentForce();
    }
    const int share = bodyCostShare(cost, body);
    return share <= currentForce() && currentHitPoints() - share > 1;
}

bool Creature::hasAffordableSpell(const Spell &spell) const {
    return _attributes.hasSpell(spell.type) && talentPowerAffordable(spell);
}

bool Creature::hasSpellUsesLeft(const Spell &spell, size_t classIndex) const {
    if (classIndex >= _attributes.classLevels().size()) {
        return false;
    }
    const int cost = adjustedSpellForcePointCost(spell, isPartyMember());
    const int body = _game.isTSL() ? forceBodyLevel() : -1;
    const int share = body == -1 ? 0 : bodyCostShare(cost, body);
    return cost - share <= currentForce() && (body == -1 || currentHitPoints() - share > 0);
}

bool Creature::canLearnPower(SpellType power, ClassType owner) const {
    return _services.game.spells.get(power) && (!_isPC || isForceUsingClass(owner, _game.isTSL()));
}

const Object &Creature::itemRepositoryOwner(bool useParty) const {
    if ((isPartyMember() && useParty) || _game.inventoryMenuCharacter() != -1) {
        return *_game.party().player();
    }
    return *this;
}

std::vector<Creature::ItemPowerSource> Creature::talentItemPowers() const {
    std::vector<ItemPowerSource> result;
    const auto append = [&](const std::shared_ptr<Item> &item, bool equipped) {
        if (!item || (!equipped && item->isEquippable())) {
            return;
        }
        const auto &properties = item->properties();
        for (size_t i = 0; i < properties.size(); ++i) {
            const auto &property = properties[i];
            if (property.type() != ItemProperty::ActivateItem) {
                continue;
            }
            if (!property.usable && (equipped || item->charges() == 0)) {
                continue;
            }
            if (!item->isPropertyActive(property)) {
                continue;
            }
            result.push_back({item, i, equipped});
        }
    };
    for (int slot = 0; slot != InventorySlots::count; ++slot) {
        const auto found = _equipment.find(slot);
        if (found != _equipment.end()) {
            append(found->second, true);
        }
    }
    for (const auto &item : itemRepositoryOwner().items()) {
        append(item, false);
    }
    return result;
}

std::optional<std::pair<std::shared_ptr<Item>, size_t>> Creature::itemForPower(int spellId) const {
    for (const auto &source : talentItemPowers()) {
        if (source.item->properties()[source.property].subtype == spellId) {
            return std::make_pair(source.item, source.property);
        }
    }
    return std::nullopt;
}

bool Creature::hasTalent(TalentType type, int id) const {
    if (type == Talent::kNoType || id == kInvalidTalentId) {
        return false;
    }
    // The dead have talents only while they can still move and act.
    constexpr int kMoveAndAct = kAIStateMove | kAIStateAct;
    if ((_effectAIStateMask & kMoveAndAct) != kMoveAndAct && isDead()) {
        return false;
    }
    if (isPartyMember() && currentHitPoints() <= 0) {
        return false;
    }
    switch (type) {
    case TalentType::Feat:
        return featRemainingUses(static_cast<FeatType>(id)) != 0;
    case TalentType::Skill: {
        const int rank = getUnopposedSkillRank(static_cast<SkillType>(id));
        if (rank > 0) {
            return true;
        }
        // A skill the skills table has no row for cannot be used untrained.
        const auto skill = _services.game.skills.get(static_cast<SkillType>(id));
        return skill && skill->untrained;
    }
    case TalentType::Spell: {
        const auto spell = _services.game.spells.get(static_cast<SpellType>(id));
        if (!spell) {
            return false;
        }
        bool found = false;
        const auto &classes = _attributes.classLevels();
        for (size_t index = 0; index < classes.size(); ++index) {
            const bool known = hasAffordableSpell(*spell) && hasSpellUsesLeft(*spell, index);
            if (isForceUsingClass(classes[index].first->type(), _game.isTSL())) {
                found |= known;
            } else {
                const bool special = std::any_of(_spellLikeAbilities.begin(), _spellLikeAbilities.end(),
                                                 [id](const auto &ability) { return ability.spell == id && ability.flags == 1; });
                found |= special || known || itemForPower(id).has_value();
            }
        }
        return found && (_effectAIStateMask & kAIStateCastPowers) != 0;
    }
    default:
        return false;
    }
}

std::shared_ptr<Talent> Creature::selectTalent(int category, int crMax, int inclusion,
                                               int excludeType, int excludeId) const {
    const bool ranked = crMax != -1;
    const int excludedSpell = excludeType == 0 ? excludeId : -1;
    const int excludedFeat = excludeType == 1 ? excludeId : -1;
    const auto suitable = [&](const Spell &spell, int excluded) {
        const int id = static_cast<int>(spell.type);
        if (id == excluded || !hasTalent(TalentType::Spell, id)) {
            return false;
        }
        return !spatialArea() || categoryMatches(category, static_cast<int>(spell.category));
    };

    const auto specialAbility = [&]() -> std::shared_ptr<Talent> {
        CandidatePool pool;
        for (const auto &ability : _spellLikeAbilities) {
            if (ability.flags == 0) {
                continue;
            }
            const auto spell = _services.game.spells.get(static_cast<SpellType>(ability.spell));
            if (!spell || !suitable(*spell, -1) || !inclusionMatches(spell->exclusion, inclusion)) {
                continue;
            }
            pool.add({TalentType::Spell, ability.spell, kSpellLikeAbilityClass, script::kObjectInvalid, -1, ability.casterLevel},
                     spell->maxCR, ranked);
        }
        return pool.choose(_game);
    };
    const auto knownPower = [&]() -> std::shared_ptr<Talent> {
        const auto &classes = _attributes.classLevels();
        for (size_t index = 0; index < classes.size(); ++index) {
            const auto clazz = classes[index].first->type();
            if (!isForceUsingClass(clazz, _game.isTSL())) {
                continue;
            }
            CandidatePool pool;
            for (const auto type : _attributes.spellsForClass(clazz)) {
                const auto spell = _services.game.spells.get(type);
                if (!spell || !talentPowerAffordable(*spell) || !suitable(*spell, excludedSpell)) {
                    continue;
                }
                const int id = static_cast<int>(type);
                pool.addKnownPower({TalentType::Spell, id, static_cast<int>(index)},
                                   spell->maxCR, ranked);
            }
            if (auto selected = pool.choose(_game)) {
                return selected;
            }
        }
        return nullptr;
    };
    const auto itemPower = [&]() -> std::shared_ptr<Talent> {
        CandidatePool pool;
        for (const auto &source : talentItemPowers()) {
            const int id = source.item->properties()[source.property].subtype;
            const auto spell = _services.game.spells.get(static_cast<SpellType>(id));
            if (!spell) {
                continue;
            }
            if (source.equipped) {
                if (!suitable(*spell, -1) || !inclusionMatches(spell->exclusion, inclusion)) {
                    continue;
                }
            } else if (!categoryMatches(category, static_cast<int>(spell->category))) {
                continue;
            }
            pool.add({TalentType::Spell, id, kUnselectedCastingClass, source.item->id(), static_cast<int>(source.property)},
                     spell->maxCR, ranked);
        }
        return pool.choose(_game);
    };
    const auto feat = [&]() -> std::shared_ptr<Talent> {
        CandidatePool pool;
        // Feats are drawn from those the creature has learned; feats granted
        // by effects count as held but are not drawn.
        for (const auto type : _attributes.featOrder()) {
            const int id = static_cast<int>(type);
            const auto definition = _services.game.feats.get(type);
            if (!definition || !categoryMatches(category, definition->category) ||
                !inclusionMatches(definition->exclusion, inclusion) || id == excludedFeat ||
                !hasTalent(TalentType::Feat, id))
                continue;
            pool.add({TalentType::Feat, id}, definition->maxCR, ranked);
        }
        return pool.choose(_game);
    };

    bool powersExhausted = false;
    bool featsExhausted = false;
    while (!powersExhausted || !featsExhausted) {
        if (randomInt(0, 1) == 0) {
            if (powersExhausted) {
                continue;
            }
            if (auto selected = specialAbility()) {
                return selected;
            }
            if (auto selected = knownPower()) {
                return selected;
            }
            if (auto selected = itemPower()) {
                return selected;
            }
            powersExhausted = true;
        } else {
            if (featsExhausted) {
                continue;
            }
            // Excluding every feat marks the powers exhausted, yet the feats
            // are still drawn from.
            if (excludeType == 1 && excludeId == -1) {
                powersExhausted = true;
            }
            if (auto selected = feat()) {
                return selected;
            }
            featsExhausted = true;
        }
    }
    return _game.newTalent(Talent::kNoType, kInvalidTalentId, kUnselectedCastingClass);
}

// END Talent selection

} // namespace game

} // namespace reone
