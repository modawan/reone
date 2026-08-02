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

#include "reone/game/pazaak.h"

#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace reone {

namespace game {

namespace pazaak {

CardDefinition::CardDefinition(CardBehavior behavior, int magnitude)
    : _behavior(behavior),
      _magnitude(magnitude) {
    switch (behavior) {
    case CardBehavior::FixedPositive:
    case CardBehavior::FixedNegative:
    case CardBehavior::SignSelectable:
        if (magnitude < 1 || magnitude > 6) {
            throw std::invalid_argument("Pazaak numbered card magnitude must be between 1 and 6");
        }
        break;
    case CardBehavior::Tiebreaker:
        // The Tiebreaker contributes a selectable +/-1 in addition to winning ties.
        if (magnitude != 1) {
            throw std::invalid_argument("Pazaak Tiebreaker card magnitude must be 1");
        }
        break;
    case CardBehavior::Double:
    case CardBehavior::FlipTwoFour:
    case CardBehavior::FlipThreeSix:
    case CardBehavior::ValueChange:
        // These cards carry no fixed magnitude of their own.
        if (magnitude != 0) {
            throw std::invalid_argument("Pazaak special card must not declare a magnitude");
        }
        break;
    default:
        throw std::invalid_argument("Unsupported Pazaak card behavior");
    }
}

CardDefinition CardDefinition::fixedPositive(int magnitude) {
    return CardDefinition(CardBehavior::FixedPositive, magnitude);
}

CardDefinition CardDefinition::fixedNegative(int magnitude) {
    return CardDefinition(CardBehavior::FixedNegative, magnitude);
}

CardDefinition CardDefinition::signSelectable(int magnitude) {
    return CardDefinition(CardBehavior::SignSelectable, magnitude);
}

CardDefinition CardDefinition::doubleCard() {
    return CardDefinition(CardBehavior::Double, 0);
}

CardDefinition CardDefinition::flipTwoFour() {
    return CardDefinition(CardBehavior::FlipTwoFour, 0);
}

CardDefinition CardDefinition::flipThreeSix() {
    return CardDefinition(CardBehavior::FlipThreeSix, 0);
}

CardDefinition CardDefinition::tiebreaker() {
    return CardDefinition(CardBehavior::Tiebreaker, 1);
}

CardDefinition CardDefinition::valueChange() {
    return CardDefinition(CardBehavior::ValueChange, 0);
}

bool CardDefinition::operator==(const CardDefinition &other) const {
    return _behavior == other._behavior && _magnitude == other._magnitude;
}

MainDeck::MainDeck(std::vector<int> cards)
    : _cards(std::move(cards)) {
    if (_cards.size() != kMainDeckSize) {
        throw std::invalid_argument("A Pazaak main deck must contain exactly 40 cards");
    }

    std::array<size_t, 11> counts {};
    for (int value : _cards) {
        if (value < 1 || value > 10) {
            throw std::invalid_argument("Pazaak main-deck values must be between 1 and 10");
        }
        ++counts[value];
    }
    for (int value = 1; value <= 10; ++value) {
        if (counts[value] != 4) {
            throw std::invalid_argument("A Pazaak main deck must contain four copies of every value from 1 to 10");
        }
    }
}

MainDeck MainDeck::standardOrdered() {
    std::vector<int> cards;
    cards.reserve(kMainDeckSize);
    for (int value = 1; value <= 10; ++value) {
        for (int copy = 0; copy < 4; ++copy) {
            cards.push_back(value);
        }
    }
    return MainDeck(std::move(cards));
}

int MainDeck::draw() {
    return _cards[_next++];
}

bool MainDeck::operator==(const MainDeck &other) const {
    return _cards == other._cards && _next == other._next;
}

bool HandCard::operator==(const HandCard &other) const {
    return definition == other.definition && used == other.used;
}

ParticipantMatchState::ParticipantMatchState(const SideDeck &sideDeck, const HandSelection &selection) {
    std::array<bool, kSideDeckSize> selected {};
    _hand.reserve(kHandSize);
    for (size_t index : selection) {
        if (index >= sideDeck.size()) {
            throw std::invalid_argument("Pazaak hand selection index is outside the ten-card side deck");
        }
        if (selected[index]) {
            throw std::invalid_argument("Pazaak hand selection indices must be unique");
        }
        selected[index] = true;
        _hand.push_back({sideDeck[index], false});
    }
}

bool ParticipantMatchState::operator==(const ParticipantMatchState &other) const {
    return _hand == other._hand && _setWins == other._setWins;
}

PlayedCard::PlayedCard(PlayedCardSource source, int value, std::optional<size_t> handIndex)
    : _source(source),
      _value(value),
      _handIndex(handIndex) {
}

PlayedCard PlayedCard::mainDeck(int value) {
    return PlayedCard(PlayedCardSource::MainDeck, value, std::nullopt);
}

PlayedCard PlayedCard::hand(size_t handIndex, int value) {
    return PlayedCard(PlayedCardSource::Hand, value, handIndex);
}

bool PlayedCard::operator==(const PlayedCard &other) const {
    return _source == other._source && _value == other._value && _handIndex == other._handIndex;
}

int ParticipantSetState::total() const {
    return std::accumulate(_board.begin(), _board.end(), 0, [](int total, const PlayedCard &card) {
        return total + card.value();
    });
}

bool ParticipantSetState::operator==(const ParticipantSetState &other) const {
    return _board == other._board &&
           _stood == other._stood &&
           _busted == other._busted &&
           _nineCardPriority == other._nineCardPriority &&
           _hasTiebreaker == other._hasTiebreaker;
}

SetState::SetState(Participant firstParticipant)
    : _activeParticipant(firstParticipant) {
}

const ParticipantSetState &SetState::participant(Participant participant) const {
    if (!MatchState::isValidParticipant(participant)) {
        throw std::invalid_argument("Invalid Pazaak participant");
    }
    return _participants[MatchState::participantIndex(participant)];
}

bool SetState::operator==(const SetState &other) const {
    return _participants == other._participants &&
           _activeParticipant == other._activeParticipant &&
           _turnStage == other._turnStage &&
           _handCardPlayedThisTurn == other._handCardPlayedThisTurn &&
           _result == other._result;
}

MatchState::MatchState(MainDeck mainDeck,
                       ParticipantMatchState participantOne,
                       ParticipantMatchState participantTwo,
                       Participant firstParticipant)
    : _mainDeck(std::move(mainDeck)),
      _participants({std::move(participantOne), std::move(participantTwo)}),
      _set(firstParticipant) {
    if (!isValidParticipant(firstParticipant)) {
        throw std::invalid_argument("Invalid first Pazaak participant");
    }
}

const ParticipantMatchState &MatchState::participant(Participant participant) const {
    if (!isValidParticipant(participant)) {
        throw std::invalid_argument("Invalid Pazaak participant");
    }
    return _participants[participantIndex(participant)];
}

bool MatchState::isValidParticipant(Participant participant) {
    return participant == Participant::One || participant == Participant::Two;
}

size_t MatchState::participantIndex(Participant participant) {
    return participant == Participant::One ? 0 : 1;
}

Participant MatchState::otherParticipant(Participant participant) {
    return participant == Participant::One ? Participant::Two : Participant::One;
}

ParticipantSetState &MatchState::setParticipant(Participant participant) {
    return _set._participants[participantIndex(participant)];
}

ParticipantMatchState &MatchState::matchParticipant(Participant participant) {
    return _participants[participantIndex(participant)];
}

ActionError MatchState::validateActor(Participant actor) const {
    if (!isValidParticipant(actor)) {
        return ActionError::InvalidParticipant;
    }
    if (_result != MatchResult::InProgress) {
        return ActionError::MatchComplete;
    }
    if (_set._result != SetResult::InProgress) {
        return ActionError::SetComplete;
    }
    if (_set._participants[participantIndex(actor)]._nineCardPriority) {
        return ActionError::ParticipantFinished;
    }
    if (_set._participants[participantIndex(actor)]._stood) {
        return ActionError::ParticipantStanding;
    }
    if (_set._activeParticipant != actor) {
        return ActionError::NotActiveParticipant;
    }
    return ActionError::None;
}

ActionError MatchState::validateDraw(const DrawCommand &command) const {
    ActionError error = validateActor(command.actor);
    if (error != ActionError::None) {
        return error;
    }
    if (_set._turnStage != TurnStage::AwaitingDraw) {
        return ActionError::DrawAlreadyPerformed;
    }
    if (_set._participants[participantIndex(command.actor)]._board.size() >= kBoardSize) {
        return ActionError::BoardFull;
    }
    return ActionError::None;
}

ActionError MatchState::validatePlayHandCard(const PlayHandCardCommand &command) const {
    ActionError error = validateActor(command.actor);
    if (error != ActionError::None) {
        return error;
    }
    if (_set._turnStage != TurnStage::AwaitingAction) {
        return ActionError::MustDrawFirst;
    }
    if (_set._handCardPlayedThisTurn) {
        return ActionError::HandCardAlreadyPlayedThisTurn;
    }

    const ParticipantSetState &setParticipantState = _set._participants[participantIndex(command.actor)];
    if (setParticipantState._board.size() >= kBoardSize) {
        return ActionError::BoardFull;
    }

    const ParticipantMatchState &matchParticipantState = _participants[participantIndex(command.actor)];
    if (command.handIndex >= matchParticipantState._hand.size()) {
        return ActionError::InvalidHandIndex;
    }
    const HandCard &handCard = matchParticipantState._hand[command.handIndex];
    if (handCard.used) {
        return ActionError::HandCardUsed;
    }

    switch (handCard.definition.behavior()) {
    case CardBehavior::FixedPositive:
    case CardBehavior::FixedNegative:
    case CardBehavior::Double:
    case CardBehavior::FlipTwoFour:
    case CardBehavior::FlipThreeSix:
        if (command.sign) {
            return ActionError::SignNotAllowed;
        }
        if (command.value) {
            return ActionError::ValueNotAllowed;
        }
        if (handCard.definition.behavior() == CardBehavior::Double &&
            setParticipantState._board.empty()) {
            // Double has nothing to double with no preceding board card.
            return ActionError::NoPrecedingCard;
        }
        break;
    case CardBehavior::SignSelectable:
    case CardBehavior::Tiebreaker:
        if (command.value) {
            return ActionError::ValueNotAllowed;
        }
        if (!command.sign) {
            return ActionError::SignRequired;
        }
        if (*command.sign != CardSign::Positive && *command.sign != CardSign::Negative) {
            return ActionError::InvalidSign;
        }
        break;
    case CardBehavior::ValueChange:
        if (command.sign) {
            return ActionError::SignNotAllowed;
        }
        if (!command.value) {
            return ActionError::ValueRequired;
        }
        if (*command.value != 1 && *command.value != 2 &&
            *command.value != -1 && *command.value != -2) {
            return ActionError::InvalidValue;
        }
        break;
    default:
        return ActionError::InvalidSign;
    }
    return ActionError::None;
}

ActionError MatchState::validateEndTurn(const EndTurnCommand &command) const {
    ActionError error = validateActor(command.actor);
    if (error != ActionError::None) {
        return error;
    }
    return _set._turnStage == TurnStage::AwaitingAction ? ActionError::None : ActionError::MustDrawFirst;
}

ActionError MatchState::validateStand(const StandCommand &command) const {
    ActionError error = validateActor(command.actor);
    if (error != ActionError::None) {
        return error;
    }
    return _set._turnStage == TurnStage::AwaitingAction ? ActionError::None : ActionError::MustDrawFirst;
}

ActionError MatchState::validate(const Command &command) const {
    return std::visit([this](const auto &typedCommand) {
        using CommandType = std::decay_t<decltype(typedCommand)>;
        if constexpr (std::is_same_v<CommandType, DrawCommand>) {
            return validateDraw(typedCommand);
        } else if constexpr (std::is_same_v<CommandType, PlayHandCardCommand>) {
            return validatePlayHandCard(typedCommand);
        } else if constexpr (std::is_same_v<CommandType, EndTurnCommand>) {
            return validateEndTurn(typedCommand);
        } else {
            return validateStand(typedCommand);
        }
    },
                      command);
}

LegalActions MatchState::legalActions(Participant participant) const {
    LegalActions actions;
    actions.canDraw = validate(DrawCommand {participant}) == ActionError::None;
    actions.canEndTurn = validate(EndTurnCommand {participant}) == ActionError::None;
    actions.canStand = validate(StandCommand {participant}) == ActionError::None;

    if (!isValidParticipant(participant)) {
        return actions;
    }
    const auto &hand = _participants[participantIndex(participant)]._hand;
    for (size_t index = 0; index < hand.size(); ++index) {
        const CardDefinition &definition = hand[index].definition;
        bool playable = false;
        if (definition.isValueSelectable()) {
            // Any of the four legal values would validate the same way.
            playable = validate(PlayHandCardCommand {participant, index, std::nullopt, 1}) == ActionError::None;
        } else if (definition.isSignSelectable()) {
            playable = validate(PlayHandCardCommand {participant, index, CardSign::Positive}) == ActionError::None ||
                       validate(PlayHandCardCommand {participant, index, CardSign::Negative}) == ActionError::None;
        } else {
            playable = validate(PlayHandCardCommand {participant, index, std::nullopt}) == ActionError::None;
        }
        if (playable) {
            actions.playableHandCards.push_back(index);
        }
    }
    return actions;
}

ActionError MatchState::apply(const Command &command) {
    ActionError error = validate(command);
    if (error != ActionError::None) {
        return error;
    }

    MatchState next(*this);
    next.applyValidated(command);
    *this = std::move(next);
    return ActionError::None;
}

void MatchState::applyValidated(const Command &command) {
    std::visit([this](const auto &typedCommand) {
        using CommandType = std::decay_t<decltype(typedCommand)>;
        if constexpr (std::is_same_v<CommandType, DrawCommand>) {
            setParticipant(typedCommand.actor)._board.push_back(PlayedCard::mainDeck(_mainDeck.draw()));
            _set._turnStage = TurnStage::AwaitingAction;
            resolveAutomaticStand(typedCommand.actor);
        } else if constexpr (std::is_same_v<CommandType, PlayHandCardCommand>) {
            playHandCard(typedCommand);
        } else if constexpr (std::is_same_v<CommandType, EndTurnCommand>) {
            resolveTurn(typedCommand.actor, false);
        } else {
            resolveTurn(typedCommand.actor, true);
        }
    },
               command);
}

void MatchState::playHandCard(const PlayHandCardCommand &command) {
    HandCard &handCard = matchParticipant(command.actor)._hand[command.handIndex];
    ParticipantSetState &actorState = setParticipant(command.actor);
    const CardDefinition &definition = handCard.definition;

    int playedValue = 0;
    switch (definition.behavior()) {
    case CardBehavior::FixedPositive:
        playedValue = definition.magnitude();
        break;
    case CardBehavior::FixedNegative:
        playedValue = -definition.magnitude();
        break;
    case CardBehavior::SignSelectable:
        playedValue = *command.sign == CardSign::Negative
                          ? -definition.magnitude()
                          : definition.magnitude();
        break;
    case CardBehavior::Tiebreaker:
        // Contributes a selectable +/-1 and marks the set for tie resolution.
        playedValue = *command.sign == CardSign::Negative ? -1 : 1;
        actorState._hasTiebreaker = true;
        break;
    case CardBehavior::ValueChange:
        // The signed value was validated to be one of +1, +2, -1, -2.
        playedValue = *command.value;
        break;
    case CardBehavior::Double:
        // Doubles the immediately preceding board card by contributing its
        // value again as a distinct card. The board is non-empty (validated).
        playedValue = actorState._board.back().value();
        break;
    case CardBehavior::FlipTwoFour:
        // Flip only the owner's positive 2s and 4s to negative.
        for (PlayedCard &card : actorState._board) {
            if (card._value == 2) {
                card._value = -2;
            } else if (card._value == 4) {
                card._value = -4;
            }
        }
        playedValue = 0;
        break;
    case CardBehavior::FlipThreeSix:
        for (PlayedCard &card : actorState._board) {
            if (card._value == 3) {
                card._value = -3;
            } else if (card._value == 6) {
                card._value = -6;
            }
        }
        playedValue = 0;
        break;
    }

    actorState._board.push_back(PlayedCard::hand(command.handIndex, playedValue));
    handCard.used = true;
    _set._handCardPlayedThisTurn = true;
    resolveAutomaticStand(command.actor);
}

void MatchState::resolveAutomaticStand(Participant actor) {
    if (_set._result == SetResult::InProgress &&
        setParticipant(actor).total() == kTargetTotal) {
        // resolveTurn owns bust and nine-card precedence. In particular, a
        // legal ninth card at 20 becomes nine-card priority, not an ordinary
        // stand.
        resolveTurn(actor, true);
    }
}

void MatchState::resolveTurn(Participant actor, bool stand) {
    ParticipantSetState &actorState = setParticipant(actor);
    Participant other = otherParticipant(actor);
    ParticipantSetState &otherState = setParticipant(other);

    if (actorState.total() > kTargetTotal) {
        actorState._busted = true;
        completeSet(actor == Participant::One ? SetResult::ParticipantTwoWon : SetResult::ParticipantOneWon);
        return;
    }

    if (actorState._board.size() == kBoardSize) {
        actorState._nineCardPriority = true;
        if (otherState._nineCardPriority) {
            completeSet(SetResult::UnresolvedBothNine);
        } else if (otherState.finished()) {
            completeSet(actor == Participant::One ? SetResult::ParticipantOneWon : SetResult::ParticipantTwoWon);
        } else {
            _set._activeParticipant = other;
            _set._turnStage = TurnStage::AwaitingDraw;
            _set._handCardPlayedThisTurn = false;
        }
        return;
    }

    if (stand) {
        actorState._stood = true;
    }

    if (actorState._stood && otherState._nineCardPriority) {
        completeSet(other == Participant::One ? SetResult::ParticipantOneWon : SetResult::ParticipantTwoWon);
        return;
    }
    if (actorState._stood && otherState._stood) {
        if (actorState.total() == otherState.total()) {
            // Tiebreaker precedence applies only to an otherwise-legal tie
            // (bust and nine-card priority are resolved earlier). A single
            // committed Tiebreaker wins; if neither or both hold one it stays
            // a tie.
            if (actorState._hasTiebreaker && !otherState._hasTiebreaker) {
                completeSet(actor == Participant::One ? SetResult::ParticipantOneWon : SetResult::ParticipantTwoWon);
            } else if (otherState._hasTiebreaker && !actorState._hasTiebreaker) {
                completeSet(other == Participant::One ? SetResult::ParticipantOneWon : SetResult::ParticipantTwoWon);
            } else {
                completeSet(SetResult::Tie);
            }
        } else {
            Participant winner = actorState.total() > otherState.total() ? actor : other;
            completeSet(winner == Participant::One ? SetResult::ParticipantOneWon : SetResult::ParticipantTwoWon);
        }
        return;
    }

    _set._activeParticipant = otherState.finished() ? actor : other;
    _set._turnStage = TurnStage::AwaitingDraw;
    _set._handCardPlayedThisTurn = false;
}

void MatchState::completeSet(SetResult result) {
    if (_set._result != SetResult::InProgress) {
        return;
    }
    _set._result = result;
    _set._turnStage = TurnStage::Complete;
    if (result == SetResult::Tie || result == SetResult::UnresolvedBothNine) {
        return;
    }

    Participant winner = result == SetResult::ParticipantOneWon ? Participant::One : Participant::Two;
    ParticipantMatchState &winnerState = matchParticipant(winner);
    if (winnerState._setWins < kSetWinsForMatch) {
        ++winnerState._setWins;
    }
    if (winnerState._setWins == kSetWinsForMatch) {
        _result = winner == Participant::One ? MatchResult::ParticipantOneWon : MatchResult::ParticipantTwoWon;
    }
}

ActionError MatchState::startNextSet(MainDeck mainDeck, Participant firstParticipant) {
    if (!isValidParticipant(firstParticipant)) {
        return ActionError::InvalidParticipant;
    }
    if (_result != MatchResult::InProgress) {
        return ActionError::MatchComplete;
    }
    if (_set._result == SetResult::UnresolvedBothNine) {
        return ActionError::SetUnresolved;
    }
    if (_set._result == SetResult::InProgress) {
        return ActionError::SetInProgress;
    }

    MatchState next(*this);
    next._mainDeck = std::move(mainDeck);
    next._set = SetState(firstParticipant);
    *this = std::move(next);
    return ActionError::None;
}

bool MatchState::operator==(const MatchState &other) const {
    return _mainDeck == other._mainDeck &&
           _participants == other._participants &&
           _set == other._set &&
           _result == other._result;
}

} // namespace pazaak

} // namespace game

} // namespace reone
