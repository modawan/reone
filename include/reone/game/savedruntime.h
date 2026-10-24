/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "reone/resource/gff.h"
#include "reone/script/types.h"

#include "attack.h"
#include "combatfeedback.h"
#include "effect.h"
#include "effect/damage.h"
#include "runtimeref.h"

namespace reone {

namespace game {

class Action;
class Game;
class Object;
class SavedScriptSituationImporter;

constexpr uint32_t kSavedRuntimeInvalidObjectId = 0x7f000000;
/**
 * Saved identities above the invalid ID form the character namespace. Player
 * characters take them downward from this ceiling; ordinary objects stay below
 * the invalid ID.
 */
constexpr uint32_t kSavedRuntimeMaxCharacterObjectId = 0x7fffffff;
// The game creates the structural Module before loading its saved object graph.
// Its contextual object-reference target is therefore slot 0 even though the
// IFO contains no owned Module record or explicit Module ObjectId field.
constexpr uint32_t kSavedRuntimeModuleObjectId = 0;

/** A saved object identity which is deliberately unbound while B is built. */
struct SavedObjectReference {
    uint32_t id {kSavedRuntimeInvalidObjectId};

    SavedObjectReference() = default;
    explicit SavedObjectReference(uint32_t id) : id(id) {}
    /** The runtime's invalid object is the saved invalid object. */
    static SavedObjectReference fromRuntimeId(uint32_t id) {
        return id == script::kObjectInvalid ? SavedObjectReference() : SavedObjectReference(id);
    }
    static SavedObjectReference fromSerializedId(
        uint32_t id,
        SerializedIdentityContext identityContext) {
        SavedObjectReference result(id);
        result._serializedIdentityContext = std::move(identityContext);
        return result;
    }
    /**
     * An object slot holding a value that names no object. It binds to
     * nothing and is saved as the value itself.
     */
    static SavedObjectReference rawValue(uint32_t value) {
        SavedObjectReference result(value);
        result._rawValue = true;
        return result;
    }

    bool isInvalid() const { return id == kSavedRuntimeInvalidObjectId; }
    bool isRawValue() const { return _rawValue; }
    bool isSerializedIdentity() const {
        return _serializedIdentityContext.has_value();
    }
    const std::optional<SerializedIdentityContext> &serializedIdentityContext() const {
        return _serializedIdentityContext;
    }
    std::shared_ptr<Object> boundObject() const;

private:
    friend class Game;
    RuntimeObjectRef<Object> _object;
    std::optional<uint64_t> _runtimeSession;
    std::optional<uint64_t> _savedGraph;
    std::optional<SerializedIdentityContext> _serializedIdentityContext;
    bool _rawValue {false};
};

struct SavedLocationValue {
    glm::vec3 position {0.0f};
    glm::vec3 orientation {0.0f};
};

/**
 * The number of a script event a SignalEvent event delivers, as it is saved in
 * the event's EventType.
 */
enum class SavedSignalEventType : uint16_t {
    Heartbeat = 0,
    SpellCastAt = 2,
    Damaged = 4,
    /** A shout heard, or a request to talk. */
    Conversation = 7,
    Death = 10,
    UserDefined = 11,
    Entered = 12,
    Exited = 13,
    ModuleLoaded = 17,
    ItemActivated = 18,
    ItemAcquired = 19,
    ItemLost = 20,
    EncounterExhausted = 21,
    InventoryOpened = 22,
    InventoryClosed = 23,
    Used = 25,
    Locked = 28,
    /** A creature walked up to the trigger it was heading for. */
    Clicked = 30,
    FailToOpen = 34,
    ItemEquipped = 38,
};

struct SavedScriptEvent {
    uint16_t type {0};
    std::vector<int32_t> integers;
    std::vector<float> floats;
    std::vector<std::string> strings;
    std::vector<SavedObjectReference> objects;
};

/** Game-defined talent value used by VM structure 3. */
struct SavedTalentValue {
    int32_t id {-1};
    int32_t type {-1};
    uint8_t multiClass {0};
    SavedObjectReference item;
    int32_t itemPropertyIndex {-1};
    uint8_t casterLevel {0xff};
    uint8_t metaType {0xff};
};

enum class SavedVmStackType : int8_t {
    Integer = 3,
    Float = 4,
    String = 5,
    Object = 6,
    Effect = 16,
    Event = 17,
    Location = 18,
    Talent = 19,
};

/**
 * A stack value of a type the stack does not hold, or without the data its
 * type needs, is empty.
 */
using SavedVmStackPayload = std::variant<
    std::monostate,
    int32_t,
    float,
    std::string,
    SavedObjectReference,
    EffectInstance,
    SavedScriptEvent,
    SavedLocationValue,
    SavedTalentValue>;

struct SavedVmStackValue {
    int8_t type {0};
    SavedVmStackPayload payload;
    /** The saved stack entry of an empty value, saved again unchanged. */
    std::shared_ptr<const resource::Gff> unread;
};

enum class ScriptSituationResumeSupport {
    UnsupportedSavedSnapshot,
    ValidatedImport,
};

/** The game STORE_STATE continuation, kept separate from live ExecutionState. */
struct SerializedScriptSituation {
    int32_t codeSize {0};
    ByteBuffer code;
    uint32_t crc {0};
    int32_t instructionPointer {0};
    int32_t secondaryPointer {0};
    std::string scriptName;
    int32_t stackSize {0};
    int32_t basePointer {0};
    int32_t stackPointer {0};
    int32_t totalSize {0};
    std::vector<SavedVmStackValue> stack;
    /** The situation's fields that are not read, saved again unchanged. */
    std::vector<resource::Gff::Field> unknownFields;

