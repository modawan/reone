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

#include "reone/audio/clip.h"
#include "reone/audio/source.h"
#include "reone/graphics/lipanimation.h"
#include "reone/resource/format/2dareader.h"
#include "reone/resource/format/gffreader.h"
#include "reone/resource/parser/gff/git.h"
#include "reone/resource/strings.h"
#include "reone/resource/types.h"
#include "reone/scene/animeventlistener.h"
#include "reone/scene/node/model.h"
#include "reone/script/types.h"
#include "reone/system/timer.h"

#include "../d20/attributes.h"
#include "../d20/itemattributes.h"
#include "../object.h"
#include "../pathfinder.h"

#include "item.h"

namespace reone {

namespace resource {
class Gff;
}

namespace game {

constexpr float kDefaultAttackRange = 2.0f;

class DamagePacket;
struct AttackBonusBreakdown;

class Creature : public Object, public scene::IAnimationEventListener {
public:
    enum class ModelType {
        Creature,
        Droid,
        Character
    };

    enum class MovementType {
        None,
        Walk,
        Run
    };

    struct BodyBag {
        std::string name;
        int appearance {0}; /**< index into placeables.2da */
        bool corpse {false};
    };

    struct Perception {
        float sightRange {0.0f};
        float hearingRange {0.0f};
        std::set<uint32_t> seen;
        std::set<uint32_t> heard;
    };

    struct CombatState {
        bool active {false};
        bool shouldDeactivate {false};
        bool debilitated {false};
        std::shared_ptr<Object> attackTarget;
        uint32_t attemptedAttackTarget {script::kObjectInvalid};
        ActionType attackAction {ActionType::QueueEmpty};
        FeatType combatFeat {FeatType::Invalid};
        Timer deactivationTimer;
    };

    Creature(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services);

    static bool classof(const Object *from) {
        return from->type() == ObjectType::Creature;
    }

    void loadFromBlueprint(const std::string &resRef);
    void loadAppearance();

    void deserialize(const resource::Gff &gff);

    void update(float dt) override;

    void clearAllActions(bool force = false) override;
    void damage(int amount, uint32_t damager) override;

    void giveXP(int amount);
    void setXP(int xp);

    void playSound(resource::SoundSetEntry entry, bool positional = true);

    void startTalking(const std::shared_ptr<graphics::LipAnimation> &animation);
    void stopTalking();

    bool isSelectable() const override;
    bool isMovementRestricted() const { return _movementRestricted || !canExecuteActions(); }
    bool isLevelUpPending() const;

    glm::vec3 getSelectablePosition() const override;
    float getAttackRange() const;
    int getNeededXP() const;

    Gender gender() const { return _gender; }
    ModelType modelType() const { return _modelType; }
    int appearance() const { return _appearance; }
    std::shared_ptr<graphics::Texture> portrait() const { return _portrait; }
    float walkSpeed() const { return _walkSpeed; }
    float runSpeed() const { return _runSpeed; }
    float creaturePersonalSpace() const { return _creaturePersonalSpace; }
    CreatureSize size() const { return _size; }
    CreatureAttributes &attributes() { return _attributes; }
    const CreatureAttributes &attributes() const { return _attributes; }
    ItemAttributes &itemAttributes() { return _itemAttributes; }
    const ItemAttributes &itemAttributes() const { return _itemAttributes; }
    Faction faction() const { return _faction; }
    int xp() const { return _xp; }
    Alignment alignment() const;
    RacialType racialType() const { return _race; }
    Subrace subrace() const { return _subrace; }
    NPCAIStyle aiStyle() const { return _aiStyle; }
    int walkmeshMaterial() const { return _walkmeshMaterial; }
    bool isPC() const { return _isPC; }

    void setGender(Gender gender) { _gender = gender; }
    void setAppearance(int appearance) { _appearance = appearance; }
    void setMovementType(MovementType type);
    void setFaction(Faction faction) { _faction = faction; }
    void setMovementRestricted(bool restricted) { _movementRestricted = restricted; }
    void setImmortal(bool immortal) { _immortal = immortal; }
    void setAIStyle(NPCAIStyle style) { _aiStyle = style; }
    void setWalkmeshMaterial(int material) { _walkmeshMaterial = material; }

    // Animation

    void playAnimation(AnimationType type, scene::AnimationProperties properties = scene::AnimationProperties()) override;

    void playAnimation(CombatAnimation anim, CreatureWieldType wield, int variant = 1);
    void playAnimation(const std::string &name, scene::AnimationProperties properties = scene::AnimationProperties());
    bool playAnimation(const std::shared_ptr<graphics::Animation> &anim, scene::AnimationProperties properties = scene::AnimationProperties());
    // Holds an externally sourced animation until resumeStateDrivenAnimation is called.
    bool playExternalAnimation(const std::shared_ptr<graphics::Animation> &anim, scene::AnimationProperties properties = scene::AnimationProperties());
    void resumeStateDrivenAnimation();

