/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "reone/game/savedruntime.h"

#include <array>
#include <cmath>
#include <initializer_list>
#include <set>

#include "reone/game/action/attackobject.h"
#include "reone/game/action/castspellatlocation.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/equipitem.h"
#include "reone/game/action/followleader.h"
#include "reone/game/action/movetolocation.h"
#include "reone/game/action/movetoobject.h"
#include "reone/game/action/playanimation.h"
#include "reone/game/action/startconversation.h"
#include "reone/game/action/switchweapons.h"
#include "reone/game/action/unequipitem.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/action/wait.h"
#include "reone/game/combat.h"
#include "reone/game/d20/spells.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/script/savedsituation.h"
#include "reone/system/exception/validation.h"

namespace reone {

namespace game {

std::shared_ptr<Object> SavedObjectReference::boundObject() const {
    return _object.resolve();
}

namespace {

// A record only this build writes is written whole: one that lacks a field is
// invalid.
void requireFields(const resource::Gff &gff, std::initializer_list<const char *> labels) {
    for (const char *label : labels) {
        if (!gff.has(label)) {
            throw ValidationException(std::string("Saved record has no ") + label);
        }
    }
}

template <class... Visitors>
struct Overloaded : Visitors... {
    using Visitors::operator()...;
};

template <class... Visitors>
Overloaded(Visitors...) -> Overloaded<Visitors...>;

bool hasParameterTypes(const SavedActionRecord &record,
                       std::initializer_list<SavedActionParameterType> types) {
    if (record.declaredParameterCount != types.size() || record.parameters.size() != types.size()) {
        return false;
    }
    size_t index = 0;
    for (auto type : types) {
        const auto &parameter = record.parameters[index++];
        if (parameter.type != static_cast<uint32_t>(type)) {
            return false;
        }
        const auto &payload = parameter.payload;
        const bool held = type == SavedActionParameterType::Integer  ? std::holds_alternative<int32_t>(payload)
                          : type == SavedActionParameterType::Float  ? std::holds_alternative<float>(payload)
                          : type == SavedActionParameterType::Object ? std::holds_alternative<SavedObjectReference>(payload)
                          : type == SavedActionParameterType::String ? std::holds_alternative<std::string>(payload)
                                                                     : std::holds_alternative<SerializedScriptSituation>(payload);
        if (!held) {
            return false;
        }
    }
    return true;
}

int32_t integerParameter(const SavedActionRecord &record, size_t index) {
    return std::get<int32_t>(record.parameters[index].payload);
}

float floatParameter(const SavedActionRecord &record, size_t index) {
    return std::get<float>(record.parameters[index].payload);
}

const SavedObjectReference &objectParameter(const SavedActionRecord &record, size_t index) {
    return std::get<SavedObjectReference>(record.parameters[index].payload);
}

// An AttackObject command's parameters: whether it is a cutscene attack, the
// target, the value 1, the attack animation and its length, the attack count,
// the feat, and the three values a cutscene attack forces. An ordinary attack
// makes one attack and carries 0, 4 and 0 in the place of the forced values.
constexpr size_t kAttackCommandParameterCount = 10;
constexpr int32_t kAttackCommandThirdValue = 1;
constexpr int32_t kOrdinaryAttackCount = 1;
constexpr std::array<int32_t, 3> kOrdinaryAttackForcedValues {0, 4, 0};

// The CastSpell command's parameters: the spell, the class index, the domain
// level, the metamagic, whether payment is skipped, the target, the location,
// the projectile path with the cast modes in its top bits, the associated feat
// and the caster level.
constexpr size_t kSpellCommandParameterCount = 12;
// A cast with no associated feat carries this value.
constexpr int32_t kNoAssociatedFeat = -1;
// The path's top bits mark a fake cast and an instant cast.
constexpr uint32_t kFakeCastBit = 0x80000000u;
constexpr uint32_t kInstantCastBit = 0x40000000u;
constexpr uint32_t kProjectilePathMask = 0xffu;

// The fields of a saved struct whose labels are not among \p known, copied.
std::vector<resource::Gff::Field> unknownFieldsOf(const resource::Gff &gff, const std::set<std::string_view> &known) {
    std::vector<resource::Gff::Field> result;
    for (const auto &field : gff.fields()) {
        if (known.count(field.label) == 0) {
            result.push_back(field.deepCopy());
        }
    }
    return result;
}

SavedLocationValue savedLocationFromGff(const resource::Gff &gff) {
    SavedLocationValue result;
    result.position = glm::vec3(
        gff.getFloat("PositionX"),
        gff.getFloat("PositionY"),
        gff.getFloat("PositionZ"));
    result.orientation = glm::vec3(
        gff.getFloat("OrientationX"),
        gff.getFloat("OrientationY"),
        gff.getFloat("OrientationZ"));
    return result;
}

SavedScriptEvent savedScriptEventFromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    SavedScriptEvent result;
    result.type = static_cast<uint16_t>(gff.getUint("EventType"));
    for (const auto &item : gff.getList("IntList")) {
        result.integers.push_back(item->getInt("Parameter"));
    }
    for (const auto &item : gff.getList("FloatList")) {
        result.floats.push_back(item->getFloat("Parameter"));
    }
    for (const auto &item : gff.getList("StringList")) {
        result.strings.push_back(item->getString("Parameter"));
    }
    for (const auto &item : gff.getList("ObjectList")) {
        result.objects.push_back(SavedObjectReference::fromSerializedId(
            item->getUint("Parameter"), identityContext));
    }
    return result;
}

SavedSpellImpact savedSpellImpactFromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    SavedSpellImpact result;
    result.spellId = gff.getInt("SpellId");
    result.caster = SavedObjectReference::fromSerializedId(
        gff.getUint("CasterId"), identityContext);
    result.target = SavedObjectReference::fromSerializedId(
        gff.getUint("TargetId"), identityContext);
    result.area = SavedObjectReference::fromSerializedId(
        gff.getUint("AreaId"), identityContext);
    result.item = SavedObjectReference::fromSerializedId(
        gff.getUint("ItemId"), identityContext);
    result.script = gff.getString("Script");
    result.targetPosition = glm::vec3(
        gff.getFloat("TargetPosX"),
        gff.getFloat("TargetPosY"),
        gff.getFloat("TargetPosZ"));
    result.finalForceCost = gff.getInt("FinalForceCost");
    return result;
}

SavedBodyBag savedBodyBagFromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    SavedBodyBag result;
    result.object = SavedObjectReference::fromSerializedId(
        gff.getUint("BodyBagId"), identityContext);
    result.position = glm::vec3(
        gff.getFloat("PositionX"),
        gff.getFloat("PositionY"),
        gff.getFloat("PositionZ"));
    return result;
}

SavedTalentValue savedTalentFromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    SavedTalentValue result;
    result.id = gff.getInt("ID");
    result.type = gff.getInt("Type");
    result.multiClass = static_cast<uint8_t>(gff.getUint("MultiClass"));
    result.item = SavedObjectReference::fromSerializedId(
        gff.getUint("Item"), identityContext);
    // The saved label of the item property index is cut to sixteen characters.
    result.itemPropertyIndex = gff.getInt("ItemPropertyInde");
    result.casterLevel = static_cast<uint8_t>(gff.getUint("CasterLevel"));
    result.metaType = static_cast<uint8_t>(gff.getUint("MetaType"));
    return result;
}

struct SavedMoveToPoint {
    glm::vec3 destination {0.0f};
    SavedObjectReference area;
    SavedObjectReference target;
    bool run {false};
    float range {0.0f};
    float timeout {0.0f};
    bool forcedPending {false};
    bool forcedActive {false};
    uint32_t expiryDay {0};
    uint32_t expiryTime {0};
};

std::optional<SavedMoveToPoint> decodeMoveToPoint(const SavedActionRecord &record) {
    using Type = SavedActionParameterType;
    if (record.type() != SavedActionType::MoveToPoint ||
        !hasParameterTypes(record, {Type::Float, Type::Float, Type::Float, Type::Object, Type::Object,
                                    Type::Integer, Type::Float, Type::Integer, Type::Float, Type::Float,
                                    Type::Float, Type::Integer, Type::Integer})) {
        return std::nullopt;
    }

    SavedMoveToPoint result;
    result.destination = glm::vec3(
        std::get<float>(record.parameters[0].payload),
        std::get<float>(record.parameters[1].payload),
        std::get<float>(record.parameters[2].payload));
    result.area = std::get<SavedObjectReference>(record.parameters[3].payload);
    result.target = std::get<SavedObjectReference>(record.parameters[4].payload);
    int32_t flags = std::get<int32_t>(record.parameters[5].payload);
    result.run = (flags & 1) != 0;
    result.range = std::get<float>(record.parameters[6].payload);
    int32_t subtype = std::get<int32_t>(record.parameters[7].payload);
    result.timeout = std::get<float>(record.parameters[8].payload);
    glm::vec2 offset(
        std::get<float>(record.parameters[9].payload),
        std::get<float>(record.parameters[10].payload));
    int32_t day = std::get<int32_t>(record.parameters[11].payload);
    int32_t time = std::get<int32_t>(record.parameters[12].payload);
    bool timed = (flags & 4) != 0;
    bool ordinary = !timed && day == 0 && time == 0 && result.timeout == 0.0f;
    result.forcedPending = timed && result.timeout > 0.0f && day == 0 && time == 0;
    // A non-negative time beyond the configured day length is unnormalized,
    // not invalid: composing the absolute clock carries the excess into later
    // days. Snapshots split that clock into a normalized day/time pair.
    result.forcedActive = !timed && (day != 0 || time != 0) &&
                          result.timeout == 0.0f && day >= 0 && time >= 0;
    bool valid =
        std::isfinite(result.destination.x) && std::isfinite(result.destination.y) &&
        std::isfinite(result.destination.z) && std::isfinite(result.range) &&
        result.range >= 0.0f && std::isfinite(result.timeout) &&
        std::isfinite(offset.x) && std::isfinite(offset.y) &&
        offset == glm::vec2(0.0f) && subtype == 0 && (flags & ~5) == 0 &&
        !result.area.isInvalid() &&
        (ordinary || result.forcedPending || result.forcedActive) &&
        (result.target.isInvalid() ? result.range == 0.0f : !ordinary);
    if (!valid) {
        return std::nullopt;
    }
    result.expiryDay = static_cast<uint32_t>(day);
    result.expiryTime = static_cast<uint32_t>(time);
    return result;
}

