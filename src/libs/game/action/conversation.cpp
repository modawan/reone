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

#include <algorithm>

#include "reone/game/action/barkstring.h"
#include "reone/game/action/resumeconversation.h"
#include "reone/game/action/speakstring.h"
#include "reone/game/action/speakstringbystrref.h"
#include "reone/game/action/startconversation.h"
#include "reone/game/combat.h"
#include "reone/game/di/services.h"
#include "reone/game/effect/resurrection.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/game/savedruntime.h"
#include "reone/system/logutil.h"

namespace reone {

namespace game {

// A partner is talked to from within its use range and a metre more; once the
// speaker has set off, from anywhere within ten metres of it when the walk
// ends.
static constexpr float kConversationUseRangeExtra = 1.0f;
static constexpr float kConversationStartDistance = 10.0f;
static constexpr float kHandOffFadeLength = 0.75f;
// Followers at least this far (squared metres) from the handing-off companion
// are brought to their formation spots, placed within this radius.
static constexpr float kHandOffGatherDistance2 = 900.0f;
static constexpr float kHandOffPlacementRadius = 10.0f;

static bool isPartyInteract(const Object &object) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        return creature->isPartyInteract();
    }
    if (auto *placeable = dyn_cast<Placeable>(&object)) {
        return placeable->isPartyInteract();
    }
    return false;
}

// The speaker first looks for the partner within use range, a metre further.
// Out of it, the speaker runs to the partner's use point until that close, or
// to a door's or placeable's use range, and then starts the conversation if
// the partner is within ten metres, or sets off again.
bool StartConversationAction::approach(Creature &speaker, float dt) {
    if (!_approaching) {
        if (speaker.isInUseRange(*_objectToConverse, kConversationUseRangeExtra)) {
            return true;
        }
        _approaching = true;
    }
    const Creature::UseRange use = speaker.useRange(*_objectToConverse);
    const ObjectType partnerType = _objectToConverse->type();
    const float range = partnerType == ObjectType::Door || partnerType == ObjectType::Placeable
                            ? use.range
                            : use.range + kConversationUseRangeExtra;
    if (!speaker.navigateTo(use.point, true, range, dt, *_objectToConverse)) {
        return false;
    }
    const glm::vec3 offset {_objectToConverse->position() - speaker.position()};
    return glm::dot(offset, offset) <= kConversationStartDistance * kConversationStartDistance;
}

// A companion that reaches someone with a conversation of their own hands
// the conversation to the player character, unless the target is the
// player character or asks for party interaction.
bool StartConversationAction::handsOffToPlayer(const std::shared_ptr<Object> &actor) const {
    auto &party = _game.party();
    auto player = party.player();
    auto *creature = dyn_cast<Creature>(actor.get());
    return !_transferredFrom && creature && player && actor != player && party.isMember(*creature) &&
           _objectToConverse && _objectToConverse != player && !isPartyInteract(*_objectToConverse) &&
           !_objectToConverse->conversation().empty() && party.isMember(*player);
}

// The party stops, followers far from the companion are brought to their
// formation spots, the screen fades out with the player's input shut off, and
// the player character takes the conversation over, revived if it is down.
void StartConversationAction::handOffToPlayer(std::shared_ptr<Action> self, Object &actor) {
    auto &party = _game.party();
    auto player = party.player();
    auto handOff = _game.newAction<StartConversationAction>(_objectToConverse, _dialogResRef, _privateConversation,
                                                            _conversationType, _ignoreStartRange, _namesToIgnore, _useLeader, _barkX, _barkY, _dontClearAllActions);
    handOff->_transferredFrom = std::dynamic_pointer_cast<Creature>(_game.getObjectById(actor.id()));
    handOff->_admitted = true;
    handOff->_fadeDialog = std::move(_fadeDialog);
    // Each member's actions are cleared, forced, and so are its orders as the
    // player's controls clear them.
    for (int i = 0; i < party.getSize(); ++i) {
        if (auto member = party.getMember(i)) {
            member->clearAllActions(true);
            _game.combat().clearAllOrders(*member);
        }
    }
    const glm::vec3 leaderPosition {party.getLeader()->position()};
    auto &area = *_game.module()->area();
    for (int slot = 1; slot < kPartyFollowSlots && slot < party.getSize(); ++slot) {
        auto member = party.getMember(slot);
        if (!member) {
            continue;
        }
        const glm::vec3 offset {member->position() - leaderPosition};
        if (glm::dot(offset, offset) < kHandOffGatherDistance2) {
            continue;
        }
        const glm::vec3 spot {party.formationSpot(slot)};
        member->setPosition(area.computeSafeLocation(spot, kHandOffPlacementRadius, *member, false).value_or(spot));
        area.determineObjectRoom(*member);
    }
    _game.globalFade().request(GlobalFade::Direction::Out, 0.0f, kHandOffFadeLength);
    _game.setPlayerInputBlocked(true);
    _game.globalFade().holdForDialog();
    if (player->currentHitPoints() <= 0) {
        player->applyEffect(_game.newEffect<ResurrectionEffect>(0), DurationType::Instant);
    }
    player->addActionOnTop(std::move(handOff));
    complete();
}