    /**
     * Play an animation as a layer over whatever the creature is already doing,
     * including while it is walking or running. Unlike the other playAnimation
     * overloads this neither waits for the creature to stand still nor takes
     * over its state-driven animation, so locomotion carries on underneath and
     * the layer disappears on its own once it has run.
     */
    void playOverlayAnimation(AnimationType type);

    void updateModelAnimation();

    // END Animation

    // Equipment

    bool equip(const std::string &resRef);
    bool equip(int slot, const std::shared_ptr<Item> &item);
    void unequip(const std::shared_ptr<Item> &item);

    bool isSlotEquipped(int slot) const;

    std::shared_ptr<Item> getEquippedItem(int slot) const;
    CreatureWieldType getWieldType() const;

    const std::map<int, std::shared_ptr<Item>> &equipment() const { return _equipment; }

    // END Equipment

    // Pathfinding
    bool navigateTo(const glm::vec3 &dest, bool run, float distance, float dt);
    void clearPath();
    void advanceOnPath(const glm::vec3 &dest, const glm::vec3 &dir, bool run, float distance, float dt);
    glm::vec3 computeSteeringForce(const Uniwalk &uni, const glm::vec3 &next, float dt);
    // END Pathfinding

    // Blocking doors

    /**
     * Remember the door that obstructed the last attempted step. Written by the
     * collision layer for every mover, including the directly controlled player.
     *
     * This lives only to carry the obstruction from the collision test to the
     * blocked event raised after the step. It is not what scripts read:
     * GetBlockingDoor answers from the argument captured when the event was
     * raised, so it stays fixed for that run while this keeps changing.
     */
    void setBlockingDoor(uint32_t doorId) { _blockingDoorId = doorId; }

    void clearBlockingDoor() { _blockingDoorId = script::kObjectInvalid; }

    uint32_t blockingDoorId() const { return _blockingDoorId; }

    /**
     * Edge-trigger ScriptOnBlocked for the door currently obstructing this
     * creature. Called by navigation after each attempted step, so it only
     * applies to AI, script and action driven movement. A continuous
     * obstruction by the same door reports once; an unobstructed step re-arms.
     */
    void dispatchBlockedEvent();

    // END Blocking doors

    // Perception

    void onObjectSeen(const std::shared_ptr<Object> &object);
    void onObjectVanished(const std::shared_ptr<Object> &object);
    void onObjectHeard(const std::shared_ptr<Object> &object);
    void onObjectInaudible(const std::shared_ptr<Object> &object);

    void setObjectSeen(const std::shared_ptr<Object> &object, bool seen);
    void setObjectHeard(const std::shared_ptr<Object> &object, bool heard);
    void runOnNotice(const Object &object, bool heard, bool seen);

    const Perception &perception() const { return _perception; }

    // END Perception

    // Combat

    void activateCombat();
    void deactivateCombat(float delay);

    bool isInCombat() const { return _combatState.active; }
    bool isDebilitated() const;
    bool isTemporarilyDead() const;
    bool isTwoWeaponFighting() const;
    std::shared_ptr<Item> getOffhandAttackWeapon() const;

    uint32_t getAttemptedAttackTarget() const { return _combatState.attemptedAttackTarget; }
    std::shared_ptr<Object> getAttackTarget() const { return _combatState.attackTarget; }
    uint32_t getLastHostileTarget() const { return _lastHostileTarget; }
    ActionType getLastAttackAction() const { return _lastAttackAction; }
    FeatType getLastCombatFeat() const { return _lastCombatFeat; }
    AttackResultType getLastAttackResult() const { return _lastAttackResult; }
    int modifiedAttacks() const { return _modifiedAttacks; }
    bool hasAssuredHit() const { return _assuredHit; }
    AttackBonusBreakdown getAttackBonusBreakdown(
        const Creature *target,
        const Item *weapon,
        bool offHand) const;
    int getAttackBonus(bool offHand = false) const;
    int getDefense(const Creature *attacker, int damageFlags) const;
    int getDefense() const;
    int getFortitudeSave(SavingThrowType savingThrowType = SavingThrowType::All) const;
    bool rollFortitudeSave(
        int difficultyClass,
        SavingThrowType savingThrowType = SavingThrowType::All) const;
    int getPhysicalDamageBonus(const Item *weapon, bool offHand) const;
    int getMassiveCriticalDamage(const Item *weapon, bool criticalHit) const;
    int getItemDamageImmunity(DamageType type) const;
    int getItemDamageResistance(DamageType type) const;
    void getItemDamageReduction(int &amount, DamagePower &power) const;
    int getDamageResistanceFeatBonus() const;
    void addPhysicalDamageModifiers(
        DamagePacket &damage,
        const Creature *target,
        const Item *weapon,
        bool offHand,
        int criticalMultiplier) const;
    void getMainHandDamage(int &min, int &max) const;
    void getOffhandDamage(int &min, int &max) const;