bool bindReference(const Game &game, SavedObjectReference &reference, bool &allBound) {
    if (reference.isInvalid()) {
        return true;
    }
    bool bound = game.bindSavedObjectReference(reference);
    allBound = allBound && bound;
    return bound;
}

} // namespace

SerializedScriptSituation SerializedScriptSituation::fromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    SerializedScriptSituation result;
    result.codeSize = gff.getInt("CodeSize");
    result.code = gff.getData("Code");
    result.crc = gff.getUint("CRC");
    result.instructionPointer = gff.getInt("InstructionPtr");
    result.secondaryPointer = gff.getInt("SecondaryPtr");
    result.scriptName = gff.getString("Name");
    result.stackSize = gff.getInt("StackSize");
    result.unknownFields = unknownFieldsOf(
        gff, {"CodeSize", "Code", "CRC", "InstructionPtr", "SecondaryPtr", "Name", "StackSize", "Stack"});

    auto stackStruct = gff.findStruct("Stack");
    if (!stackStruct) {
        return result;
    }
    result.basePointer = stackStruct->getInt("BasePointer");
    result.stackPointer = stackStruct->getInt("StackPointer");
    result.totalSize = stackStruct->getInt("TotalSize");
    for (const auto &item : stackStruct->getList("Stack")) {
        SavedVmStackValue value;
        int8_t type = 0;
        item->readChar(type, "Type");
        value.type = type;
        switch (static_cast<SavedVmStackType>(type)) {
        case SavedVmStackType::Integer:
            value.payload = item->getInt("Value");
            break;
        case SavedVmStackType::Float:
            value.payload = item->getFloat("Value");
            break;
        case SavedVmStackType::String:
            value.payload = item->getString("Value");
            break;
        case SavedVmStackType::Object:
            value.payload = SavedObjectReference::fromSerializedId(
                item->getUint("Value"), identityContext);
            break;
        case SavedVmStackType::Effect:
            if (auto structure = item->findStruct("GameDefinedStrct")) {
                value.payload = EffectInstance::fromGff(*structure, identityContext);
            }
            break;
        case SavedVmStackType::Event:
            if (auto structure = item->findStruct("GameDefinedStrct")) {
                value.payload = savedScriptEventFromGff(*structure, identityContext);
            }
            break;
        case SavedVmStackType::Location:
            if (auto structure = item->findStruct("GameDefinedStrct")) {
                value.payload = savedLocationFromGff(*structure);
            }
            break;
        case SavedVmStackType::Talent:
            if (auto structure = item->findStruct("GameDefinedStrct")) {
                value.payload = savedTalentFromGff(*structure, identityContext);
            }
            break;
        default:
            break;
        }
        if (std::holds_alternative<std::monostate>(value.payload)) {
            value.unread = item->deepCopy();
        }
        result.stack.push_back(std::move(value));
    }
    return result;
}

bool SerializedScriptSituation::bindObjectReferences(const Game &game) {
    if (_runtimeSession && *_runtimeSession != game._runtimeSessionGeneration) {
        return false;
    }
    if (!_runtimeSession) {
        _runtimeSession = game._runtimeSessionGeneration;
    }

    bool allBound = true;
    for (auto &entry : stack) {
        std::visit(
            Overloaded {
                [](std::monostate &) {},
                [](int32_t &) {},
                [](float &) {},
                [](std::string &) {},
                [&game, &allBound](SavedObjectReference &reference) {
                    bindReference(game, reference, allBound);
                },
                [&game, &allBound](EffectInstance &effect) {
                    allBound = game.bindEffectCreator(effect) && allBound;
                },
                [&game, &allBound](SavedScriptEvent &event) {
                    for (auto &reference : event.objects) {
                        bindReference(game, reference, allBound);
                    }
                },
                [](SavedLocationValue &) {},
                [&game, &allBound](SavedTalentValue &talent) {
                    bindReference(game, talent.item, allBound);
                },
            },
            entry.payload);
    }
    return allBound;
}

bool SerializedScriptSituation::isBoundToCurrentRuntimeSession(const Game &game) const {
    // The game does not check the stack's objects: an object that does not
    // resolve is simply an invalid object when the script reads it.
    return _runtimeSession &&
           *_runtimeSession == game._runtimeSessionGeneration;
}

SavedActionParameter SavedActionParameter::fromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    SavedActionParameter result;
    result.type = gff.getUint("Type");
    switch (static_cast<SavedActionParameterType>(result.type)) {
    case SavedActionParameterType::Integer:
        result.payload = gff.getInt("Value");
        break;
    case SavedActionParameterType::Float:
        result.payload = gff.getFloat("Value");
        break;
    case SavedActionParameterType::Object:
        result.payload = SavedObjectReference::fromSerializedId(
            gff.getUint("Value"), identityContext);
        break;
    case SavedActionParameterType::String:
        result.payload = gff.getString("Value");
        break;
    case SavedActionParameterType::ScriptSituation:
        if (auto value = gff.findStruct("Value")) {
            result.payload = SerializedScriptSituation::fromGff(*value, identityContext);
        }
        break;
    default:
        break;
    }
    if (std::holds_alternative<std::monostate>(result.payload)) {
        result.unread = gff.deepCopy();
    }
    return result;
}

bool SavedActionParameter::bindObjectReferences(const Game &game) {
    if (auto reference = std::get_if<SavedObjectReference>(&payload)) {
        return reference->isInvalid() || game.bindSavedObjectReference(*reference);
    }
    if (auto situation = std::get_if<SerializedScriptSituation>(&payload)) {
        return situation->bindObjectReferences(game);
    }
    return true;
}

namespace {

using GffState = resource::Gff;
using StateField = resource::Gff::Field;

// The struct type a spell impact event's data is saved with; the weapon form
// of an item on-hit impact shares it.
constexpr uint32_t kSpellImpactStructType = 0x6666;
// The struct type of an attack record's damage entry.
constexpr uint32_t kAttackDamageStructType = 0xdaee;
// An attack record names no attack group with this value, and no reaction
// animation with this one.
constexpr uint32_t kNoAttackGroup = 0xff;
constexpr uint32_t kNoReactionAnimation = 10001;

// The kinds of deferred feedback a weapon impact keeps, by their saved tag.
enum class WeaponImpactFeedbackTag {
    AbilityDrain = 0,
    SavingThrow = 1,
    SneakAttackImmunity = 2,
    Mitigation = 3,
    CriticalHitImmunity = 4,
    WeaponIneffective = 5,
    ReturnedMitigation = 6,
};

std::shared_ptr<GffState> scalarState(const std::vector<int> &values) {
    std::vector<std::shared_ptr<GffState>> fields;
    for (int value : values) {
        fields.push_back(GffState::Builder().type(0).field(StateField::newInt("Value", value)).build());
    }
    return GffState::Builder().type(0).field(StateField::newList("Values", std::move(fields))).build();
}

std::shared_ptr<GffState> feedbackState(WeaponImpactFeedbackTag tag, std::vector<int> values = {}) {
    values.insert(values.begin(), static_cast<int>(tag));
    return scalarState(values);
}

int stateValue(const resource::Gff &g, size_t index) {
    return g.getList("Values").at(index)->getInt("Value");
}

std::vector<int> mitigationValues(const MitigationFeedback &feedback) {
    return {static_cast<int>(feedback.type), feedback.amount, feedback.remaining.has_value(),
            feedback.remaining.value_or(0), feedback.damageFlags};
}

} // namespace