    static SerializedScriptSituation fromGff(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    ScriptSituationResumeSupport resumeSupport() const {
        return ScriptSituationResumeSupport::ValidatedImport;
    }
    bool bindObjectReferences(const Game &game);
    bool isBoundToCurrentRuntimeSession(const Game &game) const;

private:
    friend class SavedScriptSituationImporter;
    std::optional<uint64_t> _runtimeSession;
};

/**
 * The type of a saved command: the ActionId an ActionList or a scheduled
 * entry's Command records.
 */
enum class SavedActionType : uint32_t {
    MoveToPoint = 1,
    MoveAwayFromObject = 3,
    JumpToLocation = 5,
    PlayAnimation = 6,
    PickUpItem = 7,
    EquipItem = 8,
    PutDownItem = 9,
    UnequipItem = 11,
    /** A physical attack, with or without a combat feat. */
    AttackObject = 12,
    SpeakString = 14,
    CastSpell = 15,
    /** A move to an object that ends once it is within range. */
    MoveToObject = 17,
    /** A turn to face an object. */
    FaceObject = 19,
    OpenDoor = 20,
    CloseDoor = 21,
    StartConversation = 24,
    DisarmMine = 25,
    RecoverMine = 26,
    FlagMine = 27,
    ExamineMine = 28,
    SetMine = 29,
    Wait = 30,
    ResumeConversation = 32,
    SpeakStringByStrRef = 33,
    GiveItem = 34,
    TakeItem = 35,
    DoCommand = 37,
    OpenLock = 38,
    Lock = 39,
    OpenContainer = 40,
    AnimalEmpathy = 41,
    Rest = 42,
    Taunt = 43,
    MoveAwayFromLocation = 44,
    RandomWalk = 45,
    ItemCastSpell = 46,
    JumpToObject = 48,
    /** A turn to face a point. */
    FacePoint = 49,
    Counterspell = 50,
    Appear = 52,
    PickPocket = 54,
    Follow = 55,
    Heal = 56,
    /** A follow's check on the one followed, which takes the follow's first four parameters. */
    FollowCheck = 58,
    FollowLeader = 61,
    BarkString = 62,
    /** The command that dispatches its creature's combat round. */
    CombatDispatch = 63,
    SurrenderToEnemies = 65,
    /** TSL: a lightsaber burnt through a locked door. */
    DoorSaber = 67,
    TotalDefense = 68,
    Meditate = 69,
    FollowOwner = 70,
    SwitchWeapons = 71,
    /** No command the save format represents. */
    Invalid = 0xffff,
};

enum class SavedActionParameterType : uint32_t {
    Unsupported = 0,
    Integer = 1,
    Float = 2,
    Object = 3,
    String = 4,
    ScriptSituation = 5,
};

/**
 * A parameter of a type no command takes, or without the data its type needs,
 * is empty.
 */
using SavedActionParameterPayload = std::variant<
    std::monostate,
    int32_t,
    float,
    SavedObjectReference,
    std::string,
    SerializedScriptSituation>;

struct SavedActionParameter {
    uint32_t type {0};
    SavedActionParameterPayload payload;
    /** The saved parameter entry of an empty parameter, saved again unchanged. */
    std::shared_ptr<const resource::Gff> unread;
    /**
     * The type the parameter was saved with, when it is read as another type;
     * a record saved again as it was loaded keeps it.
     */
    std::optional<uint32_t> savedType;