    void setAttemptedAttackTarget(uint32_t target) {
        _combatState.attemptedAttackTarget = target;
    }
    void beginCombatAttack(std::shared_ptr<Object> target, FeatType feat);
    void finishCombatRound();
    void setLastAttackResult(AttackResultType result) { _lastAttackResult = result; }
    void adjustModifiedAttacks(int amount);
    bool applyAssuredHit();
    void removeAssuredHit() { _assuredHit = false; }

    // END Combat

    // Gold

    void giveGold(int amount);
    void takeGold(int amount);

    int gold() const { return _gold; }

    // END Gold

    // Scripts

    void runSpawnScript();
    void runBlockedScript(uint32_t blockingDoorId);
    void runEndRoundScript();
    void runDialogueScript(uint32_t speakerId, int32_t listenNumber);
    void runAttackedScript(uint32_t attackerId);

    void setOnHeartbeat(std::string onHeartbeat) { _onHeartbeat = onHeartbeat; }
    void setOnSpawn(std::string onSpawn) { _onSpawn = onSpawn; }
    void setOnDeath(std::string onDeath) { _onDeath = onDeath; }
    void setOnNotice(std::string onNotice) { _onNotice = onNotice; }
    void setOnEndRound(std::string onEndRound) { _onEndRound = onEndRound; }
    void setOnSpellAt(std::string onSpellAt) { _onSpellAt = onSpellAt; }
    void setOnAttacked(std::string onAttacked) { _onAttacked = onAttacked; }
    void setOnDamaged(std::string onDamaged) { _onDamaged = onDamaged; }
    void setOnDisturbed(std::string onDisturbed) { _onDisturbed = onDisturbed; }
    void setOnEndDialogue(std::string onEndDialogue) { _onEndDialogue = onEndDialogue; }
    void setOnBlocked(std::string onBlocked) { _onBlocked = onBlocked; }
    void setOnDialogue(std::string onDialogue) { _onDialogue = onDialogue; }

    // END Scripts

    // IAnimationEventListener

    void onEventSignalled(const std::string &name) override;

    // END IAnimationEventListener

    // Listeners

    bool isListening() { return _isListening; }
    void setIsListening(bool value) { _isListening = value; }

    // END Listeners

protected:
    bool canExecuteActions() const override;

private:
    // Serializable
    RacialType _race {RacialType::Unknown};
    Subrace _subrace {Subrace::None};
    uint32_t _appearance {0};
    uint32_t _appearanceBeforeDisguise {0};
    bool _disguised {false};
    Gender _gender {Gender::Male};
    uint16_t _portraitId {0};
    bool _isPC {false};
    Faction _faction {Faction::Invalid};
    bool _disarmable {false};
    bool _noPermDeath {false};
    bool _notReorienting {false};
    uint8_t _bodyVariation {0};
    uint8_t _textureVar {0};
    bool _partyInteract {false};
    int32_t _walkRate {0};
    uint8_t _naturalAC {0};
    int16_t _forcePoints {0};
    int16_t _currentForce {0};
    int16_t _refBonus {0};
    int16_t _willBonus {0};
    int16_t _fortBonus {0};
    uint8_t _goodEvil {0};
    float _challengeRating {0};
    uint32_t _xp {0};

    std::string _onNotice;
    std::string _onSpellAt;
    std::string _onAttacked;
    std::string _onDamaged;
    std::string _onDisturbed;
    std::string _onEndRound;
    std::string _onEndDialogue;
    std::string _onDialogue;
    std::string _onSpawn;
    std::string _onDeath;
    std::string _onBlocked;

    // Door currently obstructing this creature, and the door the blocked event
    // was last reported for. Object ids rather than pointers, so a door that is
    // destroyed while remembered simply resolves to no object.
    uint32_t _blockingDoorId {script::kObjectInvalid};
    uint32_t _blockedEventDoorId {script::kObjectInvalid};

    resource::LocString _firstName;
    resource::LocString _lastName;

    uint16_t _soundSetId {0xFFFF};
    uint8_t _bodyBagId {0xFF};
    uint8_t _perceptionId {0xFF};

    CreatureAttributes _attributes;
    std::map<int, std::shared_ptr<Item>> _equipment;
    // END Serializable

    ModelType _modelType {ModelType::Creature};
    std::shared_ptr<graphics::Texture> _portrait;