std::shared_ptr<resource::Gff> SavedWeaponImpact::toGff() const {
    using Tag = WeaponImpactFeedbackTag;
    std::vector<std::shared_ptr<GffState>> apps, messages;
    for (const auto &a : applications) {
        const auto &e = a.effectOutcome;
        auto g = scalarState({static_cast<int>(a.subtype), a.parameter, a.emitEffectOutcome,
                              e.present, e.saveType, e.effectType, e.saveMode, e.saveRoll, e.modifierTotal,
                              e.baseSave, e.finalTotal, e.difficultyClass, e.outcome});
        g->fields().push_back(StateField::newFloat("Duration", a.duration));
        apps.push_back(std::move(g));
    }
    for (const auto &entry : feedback) {
        if (const auto *a = std::get_if<AbilityDrainFeedback>(&entry)) {
            messages.push_back(feedbackState(Tag::AbilityDrain, {static_cast<int>(a->ability), a->amount, a->durationSeconds}));
        } else if (const auto *v = std::get_if<SavingThrowFeedback>(&entry))
            messages.push_back(feedbackState(Tag::SavingThrow, {static_cast<int>(v->savingThrow), v->base, v->modifier,
                                                                v->roll, v->difficultyClass, static_cast<int>(v->savingThrowType)}));
        else if (std::holds_alternative<SneakAttackImmunityFeedback>(entry)) {
            messages.push_back(feedbackState(Tag::SneakAttackImmunity));
        } else if (const auto *m = std::get_if<MitigationFeedback>(&entry)) {
            messages.push_back(feedbackState(Tag::Mitigation, mitigationValues(*m)));
        } else if (std::holds_alternative<CriticalHitImmunityFeedback>(entry)) {
            messages.push_back(feedbackState(Tag::CriticalHitImmunity));
        } else if (std::holds_alternative<WeaponIneffectiveFeedback>(entry)) {
            messages.push_back(feedbackState(Tag::WeaponIneffective));
        } else if (const auto *r = std::get_if<ReturnedMitigationFeedback>(&entry)) {
            messages.push_back(feedbackState(Tag::ReturnedMitigation, mitigationValues(r->feedback)));
        }
    }
    return GffState::Builder().type(kSpellImpactStructType).field(StateField::newDword("WeaponImpact", 1)).field(StateField::newDword("Source", source.id)).field(StateField::newList("Applications", std::move(apps))).field(StateField::newList("Feedback", std::move(messages))).build();
}

SavedWeaponImpact SavedWeaponImpact::fromGff(const resource::Gff &g, const SerializedIdentityContext &ids) {
    using Tag = WeaponImpactFeedbackTag;
    SavedWeaponImpact result;
    result.source = SavedObjectReference::fromSerializedId(g.getUint("Source", kSavedRuntimeInvalidObjectId), ids);
    for (const auto &v : g.getList("Applications")) {
        ItemOnHitApplication a;
        a.subtype = static_cast<ItemOnHitSubtype>(stateValue(*v, 0));
        a.parameter = stateValue(*v, 1);
        a.emitEffectOutcome = stateValue(*v, 2) != 0;
        a.duration = v->getFloat("Duration");
        auto &e = a.effectOutcome;
        e.present = stateValue(*v, 3) != 0;
        e.saveType = stateValue(*v, 4);
        e.effectType = stateValue(*v, 5);
        e.saveMode = stateValue(*v, 6);
        e.saveRoll = stateValue(*v, 7);
        e.modifierTotal = stateValue(*v, 8);
        e.baseSave = stateValue(*v, 9);
        e.finalTotal = stateValue(*v, 10);
        e.difficultyClass = stateValue(*v, 11);
        e.outcome = stateValue(*v, 12);
        result.applications.push_back(std::move(a));
    }
    for (const auto &v : g.getList("Feedback")) {
        const auto mitigation = [&] {
            return MitigationFeedback {static_cast<MitigationFeedbackType>(stateValue(*v, 1)), stateValue(*v, 2),
                                       stateValue(*v, 3) != 0 ? std::optional<int>(stateValue(*v, 4)) : std::nullopt, stateValue(*v, 5)};
        };
        switch (static_cast<Tag>(stateValue(*v, 0))) {
        case Tag::AbilityDrain:
            result.feedback.push_back(AbilityDrainFeedback {
                static_cast<Ability>(stateValue(*v, 1)), stateValue(*v, 2), stateValue(*v, 3)});
            break;
        case Tag::SavingThrow:
            result.feedback.push_back(SavingThrowFeedback {static_cast<SavingThrow>(stateValue(*v, 1)),
                                                           stateValue(*v, 2), stateValue(*v, 3), stateValue(*v, 4), stateValue(*v, 5),
                                                           static_cast<SavingThrowType>(stateValue(*v, 6))});
            break;
        case Tag::SneakAttackImmunity:
            result.feedback.push_back(SneakAttackImmunityFeedback {});
            break;
        case Tag::Mitigation:
            result.feedback.push_back(mitigation());
            break;
        case Tag::CriticalHitImmunity:
            result.feedback.push_back(CriticalHitImmunityFeedback {});
            break;
        case Tag::WeaponIneffective:
            result.feedback.push_back(WeaponIneffectiveFeedback {});
            break;
        case Tag::ReturnedMitigation:
            result.feedback.push_back(ReturnedMitigationFeedback {mitigation()});
            break;
        }
    }
    return result;
}

std::shared_ptr<resource::Gff> SavedSpellImpact::toGff() const {
    using resource::Gff;
    return Gff::Builder().type(kSpellImpactStructType).field(Gff::Field::newInt("SpellId", spellId)).field(Gff::Field::newDword("CasterId", caster.id)).field(Gff::Field::newDword("TargetId", target.id)).field(Gff::Field::newDword("AreaId", area.id)).field(Gff::Field::newDword("ItemId", item.id)).field(Gff::Field::newCExoString("Script", script)).field(Gff::Field::newFloat("TargetPosX", targetPosition.x)).field(Gff::Field::newFloat("TargetPosY", targetPosition.y)).field(Gff::Field::newFloat("TargetPosZ", targetPosition.z)).field(Gff::Field::newInt("FinalForceCost", finalForceCost)).build();
}

SavedCombatAttack SavedCombatAttack::fromGff(const resource::Gff &gff, const SerializedIdentityContext &identityContext) {
    SavedCombatAttack result;
    auto &fields = *result.fields;
    // A field the record lacks loads as zero, and the group and reaction as none.
    const auto group = gff.getUint("AttackGroup", kNoAttackGroup);
    if (group != kNoAttackGroup) {
        fields.group = static_cast<int>(group);
    }
    fields.animationLength = static_cast<int>(gff.getUint("AnimationLength"));
    fields.missedBy = static_cast<int>(gff.getUint("MissedBy"));
    fields.result = static_cast<AttackResultType>(gff.getUint("AttackResult"));
    fields.reactionDelay = static_cast<int>(gff.getUint("ReaxnDelay"));
    const auto reaction = gff.getUint("ReaxnAnimation", kNoReactionAnimation);
    if (reaction != kNoReactionAnimation) {
        fields.reactionAnimation = static_cast<AttackReaction>(reaction);
    }
    fields.reactionAnimationLength = static_cast<int>(gff.getUint("ReaxnAnimLength"));
    fields.concealment = static_cast<int>(gff.getUint("Concealment"));
    fields.ranged = gff.getInt("RangedAttack") != 0;
    fields.sneakAttack = gff.getInt("SneakAttack") != 0;
    fields.weaponAttackType = static_cast<int>(gff.getUint("WeaponAttackType"));
    fields.rangedTarget = {gff.getFloat("RangedTargetX"), gff.getFloat("RangedTargetY"), gff.getFloat("RangedTargetZ")};
    // The record keeps one damage entry per damage slot.
    const auto damage = gff.getList("DamageList");
    for (size_t i = 0; i < damage.size(); ++i) {
        fields.damage.at(i) = damage[i]->getInt("DamageValue");
    }
    fields.killingBlow = gff.getUint("KillingBlow") != 0;
    fields.coupDeGrace = gff.getUint("CoupDeGrace") != 0;
    fields.criticalThreat = static_cast<int>(gff.getUint("CriticalThreat"));
    fields.deflected = gff.getUint("AttackDeflected") != 0;
    fields.attackDebugText = gff.getString("AttackDebugText");
    fields.damageDebugText = gff.getString("DamageDebugText");
    result.history->type = static_cast<int>(gff.getUint("AttackType"));
    result.history->mode = static_cast<int>(gff.getUint("AttackMode"));
    result.reactionObject = SavedObjectReference::fromSerializedId(gff.getUint("ReactObject"), identityContext);
    result.ammoItem = SavedObjectReference::fromSerializedId(gff.getUint("AmmoItem"), identityContext);
    result.unknownFields = unknownFieldsOf(
        gff, {"AttackGroup", "AnimationLength", "MissedBy", "AttackResult", "ReactObject", "ReaxnDelay",
              "ReaxnAnimation", "ReaxnAnimLength", "Concealment", "AttackType", "AttackMode", "RangedAttack",
              "SneakAttack", "WeaponAttackType", "RangedTargetX", "RangedTargetY", "RangedTargetZ", "DamageList",
              "KillingBlow", "CoupDeGrace", "CriticalThreat", "AttackDeflected", "AmmoItem", "AttackDebugText",
              "DamageDebugText"});
    return result;
}

std::shared_ptr<resource::Gff> SavedCombatAttack::toGff() const {
    using Field = resource::Gff::Field;
    std::vector<std::shared_ptr<resource::Gff>> damage;
    damage.reserve(fields->damage.size());
    for (int amount : fields->damage) {
        damage.push_back(resource::Gff::Builder().type(kAttackDamageStructType).field(Field::newShort("DamageValue", amount)).build());
    }
    auto result = resource::Gff::Builder().type(kStructType).field(Field::newByte("AttackGroup", fields->group ? static_cast<uint32_t>(*fields->group) : kNoAttackGroup)).field(Field::newWord("AnimationLength", fields->animationLength)).field(Field::newDword("MissedBy", fields->missedBy)).field(Field::newByte("AttackResult", static_cast<uint32_t>(fields->result))).field(Field::newDword("ReactObject", reactionObject.id)).field(Field::newWord("ReaxnDelay", fields->reactionDelay)).field(Field::newWord("ReaxnAnimation", fields->reactionAnimation ? static_cast<uint32_t>(*fields->reactionAnimation) : kNoReactionAnimation)).field(Field::newWord("ReaxnAnimLength", fields->reactionAnimationLength)).field(Field::newByte("Concealment", fields->concealment)).field(Field::newWord("AttackType", history->type)).field(Field::newByte("AttackMode", history->mode)).field(Field::newInt("RangedAttack", fields->ranged)).field(Field::newInt("SneakAttack", fields->sneakAttack)).field(Field::newByte("WeaponAttackType", fields->weaponAttackType)).field(Field::newFloat("RangedTargetX", fields->rangedTarget[0])).field(Field::newFloat("RangedTargetY", fields->rangedTarget[1])).field(Field::newFloat("RangedTargetZ", fields->rangedTarget[2])).field(Field::newList("DamageList", std::move(damage))).field(Field::newByte("KillingBlow", fields->killingBlow)).field(Field::newByte("CoupDeGrace", fields->coupDeGrace)).field(Field::newByte("CriticalThreat", fields->criticalThreat)).field(Field::newByte("AttackDeflected", fields->deflected)).field(Field::newDword("AmmoItem", ammoItem.id)).field(Field::newCExoString("AttackDebugText", fields->attackDebugText)).field(Field::newCExoString("DamageDebugText", fields->damageDebugText)).build();
    for (const auto &field : unknownFields) {
        result->fields().push_back(field.deepCopy());
    }
    return result;
}

