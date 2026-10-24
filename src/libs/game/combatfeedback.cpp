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

#include "reone/game/combatfeedback.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "reone/game/d20/spells.h"
#include "reone/game/di/services.h"
#include "reone/game/effect/creaturestate.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/resource/gff.h"
#include "reone/system/exception/validation.h"

using namespace reone::resource;

namespace reone {

namespace game {

static constexpr int kStrRefSavingThrow = 1406;
static constexpr int kStrRefSavingThrowFortitude = 1375;
static constexpr int kStrRefSavingThrowReflex = 1374;
static constexpr int kStrRefSavingThrowWill = 1376;
static constexpr int kStrRefSavingThrowSuccess = 1392;
static constexpr int kStrRefSavingThrowFailure = 1393;
static constexpr int kStrRefSavingThrowBaseComponent = 42387;
static constexpr int kStrRefSavingThrowEffectComponent = 42388;
static constexpr int kStrRefDamageImmunity = 1458;
static constexpr int kStrRefPoisonDamage = 41902;
static constexpr int kStrRefPoisoned = 47873;
static constexpr int kStrRefDeathExperience = 1407;
static constexpr int kStrRefDamageTaken = 1402;
static constexpr int kStrRefDamageDealt = 1403;
static constexpr int kStrRefHitPointHealing = 1495;
static constexpr int kStrRefForceHealing = 1031;
static constexpr int kStrRefBlindnessImmunity = 0;
static constexpr int kStrRefEntangleImmunity = 1491;
static constexpr int kStrRefSlowImmunity = 1492;
static constexpr int kStrRefDispel = 0;

static constexpr int kStrRefDamageResistance = 1454;
static constexpr int kStrRefDamageReduction = 1455;
static constexpr int kStrRefFiniteDamageResistance = 1456;
static constexpr int kStrRefFiniteDamageReduction = 1457;
static constexpr int kStrRefUnknownObjectK1 = 0;
static constexpr int kStrRefUnknownObjectTSL = 649;
static constexpr int kStrRefUniversalDamage = 1422;
static constexpr int kStrRefPhysicalDamage = 1423;
static constexpr int kStrRefAcidDamage = 1440;
static constexpr int kStrRefColdDamage = 1441;
static constexpr int kStrRefLightSideDamage = 1442;
static constexpr int kStrRefElectricalDamage = 1443;
static constexpr int kStrRefFireDamage = 1444;
static constexpr int kStrRefDarkSideDamage = 1445;
static constexpr int kStrRefSonicDamage = 1446;
static constexpr int kStrRefIonDamage = 1447;
static constexpr int kStrRefEnergyDamage = 1448;

static constexpr int kStrRefEffectOutcomePrefix = 42157;
static constexpr int kStrRefEffectFailed = 42158;
static constexpr int kStrRefEffectImmune = 42160;

static constexpr int kStrRefAbilityDrain = 42408;
static constexpr int kStrRefStrength = 211;
static constexpr int kStrRefDexterity = 212;
static constexpr int kStrRefConstitution = 213;
static constexpr int kStrRefWisdom = 214;
static constexpr int kStrRefIntelligence = 215;
static constexpr int kStrRefCharisma = 216;

static constexpr int kStrRefWeaponIneffective = 1473;
static constexpr int kStrRefCriticalHitImmunity = 1480;
static constexpr int kStrRefSneakAttackImmunity = 1487;

using Recipient = std::optional<std::reference_wrapper<const Object>>;
using Observed = std::optional<std::reference_wrapper<const Creature>>;

bool canReceiveCombatFeedback(const Creature &controlled, const Creature &subject) {
    return controlled.faction() == subject.faction() &&
           controlled.getSquareDistanceTo(subject) <= kCombatFeedbackRangeSquared;
}

// Recipients

static Recipient recipientOf(const std::shared_ptr<Object> &object) {
    if (!object) {
        return std::nullopt;
    }
    return Recipient {*object};
}

// The second recipient of a line that tells a creature only once.
static Recipient otherThan(Recipient recipient, const Object &first) {
    if (recipient && &recipient->get() == &first) {
        return std::nullopt;
    }
    return recipient;
}

static void addFeedbackLines(Game &game, int count, const std::string &text, MessageLog::Buffer buffer) {
    for (int line = 0; line < count; ++line) {
        game.messageLog().add(MessageLog::kFeedbackMessageType, MessageLog::Style::Normal, text, buffer);
    }
}

// A line sent to each recipient is shown once for every recipient that is the
// controlled creature: a creature named twice sees it twice. The text is made
// only when it is shown.
template <typename MakeText>
static void tellRecipients(Game &game, std::initializer_list<Recipient> recipients, MessageLog::Buffer buffer,
                           MakeText makeText) {
    const auto controlled = game.party().getLeader();
    const auto count = std::count_if(recipients.begin(), recipients.end(), [&controlled](const Recipient &recipient) {
        return recipient && &recipient->get() == controlled.get();
    });
    if (count == 0) {
        return;
    }
    addFeedbackLines(game, static_cast<int>(count), makeText(), buffer);
}

// A combat line about each subject is shown once for every subject that can
// send combat feedback to the controlled creature. With no creature under
// control, no one sees it.
template <typename MakeText>
static void tellCombatObservers(Game &game, std::initializer_list<Observed> subjects, MakeText makeText) {
    const auto controlled = game.party().getLeader();
    if (!controlled) {
        return;
    }
    const auto count = std::count_if(subjects.begin(), subjects.end(), [&controlled](const Observed &subject) {
        return subject && canReceiveCombatFeedback(*controlled, subject->get());
    });
    if (count == 0) {
        return;
    }
    addFeedbackLines(game, static_cast<int>(count), makeText(), MessageLog::Buffer::Combat);
}

// END Recipients

// Effect lines

void addHitPointHealingFeedback(Game &game, const std::shared_ptr<Object> &creator, const Creature &target,
                                int amount) {
    tellRecipients(game, {target, otherThan(recipientOf(creator), target)}, MessageLog::Buffer::Messages, [&] {
        return game.getFeedbackText(kStrRefHitPointHealing, {{0, target.name()}, {1, std::to_string(amount)}});
    });
}

void addBlindnessImmunityFeedback(Game &game, const std::shared_ptr<Object> &creator, const Creature &target) {
    tellRecipients(game, {recipientOf(creator), target}, MessageLog::Buffer::Combat, [&] {
        return game.getFeedbackText(kStrRefBlindnessImmunity, {{0, target.name()}});
    });
}

void addForceHealingFeedback(Game &game, const Creature &target, int amount) {
    tellRecipients(game, {target}, MessageLog::Buffer::Combat, [&] {
        return game.getFeedbackText(kStrRefForceHealing, {{0, target.name()}, {1, std::to_string(amount)}});
    });
}

static int getSavingThrowStrRef(SavingThrow savingThrow) {
    switch (savingThrow) {
    case SavingThrow::Fortitude:
        return kStrRefSavingThrowFortitude;
    case SavingThrow::Reflex:
        return kStrRefSavingThrowReflex;
    case SavingThrow::Will:
        return kStrRefSavingThrowWill;
    case SavingThrow::None:
        break;
    }
    assert(false && "unsupported saving-throw feedback type");
    return 0;
}

void addSavingThrowFeedback(Game &game, const Creature &saver, std::optional<std::reference_wrapper<const Object>> source,
                            SavingThrow savingThrow, const SavingThrowBreakdown &breakdown, int roll,
                            int difficultyClass) {
    // Only a creature the throw was made against is told.
    Recipient sourceCreature;
    if (source && Creature::asCreature(source->get())) {
        sourceCreature = otherThan(source, saver);
    }
    tellRecipients(game, {saver, sourceCreature}, MessageLog::Buffer::Combat, [&] {
        std::string components;
        if (breakdown.base != 0) {
            components += game.getFeedbackText(kStrRefSavingThrowBaseComponent, {{0, std::to_string(breakdown.base)}});
        }
        if (breakdown.modifier != 0) {
            components += game.getFeedbackText(kStrRefSavingThrowEffectComponent,
                                               {{0, std::to_string(breakdown.modifier)}});
        }
        const int total = roll + breakdown.total();
        return game.getFeedbackText(
            kStrRefSavingThrow,
            {
                {0, saver.name()},
                {1, game.getFeedbackText(getSavingThrowStrRef(savingThrow))},
                {2, game.getFeedbackText(total >= difficultyClass ? kStrRefSavingThrowSuccess : kStrRefSavingThrowFailure)},
                {3, std::to_string(total)},
                {4, std::to_string(roll)},
                {5, components},
                {6, std::to_string(difficultyClass)},
            });
    });
}

void addEntangleImmunityFeedback(Game &game, const std::shared_ptr<Object> &creator, const Creature &target) {
    tellRecipients(game, {recipientOf(creator), target}, MessageLog::Buffer::Combat, [&] {
        return game.getFeedbackText(kStrRefEntangleImmunity, {{0, target.name()}});
    });
}

// The string naming the target's immunity to a state. Droid confusion has
// none: its immunity is reported with no text.
static std::optional<int> getStateImmunityStrRef(CreatureState state) {
    switch (state) {
    case CreatureState::Fear:
        return 1481;
    case CreatureState::Knockdown:
        return 1482;
    case CreatureState::Paralysis:
        return 1483;
    case CreatureState::MindTrick:
        return 1485;
    case CreatureState::Sleep:
        return 1488;
    case CreatureState::Confusion:
        return 1489;
    case CreatureState::Stun:
        return 1490;
    case CreatureState::Horrified:
        return 42531;
    default:
        return std::nullopt;
    }
}

void addStateImmunityFeedback(Game &game, const std::shared_ptr<Object> &creator, const Creature &target,
                              CreatureState state) {
    const auto strRef = getStateImmunityStrRef(state);
    if (!strRef) {
        return;
    }
    tellRecipients(game, {recipientOf(creator), target}, MessageLog::Buffer::Combat, [&] {
        return game.getFeedbackText(*strRef, {{0, target.name()}});
    });
}

// The line naming a creature immune to an effect, by the immunity that refused
// it; none for the immunity types that report nothing.
static std::optional<int> getLinkImmunityStrRef(ImmunityType immunity) {
    switch (immunity) {
    case ImmunityType::Disease:
        return 1479;
    case ImmunityType::CriticalHit:
        return 1480;
    case ImmunityType::Fear:
        return 1481;
    case ImmunityType::Knockdown:
        return 1482;
    case ImmunityType::Paralysis:
        return 1483;
    case ImmunityType::NegativeLevel:
        return 1484;
    case ImmunityType::MindSpells:
        return 1485;
    case ImmunityType::Poison:
        return 1486;
    case ImmunityType::Sleep:
        return 1488;
    case ImmunityType::Confused:
    case ImmunityType::DroidConfused:
        return 1489;
    case ImmunityType::Stun:
        return 1490;
    case ImmunityType::Entangle:
        return 1491;
    case ImmunityType::Slow:
        return 1492;
    case ImmunityType::Blindness:
    case ImmunityType::Deafness:
    case ImmunityType::Silence:
    case ImmunityType::Cursed:
    case ImmunityType::Dazed:
    case ImmunityType::Death:
        return 0;
    default:
        return std::nullopt;
    }
}

void addLinkImmunityFeedback(Game &game, const std::shared_ptr<Object> &creator, const Creature &target,
                             ImmunityType immunity) {
    const auto strRef = getLinkImmunityStrRef(immunity);
    if (!strRef) {
        return;
    }
    const Recipient creatorCreature = Creature::asCreature(creator) ? recipientOf(creator) : std::nullopt;
    tellRecipients(game, {creatorCreature, target}, MessageLog::Buffer::Combat, [&] {
        return game.getFeedbackText(*strRef, {{0, target.name()}});
    });
}

void addSlowImmunityFeedback(Game &game, const std::shared_ptr<Object> &creator, const Creature &target) {
    tellRecipients(game, {recipientOf(creator), target}, MessageLog::Buffer::Combat, [&] {
        return game.getFeedbackText(kStrRefSlowImmunity, {{0, target.name()}});
    });
}

void addPoisonImmunityFeedback(Game &game, const std::shared_ptr<Object> &creator, const Creature &target) {
    tellRecipients(game, {target, recipientOf(creator)}, MessageLog::Buffer::Combat, [&] {
        return game.getFeedbackText(kStrRefDamageImmunity,
                                    {{0, target.name()}, {1, game.getFeedbackText(kStrRefPoisonDamage)}});
    });
}

void addPoisonedFeedback(Game &game, Creature &target, int poisonNameStrRef) {
    tellCombatObservers(game, {target}, [&] {
        return game.getFeedbackText(kStrRefPoisoned,
                                    {{0, target.name()}, {1, game.getFeedbackText(poisonNameStrRef)}});
    });
    // The sound is heard within combat feedback range of the controlled
    // creature, whatever the target's faction.
    const auto controlled = game.party().getLeader();
    if (controlled && controlled->getSquareDistanceTo(target) <= kCombatFeedbackRangeSquared) {
        target.playSound(resource::SoundSetEntry::Poisoned);
    }
}

void addSpellImmunityFeedback(Game &game, const Creature &target, const Object &caster,
                              const std::string &spellName) {
    tellRecipients(game, {target, caster}, MessageLog::Buffer::Combat, [&] {
        return game.getFeedbackText(kStrRefDamageImmunity, {{0, target.name()}, {1, spellName}});
    });
}

void addDispelFeedback(Game &game, ServicesView &services, const Object &target,
                       std::optional<std::reference_wrapper<const Creature>> caster,
                       const std::vector<uint32_t> &spellIds) {
    Recipient casterRecipient;
    if (caster) {
        casterRecipient = otherThan(Recipient {caster->get()}, target);
    }
    tellRecipients(game, {target, casterRecipient}, MessageLog::Buffer::Combat, [&] {
        std::string text = game.getFeedbackText(kStrRefDispel) + " : " + target.name() + " : ";
        for (size_t i = 0; i < spellIds.size(); ++i) {
            if (i != 0) {
                text += ", ";
            }
            text += services.game.spells.get(static_cast<SpellType>(spellIds[i]))->name;
        }
        return text;
    });
}

void addDeathExperienceFeedback(Game &game, const std::string &recipientName, const std::string &victimName,
                                int reportedExperience) {
    game.messageLog().add(
        MessageLog::kFeedbackMessageType,
        MessageLog::Style::Normal,
        game.getFeedbackText(kStrRefDeathExperience,
                             {{0, recipientName}, {1, victimName}, {2, std::to_string(reportedExperience)}}),
        MessageLog::Buffer::Combat);
}

// END Effect lines

// Round lines

// The name of an immune damage type: physical when any physical type is
// present, else the lowest other type.
static std::string getMitigationDamageTypeName(Game &game, int damageFlags) {
    static constexpr std::pair<DamageType, int> kDamageTypes[] {
        {DamageType::Physical, kStrRefPhysicalDamage},
        {DamageType::Universal, kStrRefUniversalDamage},
        {DamageType::Acid, kStrRefAcidDamage},
        {DamageType::Cold, kStrRefColdDamage},
        {DamageType::LightSide, kStrRefLightSideDamage},
        {DamageType::Electrical, kStrRefElectricalDamage},
        {DamageType::Fire, kStrRefFireDamage},
        {DamageType::DarkSide, kStrRefDarkSideDamage},
        {DamageType::Sonic, kStrRefSonicDamage},
        {DamageType::Ion, kStrRefIonDamage},
        {DamageType::Blaster, kStrRefEnergyDamage},
        {DamageType::Poison, kStrRefPoisonDamage},
    };
    for (const auto &[type, strRef] : kDamageTypes) {
        if ((damageFlags & static_cast<int>(type)) != 0) {
            // Damage type strings have a token that other lines fill; this
            // line leaves it empty.
            return game.getFeedbackText(strRef, {{0, std::string()}});
        }
    }
    assert(false && "damage immunity feedback without a damage type");
    return {};
}

// A mitigation line names the damaged object and the amount mitigated;
// immunity names the damage type instead, and a finite pool adds what
// remains of it.
static std::string getMitigationText(Game &game, const std::string &damagedName, const MitigationFeedback &feedback) {
    switch (feedback.type) {
    case MitigationFeedbackType::DamageImmunity: {
        auto damageType = getMitigationDamageTypeName(game, feedback.damageFlags);
        return game.getFeedbackText(kStrRefDamageImmunity, {{0, damagedName}, {1, std::move(damageType)}});
    }
    case MitigationFeedbackType::DamageResistance:
        return game.getFeedbackText(kStrRefDamageResistance, {{0, damagedName}, {1, std::to_string(feedback.amount)}});
    case MitigationFeedbackType::DamageReduction:
        return game.getFeedbackText(kStrRefDamageReduction, {{0, damagedName}, {1, std::to_string(feedback.amount)}});
    case MitigationFeedbackType::FiniteDamageResistance:
        return game.getFeedbackText(kStrRefFiniteDamageResistance,
                                    {{0, damagedName},
                                     {1, std::to_string(feedback.amount)},
                                     {2, std::to_string(feedback.remaining.value())}});
    case MitigationFeedbackType::FiniteDamageReduction:
        return game.getFeedbackText(kStrRefFiniteDamageReduction,
                                    {{0, damagedName},
                                     {1, std::to_string(feedback.amount)},
                                     {2, std::to_string(feedback.remaining.value())}});
    }
    assert(false && "invalid mitigation feedback type");
    return {};
}

// One mitigation line, sent to the damaged object and to the other side. It
// names the damaged object; one that no longer exists is named by the
// unknown-object string. Damage immunity is a combat line; resistance and
// reduction are messages.
static void addMitigationLine(Game &game, Recipient damaged, Recipient other, const MitigationFeedback &feedback) {
    const auto buffer = feedback.type == MitigationFeedbackType::DamageImmunity ? MessageLog::Buffer::Combat
                                                                                : MessageLog::Buffer::Messages;
    tellRecipients(game, {damaged, other}, buffer, [&] {
        const auto damagedName = damaged ? damaged->get().name()
                                         : game.getFeedbackText(game.isTSL() ? kStrRefUnknownObjectTSL : kStrRefUnknownObjectK1);
        return getMitigationText(game, damagedName, feedback);
    });
}

// The damage line names the creator and the damaged object, or only the
// damaged object when there is no creator, with the total; then, in
// parentheses, the physical damage and each other damage type present.
static std::string getDamageText(Game &game, const std::shared_ptr<Object> &creator, const Object &damaged,
                                 const std::array<int, DamageEffect::kSlotCount> &amounts) {
    static constexpr std::array<std::pair<DamageType, int>, 11> kOtherDamageTypes {{
        {DamageType::Universal, kStrRefUniversalDamage},
        {DamageType::Acid, kStrRefAcidDamage},
        {DamageType::Cold, kStrRefColdDamage},
        {DamageType::LightSide, kStrRefLightSideDamage},
        {DamageType::Electrical, kStrRefElectricalDamage},
        {DamageType::Fire, kStrRefFireDamage},
        {DamageType::DarkSide, kStrRefDarkSideDamage},
        {DamageType::Sonic, kStrRefSonicDamage},
        {DamageType::Ion, kStrRefIonDamage},
        {DamageType::Blaster, kStrRefEnergyDamage},
        {DamageType::Poison, kStrRefPoisonDamage},
    }};
    const auto total = std::to_string(amounts[DamageEffect::kTotal]);
    auto text = creator ? game.getFeedbackText(kStrRefDamageDealt, {{0, creator->name()}, {1, damaged.name()}, {2, total}})
                        : game.getFeedbackText(kStrRefDamageTaken, {{0, damaged.name()}, {1, total}});
    std::vector<std::string> pieces;
    const auto addPiece = [&](int strRef, int amount) {
        pieces.push_back(game.getFeedbackText(strRef, {{0, std::to_string(amount)}}));
    };
    // Bludgeoning, piercing and slashing damage are told together as
    // physical damage; a slot that took no part in the damage is negative.
    const auto physical = {amounts[getDirectDamageSlot(static_cast<int>(DamageType::Bludgeoning))],
                           amounts[getDirectDamageSlot(static_cast<int>(DamageType::Piercing))],
                           amounts[getDirectDamageSlot(static_cast<int>(DamageType::Slashing))]};
    if (std::any_of(physical.begin(), physical.end(), [](int amount) { return amount >= 0; })) {
        int sum = 0;
        for (int amount : physical) {
            sum += std::max(0, amount);
        }
        addPiece(kStrRefPhysicalDamage, sum);
    }
    for (const auto &[type, strRef] : kOtherDamageTypes) {
        const int amount = amounts[getDirectDamageSlot(static_cast<int>(type))];
        if (amount >= 0) {
            addPiece(strRef, amount);
        }
    }
    text += " (";
    for (size_t i = 0; i < pieces.size(); ++i) {
        if (i > 0) {
            text += " ";
        }
        text += pieces[i];
    }
    text += ")";
    return text;
}

void addDamageFeedback(Game &game, const std::shared_ptr<Object> &creator, const Object &damaged,
                       const std::array<int, DamageEffect::kSlotCount> &amounts) {
    // A damaged creature tells its own side; a creature creator tells its
    // side too when it is of another faction, or when the damaged object is
    // a door or container.
    const auto damagedCreature = Creature::asCreature(damaged);
    Observed creatorSide = Creature::asCreature(creator);
    if (creatorSide && damagedCreature && creatorSide->get().faction() == damagedCreature->get().faction()) {
        creatorSide.reset();
    }
    tellCombatObservers(game, {damagedCreature, creatorSide},
                        [&] { return getDamageText(game, creator, damaged, amounts); });
}

static std::optional<int> getEffectOutcomeMessageStrRef(int outcome) {
    if (outcome == EffectOutcomeBreakdown::kNoOutcome) {
        return std::nullopt;
    }
    switch (static_cast<SavingThrowResult>(outcome)) {
    case SavingThrowResult::Failed:
        return kStrRefEffectFailed;
    case SavingThrowResult::Immune:
        return kStrRefEffectImmune;
    case SavingThrowResult::Saved:
        // A resisted effect has no line.
        return std::nullopt;
    }
    assert(false && "invalid effect outcome");
    return std::nullopt;
}

void addAttackEffectOutcomeFeedback(Game &game, const std::shared_ptr<Object> &attacker, const Object &target,
                                    const EffectOutcomeBreakdown &values) {
    const auto message = getEffectOutcomeMessageStrRef(values.outcome);
    if (!values.present || !message) {
        return;
    }
    tellCombatObservers(game, {Creature::asCreature(attacker), Creature::asCreature(target)}, [&] {
        std::map<int, std::string> tokens {{0, target.name()}};
        if (const auto label = getEffectOutcomeLabelStrRef(game.isTSL(), values.effectType)) {
            tokens[1] = game.getFeedbackText(*label);
        }
        return game.getFeedbackText(kStrRefEffectOutcomePrefix) + game.getFeedbackText(*message, tokens);
    });
}

static int getAbilityStrRef(Ability ability) {
    switch (ability) {
    case Ability::Strength:
        return kStrRefStrength;
    case Ability::Dexterity:
        return kStrRefDexterity;
    case Ability::Constitution:
        return kStrRefConstitution;
    case Ability::Intelligence:
        return kStrRefIntelligence;
    case Ability::Wisdom:
        return kStrRefWisdom;
    case Ability::Charisma:
        return kStrRefCharisma;
    }
    assert(false && "unsupported ability-drain feedback ability");
    return 0;
}

// Shows one piece of a round's feedback, sent from the attacker to the target.
// Each line is shown only to the controlled creature among its recipients.
static void showFeedbackRecord(Game &game, const std::shared_ptr<Object> &attacker, const Object &target,
                               const DeferredCombatFeedback &record) {
    std::visit(
        [&](const auto &value) {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, AbilityDrainFeedback>) {
                tellRecipients(game, {target, recipientOf(attacker)}, MessageLog::Buffer::Messages, [&] {
                    return game.getFeedbackText(kStrRefAbilityDrain,
                                                {
                                                    {0, target.name()},
                                                    {1, game.getFeedbackText(getAbilityStrRef(value.ability))},
                                                    {2, std::to_string(value.amount)},
                                                    {3, std::to_string(value.durationSeconds)},
                                                });
                });
            } else if constexpr (std::is_same_v<Value, SneakAttackImmunityFeedback> ||
                                 std::is_same_v<Value, CriticalHitImmunityFeedback>) {
                // The target's immunity to the attacker's sneak attack or
                // critical hit tells a creature that is both only once.
                const int strRef = std::is_same_v<Value, SneakAttackImmunityFeedback> ? kStrRefSneakAttackImmunity
                                                                                      : kStrRefCriticalHitImmunity;
                tellRecipients(game, {target, otherThan(recipientOf(attacker), target)}, MessageLog::Buffer::Combat,
                               [&] { return game.getFeedbackText(strRef, {{0, target.name()}}); });
            } else if constexpr (std::is_same_v<Value, WeaponIneffectiveFeedback>) {
                tellRecipients(game, {recipientOf(attacker)}, MessageLog::Buffer::Messages,
                               [&] { return game.getFeedbackText(kStrRefWeaponIneffective); });
            } else if constexpr (std::is_same_v<Value, MitigationFeedback>) {
                addMitigationLine(game, target, recipientOf(attacker), value);
            } else if constexpr (std::is_same_v<Value, ReturnedMitigationFeedback>) {
                addMitigationLine(game, recipientOf(attacker), target, value.feedback);
            } else {
                static_assert(std::is_same_v<Value, SavingThrowFeedback>);
                // The target is the saver; only a creature saves.
                if (const auto saver = Creature::asCreature(target)) {
                    Recipient source;
                    if (attacker) {
                        source = *attacker;
                    }
                    addSavingThrowFeedback(game, saver->get(), source, value.savingThrow,
                                           SavingThrowBreakdown {value.base, value.modifier}, value.roll,
                                           value.difficultyClass);
                }
            }
        },
        record);
}

void showCombatFeedbackMessage(Game &game, const std::shared_ptr<Object> &attacker, const Object &target,
                               const CombatFeedbackMessage &message) {
    // A door or placeable passes a line meant for the target alone on to the
    // attacker.
    const bool toTargetOnly = message.targetOnly && Creature::asCreature(target);
    showFeedbackRecord(game, toTargetOnly ? nullptr : attacker, target, message.feedback);
}

void showCombatFeedback(Game &game, const std::shared_ptr<Object> &attacker, const Object &target,
                        const std::vector<DeferredCombatFeedback> &records) {
    for (const auto &record : records) {
        showFeedbackRecord(game, attacker, target, record);
    }
}

// END Round lines

// Saved messages

// The type of a queued message: who is told, or that it reports a saving
// throw.
enum class FeedbackMessageType {
    AttackerAndTarget = 0,
    SavingThrow = 1,
    Attacker = 3,
    Target = 4,
};

// The feedback a queued message names by its first integer.
enum class FeedbackMessageId {
    DamageImmunity = 62,
    DamageResistance = 63,
    DamageReduction = 64,
    FiniteDamageResistance = 66,
    FiniteDamageReduction = 67,
    WeaponIneffective = 117,
    CriticalHitImmunity = 126,
    SneakAttackImmunity = 134,
    AbilityDrain = 142,
};

// The structure of a queued message and of each entry of its lists.
static constexpr uint32_t kFeedbackMessageStructType = 0xcccc;
static constexpr uint32_t kFeedbackMessageEntryStructType = 0xbaad;

// A queued message as it is saved: its type, its integers (the first names
// the feedback, except in a saving throw), its floats and its objects. Its
// string list is always empty.
struct SavedFeedbackValues {
    FeedbackMessageType type {FeedbackMessageType::AttackerAndTarget};
    std::vector<int> integers;
    std::vector<float> floats;
    std::vector<uint32_t> objects;
};

static int toInt(FeedbackMessageId id) {
    return static_cast<int>(id);
}

// A mitigation message names the damaged object; finite pools add what
// remains.
static SavedFeedbackValues getMitigationValues(FeedbackMessageType type, const MitigationFeedback &feedback,
                                               uint32_t damagedId) {
    switch (feedback.type) {
    case MitigationFeedbackType::DamageImmunity:
        return {type, {toInt(FeedbackMessageId::DamageImmunity), feedback.amount, feedback.damageFlags}, {}, {damagedId}};
    case MitigationFeedbackType::DamageResistance:
        return {type, {toInt(FeedbackMessageId::DamageResistance), feedback.amount}, {}, {damagedId}};
    case MitigationFeedbackType::DamageReduction:
        return {type, {toInt(FeedbackMessageId::DamageReduction), feedback.amount}, {}, {damagedId}};
    case MitigationFeedbackType::FiniteDamageResistance:
        return {type,
                {toInt(FeedbackMessageId::FiniteDamageResistance), feedback.amount, feedback.remaining.value()},
                {},
                {damagedId}};
    case MitigationFeedbackType::FiniteDamageReduction:
        return {type,
                {toInt(FeedbackMessageId::FiniteDamageReduction), feedback.amount, feedback.remaining.value()},
                {},
                {damagedId}};
    }
    assert(false && "invalid mitigation feedback type");
    return {};
}

static SavedFeedbackValues getSavedValues(const CombatFeedbackMessage &message, uint32_t targetId,
                                          uint32_t attackerId) {
    const auto type = message.targetOnly ? FeedbackMessageType::Target : FeedbackMessageType::AttackerAndTarget;
    return std::visit(
        [&](const auto &value) -> SavedFeedbackValues {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, AbilityDrainFeedback>) {
                return {type,
                        {toInt(FeedbackMessageId::AbilityDrain), static_cast<int>(value.ability), value.amount},
                        {static_cast<float>(value.durationSeconds)},
                        {targetId}};
            } else if constexpr (std::is_same_v<Value, SavingThrowFeedback>) {
                // The category, subtype, roll, effect modifier, base save,
                // difficulty class and the saver: the target.
                return {FeedbackMessageType::SavingThrow,
                        {static_cast<int>(value.savingThrow), static_cast<int>(value.savingThrowType), value.roll,
                         value.modifier, value.base, value.difficultyClass, static_cast<int>(targetId)},
                        {},
                        {}};
            } else if constexpr (std::is_same_v<Value, SneakAttackImmunityFeedback>) {
                return {type, {toInt(FeedbackMessageId::SneakAttackImmunity)}, {}, {targetId}};
            } else if constexpr (std::is_same_v<Value, CriticalHitImmunityFeedback>) {
                return {type, {toInt(FeedbackMessageId::CriticalHitImmunity)}, {}, {targetId}};
            } else if constexpr (std::is_same_v<Value, WeaponIneffectiveFeedback>) {
                return {FeedbackMessageType::Attacker, {toInt(FeedbackMessageId::WeaponIneffective)}, {}, {}};
            } else if constexpr (std::is_same_v<Value, MitigationFeedback>) {
                return getMitigationValues(type, value, targetId);
            } else {
                static_assert(std::is_same_v<Value, ReturnedMitigationFeedback>);
                return getMitigationValues(type, value.feedback, attackerId);
            }
        },
        message.feedback);
}