    static SavedActionParameter fromGff(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    bool bindObjectReferences(const Game &game);
};

enum class SavedExecutionSupport {
    Executable,
    RepresentableButUnsupported,
    Discarded,
};

struct SavedActionRecord;

/**
 * A physical attack order as the game saves it: the ten parameters of an
 * AttackObject command. They name the target and either a combat feat or the
 * values a cutscene attack forces; the rest are the same for every attack.
 */
struct SavedAttackCommand {
    /** The attack animation and its length that the command and its round entry carry. */
    static constexpr int kAnimation = 10009;
    static constexpr int kAnimationMilliseconds = 1500;

    SavedObjectReference target;
    FeatType feat {FeatType::Invalid};
    /** A cutscene attack's forced values; it carries no attack count and no feat. */
    std::optional<CutsceneAttack> cutscene;

    /** Reads the order from a command, or none when its parameters have another shape. */
    static std::optional<SavedAttackCommand> fromAction(const SavedActionRecord &record);
    /** Writes the order as the parameters of an AttackObject command. */
    void toAction(SavedActionRecord &record) const;
};

/**
 * A spell cast order as a save records it: the parameters of a CastSpell
 * command. A creature's command has twelve: the spell, the class and caster
 * level it is cast with, the object or point it is cast at, its projectile
 * path, its associated feat, and whether the cast is fake or instant. A
 * placeable's has seven: the spell, the metamagic, the object or point it is
 * cast at, and its projectile path with whether the cast is fake.
 */
struct SavedSpellCommand {
    /** The command is a placeable's. */
    bool placeable {false};
    int spellId {-1};
    SpellSelection selection;
    /** The object the spell is cast at; invalid for a cast at a location. */
    SavedObjectReference target;
    glm::vec3 position {0.0f};
    ProjectilePathType path {ProjectilePathType::Default};
    std::optional<FeatType> associatedFeat;
    bool fake {false};
    bool instant {false};
    /** The domain level a script gave the command. */
    int domainLevel {0};
    /** The meta type a talent gave the command. */
    int metaType {0};
    /** The command's spontaneous-cast parameter. */
    int spontaneousCast {0};

    /** Reads the order from a command, or none when its parameters have another shape. */
    static std::optional<SavedSpellCommand> fromAction(const SavedActionRecord &record);
    /** Writes the order as the parameters of a CastSpell command. */
    void toAction(SavedActionRecord &record) const;
};

/**
 * An item use as a save records it: the seven parameters of an ItemCastSpell
 * command. They name the item, the property it is used through by its place
 * among the item's use properties, a value the command carries and nothing
 * reads, and the object or point it is used at. A use under way carries more
 * parameters, which have no type.
 */
struct SavedItemUseCommand {
    SavedObjectReference item;
    /** The property's place among the properties the item is used through. */
    int useProperty {0};
    /** The object the item is used at; invalid for a use at a location. */
    SavedObjectReference target;
    glm::vec3 position {0.0f};