std::shared_ptr<resource::Gff> SavedPhysicalAction::toGff() const {
    std::vector<std::shared_ptr<GffState>> refs, targetLists, records;
    for (const auto &ref : sources) {
        refs.push_back(GffState::Builder().type(0).field(StateField::newDword("Object", ref.id)).build());
    }
    for (const auto &list : targetEffects) {
        std::vector<std::shared_ptr<GffState>> entries;
        for (const auto &effect : list) {
            entries.push_back(effect.toGff());
        }
        targetLists.push_back(GffState::Builder().type(0).field(StateField::newList("Effects", std::move(entries))).build());
    }
    for (const auto &history : histories) {
        records.push_back(history.toGff());
    }
    auto result = GffState::Builder().type(0).field(StateField::newStruct("State", state)).field(StateField::newList("Sources", std::move(refs))).field(StateField::newList("TargetEffects", std::move(targetLists))).field(StateField::newList("History", std::move(records))).build();
    if (roundRecord) {
        result->fields().push_back(StateField::newStruct("RoundRecord", roundRecord->toGff()));
    }
    return result;
}

SavedPhysicalAction SavedPhysicalAction::fromGff(const resource::Gff &g, const SerializedIdentityContext &ids) {
    SavedPhysicalAction result;
    result.state = g.findStruct("State");
    for (const auto &v : g.getList("Sources"))
        result.sources.push_back(SavedObjectReference::fromSerializedId(
            v->getUint("Object", kSavedRuntimeInvalidObjectId), ids));
    for (const auto &list : g.getList("TargetEffects")) {
        auto &entries = result.targetEffects.emplace_back();
        for (const auto &effect : list->getList("Effects")) {
            entries.push_back(EffectInstance::fromGff(*effect, ids));
        }
    }
    for (const auto &v : g.getList("History")) {
        result.histories.push_back(SavedCombatAttack::fromGff(*v, ids));
    }
    if (auto record = g.findStruct("RoundRecord")) {
        result.roundRecord = SavedCombatAttack::fromGff(*record, ids);
    }
    return result;
}

std::optional<SavedAttackCommand> SavedAttackCommand::fromAction(const SavedActionRecord &record) {
    using Type = SavedActionParameterType;
    if (record.type() != SavedActionType::AttackObject ||
        !hasParameterTypes(record, {Type::Integer, Type::Object, Type::Integer, Type::Integer, Type::Integer,
                                    Type::Integer, Type::Integer, Type::Integer, Type::Integer, Type::Integer})) {
        return std::nullopt;
    }
    const auto parameter = [&record](size_t index) { return integerParameter(record, index); };
    if (parameter(2) != kAttackCommandThirdValue || parameter(3) != kAnimation ||
        parameter(4) != kAnimationMilliseconds) {
        return std::nullopt;
    }
    const auto &target = objectParameter(record, 1);
    // A cutscene attack has no attack count, no feat and any forced values.
    if (parameter(0) == 1) {
        if (parameter(5) != 0 || parameter(6) != 0) {
            return std::nullopt;
        }
        return SavedAttackCommand {target, FeatType::Invalid, CutsceneAttack {parameter(7), parameter(8), parameter(9)}};
    }
    if (parameter(0) != 0 || parameter(5) != kOrdinaryAttackCount ||
        std::array<int32_t, 3> {parameter(7), parameter(8), parameter(9)} != kOrdinaryAttackForcedValues) {
        return std::nullopt;
    }
    const auto feat = static_cast<FeatType>(parameter(6));
    if (feat != FeatType::Invalid && !isPhysicalAttackFeat(feat)) {
        return std::nullopt;
    }
    return SavedAttackCommand {target, feat, std::nullopt};
}

void SavedAttackCommand::toAction(SavedActionRecord &record) const {
    using Type = SavedActionParameterType;
    const auto integer = [](int32_t value) { return SavedActionParameter {static_cast<uint32_t>(Type::Integer), value}; };
    const auto forced = cutscene.value_or(CutsceneAttack {});
    record.actionId = static_cast<uint32_t>(SavedActionType::AttackObject);
    record.declaredParameterCount = kAttackCommandParameterCount;
    record.parameters = {
        integer(cutscene ? 1 : 0),
        SavedActionParameter {static_cast<uint32_t>(Type::Object), target},
        integer(kAttackCommandThirdValue),
        integer(kAnimation),
        integer(kAnimationMilliseconds),
        integer(cutscene ? 0 : kOrdinaryAttackCount),
        integer(cutscene ? 0 : static_cast<int32_t>(feat)),
        integer(cutscene ? forced.animation : kOrdinaryAttackForcedValues[0]),
        integer(cutscene ? forced.result : kOrdinaryAttackForcedValues[1]),
        integer(cutscene ? forced.damage : kOrdinaryAttackForcedValues[2]),
    };
}

std::optional<SavedSpellCommand> SavedSpellCommand::fromAction(const SavedActionRecord &record) {
    using Type = SavedActionParameterType;
    if (record.type() != SavedActionType::CastSpell ||
        !hasParameterTypes(record, {Type::Integer, Type::Integer, Type::Integer, Type::Integer, Type::Integer,
                                    Type::Object, Type::Float, Type::Float, Type::Float, Type::Integer,
                                    Type::Integer, Type::Integer})) {
        return std::nullopt;
    }
    const auto value = [&record](size_t index) { return integerParameter(record, index); };
    const auto path = static_cast<uint32_t>(value(9));
    if ((path & ~(kProjectilePathMask | kFakeCastBit | kInstantCastBit)) != 0) {
        return std::nullopt;
    }
    SavedSpellCommand result;
    // The domain level, the meta type and the spontaneous-cast parameter are
    // carried as the command gave them.
    result.domainLevel = value(2);
    result.metaType = value(3);
    result.spontaneousCast = value(4);
    result.spellId = value(0);
    // The class index and the caster level are byte values.
    result.selection = SpellSelection {static_cast<uint8_t>(value(1)), static_cast<uint8_t>(value(11))};
    result.target = objectParameter(record, 5);
    result.position = glm::vec3(floatParameter(record, 6), floatParameter(record, 7), floatParameter(record, 8));
    result.path = static_cast<ProjectilePathType>(path & kProjectilePathMask);
    if (value(10) != kNoAssociatedFeat) {
        result.associatedFeat = static_cast<FeatType>(value(10));
    }
    result.fake = (path & kFakeCastBit) != 0;
    result.instant = (path & kInstantCastBit) != 0;
    return result;
}

void SavedSpellCommand::toAction(SavedActionRecord &record) const {
    using Type = SavedActionParameterType;
    const auto integer = [](int32_t value) { return SavedActionParameter {static_cast<uint32_t>(Type::Integer), value}; };
    const auto real = [](float value) { return SavedActionParameter {static_cast<uint32_t>(Type::Float), value}; };
    const uint32_t packedPath = static_cast<uint32_t>(path) | (fake ? kFakeCastBit : 0u) | (instant ? kInstantCastBit : 0u);
    record.actionId = static_cast<uint32_t>(SavedActionType::CastSpell);
    record.declaredParameterCount = kSpellCommandParameterCount;
    record.parameters = {
        integer(spellId),
        integer(selection.classIndex),
        integer(domainLevel),
        integer(metaType),
        integer(spontaneousCast),
        SavedActionParameter {static_cast<uint32_t>(Type::Object), target},
        real(position.x),
        real(position.y),
        real(position.z),
        integer(static_cast<int32_t>(packedPath)),
        integer(associatedFeat ? static_cast<int32_t>(*associatedFeat) : kNoAssociatedFeat),
        integer(selection.casterLevel),
    };
}