static std::shared_ptr<Gff> makeEntry(Gff::Field field) {
    return Gff::Builder().type(kFeedbackMessageEntryStructType).field(std::move(field)).build();
}

std::shared_ptr<Gff> CombatFeedbackMessage::toGff(uint32_t targetId, uint32_t attackerId) const {
    const auto values = getSavedValues(*this, targetId, attackerId);
    std::vector<std::shared_ptr<Gff>> integers;
    for (int value : values.integers) {
        integers.push_back(makeEntry(Gff::Field::newInt("IntegerValue", value)));
    }
    std::vector<std::shared_ptr<Gff>> floats;
    for (float value : values.floats) {
        floats.push_back(makeEntry(Gff::Field::newFloat("FloatValue", value)));
    }
    std::vector<std::shared_ptr<Gff>> objects;
    for (uint32_t id : values.objects) {
        objects.push_back(makeEntry(Gff::Field::newDword("ObjectValue", id)));
    }
    return Gff::Builder()
        .type(kFeedbackMessageStructType)
        .field(Gff::Field::newByte("Type", static_cast<uint32_t>(values.type)))
        .field(Gff::Field::newList("IntList", std::move(integers)))
        .field(Gff::Field::newList("FloatList", std::move(floats)))
        .field(Gff::Field::newList("ObjectIDList", std::move(objects)))
        .field(Gff::Field::newList("StringList", {}))
        .build();
}