    /** Reads the use from a command, or none when its parameters have another shape. */
    static std::optional<SavedItemUseCommand> fromAction(const SavedActionRecord &record);
    /** Writes the use as the seven parameters of an ItemCastSpell command. */
    void toAction(SavedActionRecord &record) const;
};

/**
 * A walk as the game saves it: the thirteen parameters of a MoveToPoint
 * command. They name the point and its area, an object walked to, how the
 * walk goes and how near it ends, and the time limit of a forced walk.
 */
struct SavedMoveCommand {
    glm::vec3 destination {0.0f};
    SavedObjectReference area;
    /** The object walked to; invalid for a walk to a point. */
    SavedObjectReference target;
    bool run {false};
    /** A walk to a point keeps to the straight line to it. */
    bool straight {false};
    /** How near the point or the object the walk ends. */
    float range {0.0f};
    /** A forced walk not yet begun: it is forced to its end once timeout seconds have passed. */
    bool forcedPending {false};
    float timeout {0.0f};
    /** A forced walk under way: it is forced to its end at the expiry day and time. */
    bool forcedActive {false};
    uint32_t expiryDay {0};
    uint32_t expiryTime {0};
    /** Where a walk to an object ends, from the object. */
    glm::vec2 offset {0.0f};

    /** Reads the walk from a command, or none when its parameters do not describe one. */
    static std::optional<SavedMoveCommand> fromAction(const SavedActionRecord &record);
    /** Writes the walk as the parameters of a MoveToPoint command. */
    void toAction(SavedActionRecord &record) const;
};

/**
 * What a cast under way keeps beyond its command's parameters, saved beside
 * them as CastState: how far the cast has got, the context it was paid with,
 * a cheat order and the caster level an item use was ordered with.
 */
struct SavedCastAction {
    /** The phase of the cast's schedule, the time spent in it, and the phase timings. */
    int phase {0};
    float elapsed {0.0f};
    float conjureTime {0.0f};
    float castTime {0.0f};
    float catchTime {0.0f};
    /** The flight time of the cast's projectile. */
    float projectileTime {0.0f};
    /** The presentation that shows the cast. */
    uint64_t presentationId {0};
    /** The cast context: caster level, metamagic, casting class and Force point cost. */
    int casterLevel {0};
    int metaMagic {255};
    int castingClass {kUnselectedCastingClass};
    int forceCost {0};
    /**
     * The cast was ordered with a casting class and level; otherwise it uses
     * its caster's own, and the command's selection is not used.
     */
    bool hasSelection {true};
    /** The cast was ordered as a cheat. */
    bool cheat {false};
    /** The cast passed its admission when it was first dispatched. */
    bool admitted {false};
    /** The caster is walking up to the range it was out of when the cast was dispatched. */
    bool approaching {false};
    /** The cast goes ahead from where its caster stands, whatever the range. */
    bool skipsApproach {false};
    /** The cast waits for its caster to stand in use range of its object, with a clear line to it. */
    bool checksUseLine {false};
    bool started {false};
    bool commitAttempted {false};
    bool committed {false};
    /** The cast's impact has been released. */
    bool released {false};
    /** The cast holds its caster's movement. */
    bool movementOwned {false};
    /**
     * The caster level an item use was ordered with, if any. An item use
     * keeps no progress across a load: it starts over from its command.
     */
    std::optional<int> itemCasterLevel;