SavedCastAction SavedCastAction::fromGff(const resource::Gff &gff, const SerializedIdentityContext &identityContext) {
    SavedCastAction result;
    gff.readInt(result.phase, "Phase");
    gff.readFloat(result.elapsed, "Elapsed");
    gff.readFloat(result.conjureTime, "ConjureTime");
    gff.readFloat(result.castTime, "CastTime");
    gff.readFloat(result.catchTime, "CatchTime");
    gff.readFloat(result.projectileTime, "FlightTime");
    gff.readDword64(result.presentationId, "Presentation");
    gff.readInt(result.casterLevel, "CasterLevel");
    gff.readInt(result.metaMagic, "MetaMagic");
    gff.readInt(result.castingClass, "CastingClass");
    gff.readInt(result.forceCost, "ForceCost");
    gff.readBool(result.hasSelection, "HasSelection");
    gff.readBool(result.cheat, "Cheat");
    gff.readBool(result.admitted, "Admitted");
    gff.readBool(result.approaching, "Approaching");
    gff.readBool(result.started, "Started");
    gff.readBool(result.commitAttempted, "CommitAttempted");
    gff.readBool(result.committed, "Committed");
    gff.readBool(result.released, "Released");
    gff.readBool(result.movementOwned, "MovementOwned");
    gff.readBool(result.itemConsumed, "ItemConsumed");
    uint32_t item = 0;
    if (gff.readDword(item, "Item")) {
        auto &use = result.itemUse.emplace();
        use.item = SavedObjectReference::fromSerializedId(item, identityContext);
        int32_t value = 0;
        if (gff.readInt(value, "Property")) {
            use.property = value;
        }
        if (gff.readInt(value, "ItemLevel")) {
            use.casterLevel = value;
        }
        if (gff.readInt(value, "ItemType")) {
            use.baseItemType = value;
        }
    }
    return result;
}

std::shared_ptr<resource::Gff> SavedCastAction::toGff() const {
    using G = resource::Gff;
    auto result = G::Builder().type(0).field(G::Field::newInt("Phase", phase)).field(G::Field::newFloat("Elapsed", elapsed)).field(G::Field::newFloat("ConjureTime", conjureTime)).field(G::Field::newFloat("CastTime", castTime)).field(G::Field::newFloat("CatchTime", catchTime)).field(G::Field::newFloat("FlightTime", projectileTime)).field(G::Field::newDword64("Presentation", presentationId)).field(G::Field::newInt("CasterLevel", casterLevel)).field(G::Field::newInt("MetaMagic", metaMagic)).field(G::Field::newInt("CastingClass", castingClass)).field(G::Field::newInt("ForceCost", forceCost)).field(G::Field::newByte("HasSelection", hasSelection)).field(G::Field::newByte("Cheat", cheat)).field(G::Field::newByte("Admitted", admitted)).field(G::Field::newByte("Approaching", approaching)).field(G::Field::newByte("Started", started)).field(G::Field::newByte("CommitAttempted", commitAttempted)).field(G::Field::newByte("Committed", committed)).field(G::Field::newByte("Released", released)).field(G::Field::newByte("MovementOwned", movementOwned)).field(G::Field::newByte("ItemConsumed", itemConsumed)).build();
    if (itemUse) {
        result->fields().push_back(G::Field::newDword("Item", itemUse->item.id));
        if (itemUse->property) {
            result->fields().push_back(G::Field::newInt("Property", *itemUse->property));
        }
        if (itemUse->casterLevel) {
            result->fields().push_back(G::Field::newInt("ItemLevel", *itemUse->casterLevel));
        }
        if (itemUse->baseItemType) {
            result->fields().push_back(G::Field::newInt("ItemType", *itemUse->baseItemType));
        }
    }
    return result;
}

SavedRoundClock SavedRoundClock::fromGff(const resource::Gff &g, const SerializedIdentityContext &ids) {
    requireFields(g, {"Id", "Slot", "State", "Elapsed", "Duration", "PauseRemaining", "PauseOwner", "Master",
                      "Engaged", "Joined", "Duel"});
    SavedRoundClock s;
    s.id = g.getUint64("Id");
    s.slot = g.getInt("Slot");
    s.state = g.getInt("State");
    s.elapsed = g.getFloat("Elapsed");
    s.duration = g.getFloat("Duration");
    s.pauseRemaining = g.getFloat("PauseRemaining");
    s.pauseOwner = SavedObjectReference::fromSerializedId(g.getUint("PauseOwner"), ids);
    s.master = SavedObjectReference::fromSerializedId(g.getUint("Master"), ids);
    s.engaged = SavedObjectReference::fromSerializedId(g.getUint("Engaged"), ids);
    s.joined = g.getBool("Joined");
    s.duel = g.getBool("Duel");
    return s;
}
std::shared_ptr<resource::Gff> SavedRoundClock::toGff() const {
    using G = resource::Gff;
    return G::Builder().type(0).field(G::Field::newDword64("Id", id)).field(G::Field::newInt("Slot", slot)).field(G::Field::newInt("State", state)).field(G::Field::newFloat("Elapsed", elapsed)).field(G::Field::newFloat("Duration", duration)).field(G::Field::newFloat("PauseRemaining", pauseRemaining)).field(G::Field::newDword("PauseOwner", pauseOwner.id)).field(G::Field::newDword("Master", master.id)).field(G::Field::newDword("Engaged", engaged.id)).field(G::Field::newByte("Joined", joined ? 1 : 0)).field(G::Field::newByte("Duel", duel ? 1 : 0)).build();
}

SavedProjectile SavedProjectile::fromGff(const resource::Gff &g, const SerializedIdentityContext &ids) {
    requireFields(g, {"Id", "Kind", "Caster", "Weapon", "Spell", "Path", "Model", "Leg", "Released", "Initialized",
                      "Clockwise", "Elapsed", "Position", "Velocity", "Orientation", "Acceleration", "ParryBlocked",
                      "ActivationDelay", "TravelRate", "SourceHook", "OrientMode", "TargetHook", "CombatResult",
                      "SoundVariant", "Legs"});
    SavedProjectile p;
    auto ref = [&](const resource::Gff &r, const char *key) {
        return SavedObjectReference::fromSerializedId(r.getUint(key), ids);
    };
    p.id = g.getUint64("Id");
    p.kind = static_cast<ProjectileKind>(g.getInt("Kind"));
    p.caster = ref(g, "Caster");
    p.weapon = ref(g, "Weapon");
    p.spellId = g.getInt("Spell");
    p.path = g.getInt("Path");
    p.model = g.getString("Model");
    p.leg = g.getInt("Leg");
    p.released = g.getBool("Released");
    p.initialized = g.getBool("Initialized");
    p.clockwise = g.getBool("Clockwise");
    p.elapsed = g.getFloat("Elapsed");
    p.position = g.getVector("Position");
    p.velocity = g.getVector("Velocity");
    p.orientation = g.getOrientation("Orientation");
    p.acceleration = g.getVector("Acceleration");
    p.parryBlocked = g.getBool("ParryBlocked");
    p.activationDelay = g.getFloat("ActivationDelay");
    p.travelRate = g.getFloat("TravelRate");
    p.sourceHook = g.getString("SourceHook");
    p.orientationMode = g.getInt("OrientMode");
    p.targetHook = g.getString("TargetHook");
    p.combatResult = g.getInt("CombatResult");
    p.soundVariant = g.getInt("SoundVariant");
    for (const auto &v : g.getList("Legs")) {
        requireFields(*v, {"Source", "Target", "Origin", "Destination", "Duration", "Reacted", "Motion", "TargetOffset",
                           "TargetHook", "OwnTargetHook", "StopRadius"});
        p.legs.push_back({ref(*v, "Source"), ref(*v, "Target"),
                          v->getVector("Origin"), v->getVector("Destination"), v->getFloat("Duration"), v->getBool("Reacted"),
                          static_cast<ProjectileMotion>(v->getInt("Motion")), v->getVector("TargetOffset"), v->getString("TargetHook"),
                          v->getBool("OwnTargetHook"), v->getFloat("StopRadius")});
    }
    return p;
}
std::shared_ptr<resource::Gff> SavedProjectile::toGff() const {
    using G = resource::Gff;
    std::vector<std::shared_ptr<G>> entries;
    for (const auto &l : legs) {
        entries.push_back(G::Builder().type(0).field(G::Field::newDword("Source", l.source.id)).field(G::Field::newDword("Target", l.target.id)).field(G::Field::newVector("Origin", l.origin)).field(G::Field::newVector("Destination", l.destination)).field(G::Field::newFloat("Duration", l.duration)).field(G::Field::newByte("Reacted", l.reacted)).field(G::Field::newInt("Motion", static_cast<int>(l.motion))).field(G::Field::newVector("TargetOffset", l.targetOffset)).field(G::Field::newCExoString("TargetHook", l.targetHook)).field(G::Field::newByte("OwnTargetHook", l.ownsTargetHook)).field(G::Field::newFloat("StopRadius", l.stopRadius)).build());
    }
    return G::Builder().type(0).field(G::Field::newDword64("Id", id)).field(G::Field::newInt("Kind", static_cast<int>(kind))).field(G::Field::newDword("Caster", caster.id)).field(G::Field::newDword("Weapon", weapon.id)).field(G::Field::newInt("Spell", spellId)).field(G::Field::newInt("Path", path)).field(G::Field::newResRef("Model", model)).field(G::Field::newInt("Leg", leg)).field(G::Field::newByte("Released", released)).field(G::Field::newByte("Initialized", initialized)).field(G::Field::newByte("Clockwise", clockwise)).field(G::Field::newFloat("Elapsed", elapsed)).field(G::Field::newVector("Position", position)).field(G::Field::newVector("Velocity", velocity)).field(G::Field::newVector("Acceleration", acceleration)).field(G::Field::newByte("ParryBlocked", parryBlocked)).field(G::Field::newFloat("ActivationDelay", activationDelay)).field(G::Field::newFloat("TravelRate", travelRate)).field(G::Field::newCExoString("SourceHook", sourceHook)).field(G::Field::newCExoString("TargetHook", targetHook)).field(G::Field::newInt("CombatResult", combatResult)).field(G::Field::newInt("SoundVariant", soundVariant)).field(G::Field::newInt("OrientMode", orientationMode)).field(G::Field::newOrientation("Orientation", orientation)).field(G::Field::newList("Legs", std::move(entries))).build();
}
bool SavedProjectile::bindObjectReferences(const Game &game) {
    const bool live = game.bindSavedObjectReference(caster);
    game.bindSavedObjectReference(weapon);
    for (auto &l : legs) {
        game.bindSavedObjectReference(l.source);
        game.bindSavedObjectReference(l.target);
    }
    return live;
}

