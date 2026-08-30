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

#include <array>

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/game/action.h"
#include "reone/game/action/attackobject.h"
#include "reone/game/animationutil.h"
#include "reone/game/attack.h"
#include "reone/game/d20/classes.h"
#include "reone/game/debug.h"
#include "reone/game/di/services.h"
#include "reone/game/effect/acdecrease.h"
#include "reone/game/effect/acincrease.h"
#include "reone/game/effect/attackdecrease.h"
#include "reone/game/effect/attackincrease.h"
#include "reone/game/effect/damage.h"
#include "reone/game/effect/damagedecrease.h"
#include "reone/game/effect/damageincrease.h"
#include "reone/game/effect/immunity.h"
#include "reone/game/effect/savingthrowdecrease.h"
#include "reone/game/effect/savingthrowincrease.h"
#include "reone/game/footstepsounds.h"
#include "reone/game/game.h"
#include "reone/game/party.h"
#include "reone/game/portraits.h"
#include "reone/game/script/runner.h"
#include "reone/game/surfaces.h"
#include "reone/game/twodautil.h"
#include "reone/graphics/context.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/textureregistry.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/gff.h"
#include "reone/resource/provider/2das.h"
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

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;
using namespace reone::script;

namespace reone {

namespace game {

static constexpr int kStrRefRemains = 38151;
static constexpr int kMaximumDodgeBonus = 10;
static constexpr int kMaximumSavingThrowModifier = 20;
static constexpr int kAllSavingThrows = 0;
static constexpr int kFortitudeSavingThrow = 1;
static constexpr int kSituationalAttackBonus = 10;
static constexpr float kCloseRangeAttackDistance2 = 25.0f;
static constexpr size_t kACBonusTypeCount = static_cast<size_t>(ACBonus::Deflection) + 1;
static constexpr float kKeepPathDuration = 1000.0f;
static constexpr float kPathPointTolerance = 0.5f;

static constexpr char kItemPropertyCostTable[] = "iprp_costtable";
static constexpr char kBonusCostTable[] = "iprp_bonuscost";
static constexpr char kMeleeCostTable[] = "iprp_meleecost";
static constexpr char kDecreaseCostTable[] = "iprp_neg5cost";
static constexpr char kDamageCostTable[] = "iprp_damagecost";
static constexpr char kResistanceCostTable[] = "iprp_resistcost";
static constexpr char kReductionCostTable[] = "iprp_soakcost";
static constexpr char kVulnerabilityCostTable[] = "iprp_damvulcost";
static constexpr char kDamageTypeTable[] = "iprp_damagetype";
static constexpr char kProtectionTable[] = "iprp_protection";

static std::string g_talkDummyNode("talkdummy");

static const std::string g_headHookNode("headhook");
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

static bool attackModifierApplies(
    AttackBonus modifierType,
    const Item *weapon,
    bool offHand) {

    switch (modifierType) {
    case AttackBonus::Misc:
        return true;
    case AttackBonus::Onhand:
        return weapon && !offHand;
    case AttackBonus::Offhand:
        return weapon && offHand;
    default:
        return false;
    }
}

static bool racialTypeMatches(uint16_t racialType, const Creature &target) {
    auto type = static_cast<RacialType>(racialType);
    return type == RacialType::All || type == target.racialType();
}

static bool attackAlignmentGroupMatches(uint16_t alignment, const Creature &target) {
    auto group = static_cast<Alignment>(alignment);
    // The native attack-property handler stores Neutral in the unused
    // law/chaos qualifier, so it applies to every target.
    return group == Alignment::All ||
           group == Alignment::Neutral ||
           group == target.alignment();
}

static bool defenseAlignmentGroupMatches(uint16_t alignment, const Creature &attacker) {
    auto group = static_cast<Alignment>(alignment);
    return group == Alignment::All || group == attacker.alignment();
}

static DamageType getItemPropertyDamageType(
    ServicesView &services,
    uint16_t subtype) {

    auto table = getRequiredTwoDA(
        services.resource.twoDas,
        kDamageTypeTable);
    validateTwoDARow(*table, kDamageTypeTable, subtype);
    return static_cast<DamageType>(1 << subtype);
}

static DamagePower getDamageReductionPower(
    ServicesView &services,
    uint16_t subtype) {

    auto table = getRequiredTwoDA(
        services.resource.twoDas,
        kProtectionTable);
    validateTwoDARow(*table, kProtectionTable, subtype);
    return static_cast<DamagePower>(subtype + 1);
}

static bool acDamageTypePropertyApplies(uint16_t subtype, int damageFlags) {
    // KOTOR 1 stores the raw IPRP_COMBATDAM row in the effect qualifier, then
    // compares it directly with runtime damage flags. Preserve that mismatch.
    switch (subtype) {
    case 1:
        return damageFlags == 0;
    case 2:
        return damageFlags == static_cast<int>(DamageType::Bludgeoning);
    case 4:
        return damageFlags == static_cast<int>(DamageType::Piercing);
    default:
        return false;
    }
}

static bool attackPropertyApplies(
    ItemProperty property,
    uint16_t subtype,
    const Creature *target) {

    switch (property) {
    case ItemProperty::EnhancementBonus:
    case ItemProperty::AttackBonus:
        return true;
    case ItemProperty::EnhancementBonusVsAlignmentGroup:
    case ItemProperty::AttackBonusVsAlignmentGroup:
        return target && attackAlignmentGroupMatches(subtype, *target);
    case ItemProperty::EnhancementBonusVsRacialGroup:
    case ItemProperty::AttackBonusVsRacialGroup:
        return target && racialTypeMatches(subtype, *target);
    default:
        return false;
    }
}

static bool defensePropertyApplies(
    ItemProperty property,
    uint16_t subtype,
    const Creature *attacker,
    int damageFlags) {

    switch (property) {
    case ItemProperty::AcBonus:
        return true;
    case ItemProperty::AcBonusVsAlignmentGroup:
        return attacker && defenseAlignmentGroupMatches(subtype, *attacker);
    case ItemProperty::AcBonusVsDamageType:
        return acDamageTypePropertyApplies(subtype, damageFlags);
    case ItemProperty::AcBonusVsRacialGroup:
        return attacker && racialTypeMatches(subtype, *attacker);
    default:
        return false;
    }
}

static void getSituationalAttackBonuses(
    const Creature &attacker,
    const Creature &target,
    const Item *weapon,
    int &closeProximityRangedBonus,
    int &meleeOnRangedBonus) {

    closeProximityRangedBonus = 0;
    meleeOnRangedBonus = 0;

    if (weapon && weapon->isRanged()) {
        if (attacker.getSquareDistanceTo(target) <= kCloseRangeAttackDistance2) {
            closeProximityRangedBonus = kSituationalAttackBonus;
        }
        return;
    }

    auto targetWeapon = target.getEquippedItem(InventorySlots::rightWeapon);
    if (targetWeapon && targetWeapon->isRanged()) {
        meleeOnRangedBonus = kSituationalAttackBonus;
    }
}

static bool equippedItemAppliesToAttack(
    int slot,
    const Item &item,
    const Item *weapon,
    bool offHand) {

    switch (slot) {
    case InventorySlots::rightWeapon:
        return weapon == &item &&
               (!offHand || item.weaponWield() == WeaponWield::DoubleBladedSword);
    case InventorySlots::leftWeapon:
        return weapon == &item && offHand;
    case InventorySlots::hands:
        return !weapon && !offHand;
    case InventorySlots::cWeaponL:
    case InventorySlots::cWeaponR:
    case InventorySlots::cWeaponB:
    case InventorySlots::rightWeapon2:
    case InventorySlots::leftWeapon2:
        return false;
    default:
        return true;
    }
}

static bool isHandSpecificAttackModifierSlot(int slot) {
    switch (slot) {
    case InventorySlots::rightWeapon:
    case InventorySlots::leftWeapon:
    case InventorySlots::hands:
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

static int getCostTableValue(
    ServicesView &services,
    const std::string &resRef,
    int row,
    const std::string &column,
    int blankValue) {

    auto table = getRequiredTwoDA(services.resource.twoDas, resRef);
    return table->getInt(row, column, blankValue);
}

struct NamedTwoDA {
    std::string resRef;
    std::shared_ptr<TwoDA> table;
};

static NamedTwoDA getItemPropertyCostTable(
    ServicesView &services,
    int index) {

    auto costTables = getRequiredTwoDA(
        services.resource.twoDas,
        kItemPropertyCostTable);
    std::string resRef = boost::to_lower_copy(
        costTables->getString(index, "name"));
    return {resRef, getRequiredTwoDA(services.resource.twoDas, resRef)};
}

static int getItemPropertyValue(
    ServicesView &services,
    const Item::PropertyEntry &property,
    const std::string &column,
    int blankValue) {

    auto costTable = getItemPropertyCostTable(
        services,
        property.costTable);
    return costTable.table->getInt(
        property.costValue,
        column,
        blankValue);
}

static bool savingThrowModifierApplies(
    int save,
    SavingThrowType modifierType,
    int requestedSave,
    SavingThrowType requestedType) {

    return (save == kAllSavingThrows || save == requestedSave) &&
           (modifierType == SavingThrowType::All ||
            modifierType == requestedType);
}

static bool savingThrowPropertyApplies(
    ItemProperty property,
    uint16_t subtype,
    int requestedSave,
    SavingThrowType requestedType) {

    switch (property) {
    case ItemProperty::ImprovedSavingThrow:
    case ItemProperty::DecreasedSavingThrows:
        return subtype == static_cast<uint16_t>(SavingThrowType::All) ||
               subtype == static_cast<uint16_t>(requestedType);
    case ItemProperty::ImprovedSavingThrowSpecific:
    case ItemProperty::DecreasedSavingThrowsSpecific:
        return subtype == kAllSavingThrows || subtype == requestedSave;
    default:
        return false;
    }
}

struct DamageModifier {
    int costValue;
    int numDice;
    int die;
    int flat;
    DamageType type;
};

static std::optional<DamageModifier> getDamageModifier(
    ServicesView &services,
    uint16_t costValue,
    DamageType type) {

    auto table = getRequiredTwoDA(services.resource.twoDas, kDamageCostTable);

    DamageModifier result {costValue, 0, 0, 0, type};

    auto numDice = table->getIntOpt(costValue, "numdice");
    if (!numDice) {
        result.flat = costValue;
    } else {
        auto die = table->getIntOpt(costValue, "die");
        if (!die) {
            throw ValidationException(
                "Missing die in iprp_damagecost row " +
                std::to_string(costValue));
        }
        result.numDice = *numDice;
        result.die = *die;
    }

    if (result.numDice <= 0 && result.flat <= 0) {
        return std::nullopt;
    }
    return result;
}

static std::optional<DamageModifier> getFlatDamageModifier(
    ServicesView &services,
    const std::string &resRef,
    uint16_t costValue,
    DamageType type) {

    int value = getCostTableValue(
        services,
        resRef,
        costValue,
        "value",
        0);
    if (value == 0) {
        return std::nullopt;
    }

    return DamageModifier {costValue, 0, 0, value, type};
}

static int rollDamageModifier(
    const DamageModifier &modifier,
    int multiplier) {

    if (modifier.numDice <= 0 || modifier.die <= 0) {
        return multiplier * modifier.flat;
    }

    int result = 0;
    for (int multiple = 0; multiple < multiplier; ++multiple) {
        for (int die = 0; die < modifier.numDice; ++die) {
            result += randomInt(1, modifier.die);
        }
    }
    return result;
}

static void selectDamageModifier(
    std::map<int, DamageModifier> &modifiers,
    DamageModifier modifier) {

    int type = static_cast<int>(modifier.type);
    auto it = modifiers.find(type);
    if (it == modifiers.end() ||
        modifier.costValue > it->second.costValue) {
        modifiers.insert_or_assign(type, std::move(modifier));
    }
}

static bool damagePropertyApplies(
    ItemProperty property,
    uint16_t subtype,
    const Creature *target) {

    switch (property) {
    case ItemProperty::EnhancementBonus:
    case ItemProperty::DamageBonus:
    case ItemProperty::DecreasedDamage:
        return true;
    case ItemProperty::EnhancementBonusVsAlignmentGroup:
    case ItemProperty::DamageBonusVsAlignmentGroup:
        return target && attackAlignmentGroupMatches(subtype, *target);
    case ItemProperty::EnhancementBonusVsRacialGroup:
    case ItemProperty::DamageBonusVsRacialGroup:
        return target && racialTypeMatches(subtype, *target);
    default:
        return false;
    }
}

static bool equippedItemAppliesToDefense(int slot) {
    return slot != InventorySlots::rightWeapon2 &&
           slot != InventorySlots::leftWeapon2;
}

static void addDefenseModifier(
    int value,
    ACBonus modifierType,
    std::array<int, kACBonusTypeCount> &modifiers) {

    int index = static_cast<int>(modifierType);
    if (value <= 0 || index < 0 || index >= static_cast<int>(modifiers.size())) {
        return;
    }

    if (modifierType == ACBonus::Dodge) {
        modifiers[index] += value;
    } else {
        modifiers[index] = std::max(modifiers[index], value);
    }
}

static int getDefenseModifier(
    const std::array<int, kACBonusTypeCount> &bonuses,
    const std::array<int, kACBonusTypeCount> &penalties) {

    int result = std::min(
        bonuses[static_cast<int>(ACBonus::Dodge)] -
            penalties[static_cast<int>(ACBonus::Dodge)],
        kMaximumDodgeBonus);

    for (int i = static_cast<int>(ACBonus::Natural);
         i <= static_cast<int>(ACBonus::Deflection);
         ++i) {
        result += bonuses[i] - penalties[i];
    }
    return result;
}

Creature::Creature(
    uint32_t id,
    std::string sceneName,
    Game &game,
    ServicesView &services) :
    Object(id, ObjectType::Creature, std::move(sceneName), game, services) {

    // Workaround: the original engine does not retain perception range in
    // savegames. Set default ranges to match PercepRngDefault from ranges.2da.
    _perception.sightRange = 20.0f;
    _perception.hearingRange = 20.0f;
}

void Creature::retireAreaRuntime(
    Pathfinder &pathfinder,
    const std::set<const Object *> &retainedObjects) {
    retireAreaRuntimeState(retainedObjects);

    if (_path) {
        releasePath(pathfinder, *_path);
        _path.reset();
    }
    _pathVelocity = glm::vec3(0.0f);
    _previousPosition = _position;
    _stuckTimer.reset(0.0f);
    _stuckForce = glm::vec3(0.0f);
    setMovementType(MovementType::None);
    _blockingDoorId = script::kObjectInvalid;
    _blockedEventDoorId = script::kObjectInvalid;

    deactivateCombat(0.0f);
    _combatState.attackTarget.reset();
    _combatState.attemptedAttackTarget.reset();
    _combatState.attackAction = ActionType::QueueEmpty;
    _combatState.combatFeat = FeatType::Invalid;
    _combatState.deactivationTimer.reset(0.0f);
    _lastHostileTarget.reset();
    _lastAttackAction = ActionType::QueueEmpty;
    _lastCombatFeat = FeatType::Invalid;
    _lastAttackResult = AttackResultType::Invalid;

    _perception.seen.clear();
    _perception.heard.clear();
    stopTalking();
    stopStuntMode();
    if (_audioSourceVoice) _audioSourceVoice->stop();
    if (_audioSourceFootstep) _audioSourceFootstep->stop();
    _audioSourceVoice.reset();
    _audioSourceFootstep.reset();
}

void Creature::loadFromBlueprint(const std::string &resRef) {
    auto utc = _services.resource.gffs.get(resRef, ResType::Utc);
    if (!utc) {
        return;
    }
    // A blueprint is a single source, so deserialize it once. Routing through
    // deserialize() would re-read the self-referential TemplateResRef and
    // deserialize the same data twice, doubling accumulated class levels.
    deserializeAll(*utc, SerializedIdentityContext::templateResource(resRef));
    updateTransform();
    loadAppearance();
}

void Creature::loadAppearanceProperties() {
    std::shared_ptr<TwoDA> appearances(_services.resource.twoDas.get("appearance"));
    if (!appearances) {
        throw ResourceNotFoundException("appearance 2DA not found");
    }

    _modelType = parseModelType(appearances->getString(_appearance, "modeltype"));
    _walkSpeed = appearances->getFloat(_appearance, "walkdist", 1.0f);
    _runSpeed = appearances->getFloat(_appearance, "rundist", 1.0f);
    float personalSpace = appearances->getFloat(_appearance, "perspace", 0.6f);
    _creaturePersonalSpace = appearances->getFloat(_appearance, "creperspace", personalSpace);
    _size = static_cast<CreatureSize>(appearances->getInt(
        _appearance,
        "sizecategory",
        static_cast<int>(CreatureSize::Invalid)));
    _footstepType = appearances->getInt(_appearance, "footsteptype", -1);
    _envmap = boost::to_lower_copy(appearances->getString(_appearance, "envmap"));

    if (_portraitId > 0) {
        _portrait = _services.game.portraits.getTextureByIndex(_portraitId);
    } else {
        _portrait = _services.game.portraits.getTextureByAppearance(_appearance);
    }
}

void Creature::loadAppearance() {
    loadAppearanceProperties();

    auto modelSceneNode = buildModel();
    if (modelSceneNode) {
        finalizeModel(*modelSceneNode);
        _sceneNode = std::move(modelSceneNode);
        _sceneNode->setUser(*this);
        _sceneNode->setLocalTransform(_transform);
    }

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
    model->setModel(*replacement);
    finalizeModel(*model);
    if (!_stunt) {
        model->setLocalTransform(_transform);
    }
    _animDirty = true;
}

void Creature::loadTransformFromGIT(const resource::generated::GIT_Creature_List &git) {
    _position[0] = git.XPosition;
    _position[1] = git.YPosition;
    _position[2] = git.ZPosition;

    float cosine = git.XOrientation;
    float sine = git.YOrientation;
    _orientation = glm::quat(glm::vec3(0.0f, 0.0f, -glm::atan(cosine, sine)));

    updateTransform();
}

bool Creature::isDebilitated() const {
    return _combatState.debilitated || hasEffect(EffectType::Stunned);
}

bool Creature::isTemporarilyDead() const {
    return _game.party().isMember(*this) && currentHitPoints() <= 0;
}

bool Creature::canExecuteActions() const {
    return !hasEffect(EffectType::Stunned);
}

bool Creature::isSelectable() const {
    bool hasDropableItems = false;
    for (auto &item : _items) {
        if (item->isDropable()) {
            hasDropableItems = true;
            break;
        }
    }
    return !_dead || hasDropableItems;
}

void Creature::update(float dt) {
    Object::update(dt);
    updateModelAnimation();
    updateCombat(dt);
    updateLightsaberSoundPositions();
}

void Creature::updateModelAnimation() {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model)
        return;

    if (_animFireForget) {
        if (!model->isAnimationFinished())
            return;

        _animFireForget = false;
        _animDirty = true;
    }
    if (!_animDirty)
        return;

    std::shared_ptr<Animation> anim;
    std::shared_ptr<Animation> talkAnim;

    switch (_movementType) {
    case MovementType::Run:
        anim = model->model().getAnimation(getRunAnimation());
        break;
    case MovementType::Walk:
        anim = model->model().getAnimation(getWalkAnimation());
        break;
    default:
        if (_dead) {
            anim = model->model().getAnimation(getDeadAnimation());
        } else if (_talking) {
            anim = model->model().getAnimation(getTalkNormalAnimation());
            talkAnim = model->model().getAnimation(getHeadTalkAnimation());
        } else {
            anim = model->model().getAnimation(getPauseAnimation());
        }
        break;
    }

    if (talkAnim && anim) {
        model->playAnimation(*anim, nullptr, AnimationProperties::fromFlags(AnimationFlags::loopOverlay | AnimationFlags::propagate));
        model->playAnimation(*talkAnim, _lipAnimation, AnimationProperties::fromFlags(AnimationFlags::loopOverlay | AnimationFlags::propagate));
    } else {
        if (anim) {
            // The corpse pose is a short clip; looping/blending it makes the
            // model jerk and never settle, so play it once and hold the final
            // frame. Living poses keep looping and blending.
            int animFlags = _dead ? AnimationFlags::propagate
                                  : (AnimationFlags::loopBlend | AnimationFlags::propagate);
            model->playAnimation(*anim, nullptr, AnimationProperties::fromFlags(animFlags));
        }

        if (talkAnim) {
            model->playAnimation(*talkAnim, _lipAnimation, AnimationProperties::fromFlags(AnimationFlags::loopBlend | AnimationFlags::propagate));
        }
    }

    _animDirty = false;
}

void Creature::restorePrimaryPlayerHitPoints() {
    _currentHitPoints = maxHitPoints();
    _dead = false;
}

void Creature::damage(int amount, uint32_t damager) {
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
    int previousHitPoints = _currentHitPoints;
    if (deathEffect) {
        _currentHitPoints = 0; // special case for Death effect
    } else {
        int adjustedAmount = applyDamageToHitPoints(amount, _currentHitPoints);
        if (amount > 0) {
            _game.floatingText().addDamage(*this, amount, adjustedAmount, damager);
        }
    }

    damager = damager ? damager : script::kObjectInvalid;
    runDamagedScript(damager);

    if (_immortal || _currentHitPoints > 0) {
        return;
    }

    _dead = true;
    _name = _services.resource.strings.getText(kStrRefRemains);

    debug(str(boost::format("Creature %s is dead") % _tag));

    playSound(SoundSetEntry::Dead);
    playAnimation(getDieAnimation());
    runDeathScript(damager);
}

void Creature::updateCombat(float dt) {
    _combatState.deactivationTimer.update(dt);
    _lightsaberIdlePowerDownTimer.update(dt);
    if (_lightsaberIdlePowerDownPending &&
        !_combatState.active &&
        _lightsaberIdlePowerDownTimer.elapsed()) {
        _lightsaberIdlePowerDownPending = false;
        setLightsabersPowered(false, true);
    }
    if (_combatState.shouldDeactivate && _combatState.deactivationTimer.elapsed()) {
        _combatState.active = false;
        _combatState.shouldDeactivate = false;
        _combatState.debilitated = false;
        _combatState.attackTarget.reset();
        _combatState.attemptedAttackTarget.reset();
        _combatState.attackAction = ActionType::QueueEmpty;
        _combatState.combatFeat = FeatType::Invalid;
        _lastHostileTarget.reset();
        _lastAttackAction = ActionType::QueueEmpty;
        _lastCombatFeat = FeatType::Invalid;
        setLastHostileActor(script::kObjectInvalid);

        _animDirty = true;
        setLightsabersPowered(false, true);
    }
}

void Creature::clearAllActions(bool force) {
    Object::clearAllActions(force);
    setMovementType(MovementType::None);
}

void Creature::playAnimation(AnimationType type, AnimationProperties properties) {
    // If animation is looping by type and duration is -1.0, set flags accordingly
    bool looping = isAnimationLooping(type) && properties.duration == -1.0f;
    if (looping) {
        properties.flags |= AnimationFlags::loop;
    }

    std::string animName(getAnimationName(type));
    if (animName.empty())
        return;

    playAnimation(animName, std::move(properties));
}

void Creature::playAnimation(const std::string &name, AnimationProperties properties) {
    bool fireForget = !(properties.flags & AnimationFlags::loop);

    doPlayAnimation(fireForget, [&]() {
        auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
        if (model) {
            model->playAnimation(name, nullptr, properties);
        }
    });
}

bool Creature::doPlayAnimation(bool fireForget, const std::function<void()> &callback) {
    if (!_sceneNode || _movementType != MovementType::None) {
        return false;
    }

    callback();

    if (fireForget) {
        _animFireForget = true;
    }
    return true;
}

bool Creature::playAnimation(const std::shared_ptr<Animation> &anim, AnimationProperties properties) {
    bool fireForget = !(properties.flags & AnimationFlags::loop);

    return doPlayAnimation(fireForget, [&]() {
        auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
        if (model) {
            model->playAnimation(*anim, nullptr, properties);
        }
    });
}

bool Creature::playExternalAnimation(const std::shared_ptr<Animation> &anim, AnimationProperties properties) {
    properties.flags |= AnimationFlags::retargetRoot;
    bool started = doPlayAnimation(false, [&]() {
        auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
        if (model) {
            model->playAnimation(*anim, nullptr, properties);
        }
    });
    if (started) {
        // Dialogue owns the external clip until it explicitly releases the
        // participant. A pending state-driven refresh must not install an idle
        // or locomotion channel before the first stunt render update.
        _animDirty = false;
    }
    return started;
}

void Creature::playOverlayAnimation(AnimationType type) {
    std::string animName(getAnimationName(type));
    if (animName.empty()) {
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
    // and the layer erases itself once it has finished.
    model->playAnimation(
        animName,
        nullptr,
        AnimationProperties::fromFlags(
            AnimationFlags::overlay |
            AnimationFlags::layer |
            AnimationFlags::fireForget |
            AnimationFlags::propagate));
}

void Creature::resumeStateDrivenAnimation() {
    _animFireForget = false;
    _animDirty = true;
    updateModelAnimation();
}

void Creature::playAnimation(CombatAnimation anim, CreatureWieldType wield, int variant) {
    std::string animName(getAnimationName(anim, wield, variant));
    if (!animName.empty()) {
        playAnimation(animName, AnimationProperties::fromFlags(AnimationFlags::blend));
    }
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

void Creature::updateDisguise() {
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
        _appearance = static_cast<uint32_t>(disguiseAppearance);
    } else if (_disguised) {
        _appearance = _appearanceBeforeDisguise;
        _disguised = false;
    }
}

bool Creature::equip(int slot, const std::shared_ptr<Item> &item) {
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
    if (previous != item && item->owner() != 0 &&
        item->owner() != script::kObjectInvalid) {
        return false;
    }
    if (previous && previous != item) return false;
    _equipment[slot] = item;
    item->setEquipped(true);
    item->setOwner(_id);

    updateEquipmentPresentation();
    if (_sceneNode && _combatState.active &&
        (slot == InventorySlots::rightWeapon ||
         slot == InventorySlots::leftWeapon)) {
        setLightsabersPowered(true, true);
    }

    return true;
}

bool Creature::replaceEquipment(
    int slot,
    const std::shared_ptr<Item> &item,
    Object &displacedReceiver) {
    auto previous = getEquippedItem(slot);
    if (!previous || previous == item) return equip(slot, item);
    if (!item || (!item->isRuntimeLive() && !item->isPresentationOnly()) ||
        !item->isEquippable(getEquipabilitySlot(slot)) ||
        std::find(_items.begin(), _items.end(), item) != _items.end() ||
        (item->owner() != 0 && item->owner() != script::kObjectInvalid)) {
        return false;
    }
    for (const auto &[equippedSlot, equipped] : _equipment) {
        if (equipped == item && equippedSlot != slot) return false;
    }
    if ((!previous->isRuntimeLive() && !previous->isPresentationOnly()) ||
        (!displacedReceiver.isRuntimeLive() &&
         !displacedReceiver.isPresentationOnly()) ||
        (isPresentationOnly() != displacedReceiver.isPresentationOnly())) {
        return false;
    }

    // All ordinary recoverable validation is complete. The remainder is one
    // synchronous ownership move; only exceptional allocation failure remains.
    previous->powerDown(_position);
    previous->setEquipped(false);
    previous->setOwner(0);
    _equipment[slot] = item;
    item->setEquipped(true);
    item->setOwner(_id);
    displacedReceiver.addItem(previous);
    updateEquipmentPresentation();
    return true;
}

std::shared_ptr<Item> Creature::takeEquippedItem(
    const std::shared_ptr<Item> &item) {
    auto equipped = std::find_if(
        _equipment.begin(), _equipment.end(),
        [&item](const auto &entry) { return entry.second == item; });
    if (equipped == _equipment.end()) return nullptr;

    auto result = equipped->second;
    result->powerDown(_position);
    result->setEquipped(false);
    result->setOwner(0);
    _equipment.erase(equipped);
    updateEquipmentPresentation();
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
    if (!taken) return false;
    receiver.addItem(taken);
    return true;
}

void Creature::updateEquipmentPresentation() {
    uint32_t prevAppearance = _appearance;
    updateDisguise();
    if (_appearance != prevAppearance) {
        // Refresh appearance-derived state so the in-place model swap below picks
        // up the disguise (or restored) model; rebuilding the scene node here would
        // orphan it from the area scene graph.
        loadAppearanceProperties();
    }
    if (_sceneNode) updateModel();
}

std::shared_ptr<Item> Creature::getEquippedItem(int slot) const {
    auto equipped = _equipment.find(slot);
    return equipped != _equipment.end() ? equipped->second : nullptr;
}

bool Creature::isSlotEquipped(int slot) const {
    return _equipment.find(slot) != _equipment.end();
}

void Creature::setMovementType(MovementType type) {
    if (_movementType == type)
        return;

    _movementType = type;
    _animDirty = true;
    _animFireForget = false;
}

glm::vec3 Creature::getSelectablePosition() const {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model) {
        return _position;
    }
    if (_dead) {
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

float Creature::getAttackRange() const {
    float result = kDefaultAttackRange;

    std::shared_ptr<Item> item(getEquippedItem(InventorySlots::rightWeapon));
    if (item && item->attackRange() > kDefaultAttackRange) {
        result = item->attackRange();
    }

    return result;
}

bool Creature::isLevelUpPending() const {
    return _xp >= getNeededXP();
}

int Creature::getNeededXP() const {
    int level = _attributes.getAggregateLevel();
    return level * (level + 1) * 500;
}

void Creature::runSpawnScript() {
    // Retail gates the creation script on CreatnScrptFird, so it fires at most
    // once per creature rather than once per area attachment. A party member
    // carried through an ordinary module transition is the same creature
    // object: the destination area takes it in without recreating it, and its
    // OnSpawn must not run a second time. Retail also latches the flag
    // regardless of whether a script was authored.
    if (_spawnScriptFired) {
        return;
    }
    _spawnScriptFired = true;
    if (!_onSpawn.empty()) {
        _game.scriptRunner().run(_onSpawn, _id);
    }
}

void Creature::runBlockedScript(uint32_t blockingDoorId) {
    if (_onBlocked.empty()) {
        return;
    }
    // The obstructing door travels with the run as an argument, so every
    // continuation and delayed action started from it goes on seeing the door
    // this event was raised for, whatever the creature has run into since.
    _game.scriptRunner().run(
        _onBlocked,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::BlockingDoor, Variable::ofObject(blockingDoorId)}});
}

void Creature::runEndRoundScript() {
    if (!_onEndRound.empty()) {
        _game.scriptRunner().run(_onEndRound, _id);
    }
}

void Creature::runDialogueScript(uint32_t speakerId, int32_t listenNumber) {
    _game.scriptRunner().run(
        _onDialogue,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastSpeaker, Variable::ofObject(speakerId)},
         {script::ArgKind::ListenPatternNumber, Variable::ofInt(listenNumber)}});
}

void Creature::giveXP(int amount) {
    setXP(_xp + amount);
}

void Creature::setXP(int xp) {
    bool wasLevelUpPending = isLevelUpPending();
    _xp = xp;

    if (!wasLevelUpPending && isLevelUpPending()) {
        _game.notifyLevelUpPending(*this);
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
    std::optional<glm::vec3> position;
    if (positional) {
        position = _position + glm::vec3 {0.0f, 0.0f, 1.7f};
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

void Creature::runDamagedScript(uint32_t damagerId) {
    if (_onDamaged.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onDamaged,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastAttacker, Variable::ofObject(damagerId)},
         {script::ArgKind::LastDamager, Variable::ofObject(damagerId)}});
}

void Creature::runDeathScript(uint32_t damagerId) {
    if (_onDeath.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onDeath,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastAttacker, Variable::ofObject(damagerId)},
         {script::ArgKind::LastDamager, Variable::ofObject(damagerId)}});
}