    static SavedCastAction fromGff(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    std::shared_ptr<resource::Gff> toGff() const;
};

/** The combat round a command holds: its clock, its pause and who engaged it. */
struct SavedRoundClock {
    uint64_t id {0};
    int slot {0};
    int state {0};
    float elapsed {0.0f};
    float duration {3.0f};
    float pauseRemaining {0.0f};
    SavedObjectReference pauseOwner;
    SavedObjectReference master;
    SavedObjectReference engaged;
    /** The command took over the round from an earlier, finished command. */
    bool joined {false};
    /** Two creatures take their turns in the round. */
    bool duel {false};
    static SavedRoundClock fromGff(const resource::Gff &, const SerializedIdentityContext &);
    std::shared_ptr<resource::Gff> toGff() const;
};

/** A creature's round that no queued command carries: its clock and whether it is a spell round. */
struct SavedCreatureRound {
    SavedRoundClock clock;
    bool spellRound {false};
};

/** What a projectile shows: a spell's projectile, a thrown lightsaber's route, or a ranged attack's bolt. */
enum class ProjectileKind {
    Spell = 0,
    LightsaberRoute = 1,
    AttackBolt = 2
};

/**
 * How a projectile moves along a leg: it jumps to the leg's end, homes in,
 * falls under gravity, speeds up, closes a spiral, follows its target linked
 * to it, bursts out to one side, or swings out into a spiral.
 */
enum class ProjectileMotion {
    Jump = 0,
    Homing = 1,
    Ballistic = 2,
    Accelerating = 5,
    SpiralClose = 6,
    Linked = 7,
    Burst = 9,
    SpiralOpen = 10
};

/** Presentation-only state. Gameplay hits are saved by the existing event queue. */
struct SavedProjectile {
    struct Leg {
        SavedObjectReference source;
        SavedObjectReference target;
        glm::vec3 origin {0.0f};
        glm::vec3 destination {0.0f};
        float duration {0.0f};
        ProjectileMotion motion {ProjectileMotion::Homing};
        glm::vec3 targetOffset {0.0f};
        std::string targetHook;
        bool ownsTargetHook {false};
        float stopRadius {0.0f};
        /** The ground material a grenade's bounce lands on; none for any other leg. */
        std::optional<int> surface;
    };
    uint64_t id {0};
    ProjectileKind kind {ProjectileKind::Spell};
    SavedObjectReference caster;
    SavedObjectReference weapon;
    int spellId {-1};
    int path {0};
    std::string model;
    std::vector<Leg> legs;
    int leg {0};
    bool released {false};
    bool clockwise {false};
    float elapsed {0.0f};
    glm::vec3 position {0.0f};
    glm::vec3 velocity {0.0f};
    glm::vec3 acceleration {0.0f};
    float activationDelay {0.0f};
    float travelRate {0.0f};
    std::string sourceHook;
    std::string targetHook {"impact"};
    int combatResult {0};
    int soundVariant {0};
    int orientationMode {0};
    glm::quat orientation {1.0f, 0.0f, 0.0f, 0.0f};
    static SavedProjectile fromGff(const resource::Gff &, const SerializedIdentityContext &);
    std::shared_ptr<resource::Gff> toGff() const;
    bool bindObjectReferences(const Game &);
};

/**
 * Item on-hit work of an attack. An attack's saved continuation also keeps
 * the feedback its round has not yet released here.
 */
struct SavedWeaponImpact {
    SavedObjectReference source;
    std::vector<ItemOnHitApplication> applications;
    std::vector<DeferredCombatFeedback> feedback;
    static SavedWeaponImpact fromGff(const resource::Gff &, const SerializedIdentityContext &);
    std::shared_ptr<resource::Gff> toGff() const;
};

/**
 * An attack record: the attack data an attacked event and a ranged round
 * carry, with the target that reacts and the ammunition used.
 */
struct SavedCombatAttack {
    /** The struct type the record is saved with. */
    static constexpr uint32_t kStructType = 0x2222;

    /** Live event nodes borrow the attack record; loaded nodes own a new record. */
    std::shared_ptr<AttackHistory> history {std::make_shared<AttackHistory>()};
    std::shared_ptr<AttackEventFields> fields {std::make_shared<AttackEventFields>()};
    SavedObjectReference reactionObject;
    SavedObjectReference ammoItem;
    /** The record's fields that are not read, saved again unchanged. */
    std::vector<resource::Gff::Field> unknownFields;