SavedScheduledAction SavedScheduledAction::fromGff(
    const resource::Gff &gff, const SerializedIdentityContext &ids) {
    using Type = SavedActionParameterType;
    SavedScheduledAction record;
    record.timer = gff.getInt("ActionTimer");
    record.animation = static_cast<int>(gff.getUint("Animation"));
    record.animationTime = gff.getInt("AnimationTime");
    record.numAttacks = gff.getInt("NumAttacks");
    record.type = static_cast<int>(gff.getUint("ActionType"));
    record.target = SavedObjectReference::fromSerializedId(gff.getUint("Target", kSavedRuntimeInvalidObjectId), ids);
    record.retargettable = gff.getUint("Retargettable") != 0;
    record.inventorySlot = gff.getUint("InventorySlot");
    record.repository = SavedObjectReference::fromSerializedId(gff.getUint("TargetRepository", kSavedRuntimeInvalidObjectId), ids);
    record.applied = gff.getInt("Applied") != 0;
    record.remainingPause = gff.getFloat("PauseRemaining");
    const auto command = [](SavedActionType type, std::vector<SavedActionParameter> parameters) {
        SavedActionRecord result;
        result.actionId = static_cast<uint32_t>(type);
        result.declaredParameterCount = static_cast<uint16_t>(parameters.size());
        result.parameters = std::move(parameters);
        return result;
    };
    // An entry of the game's own carries no command; its type and fields give it.
    // A combat stance entry's attack count names the stance.
    if (auto saved = gff.findStruct("Command")) {
        record.command = SavedActionRecord::fromGff(*saved, ids);
    } else if (record.type == kCombatStanceType && record.numAttacks == static_cast<int32_t>(CombatStance::TotalDefense)) {
        record.command = command(SavedActionType::TotalDefense, {{static_cast<uint32_t>(Type::Object), record.target}});
    } else if (record.type == kCombatStanceType && record.numAttacks == static_cast<int32_t>(CombatStance::Meditative)) {
        record.command = command(SavedActionType::Meditate, {});
    } else if (record.type == kSwitchWeaponsType) {
        record.command = command(SavedActionType::SwitchWeapons, {});
    } else if (record.type == kEquipType) {
        record.command = command(SavedActionType::EquipItem,
                                 {{static_cast<uint32_t>(Type::Object), record.target},
                                  {static_cast<uint32_t>(Type::Integer), static_cast<int32_t>(record.inventorySlot)},
                                  {static_cast<uint32_t>(Type::Integer), int32_t {0}}});
    } else if (record.type == kUnequipType) {
        record.command = command(SavedActionType::UnequipItem,
                                 {{static_cast<uint32_t>(Type::Object), record.target},
                                  {static_cast<uint32_t>(Type::Object), record.repository},
                                  {static_cast<uint32_t>(Type::Integer), int32_t {0}}});
    }
    return record;
}

bool SavedScheduledAction::bindObjectReferences(const Game &game) {
    // An absent target or repository has nothing to bind.
    const bool targetBound = target.isInvalid() || game.bindSavedObjectReference(target);
    const bool repositoryBound = repository.isInvalid() || game.bindSavedObjectReference(repository);
    const bool commandBound = !command || command->bindObjectReferences(game);
    return targetBound && repositoryBound && commandBound;
}

SavedActionRecord SavedActionRecord::fromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    SavedActionRecord result;
    result.actionId = gff.getUint("ActionId");
    result.scheduled = gff.getBool("Scheduled");
    result.groupActionId = static_cast<int>(gff.getUint("GroupActionId"));
    result.declaredParameterCount = static_cast<uint16_t>(gff.getUint("NumParams"));
    for (const auto &parameter : gff.getList("Paramaters")) {
        result.parameters.push_back(SavedActionParameter::fromGff(
            *parameter, identityContext));
    }
    if (auto value = gff.findStruct("CastState")) {
        result.cast = SavedCastAction::fromGff(*value, identityContext);
    }
    if (auto value = gff.findStruct("RoundState")) {
        result.round = SavedRoundClock::fromGff(*value, identityContext);
    }
    if (auto value = gff.findStruct("PhysicalState")) {
        result.physical = SavedPhysicalAction::fromGff(*value, identityContext);
    }
    result.unknownFields = unknownFieldsOf(
        gff, {"ActionId", "Scheduled", "GroupActionId", "NumParams", "Paramaters", "CastState", "RoundState", "PhysicalState"});
    return result;
}