    // Current path that the creature is following, its velocity and position at
    // the previous frame.
    std::optional<Path> _path;
    glm::vec3 _pathVelocity;
    glm::vec3 _previousPosition;
    // When there is no progress on the path, apply _stuckForce to steer the
    // creature in a random direction until the timer runs out.
    Timer _stuckTimer;
    glm::vec3 _stuckForce;

    float _walkSpeed {0.0f};
    float _runSpeed {0.0f};
    float _creaturePersonalSpace {0.6f};
    CreatureSize _size {CreatureSize::Invalid};
    MovementType _movementType {MovementType::None};
    bool _talking {false};

    ItemAttributes _itemAttributes;

    bool _movementRestricted {false};
    CombatState _combatState;
    uint32_t _lastHostileTarget {script::kObjectInvalid};
    ActionType _lastAttackAction {ActionType::QueueEmpty};
    FeatType _lastCombatFeat {FeatType::Invalid};
    AttackResultType _lastAttackResult {AttackResultType::Invalid};
    int _modifiedAttacks {0};
    bool _assuredHit {false};
    bool _immortal {false};
    std::shared_ptr<resource::SoundSet> _soundSet;
    BodyBag _bodyBag;
    Perception _perception;
    NPCAIStyle _aiStyle {NPCAIStyle::DefaultAttack};

    uint32_t _footstepType {0};
    int _walkmeshMaterial {-1};
    int _gold {0}; /**< aka credits */
    std::string _envmap;
    bool _isListening {false};

    std::shared_ptr<audio::AudioSource> _audioSourceVoice;
    std::shared_ptr<audio::AudioSource> _audioSourceFootstep;
    bool _lightsaberIdlePowerDownPending {false};
    Timer _lightsaberIdlePowerDownTimer;

    // Animation

    bool _animDirty {true};
    bool _animFireForget {false};
    std::shared_ptr<graphics::LipAnimation> _lipAnimation;

    // END Animation

    // Scripts

    // END Scripts

    void loadTransformFromGIT(const resource::generated::GIT_Creature_List &git);

    void onEffectsCleared() override;
    void updateModel();

    // Refresh appearance-derived state (model type, size, speeds, footstep, envmap,
    // portrait) for the current _appearance, without building a scene node.
    void loadAppearanceProperties();

    // Recompute the disguise appearance override from equipped items: switch to a
    // disguise item's appearance when one is equipped, and restore the original
    // appearance when none remains. Updates _appearance only; callers rebuild the model.
    void updateDisguise();
    void updateCombat(float dt);
    void setLightsabersPowered(bool powered, bool animate);
    void updateLightsaberSoundPositions();

    void runDeathScript(uint32_t damagerId);
    void runDamagedScript(uint32_t damagerId);

    ModelType parseModelType(const std::string &s) const;

    // Appearance

    std::shared_ptr<scene::ModelSceneNode> buildModel();
    void finalizeModel(scene::ModelSceneNode &body);

    std::string getBodyModelName() const;
    std::string getBodyTextureName() const;
    std::string getHeadModelName() const;
    std::string getMaskModelName() const;
    std::string getWeaponModelName(int slot) const;

    // END Appearance

    // Animation

    bool doPlayAnimation(bool fireForget, const std::function<void()> &callback);

    std::string getAnimationName(AnimationType anim) const override;
    std::string getAnimationName(CombatAnimation anim, CreatureWieldType wield, int variant) const;
    std::string getActiveAnimationName() const override;

    std::string getDeadAnimation() const;
    std::string getDieAnimation() const;
    std::string getHeadTalkAnimation() const;
    std::string getPauseAnimation() const;
    std::string getRunAnimation() const;
    std::string getTalkNormalAnimation() const;
    std::string getWalkAnimation() const;

    /**
     * @return creatureAnim if model type is creature, elseAnim otherwise
     */
    inline std::string getFirstIfCreatureModel(std::string creatureAnim, std::string elseAnim) const;

    bool getWeaponInfo(WeaponType &type, WeaponWield &wield) const;
    int getWeaponWieldNumber(WeaponWield wield) const;
    int getRelativeWeaponSize(const Item &weapon) const;
    int getTwoWeaponAttackPenalty(
        const Item *weapon,
        bool offHand,
        int *smallOffhandBonus = nullptr) const;
    int getDuelingBonus() const;
    void getWeaponDamage(const Item *weapon, int &min, int &max) const;

    // END Animation

    // Blueprint
    void deserializeAll(const resource::Gff &gff);
    void deserializeName(const resource::Gff &gff);
    void deserializeSoundSet(const resource::Gff &gff);
    void deserializeBodyBag(const resource::Gff &gff);
    void deserializeAttributes(const resource::Gff &gff);
    void deserializeClass(const resource::Gff &gff);
    void deserializePerception(const resource::Gff &gff);
    void deserializeEquipItems(const resource::Gff &gff);
    // END Blueprint
};

} // namespace game

} // namespace reone