    static SavedCombatAttack fromGff(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    std::shared_ptr<resource::Gff> toGff() const;
};

/**
 * The progress of a physical attack command: its round state, the sources and
 * effects of its attacks and their attack records.
 */
struct SavedPhysicalAction {
    std::shared_ptr<resource::Gff> state;
    std::vector<SavedObjectReference> sources;
    std::vector<std::vector<EffectInstance>> targetEffects;
    std::vector<SavedCombatAttack> histories;
    /** A ranged round's shared attack record. */
    std::optional<SavedCombatAttack> roundRecord;
    static SavedPhysicalAction fromGff(const resource::Gff &, const SerializedIdentityContext &);
    std::shared_ptr<resource::Gff> toGff() const;
};

/** A saved command: the game's action record, with the progress of a command under way. */
struct SavedActionRecord {
    /** The command was issued by a scheduled round entry. */
    bool scheduled {false};
    uint32_t actionId {0};
    int groupActionId {0};
    uint16_t declaredParameterCount {0};
    std::vector<SavedActionParameter> parameters;
    std::optional<SavedCastAction> cast;
    std::optional<SavedRoundClock> round;
    std::optional<SavedPhysicalAction> physical;
    /** The record's fields that are not read, saved again unchanged. */
    std::vector<resource::Gff::Field> unknownFields;

    static SavedActionRecord fromGff(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    /** A command of \p type with its parameters, and nothing more. */
    static SavedActionRecord command(SavedActionType type, std::vector<SavedActionParameter> parameters);
    /** Whether a command of this type comes back with these parameters. */
    SavedExecutionSupport executionSupport() const;
    /**
     * The action the command comes back as, or none when it cannot come back:
     * its parameters are not ones its type takes, or an object it needs is
     * gone.
     */
    std::shared_ptr<Action> toRuntimeAction(
        Game &game,
        const SavedScriptSituationImporter *importer = nullptr) const;
    bool bindObjectReferences(const Game &game);

    /** The command has exactly these parameters, in this order. */
    bool hasParameterTypes(std::initializer_list<SavedActionParameterType> types) const;
    int32_t integerParameter(size_t index) const { return std::get<int32_t>(parameters[index].payload); }
    float floatParameter(size_t index) const { return std::get<float>(parameters[index].payload); }
    const SavedObjectReference &objectParameter(size_t index) const {
        return std::get<SavedObjectReference>(parameters[index].payload);
    }
    const std::string &stringParameter(size_t index) const { return std::get<std::string>(parameters[index].payload); }

    SavedActionType type() const { return static_cast<SavedActionType>(actionId); }
};

/**
 * An entry of a combat round's scheduled list (SchedActionList): the game's
 * fields, with the command it runs, whether it has been applied and the pause
 * left on it.
 */
struct SavedScheduledAction {
    /** The ActionType of an entry. */
    static constexpr int kAttackType = 1;
    /** A pause, which carries no command. */
    static constexpr int kPauseType = 3;
    static constexpr int kEquipType = 6;
    static constexpr int kUnequipType = 7;
    static constexpr int kCastSpellType = 9;
    static constexpr int kItemCastSpellType = 10;
    static constexpr int kFeatAttackType = 11;
    /** A move to a point or to an object. */
    static constexpr int kMoveType = 12;
    static constexpr int kCombatStanceType = 13;
    static constexpr int kSwitchWeaponsType = 14;

    int32_t timer {0};
    int animation {0};
    int32_t animationTime {1500};
    int32_t numAttacks {0};
    int type {0};
    SavedObjectReference target;
    bool retargettable {true};
    /** The equipment slot mask of an equip entry. */
    uint32_t inventorySlot {0};
    SavedObjectReference repository;
    std::optional<SavedActionRecord> command;
    bool applied {false};
    float remainingPause {0.0f};