CreatureWieldType Creature::getWieldType() const {
    auto rightWeapon = getEquippedItem(InventorySlots::rightWeapon);
    auto leftWeapon = getEquippedItem(InventorySlots::leftWeapon);

    if (rightWeapon && leftWeapon) {
        return (rightWeapon->weaponWield() == WeaponWield::BlasterPistol) ? CreatureWieldType::DualPistols : CreatureWieldType::DualSwords;
    } else if (rightWeapon) {
        switch (rightWeapon->weaponWield()) {
        case WeaponWield::SingleSword:
            return CreatureWieldType::SingleSword;
        case WeaponWield::DoubleBladedSword:
            return CreatureWieldType::DoubleBladedSword;
        case WeaponWield::BlasterPistol:
            return CreatureWieldType::BlasterPistol;
        case WeaponWield::BlasterRifle:
            return CreatureWieldType::BlasterRifle;
        case WeaponWield::HeavyWeapon:
            return CreatureWieldType::HeavyWeapon;
        case WeaponWield::StunBaton:
        default:
            return CreatureWieldType::StunBaton;
        }
    }

    if (attributes().hasFeat(FeatType::ComplexUnarmedAnims)) {
        return CreatureWieldType::HandToHandComplex;
    }

    return CreatureWieldType::HandToHand;
}