// Once the fade is done the player character takes the companion's place
// facing the target, takes control, gets the player's input back, and hands
// control back after the conversation.
bool StartConversationAction::takeOverFromCompanion(Object &actor) {
    if (_game.globalFade().fading()) {
        return false;
    }
    auto companion = std::move(_transferredFrom);
    _transferredFrom.reset();
    if (!companion || !companion->isRuntimeLive()) {
        return true;
    }
    const glm::vec3 own = actor.position();
    actor.setPosition(companion->position());
    if (_objectToConverse) {
        actor.face(*_objectToConverse);
    } else {
        actor.setFacing(companion->getFacing());
    }
    companion->setPosition(own);
    auto &party = _game.party();
    for (int i = 1; i < party.getSize(); ++i) {
        if (party.getMember(i).get() == &actor) {
            party.setPartyLeaderByIndex(i);
            break;
        }
    }
    _game.setPostDialogCharacterSwitch(companion);
    _game.setPlayerInputBlocked(false);
    return true;
}

void StartConversationAction::admit() {
    if (_admitted) {
        return;
    }
    _admitted = true;
    if (!_game.isConversationActive()) {
        _fadeDialog = _game.globalFade().admitDialog();
    }
    if (!_fadeDialog) {
        complete();
    }
}

bool StartConversationAction::cancel(std::shared_ptr<Action> self, Object &actor) {
    // A request that has started its conversation is done: clearing it, as a
    // script of the opening entry may, leaves the conversation running.
    if (isCompleted()) {
        return true;
    }
    // A dropped or failed request ends the conversation being under way.
    _game.setDialogUnderWay(false);
    _game.globalFade().finishDialog(_fadeDialog);
    _fadeDialog.reset();
    return true;
}

void StartConversationAction::onQueued(Object &actor) {
    admit();
}

// The party has members and none of them stands: each names no creature or
// one at zero vitality.
static bool isPartyDown(const Party &party) {
    const auto &members = party.members();
    return !members.empty() &&
           std::none_of(members.begin(), members.end(), [](const Party::Member &member) {
               return member.creature && !member.creature->isTemporarilyDead();
           });
}

void StartConversationAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // Direct executors and restored queues use the same admission path.
    admit();
    // No conversation starts, whoever starts it, while the whole party is down.
    if (isPartyDown(_game.party())) {
        cancel(self, actor);
        complete();
        return;
    }
    if (!_game.globalFade().isCurrentDialog(_fadeDialog)) {
        complete();
        return;
    }
    // Discard a queued conversation if a dialogue is already running. Starting
    // it now would replace the current scene; deferring it would cause an
    // immediate extra greeting when the current dialogue ends.
    if (_game.isConversationActive()) {
        debug("Discarding StartConversation, a conversation is already active",
              LogChannel::Conversation);
        // The running conversation stays under way.
        _game.globalFade().finishDialog(_fadeDialog);
        _fadeDialog.reset();
        complete();
        return;
    }
    // While the world is held, a conversation waits to start.
    if (_game.holdsWorld()) {
        return;
    }
    _game.setDialogUnderWay(true);

    auto actorPtr = _game.getObjectById(actor.id());

    // A creature must have a valid conversation partner to approach. Drop the
    // action if its target is missing or destroyed instead of starting a
    // partnerless dialogue.
    if (_transferredFrom) {
        if (!takeOverFromCompanion(actor)) {
            return;
        }
    } else if (auto creatureActor = dyn_cast<Creature>(actorPtr)) {
        if (!_objectToConverse) {
            cancel(self, actor);
            complete();
            return;
        }
        bool reached = _ignoreStartRange || approach(*creatureActor, dt);

        if (!reached) {
            return;
        }
        if (handsOffToPlayer(actorPtr)) {
            // The hand-off is refused, silently, while the player character is in direct combat.
            auto player = _game.party().player();
            if (player->isInCombat() && player->combatActivationType() == CombatActivation::Direct) {
                cancel(self, actor);
                complete();
                return;
            }
            handOffToPlayer(self, actor);
            return;
        }
    }

    // Dialog owner selection - i.e. which participant's Conversation/DLG plays:
    //
    //  - Explicit DialogResRef: the named dialog is caller-owned (PR #150).
    //
    //  - Blank DialogResRef: a party member is always the listener, never the
    //    dialog owner, so the OTHER participant owns the conversation:
    //      * the target owns it when the target is not a party member and has
    //        its own Conversation. This covers the player clicking an NPC or a
    //        placeable, and a script pointing the caller at an invisible dialog
    //        anchor whose Conversation is the scene's dialogue (an NPC told to
    //        converse with such a placeable).
    //      * otherwise the caller owns it when the caller has a Conversation.
    //        This is the common NPC-starts-dialogue-with-the-PC shape, where the
    //        target is the party member.
    //      * otherwise fall back to the target's Conversation if it has one.
    //
    // Keyed only on party membership and Conversation presence, so it is generic
    // (it does not depend on whether the PC/leader happens to have a
    // Conversation field of its own).
    std::shared_ptr<Object> dialogOwner;
    if (!_dialogResRef.empty()) {
        dialogOwner = actorPtr;
    } else {
        bool targetHasConversation =
            _objectToConverse && !_objectToConverse->conversation().empty();
        bool targetIsPartyMember =
            _objectToConverse && _game.party().isMember(*_objectToConverse);
        bool callerHasConversation = actorPtr && !actorPtr->conversation().empty();

        if (targetHasConversation && !targetIsPartyMember) {
            dialogOwner = _objectToConverse;
        } else if (callerHasConversation) {
            dialogOwner = actorPtr;
        } else if (targetHasConversation) {
            dialogOwner = _objectToConverse;
        }
    }

    // If no valid owner can be resolved there is no dialogue to start (e.g. an
    // invalid target combined with an empty resref). Complete the action so it
    // does not block the queue, but do not dereference a null owner -
    // Area::startDialog reads owner->conversation() for an empty resref.
    if (!dialogOwner) {
        cancel(self, actor);
        complete();
        return;
    }

    // The request is done before its conversation starts, and the
    // conversation owns the admission from then on: the opening entry's
    // scripts run inside the start, and may clear this request.
    const auto listener = dialogOwner == actorPtr ? _objectToConverse : actorPtr;
    auto admission = std::move(_fadeDialog);
    complete();
    _game.startDialog(dialogOwner, _dialogResRef.empty() ? dialogOwner->conversation() : _dialogResRef,
                      std::move(admission), listener);
}

// A conversation names its dialog by a resource name of at most sixteen
// characters, and is private or not.
static constexpr size_t kMaxDialogNameLength = 16;

// The command's parameters: the one talked to, the dialog, whether it is
// private, whether the speaker has still to look for its partner within use
// range, whether it starts wherever the speaker stands, and the companion
// that handed it to the player character.
std::optional<SavedActionRecord> StartConversationAction::saveFacingState() const {
    bool ignoredNamesAreEmpty = std::all_of(
        _namesToIgnore.begin(), _namesToIgnore.end(),
        [](const std::string &name) { return name.empty(); });
    if (!_objectToConverse || _dialogResRef.size() > kMaxDialogNameLength ||
        _conversationType != resource::ConversationType::Cinematic ||
        !ignoredNamesAreEmpty || _useLeader ||
        _barkX != -1 || _barkY != -1 || _dontClearAllActions) {
        return std::nullopt;
    }

    return SavedActionRecord::command(SavedActionType::StartConversation, {
                                                                              {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_objectToConverse->id())},
                                                                              {kSavedStringParameter, _dialogResRef},
                                                                              {kSavedIntegerParameter, int32_t {_privateConversation ? 1 : 0}},
                                                                              {kSavedIntegerParameter, int32_t {_approaching ? 0 : 1}},
                                                                              {kSavedIntegerParameter, int32_t {_ignoreStartRange ? 1 : 0}},
                                                                              {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_transferredFrom ? _transferredFrom->id() : script::kObjectInvalid)},
                                                                          });
}

std::shared_ptr<Action> StartConversationAction::restore(const SavedActionRecord &record, Game &game) {
    auto target = record.objectParameter(0).boundObject();
    const auto &dialog = record.stringParameter(1);
    const int32_t privateConversation = record.integerParameter(2);
    if (!target || dialog.size() > kMaxDialogNameLength || (privateConversation != 0 && privateConversation != 1)) {
        return nullptr;
    }
    auto action = game.newAction<StartConversationAction>(std::move(target), dialog, privateConversation != 0,
                                                          resource::ConversationType::Cinematic,
                                                          record.integerParameter(4) != 0);
    action->_approaching = record.integerParameter(3) == 0;
    action->_transferredFrom = std::dynamic_pointer_cast<Creature>(record.objectParameter(5).boundObject());
    return action;
}

void ResumeConversationAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    _game.resumeConversationBy(actor);
    complete();
}