// Reads a value of each entry of a list; every entry holds one.
template <typename T, typename Read>
static std::vector<T> readListValues(const Gff &gff, const std::string &list, const char *label, Read read) {
    std::vector<T> result;
    for (const auto &entry : gff.getList(list)) {
        T value {};
        if (!((*entry).*read)(value, label)) {
            throw ValidationException("Feedback message entry without " + std::string(label));
        }
        result.push_back(value);
    }
    return result;
}

// One value of a message; a message without a value its feedback needs is
// malformed.
template <typename T>
static T requiredValue(const std::vector<T> &values, size_t index) {
    if (index >= values.size()) {
        throw ValidationException("Feedback message without value " + std::to_string(index));
    }
    return values[index];
}

std::optional<CombatFeedbackMessage> CombatFeedbackMessage::fromGff(const Gff &gff, uint32_t attackerId) {
    uint8_t savedType = 0;
    if (!gff.readByte(savedType, "Type")) {
        throw ValidationException("Feedback message without a type");
    }
    const auto ints = readListValues<int32_t>(gff, "IntList", "IntegerValue", &Gff::readInt);
    const auto floats = readListValues<float>(gff, "FloatList", "FloatValue", &Gff::readFloat);
    const auto objects = readListValues<uint32_t>(gff, "ObjectIDList", "ObjectValue", &Gff::readDword);
    const auto integer = [&ints](size_t index) { return requiredValue(ints, index); };

    const auto type = static_cast<FeedbackMessageType>(savedType);
    if (type == FeedbackMessageType::SavingThrow) {
        // The saver, the seventh integer, is the target.
        return CombatFeedbackMessage {SavingThrowFeedback {static_cast<SavingThrow>(integer(0)), integer(4),
                                                           integer(3), integer(2), integer(5),
                                                           static_cast<SavingThrowType>(integer(1))}};
    }
    const auto id = static_cast<FeedbackMessageId>(integer(0));
    // An ineffective weapon is told to the attacker alone, and is the only
    // feedback that is.
    const bool weaponIneffective = id == FeedbackMessageId::WeaponIneffective;
    if (type == FeedbackMessageType::Attacker || weaponIneffective) {
        if (type == FeedbackMessageType::Attacker && weaponIneffective) {
            return CombatFeedbackMessage {WeaponIneffectiveFeedback {}};
        }
        return std::nullopt;
    }
    // Any other type tells both sides.
    const bool targetOnly = type == FeedbackMessageType::Target;
    const auto mitigation = [&](MitigationFeedback feedback) {
        // The line names its object: the target, or the attacker that its own
        // deflected shot came back to.
        if (requiredValue(objects, 0) == attackerId) {
            return CombatFeedbackMessage {ReturnedMitigationFeedback {feedback}, targetOnly};
        }
        return CombatFeedbackMessage {feedback, targetOnly};
    };
    switch (id) {
    case FeedbackMessageId::DamageImmunity:
        return mitigation({MitigationFeedbackType::DamageImmunity, integer(1), std::nullopt, integer(2)});
    case FeedbackMessageId::DamageResistance:
        return mitigation({MitigationFeedbackType::DamageResistance, integer(1), std::nullopt, 0});
    case FeedbackMessageId::DamageReduction:
        return mitigation({MitigationFeedbackType::DamageReduction, integer(1), std::nullopt, 0});
    case FeedbackMessageId::FiniteDamageResistance:
        return mitigation({MitigationFeedbackType::FiniteDamageResistance, integer(1), integer(2), 0});
    case FeedbackMessageId::FiniteDamageReduction:
        return mitigation({MitigationFeedbackType::FiniteDamageReduction, integer(1), integer(2), 0});
    case FeedbackMessageId::CriticalHitImmunity:
        return CombatFeedbackMessage {CriticalHitImmunityFeedback {}, targetOnly};
    case FeedbackMessageId::SneakAttackImmunity:
        return CombatFeedbackMessage {SneakAttackImmunityFeedback {}, targetOnly};
    case FeedbackMessageId::AbilityDrain:
        // The ability, the decrease and the duration in seconds, shown whole.
        return CombatFeedbackMessage {AbilityDrainFeedback {static_cast<Ability>(integer(1)), integer(2),
                                                            static_cast<int>(requiredValue(floats, 0))},
                                      targetOnly};
    default:
        // Feedback that has no line here.
        return std::nullopt;
    }
}

// END Saved messages

} // namespace game

} // namespace reone