void Creature::startTalking(const std::shared_ptr<LipAnimation> &animation) {
    if (!_talking || _lipAnimation != animation) {
        _lipAnimation = animation;
        _talking = true;
        _animDirty = true;
    }
}

void Creature::stopTalking() {
    if (_talking || _lipAnimation) {
        _lipAnimation.reset();
        _talking = false;
        _animDirty = true;
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

void Creature::runOnNotice(const Object &object, bool heard, bool seen) {
    // Execute onNotice once to handle both "heard" and "seen" perception
    // checks. k_ai_master script checks them in sequence, and performs
    // differently when an object is just "heard" assuming that it is not
    // "seen".

    if (_onNotice.empty()) {
        return;
    }

    _game.scriptRunner().run(
        _onNotice,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastPerceived, Variable::ofObject(object.id())},
         {script::ArgKind::LastPerceptionHeard, Variable::ofInt(heard)},
         {script::ArgKind::LastPerceptionInaudible, Variable::ofInt(!heard)},
         {script::ArgKind::LastPerceptionSeen, Variable::ofInt(seen)},
         {script::ArgKind::LastPerceptionVanished, Variable::ofInt(!seen)}});
}

void Creature::activateCombat() {
    _lightsaberIdlePowerDownPending = false;
    if (_combatState.active) {
        _combatState.shouldDeactivate = false;
        return;
    }
    _combatState.active = true;
    _combatState.shouldDeactivate = false;
    _animDirty = true;
    setLightsabersPowered(true, true);
}