std::optional<SavedActionRecord> ResumeConversationAction::saveFacingState() const {
    return SavedActionRecord::command(SavedActionType::ResumeConversation, {});
}

std::shared_ptr<Action> ResumeConversationAction::restore(const SavedActionRecord &record, Game &game) {
    return game.newAction<ResumeConversationAction>();
}

void SpeakStringAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // A creature that speaks gives up stealth. The words reach listeners as
    // SpeakString's do.
    if (auto *creature = dyn_cast<Creature>(&actor)) {
        creature->setStealthMode(false);
    }
    // Away from any area nobody hears it.
    if (auto area = actor.spatialArea()) {
        area->get().broadcastDialog(actor, _stringToSpeak, _talkVolume);
    }
    complete();
}

// A spoken string keeps its volume as the chat type it is said with: talk,
// whisper, shout, silent talk or silent shout; any other volume is talk.
static constexpr int32_t kTalkChatType = 1;
static constexpr int32_t kShoutChatType = 2;
static constexpr int32_t kWhisperChatType = 3;
static constexpr int32_t kSilentTalkChatType = 13;
static constexpr int32_t kSilentShoutChatType = 14;

static int32_t chatTypeOf(int talkVolume) {
    switch (static_cast<TalkVolume>(talkVolume)) {
    case TalkVolume::Whisper:
        return kWhisperChatType;
    case TalkVolume::Shout:
        return kShoutChatType;
    case TalkVolume::SilentTalk:
        return kSilentTalkChatType;
    case TalkVolume::SilentShout:
        return kSilentShoutChatType;
    default:
        return kTalkChatType;
    }
}

static int talkVolumeOfChatType(int32_t chatType) {
    switch (chatType) {
    case kWhisperChatType:
        return static_cast<int>(TalkVolume::Whisper);
    case kShoutChatType:
        return static_cast<int>(TalkVolume::Shout);
    case kSilentTalkChatType:
        return static_cast<int>(TalkVolume::SilentTalk);
    case kSilentShoutChatType:
        return static_cast<int>(TalkVolume::SilentShout);
    default:
        return static_cast<int>(TalkVolume::Talk);
    }
}

std::optional<SavedActionRecord> SpeakStringAction::saveFacingState() const {
    return SavedActionRecord::command(SavedActionType::SpeakString, {
                                                                        {kSavedStringParameter, _stringToSpeak},
                                                                        {kSavedIntegerParameter, chatTypeOf(_talkVolume)},
                                                                    });
}

std::shared_ptr<Action> SpeakStringAction::restore(const SavedActionRecord &record, Game &game) {
    return game.newAction<SpeakStringAction>(record.stringParameter(0), talkVolumeOfChatType(record.integerParameter(1)));
}

// The volume is kept as the chat channel it speaks on: a whisper, a shout, or
// talk for anything else.
static constexpr int32_t kTalkChannel = 8;
static constexpr int32_t kShoutChannel = 9;
static constexpr int32_t kWhisperChannel = 10;

static int32_t chatChannelOf(int talkVolume) {
    switch (static_cast<TalkVolume>(talkVolume)) {
    case TalkVolume::Whisper:
        return kWhisperChannel;
    case TalkVolume::Shout:
        return kShoutChannel;
    default:
        return kTalkChannel;
    }
}

static int talkVolumeOf(int32_t chatChannel) {
    switch (chatChannel) {
    case kWhisperChannel:
        return static_cast<int>(TalkVolume::Whisper);
    case kShoutChannel:
        return static_cast<int>(TalkVolume::Shout);
    default:
        return static_cast<int>(TalkVolume::Talk);
    }
}

std::optional<SavedActionRecord> SpeakStringByStrRefAction::saveFacingState() const {
    return SavedActionRecord::command(SavedActionType::SpeakStringByStrRef, {
                                                                                {kSavedIntegerParameter, int32_t {_strRef}},
                                                                                {kSavedIntegerParameter, chatChannelOf(_talkVolume)},
                                                                            });
}

std::shared_ptr<Action> SpeakStringByStrRefAction::restore(const SavedActionRecord &record, Game &game) {
    return game.newAction<SpeakStringByStrRefAction>(record.integerParameter(0), talkVolumeOf(record.integerParameter(1)));
}

void BarkStringAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // TODO: implement

    complete();
}

std::optional<SavedActionRecord> BarkStringAction::saveFacingState() const {
    return SavedActionRecord::command(SavedActionType::BarkString, {{kSavedIntegerParameter, int32_t {_strRef}}});
}

std::shared_ptr<Action> BarkStringAction::restore(const SavedActionRecord &record, Game &game) {
    return game.newAction<BarkStringAction>(record.integerParameter(0));
}

} // namespace game

} // namespace reone