namespace {

// The second parameter of an equip names its slots; an unequip's names the
// container the item goes to. TSL writes an unequip with two trailing
// integers, always zero.
bool isEquipmentCommand(const SavedActionRecord &record) {
    using Type = SavedActionParameterType;
    if (record.type() == SavedActionType::EquipItem) {
        return hasParameterTypes(record, {Type::Object, Type::Integer, Type::Integer}) &&
               equipmentSlotFromMask(static_cast<uint32_t>(integerParameter(record, 1))).has_value();
    }
    return record.type() == SavedActionType::UnequipItem &&
           (hasParameterTypes(record, {Type::Object, Type::Object, Type::Integer}) ||
            hasParameterTypes(record, {Type::Object, Type::Object, Type::Integer, Type::Integer, Type::Integer}));
}

std::shared_ptr<Action> restoreCastSpell(const SavedActionRecord &record, Game &game) {
    const auto command = SavedSpellCommand::fromAction(record);
    auto spell = game.getSpell(static_cast<SpellType>(command->spellId));
    if (!spell) {
        return nullptr;
    }
    const auto &cast = record.cast;
    std::optional<std::shared_ptr<Item>> item;
    std::optional<size_t> itemProperty;
    std::optional<int> itemCasterLevel;
    if (cast && cast->itemUse) {
        item = std::dynamic_pointer_cast<Item>(cast->itemUse->item.boundObject());
        // A use whose item is gone continues only once released.
        if (!*item && !cast->released) {
            return nullptr;
        }
        if (cast->itemUse->property) {
            itemProperty = static_cast<size_t>(*cast->itemUse->property);
        }
        itemCasterLevel = cast->itemUse->casterLevel;
    }
    const bool cheat = cast && cast->cheat;
    if (command->target.isInvalid()) {
        auto location = std::make_shared<Location>(command->position, 0.0f);
        auto action = game.newAction<CastSpellAtLocationAction>(spell, location, cheat, command->path,
                                                                command->instant, item, itemProperty, itemCasterLevel, command->selection, command->associatedFeat,
                                                                command->fake);
        action->setCommandParameters(command->domainLevel, command->metaType, command->spontaneousCast);
        if (cast) {
            action->restoreCastState(*cast);
        }
        return action;
    }
    auto target = command->target.boundObject();
    if (!target) {
        return nullptr;
    }
    auto action = game.newAction<CastSpellAtObjectAction>(spell, std::move(target), item, cheat, command->path,
                                                          command->instant, itemProperty, itemCasterLevel, command->selection, command->associatedFeat, command->fake);
    action->setCommandLocation(command->position);
    action->setCommandParameters(command->domainLevel, command->metaType, command->spontaneousCast);
    if (cast) {
        action->restoreCastState(*cast);
    }
    return action;
}

std::shared_ptr<Action> restoreEquipment(const SavedActionRecord &record, Game &game) {
    auto item = std::dynamic_pointer_cast<Item>(objectParameter(record, 0).boundObject());
    if (!item) {
        return nullptr;
    }
    const int32_t flags = integerParameter(record, 2);
    if (record.type() == SavedActionType::EquipItem) {
        const auto slot = *equipmentSlotFromMask(static_cast<uint32_t>(integerParameter(record, 1)));
        return game.newAction<EquipItemAction>(std::move(item), slot, flags);
    }
    const auto &reference = objectParameter(record, 1);
    auto container = std::dynamic_pointer_cast<Item>(reference.boundObject());
    auto unequip = game.newAction<UnequipItemAction>(std::move(item), flags, container);
    // A container value that names no item is kept as it is.
    if (!reference.isInvalid() && !container) {
        unequip->overwriteContainer(static_cast<int32_t>(reference.id));
    }
    return unequip;
}

std::shared_ptr<Action> restoreCombatStance(const SavedActionRecord &record, Game &game) {
    std::shared_ptr<Object> target;
    if (record.type() == SavedActionType::TotalDefense) {
        const auto &reference = objectParameter(record, 0);
        target = reference.boundObject();
        if (!reference.isInvalid() && !target) {
            return nullptr;
        }
        return game.newAction<CombatStanceAction>(CombatStance::TotalDefense, std::move(target));
    }
    return game.newAction<CombatStanceAction>(CombatStance::Meditative, std::move(target));
}

std::shared_ptr<Action> restorePhysicalAttack(const SavedActionRecord &record, Game &game) {
    const auto decoded = SavedAttackCommand::fromAction(record);
    auto target = decoded->target.boundObject();
    if (!target) {
        return nullptr;
    }
    if (decoded->feat != FeatType::Invalid) {
        auto action = game.newAction<UseFeatAction>(decoded->feat, std::move(target));
        if (record.physical) {
            action->restorePhysicalState(*record.physical);
        }
        return action;
    }
    auto action = game.newAction<AttackObjectAction>(std::move(target));
    if (decoded->cutscene) {
        action->forceCutsceneAttack(*decoded->cutscene);
    }
    if (record.physical) {
        action->restorePhysicalState(*record.physical);
    }
    return action;
}

// A conversation names its dialog by a resource name of at most sixteen
// characters, and is private or not.
constexpr size_t kMaxDialogNameLength = 16;

std::shared_ptr<Action> restoreConversation(const SavedActionRecord &record, Game &game) {
    auto target = objectParameter(record, 0).boundObject();
    const auto &dialog = std::get<std::string>(record.parameters[1].payload);
    const int32_t privateConversation = integerParameter(record, 2);
    if (!target || dialog.size() > kMaxDialogNameLength || (privateConversation != 0 && privateConversation != 1)) {
        return nullptr;
    }
    return game.newAction<StartConversationAction>(std::move(target), dialog, privateConversation != 0);
}

std::shared_ptr<Action> restoreMoveToPoint(const SavedActionRecord &record, Game &game) {
    const auto decoded = decodeMoveToPoint(record);
    if (!std::dynamic_pointer_cast<Area>(decoded->area.boundObject())) {
        return nullptr;
    }
    const uint64_t expiryMilliseconds =
        static_cast<uint64_t>(decoded->expiryDay) * game.millisecondsPerWorldDay() + decoded->expiryTime;
    const bool forced = decoded->forcedPending || decoded->forcedActive;
    const float timeout = decoded->forcedPending ? decoded->timeout : 0.0f;
    if (decoded->target.isInvalid()) {
        MoveToLocationAction::ForcedState state;
        state.areaId = decoded->area.id;
        state.active = decoded->forcedActive;
        state.expiryMilliseconds = expiryMilliseconds;
        auto location = std::make_shared<Location>(decoded->destination, 0.0f);
        return game.newAction<MoveToLocationAction>(std::move(location), decoded->run, forced, timeout, state);
    }
    auto target = decoded->target.boundObject();
    if (!target) {
        return nullptr;
    }
    MoveToObjectAction::ForcedState state;
    state.destination = decoded->destination;
    state.areaId = decoded->area.id;
    state.active = decoded->forcedActive;
    state.expiryMilliseconds = expiryMilliseconds;
    return game.newAction<MoveToObjectAction>(std::move(target), decoded->run, decoded->range, timeout, state, forced);
}

// A move to an object in range always carries a move flag of 1.
constexpr int32_t kMoveToObjectFlags = 1;

std::shared_ptr<Action> restoreMoveToObject(const SavedActionRecord &record, Game &game) {
    auto target = objectParameter(record, 0).boundObject();
    const int32_t run = integerParameter(record, 1);
    const float moveRange = floatParameter(record, 2);
    const float checkRange = floatParameter(record, 3);
    const int32_t moveFlags = integerParameter(record, 4);
    if (!target || (run != 0 && run != 1) ||
        !std::isfinite(moveRange) || moveRange < 0.0f ||
        !std::isfinite(checkRange) || checkRange < 0.0f ||
        moveFlags != kMoveToObjectFlags) {
        return nullptr;
    }
    return game.newAction<MoveToObjectAction>(std::move(target), run != 0, moveRange, false, -1.0f, false, checkRange, true);
}

std::shared_ptr<Action> restorePlayAnimation(const SavedActionRecord &record, Game &game) {
    // The animation is kept in the command's object slot. Any animation
    // constant the command queues comes back; it is resolved as the action runs.
    const auto animation = objectParameter(record, 0).id;
    const float speed = floatParameter(record, 1);
    const float duration = floatParameter(record, 2);
    const int32_t start = integerParameter(record, 3);
    const int32_t looping = integerParameter(record, 4);
    if (!std::isfinite(speed) || !std::isfinite(duration) ||
        (start != 0 && start != 1) || (looping != 0 && looping != 1)) {
        return nullptr;
    }
    return game.newAction<PlayAnimationAction>(static_cast<AnimationType>(animation), speed, duration, start == 0, looping != 0);
}

std::shared_ptr<Action> restoreDoCommand(const SavedActionRecord &record, Game &game,
                                         const SavedScriptSituationImporter *importer) {
    if (!importer) {
        return nullptr;
    }
    auto result = importer->import(std::get<SerializedScriptSituation>(record.parameters.front().payload));
    if (!result) {
        return nullptr;
    }
    return game.newAction<SavedDoCommandAction>(std::move(result.continuation));
}

} // namespace

SavedExecutionSupport SavedActionRecord::executionSupport() const {
    using Type = SavedActionParameterType;
    bool executable = false;
    switch (type()) {
    case SavedActionType::CastSpell:
        executable = SavedSpellCommand::fromAction(*this).has_value();
        break;
    case SavedActionType::EquipItem:
    case SavedActionType::UnequipItem:
        executable = isEquipmentCommand(*this);
        break;
    case SavedActionType::CombatDispatch:
        executable = hasParameterTypes(*this, {Type::Integer});
        break;
    case SavedActionType::FollowLeader:
    case SavedActionType::Meditate:
    case SavedActionType::SwitchWeapons:
        executable = hasParameterTypes(*this, {});
        break;
    case SavedActionType::TotalDefense:
        executable = hasParameterTypes(*this, {Type::Object});
        break;
    case SavedActionType::AttackObject:
        executable = SavedAttackCommand::fromAction(*this).has_value();
        break;
    case SavedActionType::StartConversation:
        executable = hasParameterTypes(*this, {Type::Object, Type::String, Type::Integer});
        break;
    case SavedActionType::MoveToPoint:
        executable = decodeMoveToPoint(*this).has_value();
        break;
    case SavedActionType::MoveToObject:
        executable = hasParameterTypes(*this, {Type::Object, Type::Integer, Type::Float, Type::Float, Type::Integer});
        break;
    case SavedActionType::PlayAnimation:
        executable = hasParameterTypes(*this, {Type::Object, Type::Float, Type::Float, Type::Integer, Type::Integer});
        break;
    case SavedActionType::Wait:
        executable = !parameters.empty() && std::holds_alternative<float>(parameters.front().payload);
        break;
    case SavedActionType::DoCommand:
        executable = hasParameterTypes(*this, {Type::ScriptSituation});
        break;
    default:
        break;
    }
    return executable ? SavedExecutionSupport::Executable : SavedExecutionSupport::RepresentableButUnsupported;
}

std::shared_ptr<Action> SavedActionRecord::toRuntimeAction(
    Game &game, const SavedScriptSituationImporter *importer) const {
    if (executionSupport() != SavedExecutionSupport::Executable) {
        return nullptr;
    }
    std::shared_ptr<Action> action;
    switch (type()) {
    case SavedActionType::CastSpell:
        action = restoreCastSpell(*this, game);
        break;
    case SavedActionType::CombatDispatch:
        action = game.newAction<CombatDispatchAction>(integerParameter(*this, 0));
        break;
    case SavedActionType::EquipItem:
    case SavedActionType::UnequipItem:
        action = restoreEquipment(*this, game);
        break;
    case SavedActionType::FollowLeader:
        action = game.newAction<FollowLeaderAction>();
        break;
    case SavedActionType::TotalDefense:
    case SavedActionType::Meditate:
        action = restoreCombatStance(*this, game);
        break;
    case SavedActionType::SwitchWeapons:
        action = game.newAction<SwitchWeaponsAction>();
        break;
    case SavedActionType::Wait:
        action = game.newAction<WaitAction>(floatParameter(*this, 0));
        break;
    case SavedActionType::AttackObject:
        action = restorePhysicalAttack(*this, game);
        break;
    case SavedActionType::StartConversation:
        action = restoreConversation(*this, game);
        break;
    case SavedActionType::MoveToPoint:
        action = restoreMoveToPoint(*this, game);
        break;
    case SavedActionType::MoveToObject:
        action = restoreMoveToObject(*this, game);
        break;
    case SavedActionType::PlayAnimation:
        action = restorePlayAnimation(*this, game);
        break;
    case SavedActionType::DoCommand:
        action = restoreDoCommand(*this, game, importer);
        break;
    default:
        break;
    }
    if (action) {
        action->attachSavedAction(*this);
    }
    return action;
}

bool SavedActionRecord::bindObjectReferences(const Game &game) {
    bool allBound = true;
    if (cast && cast->itemUse) {
        // A use whose item is gone continues only once released.
        const bool itemBound = game.bindSavedObjectReference(cast->itemUse->item);
        if (!cast->released) {
            allBound = itemBound && allBound;
        }
    }
    if (round) {
        game.bindSavedObjectReference(round->pauseOwner);
        game.bindSavedObjectReference(round->master);
        game.bindSavedObjectReference(round->engaged);
    }
    if (physical) {
        for (auto &source : physical->sources) {
            if (!source.isInvalid()) {
                game.bindSavedObjectReference(source);
            }
        }
        for (auto &list : physical->targetEffects) {
            for (auto &effect : list) {
                game.bindEffectCreator(effect);
            }
        }
        for (auto &history : physical->histories) {
            game.bindSavedObjectReference(history.reactionObject);
            game.bindSavedObjectReference(history.ammoItem);
        }
        if (physical->roundRecord) {
            game.bindSavedObjectReference(physical->roundRecord->reactionObject);
            game.bindSavedObjectReference(physical->roundRecord->ammoItem);
        }
    }
    for (size_t index = 0; index < parameters.size(); ++index) {
        auto &parameter = parameters[index];
        // A PlayAnimation keeps its animation, not an object, in its object slot.
        if (type() == SavedActionType::PlayAnimation && index == 0) {
            continue;
        }
        // An unequip whose container names no item still runs; it takes
        // nothing off.
        if (type() == SavedActionType::UnequipItem && index == 1) {
            parameter.bindObjectReferences(game);
            continue;
        }
        allBound = parameter.bindObjectReferences(game) && allBound;
    }
    return allBound;
}