void Creature::setLightsabersPowered(bool powered, bool animate) {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model) {
        return;
    }

    const std::array<std::pair<int, std::string>, 2> weaponSlots {{
        {InventorySlots::rightWeapon, g_rightHandNode},
        {InventorySlots::leftWeapon, g_leftHandNode},
    }};
    for (auto [slot, attachment] : weaponSlots) {
        auto weapon = static_cast<ModelSceneNode *>(model->getAttachment(attachment));
        if (!weapon || weapon->model().classification() != MdlClassification::lightsaber) {
            continue;
        }

        const auto activeAnimation = weapon->activeAnimationName();
        if ((powered && (activeAnimation == "powerup" || activeAnimation == "powered")) ||
            (!powered && (activeAnimation == "powerdown" || activeAnimation == "off"))) {
            continue;
        }
        weapon->playAnimation(animate ? (powered ? "powerup" : "powerdown") : (powered ? "powered" : "off"));
        if (!animate) {
            continue;
        }
        auto item = getEquippedItem(slot);
        if (item) {
            powered ? item->powerUp(_position) : item->powerDown(_position);
        }
    }
}

void Creature::updateLightsaberSoundPositions() {
    for (int slot : {InventorySlots::rightWeapon, InventorySlots::leftWeapon}) {
        auto item = getEquippedItem(slot);
        if (item) {
            item->updatePoweredSoundPosition(_position);
        }
    }
}