    static SavedScheduledAction fromGff(const resource::Gff &, const SerializedIdentityContext &);
    bool bindObjectReferences(const Game &game);
    bool isEquipment() const { return type == kEquipType || type == kUnequipType; }
};

/** Parsed per-object queue; publication waits until B object registration. */
struct SavedActionQueue {
    std::vector<SavedActionRecord> actions;

    static SavedActionQueue fromGff(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext,
        const std::string &label = "ActionList");
};

enum class SavedEventType : uint32_t {
    Timed = 1,
    EnteredTrigger = 2,
    LeftTrigger = 3,
    RemoveFromArea = 4,
    ApplyEffect = 5,
    CloseObject = 6,
    OpenObject = 7,
    SpellImpact = 8,
    PlayAnimation = 9,
    SignalEvent = 10,
    DestroyObject = 11,
    Unlock = 12,
    Lock = 13,
    RemoveEffect = 14,
    OnMeleeAttacked = 15,
    DecrementStackSize = 16,
    SpawnBodyBag = 17,
    ForcedAction = 18,
    ItemOnHitSpellImpact = 19,
    BroadcastAoo = 20,
    BroadcastSafeProjectile = 21,
    FeedbackMessage = 22,
    AbilityEffectApplied = 23,
    SummonCreature = 24,
    AcquireItem = 25,
    AreaTransition = 26,
    ControllerRumble = 27,
};

struct SavedBytePayload {
    uint8_t value {0};
};
struct SavedIntPayload {
    int32_t value {0};
};
/**
 * A spell impact event: the spell, its caster, target, area and item, its
 * script, where it lands and the Force points the cast cost. The impact reads
 * the rest of the cast from its caster when it is delivered.
 */
struct SavedSpellImpact {
    int32_t spellId {0};
    SavedObjectReference caster;
    SavedObjectReference target;
    SavedObjectReference area;
    SavedObjectReference item;
    std::string script;
    glm::vec3 targetPosition {0.0f};
    int32_t finalForceCost {0};
    std::shared_ptr<resource::Gff> toGff() const;
};

struct SavedBodyBag {
    SavedObjectReference object;
    glm::vec3 position {0.0f};
};

/**
 * An open-object event raised during play: the caller opens the target's
 * inventory. The event is saved without data, so a loaded one opens nothing.
 */
struct SavedOpenObject {};

/** The game EVENT_BROADCAST_AOO stores an ObjectId in its generic DWORD Value. */
struct SavedBroadcastAoo {
    SavedObjectReference target;
};

/** An event whose data is not read, or that carries none, is empty. */
using SavedEventPayload = std::variant<
    std::monostate,
    SerializedScriptSituation,
    EffectInstance,
    SavedBytePayload,
    SavedIntPayload,
    SavedSpellImpact,
    SavedWeaponImpact,
    SavedScriptEvent,
    SavedBodyBag,
    SavedOpenObject,
    SavedBroadcastAoo,
    SavedCombatAttack,
    CombatFeedbackMessage>;

struct SavedEventRecord {
    /** Absolute world/game timestamp, never a wall-clock or derived delay. */
    uint32_t day {0};
    uint32_t time {0};
    SavedObjectReference object;
    SavedObjectReference caller;
    uint32_t eventId {0};
    SavedEventPayload payload;
    /** The event's data when it is not read, saved again unchanged. */
    std::shared_ptr<const resource::Gff> unreadData;
    /** The record's fields that are not read, saved again unchanged. */
    std::vector<resource::Gff::Field> unknownFields;

    static SavedEventRecord fromGff(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    SavedExecutionSupport executionSupport() const;
    bool shouldRestore() const;
    bool bindObjectReferences(const Game &game);
};

/** Parsed module/server queue; list order is the authoritative queue order. */
struct SavedEventQueue {
    std::vector<SavedEventRecord> events;

    static SavedEventQueue fromGff(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext,
        const std::string &label = "EventQueue");
};

} // namespace game

} // namespace reone