SavedActionQueue SavedActionQueue::fromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    const std::string &label) {
    SavedActionQueue result;
    for (const auto &action : gff.getList(label)) {
        result.actions.push_back(SavedActionRecord::fromGff(
            *action, identityContext));
    }
    return result;
}

SavedEventRecord SavedEventRecord::fromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    SavedEventRecord result;
    result.day = gff.getUint("Day");
    result.time = gff.getUint("Time");
    result.object = SavedObjectReference::fromSerializedId(
        gff.getUint("ObjectId"), identityContext);
    result.caller = SavedObjectReference::fromSerializedId(
        gff.getUint("CallerId"), identityContext);
    result.eventId = gff.getUint("EventId");
    result.unknownFields = unknownFieldsOf(gff, {"Day", "Time", "ObjectId", "CallerId", "EventId", "EventData"});

    auto data = gff.findStruct("EventData");
    if (!data) {
        return result;
    }
    switch (static_cast<SavedEventType>(result.eventId)) {
    case SavedEventType::Timed:
        result.payload = SerializedScriptSituation::fromGff(
            *data, identityContext);
        break;
    case SavedEventType::RemoveFromArea:
        result.payload = SavedBytePayload {static_cast<uint8_t>(data->getUint("Value"))};
        break;
    case SavedEventType::ApplyEffect:
    case SavedEventType::RemoveEffect:
        result.payload = EffectInstance::fromGff(*data, identityContext);
        break;
    case SavedEventType::SpellImpact:
    case SavedEventType::ItemOnHitSpellImpact:
        if (result.eventId == static_cast<uint32_t>(SavedEventType::ItemOnHitSpellImpact) &&
            data->getUint("WeaponImpact") == 1) {
            result.payload = SavedWeaponImpact::fromGff(*data, identityContext);
        } else {
            result.payload = savedSpellImpactFromGff(*data, identityContext);
        }
        break;
    case SavedEventType::PlayAnimation:
    case SavedEventType::ControllerRumble:
        result.payload = SavedIntPayload {data->getInt("Value")};
        break;
    case SavedEventType::SignalEvent:
    case SavedEventType::SummonCreature:
    case SavedEventType::AreaTransition:
        result.payload = savedScriptEventFromGff(*data, identityContext);
        break;
    case SavedEventType::SpawnBodyBag:
        result.payload = savedBodyBagFromGff(*data, identityContext);
        break;
    case SavedEventType::OnMeleeAttacked:
    case SavedEventType::BroadcastSafeProjectile:
        result.payload = SavedCombatAttack::fromGff(*data, identityContext);
        break;
    case SavedEventType::BroadcastAoo:
        result.payload = SavedBroadcastAoo {
            SavedObjectReference::fromSerializedId(
                data->getUint("Value"), identityContext)};
        break;
    case SavedEventType::FeedbackMessage:
        if (auto message = CombatFeedbackMessage::fromGff(*data, gff.getUint("CallerId"))) {
            result.payload = std::move(*message);
        }
        break;
    default:
        break;
    }
    // Data that is not read, such as feedback that shows no line here, is
    // kept as it was saved.
    if (std::holds_alternative<std::monostate>(result.payload)) {
        result.unreadData = data->deepCopy();
    }
    return result;
}

SavedExecutionSupport SavedEventRecord::executionSupport() const {
    if (eventId == static_cast<uint32_t>(SavedEventType::ItemOnHitSpellImpact) &&
        std::holds_alternative<SavedWeaponImpact>(payload))
        return SavedExecutionSupport::Executable;
    // A loaded item on-hit spell impact carries no payload and does nothing.
    // Only the weapon-impact form runs.
    if (eventId == static_cast<uint32_t>(SavedEventType::SpellImpact) &&
        std::holds_alternative<SavedSpellImpact>(payload))
        return SavedExecutionSupport::Executable;
    if (eventId == static_cast<uint32_t>(SavedEventType::BroadcastSafeProjectile)) {
        // Only a bolt with an attack result is shown.
        const auto *attack = std::get_if<SavedCombatAttack>(&payload);
        if (attack && attack->fields->result >= AttackResultType::HitSuccessful &&
            attack->fields->result <= AttackResultType::ShieldHit)
            return SavedExecutionSupport::Executable;
        return SavedExecutionSupport::RepresentableButUnsupported;
    }
    if (eventId == static_cast<uint32_t>(SavedEventType::OnMeleeAttacked) &&
        (std::holds_alternative<SavedCombatAttack>(payload) ||
         std::holds_alternative<std::monostate>(payload)))
        return SavedExecutionSupport::Executable;
    if (eventId == static_cast<uint32_t>(SavedEventType::SignalEvent)) {
        using Signal = SavedSignalEventType;
        static const std::set<Signal> delivered {
            Signal::Heartbeat, Signal::SpellCastAt, Signal::Damaged, Signal::Death, Signal::UserDefined,
            Signal::Entered, Signal::Exited, Signal::ModuleLoaded, Signal::ItemActivated, Signal::ItemAcquired,
            Signal::ItemLost, Signal::EncounterExhausted, Signal::InventoryOpened, Signal::InventoryClosed,
            Signal::Used, Signal::Locked, Signal::FailToOpen};
        const auto *event = std::get_if<SavedScriptEvent>(&payload);
        if (event && delivered.count(static_cast<Signal>(event->type)) != 0) {
            return SavedExecutionSupport::Executable;
        }
    }
    if (eventId == static_cast<uint32_t>(SavedEventType::DestroyObject) &&
        std::holds_alternative<std::monostate>(payload))
        return SavedExecutionSupport::Executable;
    if (eventId == static_cast<uint32_t>(SavedEventType::SpawnBodyBag) &&
        std::holds_alternative<SavedBodyBag>(payload))
        return SavedExecutionSupport::Executable;
    if (eventId == static_cast<uint32_t>(SavedEventType::OpenObject) &&
        std::holds_alternative<SavedOpenObject>(payload))
        return SavedExecutionSupport::Executable;
    if (eventId == static_cast<uint32_t>(SavedEventType::FeedbackMessage) &&
        std::holds_alternative<CombatFeedbackMessage>(payload))
        return SavedExecutionSupport::Executable;
    if (eventId == static_cast<uint32_t>(SavedEventType::ForcedAction)) {
        return SavedExecutionSupport::Discarded;
    }
    if (eventId == static_cast<uint32_t>(SavedEventType::Timed) &&
        std::holds_alternative<SerializedScriptSituation>(payload)) {
        return SavedExecutionSupport::Executable;
    }
    if ((eventId == static_cast<uint32_t>(SavedEventType::ApplyEffect) ||
         eventId == static_cast<uint32_t>(SavedEventType::RemoveEffect)) &&
        std::holds_alternative<EffectInstance>(payload)) {
        return SavedExecutionSupport::Executable;
    }
    return SavedExecutionSupport::RepresentableButUnsupported;
}

bool SavedEventRecord::shouldRestore() const {
    return eventId > 0 &&
           eventId <= static_cast<uint32_t>(SavedEventType::ControllerRumble) &&
           eventId != static_cast<uint32_t>(SavedEventType::ForcedAction);
}

bool SavedEventRecord::bindObjectReferences(const Game &game) {
    bool allBound = true;
    bindReference(game, object, allBound);
    bindReference(game, caller, allBound);
    if (auto situation = std::get_if<SerializedScriptSituation>(&payload)) {
        allBound = situation->bindObjectReferences(game) && allBound;
    } else if (auto effect = std::get_if<EffectInstance>(&payload)) {
        allBound = game.bindEffectCreator(*effect) && allBound;
    } else if (auto hit = std::get_if<SavedWeaponImpact>(&payload)) {
        bindReference(game, hit->source, allBound);
    } else if (auto spell = std::get_if<SavedSpellImpact>(&payload)) {
        bindReference(game, spell->caster, allBound);
        bindReference(game, spell->target, allBound);
        bindReference(game, spell->area, allBound);
        bindReference(game, spell->item, allBound);
    } else if (auto event = std::get_if<SavedScriptEvent>(&payload)) {
        for (auto &reference : event->objects) {
            bindReference(game, reference, allBound);
        }
    } else if (auto bodyBag = std::get_if<SavedBodyBag>(&payload)) {
        bindReference(game, bodyBag->object, allBound);
    } else if (auto broadcast = std::get_if<SavedBroadcastAoo>(&payload)) {
        bindReference(game, broadcast->target, allBound);
    } else if (auto combat = std::get_if<SavedCombatAttack>(&payload)) {
        bindReference(game, combat->reactionObject, allBound);
        bindReference(game, combat->ammoItem, allBound);
    }
    return allBound;
}

SavedEventQueue SavedEventQueue::fromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    const std::string &label) {
    SavedEventQueue result;
    for (const auto &event : gff.getList(label)) {
        result.events.push_back(SavedEventRecord::fromGff(
            *event, identityContext));
    }
    return result;
}

} // namespace game

} // namespace reone