void Creature::deactivateCombat(float delay) {
    if (delay <= 0.0f) {
        _lightsaberIdlePowerDownPending = false;
        _combatState.active = false;
        _combatState.shouldDeactivate = false;
        _combatState.debilitated = false;
        _animDirty = true;
        setLightsabersPowered(false, true);
        return;
    }
    if (!_combatState.active) {
        return;
    }
    _combatState.shouldDeactivate = true;
    _combatState.deactivationTimer.reset(delay);
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

void Creature::beginCombatAttack(std::shared_ptr<Object> target, FeatType feat) {
    _combatState.attackTarget = target;
    _combatState.attackAction = ActionType::AttackObject;
    _combatState.combatFeat = feat;
}

void Creature::setAttemptedAttackTarget(uint32_t target) {
    _combatState.attemptedAttackTarget = _game.getObjectById(target);
}

void Creature::finishCombatRound() {
    if (auto target = _combatState.attackTarget.resolve()) {
        _lastHostileTarget = target;
    } else {
        _lastHostileTarget.reset();
    }
    _lastAttackAction = _combatState.attackAction;
    if (_combatState.combatFeat != FeatType::Invalid) {
        _lastCombatFeat = _combatState.combatFeat;
    }
}

void Creature::adjustModifiedAttacks(int amount) {
    _modifiedAttacks = std::clamp(_modifiedAttacks + amount, 0, 2);
}

void Creature::onEffectsCleared() {
    _modifiedAttacks = 0;
    _assuredHit = false;
}

bool Creature::applyAssuredHit() {
    if (_assuredHit) {
        return false;
    }
    _assuredHit = true;
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

AttackBonusBreakdown Creature::getAttackBonusBreakdown(
    const Creature *target,
    const Item *weapon,
    bool offHand) const {

    AttackBonusBreakdown result;

    int strengthModifier = _attributes.getAbilityModifier(Ability::Strength);
    int dexterityModifier = _attributes.getAbilityModifier(Ability::Dexterity);

    if (weapon && weapon->isRanged()) {
        result.dexterityModifier = dexterityModifier;
    } else if (weapon && dexterityModifier > strengthModifier) {
        bool finesse = !_game.isTSL() && weapon->isLightsaber();
        if (weapon->isLightsaber() &&
            _attributes.hasFeat(FeatType::FinesseLightsabers)) {
            finesse = true;
        }
        switch (weapon->weaponWield()) {
        case WeaponWield::StunBaton:
        case WeaponWield::SingleSword:
        case WeaponWield::DoubleBladedSword:
            finesse = finesse ||
                      _attributes.hasFeat(FeatType::FinesseMeleeWeapons);
            break;
        default:
            break;
        }

        if (finesse) {
            result.dexterityModifier = dexterityModifier;
        } else {
            result.strengthModifier = strengthModifier;
        }
    } else {
        result.strengthModifier = strengthModifier;
    }

    int modifierBonus = 0;
    int modifierPenalty = 0;

    for (const auto &applied : effects()) {
        if (!applied.effect) {
            continue;
        }
        switch (applied.effect->type()) {
        case EffectType::AttackIncrease: {
            const auto &effect = static_cast<const AttackIncreaseEffect &>(*applied.effect);
            if (effect.bonus() > 0 &&
                attackModifierApplies(effect.modifierType(), weapon, offHand)) {
                modifierBonus += effect.bonus();
            }
            break;
        }
        case EffectType::AttackDecrease: {
            const auto &effect = static_cast<const AttackDecreaseEffect &>(*applied.effect);
            if (effect.penalty() > 0 &&
                attackModifierApplies(effect.modifierType(), weapon, offHand)) {
                modifierPenalty += effect.penalty();
            }
            break;
        }
        default:
            break;
        }
    }

    for (const auto &[slot, item] : _equipment) {
        if (!item || !equippedItemAppliesToAttack(slot, *item, weapon, offHand)) {
            continue;
        }

        int itemBonus = 0;
        int itemPenalty = 0;
        for (const auto &property : item->properties()) {
            if (property.upgradeType != 0) {
                continue;
            }

            int modifier = 0;
            auto propertyType = static_cast<ItemProperty>(property.propertyName);
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
                    "value",
                    0);
                if (value <= 0) {
                    continue;
                }
                modifier = value;
                break;
            }
            case ItemProperty::AttackPenalty:
            case ItemProperty::DecreasedAttackModifier: {
                int value = getCostTableValue(
                    _services,
                    kDecreaseCostTable,
                    property.costValue,
                    "value",
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
        modifierBonus += itemBonus;
        modifierPenalty += itemPenalty;
    }

    result.effectBonus = std::min(modifierBonus, 20) -
                         std::min(modifierPenalty, 20);

    if (weapon &&
        weapon->weaponFocusFeat() != FeatType::Invalid &&
        _attributes.hasFeat(weapon->weaponFocusFeat())) {
        result.weaponFocusBonus = 1;
    }

    if (target) {
        getSituationalAttackBonuses(
            *this,
            *target,
            weapon,
            result.closeProximityRangedBonus,
            result.meleeOnRangedBonus);
    }

    int twoWeaponPenalty = getTwoWeaponAttackPenalty(
        weapon,
        offHand,
        &result.smallOffhandBonus);
    result.dualWieldPenalty = -twoWeaponPenalty - result.smallOffhandBonus;

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
    return result;
}

int Creature::getAttackBonus(bool offHand) const {
    auto weapon = offHand
                      ? getOffhandAttackWeapon()
                      : getEquippedItem(InventorySlots::rightWeapon);
    return getAttackBonusBreakdown(nullptr, weapon.get(), offHand).total();
}

int Creature::getDefense(const Creature *attacker, int damageFlags) const {
    int dexterityModifier = _attributes.getAbilityModifier(Ability::Dexterity);
    auto armor = getEquippedItem(InventorySlots::body);
    int armorDefense = armor ? armor->baseDefense() : 0;

    if (isDebilitated()) {
        dexterityModifier = std::min(dexterityModifier, 0);
    } else if (armorDefense > 0 && armor->maxDexterityBonus() >= 0) {
        dexterityModifier = std::min(
            dexterityModifier,
            armor->maxDexterityBonus());
    }

    std::array<int, kACBonusTypeCount> modifierBonuses {};
    std::array<int, kACBonusTypeCount> modifierPenalties {};

    for (const auto &applied : effects()) {
        if (!applied.effect) {
            continue;
        }
        switch (applied.effect->type()) {
        case EffectType::ACIncrease: {
            const auto &effect = static_cast<const ACIncreaseEffect &>(*applied.effect);
            if (damageTypeMatches(effect.damageType(), damageFlags)) {
                addDefenseModifier(
                    effect.bonus(),
                    effect.modifierType(),
                    modifierBonuses);
            }
            break;
        }
        case EffectType::ACDecrease: {
            const auto &effect = static_cast<const ACDecreaseEffect &>(*applied.effect);
            if (damageTypeMatches(effect.damageType(), damageFlags)) {
                addDefenseModifier(
                    effect.penalty(),
                    effect.modifierType(),
                    modifierPenalties);
            }
            break;
        }
        default:
            break;
        }
    }

    for (const auto &[slot, item] : _equipment) {
        if (!item || !equippedItemAppliesToDefense(slot)) {
            continue;
        }

        for (const auto &property : item->properties()) {
            if (property.upgradeType != 0) {
                continue;
            }

            auto propertyType = static_cast<ItemProperty>(property.propertyName);
            switch (propertyType) {
            case ItemProperty::AcBonus:
            case ItemProperty::AcBonusVsAlignmentGroup:
            case ItemProperty::AcBonusVsDamageType:
            case ItemProperty::AcBonusVsRacialGroup: {
                if (!defensePropertyApplies(
                        propertyType,
                        property.subtype,
                        attacker,
                        damageFlags)) {
                    continue;
                }
                int value = getCostTableValue(
                    _services,
                    kBonusCostTable,
                    property.costValue,
                    "value",
                    0);
                addDefenseModifier(
                    value,
                    item->acBonusType(),
                    modifierBonuses);
                break;
            }
            case ItemProperty::DecreasedAc: {
                int value = getCostTableValue(
                    _services,
                    kDecreaseCostTable,
                    property.costValue,
                    "value",
                    0);
                addDefenseModifier(
                    -value,
                    static_cast<ACBonus>(property.subtype),
                    modifierPenalties);
                break;
            }
            default:
                break;
            }
        }
    }

    int defense = 10 +
                  _attributes.getAggregateDefenseBonus() +
                  armorDefense +
                  _naturalAC +
                  dexterityModifier +
                  getDefenseModifier(modifierBonuses, modifierPenalties) +
                  getDuelingBonus();

    return defense - (isDebilitated() ? 4 : 0);
}

int Creature::getDefense() const {
    return getDefense(nullptr, 0);
}

int Creature::getFortitudeSave(SavingThrowType savingThrowType) const {
    int modifier = 0;
    for (const auto &applied : effects()) {
        if (!applied.effect) {
            continue;
        }
        switch (applied.effect->type()) {
        case EffectType::SavingThrowIncrease: {
            const auto &effect =
                static_cast<const SavingThrowIncreaseEffect &>(*applied.effect);
            if (savingThrowModifierApplies(
                    effect.save(),
                    effect.savingThrowType(),
                    kFortitudeSavingThrow,
                    savingThrowType)) {
                modifier += effect.value();
            }
            break;
        }
        case EffectType::SavingThrowDecrease: {
            const auto &effect =
                static_cast<const SavingThrowDecreaseEffect &>(*applied.effect);
            if (savingThrowModifierApplies(
                    effect.save(),
                    effect.savingThrowType(),
                    kFortitudeSavingThrow,
                    savingThrowType)) {
                modifier -= effect.value();
            }
            break;
        }
        default:
            break;
        }
    }

    for (const auto &[slot, item] : _equipment) {
        if (!item || !equippedItemAppliesToDefense(slot)) {
            continue;
        }

        int itemBonus = 0;
        int itemPenalty = 0;
        for (const auto &property : item->properties()) {
            if (property.upgradeType != 0) {
                continue;
            }

            auto propertyType = static_cast<ItemProperty>(property.propertyName);
            if (!savingThrowPropertyApplies(
                    propertyType,
                    property.subtype,
                    kFortitudeSavingThrow,
                    savingThrowType)) {
                continue;
            }

            switch (propertyType) {
            case ItemProperty::ImprovedSavingThrow:
            case ItemProperty::ImprovedSavingThrowSpecific: {
                int value = getCostTableValue(
                    _services,
                    kBonusCostTable,
                    property.costValue,
                    "value",
                    0);
                itemBonus = std::max(itemBonus, value);
                break;
            }
            case ItemProperty::DecreasedSavingThrows:
            case ItemProperty::DecreasedSavingThrowsSpecific: {
                int value = getCostTableValue(
                    _services,
                    kDecreaseCostTable,
                    property.costValue,
                    "value",
                    0);
                itemPenalty = std::max(itemPenalty, -value);
                break;
            }
            default:
                break;
            }
        }
        modifier += itemBonus - itemPenalty;
    }
    modifier = std::min(modifier, kMaximumSavingThrowModifier);

    int conditioningBonus = 0;
    if (_attributes.hasFeat(FeatType::LightningReflexes)) {
        conditioningBonus = 3;
    } else if (_attributes.hasFeat(FeatType::IronWill)) {
        conditioningBonus = 2;
    } else if (_attributes.hasFeat(FeatType::GreatFortitude)) {
        conditioningBonus = 1;
    }

    return _attributes.getAggregateSavingThrows().fortitude +
           _attributes.getAbilityModifier(Ability::Constitution) +
           _fortBonus +
           conditioningBonus +
           modifier;
}

bool Creature::rollFortitudeSave(
    int difficultyClass,
    SavingThrowType savingThrowType) const {

    return randomInt(1, 20) + getFortitudeSave(savingThrowType) >= difficultyClass;
}

int Creature::getPhysicalDamageBonus(
    const Item *weapon,
    bool offHand) const {

    int strengthModifier = _attributes.getAbilityModifier(Ability::Strength);
    int abilityModifier = strengthModifier;

    if (weapon && weapon->isRanged()) {
        if (strengthModifier > 0) {
            int mighty = 0;
            for (const auto &property : weapon->properties()) {
                if (property.upgradeType != 0 ||
                    property.propertyName != static_cast<uint16_t>(ItemProperty::Mighty)) {
                    continue;
                }
                mighty = property.costValue;
                break;
            }
            abilityModifier = std::min(strengthModifier, mighty);
        }
    } else if (strengthModifier > 0) {
        if (offHand) {
            abilityModifier = strengthModifier / 2;
        } else if (weapon &&
                   weapon->weaponWield() != WeaponWield::DoubleBladedSword &&
                   static_cast<int>(weapon->weaponSize()) ==
                       static_cast<int>(_size) + 1) {
            abilityModifier = 3 * strengthModifier / 2;
        }
    }

    int specialization = 0;
    if (weapon &&
        weapon->weaponSpecializationFeat() != FeatType::Invalid &&
        _attributes.hasFeat(weapon->weaponSpecializationFeat())) {
        specialization = 2;
    }

    return abilityModifier + specialization;
}

int Creature::getMassiveCriticalDamage(
    const Item *weapon,
    bool criticalHit) const {

    if (!criticalHit) {
        return 0;
    }

    auto handItem = weapon
                        ? std::shared_ptr<Item>()
                        : getEquippedItem(InventorySlots::hands);
    const Item *sourceItem = weapon ? weapon : handItem.get();
    if (!sourceItem) {
        return 0;
    }

    for (const auto &property : sourceItem->properties()) {
        if (property.upgradeType != 0 ||
            property.propertyName != static_cast<uint16_t>(ItemProperty::MassiveCriticals)) {
            continue;
        }

        auto modifier = getDamageModifier(
            _services,
            property.costValue,
            weapon
                ? getPrimaryDamageType(sourceItem->damageFlags())
                : DamageType::Bludgeoning);
        return modifier
                   ? rollDamageModifier(*modifier, 1)
                   : 0;
    }

    return 0;
}

int Creature::getItemDamageImmunity(DamageType type) const {
    int result = 0;
    int damageFlags = static_cast<int>(type);
    bool immuneToDecrease = plotFlag();

    for (const auto &[slot, item] : _equipment) {
        if (!item || !equippedItemAppliesToDefense(slot)) {
            continue;
        }

        for (const auto &property : item->properties()) {
            if (property.upgradeType != 0) {
                continue;
            }

            auto propertyType = static_cast<ItemProperty>(property.propertyName);
            if (propertyType != ItemProperty::ImmunityDamageType &&
                propertyType != ItemProperty::DamageVulnerability) {
                continue;
            }
            if (propertyType == ItemProperty::DamageVulnerability &&
                immuneToDecrease) {
                continue;
            }

            DamageType propertyDamageType = getItemPropertyDamageType(
                _services,
                property.subtype);
            if (!damageTypeMatches(
                    static_cast<int>(propertyDamageType),
                    damageFlags)) {
                continue;
            }

            int value =
                propertyType == ItemProperty::ImmunityDamageType
                    ? getItemPropertyValue(
                          _services,
                          property,
                          "value",
                          0)
                    : getCostTableValue(
                          _services,
                          kVulnerabilityCostTable,
                          property.costValue,
                          "value",
                          0);
            result = std::clamp(
                result + (propertyType == ItemProperty::ImmunityDamageType
                              ? value
                              : -value),
                -100,
                100);
        }
    }
    return result;
}

int Creature::getItemDamageResistance(DamageType type) const {
    int result = 0;
    int damageFlags = static_cast<int>(type);

    for (const auto &[slot, item] : _equipment) {
        if (!item || !equippedItemAppliesToDefense(slot)) {
            continue;
        }

        for (const auto &property : item->properties()) {
            if (property.upgradeType != 0 ||
                property.propertyName != static_cast<uint16_t>(ItemProperty::DamageResistance)) {
                continue;
            }

            DamageType propertyDamageType = getItemPropertyDamageType(
                _services,
                property.subtype);
            if (!damageTypeMatches(
                    static_cast<int>(propertyDamageType),
                    damageFlags)) {
                continue;
            }

            int value = getCostTableValue(
                _services,
                kResistanceCostTable,
                property.costValue,
                "amount",
                0);
            result = std::max(result, value);
        }
    }
    return result;
}

void Creature::getItemDamageReduction(
    int &amount,
    DamagePower &power) const {

    amount = 0;
    power = DamagePower::Normal;

    for (const auto &[slot, item] : _equipment) {
        if (!item || !equippedItemAppliesToDefense(slot)) {
            continue;
        }

        for (const auto &property : item->properties()) {
            if (property.upgradeType != 0 ||
                property.propertyName != static_cast<uint16_t>(ItemProperty::DamageReduction)) {
                continue;
            }

            DamagePower propertyPower = getDamageReductionPower(
                _services,
                property.subtype);
            int value = getCostTableValue(
                _services,
                kReductionCostTable,
                property.costValue,
                "amount",
                0);
            if (value > amount) {
                amount = value;
                power = propertyPower;
            }
        }
    }
}

int Creature::getDamageResistanceFeatBonus() const {
    int result = 0;
    if (_attributes.hasFeat(FeatType::ImprovedToughness)) {
        result += 2;
    }
    if (_attributes.hasFeat(FeatType::WookieEndurance)) {
        result += 2;
    }
    return result;
}

void Creature::addPhysicalDamageModifiers(
    DamagePacket &damage,
    const Creature *target,
    const Item *weapon,
    bool offHand,
    int criticalMultiplier) const {

    std::vector<DamageModifier> itemBonuses;
    std::vector<DamageModifier> itemPenalties;

    auto handItem = weapon
                        ? std::shared_ptr<Item>()
                        : getEquippedItem(InventorySlots::hands);
    const Item *sourceItem = weapon ? weapon : handItem.get();

    for (const auto &[slot, item] : _equipment) {
        if (!item || !equippedItemAppliesToAttack(slot, *item, weapon, offHand)) {
            continue;
        }

        std::map<int, DamageModifier> bonuses;
        std::map<int, DamageModifier> penalties;

        for (const auto &property : item->properties()) {
            if (property.upgradeType != 0) {
                continue;
            }

            auto propertyType = static_cast<ItemProperty>(property.propertyName);
            if (!damagePropertyApplies(
                    propertyType,
                    property.subtype,
                    target)) {
                continue;
            }

            switch (propertyType) {
            case ItemProperty::EnhancementBonus:
            case ItemProperty::EnhancementBonusVsAlignmentGroup:
            case ItemProperty::EnhancementBonusVsRacialGroup: {
                auto modifier = getFlatDamageModifier(
                    _services,
                    kMeleeCostTable,
                    property.costValue,
                    !weapon && item.get() == sourceItem
                        ? DamageType::Bludgeoning
                        : getPrimaryDamageType(item->damageFlags()));
                if (!modifier || modifier->flat <= 0) {
                    break;
                }
                selectDamageModifier(bonuses, *modifier);
                damage.setPower(static_cast<DamagePower>(modifier->flat));
                break;
            }
            case ItemProperty::DamageBonus: {
                DamageType type = getItemPropertyDamageType(
                    _services,
                    property.subtype);
                auto modifier = getDamageModifier(
                    _services,
                    property.costValue,
                    type);
                if (modifier) {
                    selectDamageModifier(bonuses, *modifier);
                }
                break;
            }
            case ItemProperty::DamageBonusVsAlignmentGroup:
            case ItemProperty::DamageBonusVsRacialGroup: {
                DamageType type = getItemPropertyDamageType(
                    _services,
                    property.paramValue);
                auto modifier = getDamageModifier(
                    _services,
                    property.costValue,
                    type);
                if (modifier) {
                    selectDamageModifier(bonuses, *modifier);
                }
                break;
            }
            case ItemProperty::DecreasedDamage: {
                auto modifier = getFlatDamageModifier(
                    _services,
                    kDecreaseCostTable,
                    property.costValue,
                    !weapon && item.get() == sourceItem
                        ? DamageType::Bludgeoning
                        : getPrimaryDamageType(item->damageFlags()));
                if (!modifier) {
                    break;
                }
                selectDamageModifier(penalties, *modifier);
                break;
            }
            default:
                break;
            }
        }

        for (const auto &entry : bonuses) {
            itemBonuses.push_back(entry.second);
        }
        for (const auto &entry : penalties) {
            itemPenalties.push_back(entry.second);
        }
    }

    std::map<int, int> effectBonuses;
    std::map<int, int> effectPenalties;
    for (const auto &applied : effects()) {
        if (!applied.effect) {
            continue;
        }
        switch (applied.effect->type()) {
        case EffectType::DamageIncrease: {
            const auto &effect = static_cast<const DamageIncreaseEffect &>(*applied.effect);
            if (effect.bonus() > 0) {
                effectBonuses[static_cast<int>(getPrimaryDamageType(
                    static_cast<int>(effect.damageType())))] +=
                    criticalMultiplier * effect.bonus();
            }
            break;
        }
        case EffectType::DamageDecrease: {
            const auto &effect = static_cast<const DamageDecreaseEffect &>(*applied.effect);
            if (effect.penalty() > 0) {
                effectPenalties[static_cast<int>(getPrimaryDamageType(
                    static_cast<int>(effect.damageType())))] +=
                    criticalMultiplier * effect.penalty();
            }
            break;
        }
        default:
            break;
        }
    }

    for (const auto &[type, amount] : effectBonuses) {
        damage.add(amount, static_cast<DamageType>(type));
    }
    for (const DamageModifier &modifier : itemBonuses) {
        damage.add(
            rollDamageModifier(modifier, criticalMultiplier),
            modifier.type);
    }
    for (const auto &[type, amount] : effectPenalties) {
        damage.add(-amount, static_cast<DamageType>(type));
    }
    for (const DamageModifier &modifier : itemPenalties) {
        damage.add(
            rollDamageModifier(modifier, criticalMultiplier),
            modifier.type);
    }
}

void Creature::getMainHandDamage(int &min, int &max) const {
    auto weapon = getEquippedItem(InventorySlots::rightWeapon);
    getWeaponDamage(weapon.get(), min, max);
}

void Creature::getWeaponDamage(const Item *weapon, int &min, int &max) const {
    if (!weapon) {
        min = 1;
        max = 1;
    } else {
        min = weapon->numDice();
        max = weapon->numDice() * weapon->dieToRoll();
    }

    int modifier;
    if (weapon && weapon->isRanged()) {
        modifier = _attributes.getAbilityModifier(Ability::Dexterity);
    } else {
        modifier = _attributes.getAbilityModifier(Ability::Strength);
    }
    min += modifier;
    max += modifier;
}

void Creature::getOffhandDamage(int &min, int &max) const {
    auto weapon = getOffhandAttackWeapon();
    getWeaponDamage(weapon.get(), min, max);
}

void Creature::onEventSignalled(const std::string &name) {
    if (name == "draw_weapon") {
        setLightsabersPowered(true, true);
        if (!_combatState.active) {
            _lightsaberIdlePowerDownPending = true;
            _lightsaberIdlePowerDownTimer.reset(8.0f);
        }
        return;
    }
    if (_footstepType == -1 || _walkmeshMaterial == -1 || name != "snd_footstep") {
        return;
    }
    std::shared_ptr<FootstepTypeSounds> sounds(_services.game.footstepSounds.get(_footstepType));
    if (!sounds) {
        return;
    }
    const Surface &surface = _services.game.surfaces.getSurface(_walkmeshMaterial);
    std::vector<std::shared_ptr<AudioClip>> materialSounds;
    if (surface.sound == "DT") {
        materialSounds = sounds->dirt;
    } else if (surface.sound == "GR") {
        materialSounds = sounds->grass;
    } else if (surface.sound == "ST") {
        materialSounds = sounds->stone;
    } else if (surface.sound == "WD") {
        materialSounds = sounds->wood;
    } else if (surface.sound == "WT") {
        materialSounds = sounds->water;
    } else if (surface.sound == "CP") {
        materialSounds = sounds->carpet;
    } else if (surface.sound == "MT") {
        materialSounds = sounds->metal;
    } else if (surface.sound == "LV") {
        materialSounds = sounds->leaves;
    }
    int index = randomInt(0, 3);
    if (index >= static_cast<int>(materialSounds.size())) {
        return;
    }
    auto clip = materialSounds[index];
    if (clip) {
        _audioSourceFootstep = _services.audio.mixer.play(
            std::move(clip),
            AudioType::Sound,
            1.0f,
            false,
            _position);
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
    if (!_stuckTimer.elapsed() || glm::length2(_position - _previousPosition) < 0.0001) {
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

bool Creature::navigateTo(const glm::vec3 &dest, bool run, float distance, float dt) {
    if (_movementRestricted)
        return false;

    auto module = _game.module();
    if (!module || !module->area()) {
        // Navigation without a module does not make sense. This is only useful
        // for unit tests.
        return true;
    }

    Pathfinder &pf = module->area()->pathfinder();

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

    if (_path && !updatePath(pf, *_path, position())) {
        // Lost the path and cannot recalculate.
        releasePath(pf, *_path);
        _path = std::nullopt;
        return false;
    }

    // Advance on path.
    if (_path) {
        glm::vec3 steeringForce = computeSteeringForce(pf.uni, getNextPathPoint(pf, *_path), dt);
        _pathVelocity += steeringForce * dt;

        float maxSpeed = 0.3f;
        float speed = glm::min(glm::length(_pathVelocity), maxSpeed);
        _pathVelocity = glm::normalize(_pathVelocity) * speed;

        glm::vec3 dir = glm::normalize(_pathVelocity);
        advanceOnPath(dest, dir, run, distance, dt);
        return false;
    }

    // Find a path and start following it.
    _path = createPath(pf, position(), dest);
    if (!_path) {
        return false;
    }
    _pathVelocity = {0.0f, 0.0f, 0.0f};

    return navigateTo(dest, run, distance, dt);
}

void Creature::advanceOnPath(const glm::vec3 &dest, const glm::vec3 &dir, bool run, float distance, float dt) {
    setMovementType(run ? Creature::MovementType::Run : Creature::MovementType::Walk);
    _game.module()->area()->moveCreature(
        _game.getObjectById<Creature>(_id), dir, run, dt, getDistanceTo(dest));

    // Report a door that obstructed this step. A door can stop the creature
    // from making progress while the slide in moveCreature still produces
    // some sideways motion, so this is keyed on the recorded obstruction
    // rather than on whether the step moved the creature at all.
    dispatchBlockedEvent();
}

void Creature::clearPath() {
    if (!_path) {
        return;
    }

    Pathfinder &pf = _game.module()->area()->pathfinder();
    releasePath(pf, *_path);
    _path = std::nullopt;
}

void Creature::dispatchBlockedEvent() {
    if (_blockingDoorId == script::kObjectInvalid) {
        // Nothing obstructed this step. Re-arm, so meeting the same door again
        // later reports again.
        _blockedEventDoorId = script::kObjectInvalid;
        return;
    }
    if (_blockedEventDoorId == _blockingDoorId) {
        // Still the same obstruction that was already reported.
        return;
    }
    _blockedEventDoorId = _blockingDoorId;
    runBlockedScript(_blockingDoorId);
}

std::string Creature::getAnimationName(AnimationType anim) const {
    std::string result;
    switch (anim) {
    case AnimationType::LoopingPause:
        return getPauseAnimation();
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
        return getAnimationName(CombatAnimation::Ready, getWieldType(), 0);

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
    return getFirstIfCreatureModel("cdie", "die");
}

std::string Creature::getFirstIfCreatureModel(std::string creatureAnim, std::string elseAnim) const {
    return _modelType == Creature::ModelType::Creature ? std::move(creatureAnim) : std::move(elseAnim);
}

std::string Creature::getDeadAnimation() const {
    return getFirstIfCreatureModel("cdead", "dead");
}

std::string Creature::getPauseAnimation() const {
    if (_modelType == Creature::ModelType::Creature)
        return "cpause1";

    // TODO: if (_lowHP) return "pauseinj"

    if (_combatState.active) {
        WeaponType type = WeaponType::None;
        WeaponWield wield = WeaponWield::None;
        getWeaponInfo(type, wield);

        int wieldNumber = getWeaponWieldNumber(wield);
        return str(boost::format("g%dr1") % wieldNumber);
    }

    return "pause1";
}

bool Creature::getWeaponInfo(WeaponType &type, WeaponWield &wield) const {
    std::shared_ptr<Item> item(getEquippedItem(InventorySlots::rightWeapon));
    if (item) {
        type = item->weaponType();
        wield = item->weaponWield();
        return true;
    }

    return false;
}

int Creature::getWeaponWieldNumber(WeaponWield wield) const {
    switch (wield) {
    case WeaponWield::StunBaton:
        return 1;
    case WeaponWield::SingleSword:
        return getOffhandAttackWeapon() ? 4 : 2;
    case WeaponWield::DoubleBladedSword:
        return 3;
    case WeaponWield::BlasterPistol:
        return getOffhandAttackWeapon() ? 6 : 5;
    case WeaponWield::BlasterRifle:
        return 7;
    case WeaponWield::HeavyWeapon:
        return 9;
    default:
        return 8;
    }
}

int Creature::getRelativeWeaponSize(const Item &weapon) const {
    if (_size == CreatureSize::Invalid ||
        weapon.weaponSize() == CreatureSize::Invalid) {
        return -10;
    }

    int relativeSize = static_cast<int>(weapon.weaponSize()) -
                       static_cast<int>(_size);
    return relativeSize >= -2 && relativeSize <= 1 ? relativeSize : -10;
}

int Creature::getTwoWeaponAttackPenalty(
    const Item *weapon,
    bool offHand,
    int *smallOffhandBonus) const {

    if (smallOffhandBonus) {
        *smallOffhandBonus = 0;
    }

    auto mainHand = getEquippedItem(InventorySlots::rightWeapon);
    if (!mainHand || mainHand->weaponType() == WeaponType::None) {
        return 0;
    }

    auto offHandWeapon = getOffhandAttackWeapon();
    bool doubleBladed = offHandWeapon == mainHand &&
                        mainHand->weaponWield() == WeaponWield::DoubleBladedSword;
    if (!offHandWeapon || offHandWeapon->weaponType() == WeaponType::None) {
        return 0;
    }

    if (offHand) {
        if (weapon != offHandWeapon.get()) {
            return 0;
        }
        if (_attributes.hasFeat(FeatType::AdvancedDoubleWeaponFighting)) {
            return 2;
        }
        if (_attributes.hasFeat(FeatType::DoubleWeaponFighting)) {
            return 4;
        }
        if (_attributes.hasFeat(FeatType::Ambidexterity)) {
            return 6;
        }
        return 10;
    }

    if (weapon != mainHand.get()) {
        return 0;
    }

    bool balanced = doubleBladed;
    if (!balanced) {
        int relativeSize = getRelativeWeaponSize(
            mainHand->isRanged() ? *mainHand : *offHandWeapon);
        balanced = mainHand->isRanged() ? relativeSize <= -1 : relativeSize == -1;
    }

    if (balanced && smallOffhandBonus) {
        *smallOffhandBonus = 2;
    }

    int penalty = balanced ? 4 : 6;
    if (_attributes.hasFeat(FeatType::AdvancedDoubleWeaponFighting)) {
        penalty -= 4;
    } else if (_attributes.hasFeat(FeatType::DoubleWeaponFighting)) {
        penalty -= 2;
    }
    return penalty;
}

int Creature::getDuelingBonus() const {
    auto mainHand = getEquippedItem(InventorySlots::rightWeapon);
    if (!mainHand || getEquippedItem(InventorySlots::leftWeapon)) {
        return 0;
    }
    if (mainHand->weaponWield() != WeaponWield::SingleSword &&
        mainHand->weaponWield() != WeaponWield::BlasterPistol) {
        return 0;
    }

    if (_attributes.hasFeat(FeatType::MasterDueling)) {
        return 3;
    }
    if (_attributes.hasFeat(FeatType::ImprovedDueling)) {
        return 2;
    }
    return _attributes.hasFeat(FeatType::Dueling) ? 1 : 0;
}

std::string Creature::getWalkAnimation() const {
    return getFirstIfCreatureModel("cwalk", "walk");
}

std::string Creature::getRunAnimation() const {
    if (_modelType == Creature::ModelType::Creature)
        return "crun";

    // TODO: if (_lowHP) return "runinj"

    if (_combatState.active) {
        WeaponType type = WeaponType::None;
        WeaponWield wield = WeaponWield::None;
        getWeaponInfo(type, wield);

        switch (wield) {
        case WeaponWield::SingleSword:
            return isSlotEquipped(InventorySlots::leftWeapon) ? "runds" : "runss";
        case WeaponWield::DoubleBladedSword:
            return "runst";
        case WeaponWield::BlasterRifle:
        case WeaponWield::HeavyWeapon:
            return "runrf";
        default:
            break;
        }
    }

    return "run";
}

std::string Creature::getTalkNormalAnimation() const {
    return "tlknorm";
}

std::string Creature::getHeadTalkAnimation() const {
    return "talk";
}

static std::string formatCombatAnimation(const std::string &format, CreatureWieldType wield, int variant) {
    return str(boost::format(format) % static_cast<int>(wield) % variant);
}

std::string Creature::getAnimationName(CombatAnimation anim, CreatureWieldType wield, int variant) const {
    switch (anim) {
    case CombatAnimation::Draw:
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
    if (!model)
        return "";

    return model->activeAnimationName();
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

void Creature::finalizeModel(ModelSceneNode &body) {
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);

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

std::string Creature::getBodyModelName() const {
    std::string column;

    if (_modelType == Creature::ModelType::Character) {
        column = "model";

        std::shared_ptr<Item> bodyItem(getEquippedItem(InventorySlots::body));
        if (bodyItem) {
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

        if (bodyItem) {
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
    if (texName.empty())
        return "";

    if (_modelType == Creature::ModelType::Character) {
        bool texFound = false;
        if (bodyItem) {
            std::string tmp(str(boost::format("%s%02d") % texName % bodyItem->textureVariation()));
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
    if (!headItem)
        return "";

    std::string modelName(boost::to_lower_copy(headItem->itemClass()));
    modelName += str(boost::format("_%03d") % headItem->modelVariation());

    return modelName;
}

std::string Creature::getWeaponModelName(int slot) const {
    std::shared_ptr<Item> bodyItem(getEquippedItem(slot));
    if (!bodyItem)
        return "";

    std::string modelName(bodyItem->itemClass());
    boost::to_lower(modelName);

    modelName += str(boost::format("_%03d") % bodyItem->modelVariation());

    return modelName;
}

void Creature::deserialize(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    std::string templateRes;
    if (!identityContext.isSerializedState() &&
        gff.readResRef(templateRes, "TemplateResRef")) {
        if (auto utc = _services.resource.gffs.get(templateRes, ResType::Utc)) {
            deserializeAll(
                *utc,
                SerializedIdentityContext::templateResource(templateRes));
        }
    }
    deserializeAll(gff, identityContext);

    updateTransform();
    loadAppearance();
}

void Creature::deserializeAll(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    Object::deserialize(gff, identityContext);

    // Retail reads IsPC before post-processing hit points. A player character
    // remains an incapacitated, resumable runtime object through 0..-9 HP and
    // becomes truly dead only at -10 HP. Preserve the saved HP verbatim; this
    // distinction is runtime state, not a load-time recovery/clamp.
    gff.readBool(_isPC, "IsPC");
    gff.readInt(_assignedPuppet, "AssignedPup");
    if (identityContext.isSerializedState() && gff.has("CurrentHitPoints")) {
        if (_minOneHP && _currentHitPoints < 1) {
            _currentHitPoints = 1;
        }
        _dead = _currentHitPoints <= (_game.isTSL() && _isPC ? -10 : 0);
    }


    // index into racialtypes.2da
    gff.readEnum(_race, "Race");

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

    // index into repute.2da
    gff.readEnum(_faction, "FactionID");

    gff.readBool(_disarmable, "Disarmable");
    gff.readBool(_noPermDeath, "NoPermDeath");
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
    gff.readBool(_isListening, "Listening");

    gff.readByte(_naturalAC, "NaturalAC");
    gff.readShort(_forcePoints, "ForcePoints");
    gff.readShort(_currentForce, "CurrentForce");
    gff.readShort(_refBonus, "refbonus");
    gff.readShort(_willBonus, "willbonus");
    gff.readShort(_fortBonus, "fortbonus");
    gff.readByte(_goodEvil, "GoodEvil");
    gff.readFloat(_challengeRating, "ChallengeRating");
    gff.readDword(_xp, "Experience");

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

    deserializeName(gff);
    deserializeSoundSet(gff);
    deserializeBodyBag(gff);
    deserializeAttributes(gff);
    deserializePerception(gff);
    deserializeOwnedItemsAndEquipment(gff, identityContext);
}

void Creature::deserializeName(const resource::Gff &gff) {
    gff.readLocString(_firstName, "FirstName", _services.resource.strings);
    gff.readLocString(_lastName, "LastName", _services.resource.strings);

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

void Creature::deserializeBodyBag(const resource::Gff &gff) {
    gff.readByte(_bodyBagId, "BodyBag");
    if (_bodyBagId == 0xFF) {
        return;
    }

    std::shared_ptr<TwoDA> bodyBags(_services.resource.twoDas.get("bodybag"));
    if (!bodyBags) {
        return;
    }
    _bodyBag.name = _services.resource.strings.getText(bodyBags->getInt(_bodyBagId, "name"));
    _bodyBag.appearance = bodyBags->getInt(_bodyBagId, "appearance");
    _bodyBag.corpse = bodyBags->getBool(_bodyBagId, "corpse");
    return;
}

void Creature::deserializeAttributes(const resource::Gff &gff) {
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

    int skillType = 0;
    for (const auto &skill : gff.getList("SkillList")) {
        attributes.setSkillRank(
            static_cast<SkillType>(skillType++), skill->getUint("Rank"));
    }

    for (const auto &feat : gff.getList("FeatList")) {
        auto featType = static_cast<FeatType>(feat->getUint("Feat"));
        _attributes.addFeat(featType);
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
        _attributes.addSpell(spellType);
    }
}

void Creature::deserializePerception(const resource::Gff &gff) {
    gff.readByte(_perceptionId, "PerceptionRange");
    if (_perceptionId == 0xFF) {
        return;
    }

    std::shared_ptr<TwoDA> ranges(_services.resource.twoDas.get("ranges"));
    if (!ranges) {
        return;
    }

    _perception.sightRange = ranges->getFloat(_perceptionId, "primaryrange");
    _perception.hearingRange = ranges->getFloat(_perceptionId, "secondaryrange");
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

void Creature::deserializeOwnedItemsAndEquipment(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    const bool replaceItems = gff.has("ItemList");
    const bool replaceEquipment = gff.has("Equip_ItemList");
    if (!replaceItems && !replaceEquipment) return;

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
        _game.replaceRuntimeObjectGraph(
            obsolete,
            [&]() {
                if (replaceItems) {
                    for (const auto &itemGff : gff.getList("ItemList")) {
                        auto item = _game.newOwnedItem(*itemGff, identityContext);
                        item->setOwner(_id);
                        replacementItems.push_back(std::move(item));
                    }
                }
                if (replaceEquipment) {
                    for (const auto &itemGff : gff.getList("Equip_ItemList")) {
                        auto item = _game.newOwnedItem(*itemGff, identityContext);
                        int slot = item->isEquippable(InventorySlots::body)
                                       ? InventorySlots::body
                                       : InventorySlots::rightWeapon;
                        if (!item->isEquippable(getEquipabilitySlot(slot)) ||
                            replacementEquipment.count(slot) != 0) {
                            item->setOwner(_id);
                            replacementItems.push_back(std::move(item));
                            continue;
                        }
                        item->setOwner(_id);
                        item->setEquipped(true);
                        replacementEquipment.emplace(slot, std::move(item));
                    }
                }
                for (const auto &item : replacementItems) {
                    replacementAttributes.addItem(item, _services.game);
                }
            },
            [&]() noexcept {
                _items = std::move(replacementItems);
                _equipment = std::move(replacementEquipment);
                _itemAttributes = std::move(replacementAttributes);
            });
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
    _game.replaceRuntimeObjectGraph(
        obsolete,
        [&]() {
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

            if (replaceEquipment) {
                for (const auto &itemGff : gff.getList("Equip_ItemList")) {
                    auto item = _game.newOwnedItem(*itemGff, identityContext);
                    item->captureSaveRecord(
                        *itemGff,
                        identityContext,
                        {SaveRecordOriginKind::EquippedItem, std::to_string(_id)});

                    std::optional<int> slot;
                    uint32_t slotMask = itemGff->type();
                    if (slotMask != 0 && (slotMask & (slotMask - 1)) == 0) {
                        int value = 0;
                        while ((slotMask >>= 1) != 0) {
                            ++value;
                        }
                        if (item->isEquippable(getEquipabilitySlot(value))) {
                            slot = value;
                        }
                    }
                    if (!slot && item->isEquippable(InventorySlots::body)) {
                        slot = InventorySlots::body;
                    } else if (!slot &&
                               item->isEquippable(InventorySlots::rightWeapon)) {
                        slot = InventorySlots::rightWeapon;
                    }

                    if (!slot) {
                        item->setOwner(_id);
                        replacementItems.push_back(std::move(item));
                        warn(str(boost::format("item is not equippable: %s") %
                                 replacementItems.back()->tag()));
                        continue;
                    }
                    if (replacementEquipment.count(*slot) != 0) {
                        throw ValidationException(
                            "Multiple saved items occupy equipment slot " +
                            std::to_string(*slot));
                    }
                    item->setOwner(_id);
                    item->setEquipped(true);
                    replacementEquipment.emplace(*slot, std::move(item));
                }
            }
            for (const auto &item : replacementItems) {
                replacementAttributes.addItem(item, _services.game);
            }
        },
        [&]() noexcept {
            if (replaceEquipment) {
                for (auto &[_, item] : _equipment) {
                    if (item) {
                        item->setEquipped(false);
                        item->setOwner(0);
                    }
                }
            }
            if (replaceItems) {
                for (auto &item : _items) {
                    if (item) {
                        item->setOwner(0);
                    }
                }
            }
            _items = std::move(replacementItems);
            _equipment = std::move(replacementEquipment);
            _itemAttributes = std::move(replacementAttributes);
        });
    uint32_t previousAppearance = _appearance;
    updateDisguise();
    if (_appearance != previousAppearance) {
        loadAppearanceProperties();
    }
}

} // namespace game

} // namespace reone
