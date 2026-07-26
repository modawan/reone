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

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace reone {

namespace game {

namespace pazaak {

constexpr int kTargetTotal = 20;
constexpr size_t kMainDeckSize = 40;
constexpr size_t kSideDeckSize = 10;
constexpr size_t kHandSize = 4;
constexpr size_t kBoardSize = 9;
constexpr uint8_t kSetWinsForMatch = 3;

enum class Participant {
    One,
    Two,
};

enum class CardBehavior {
    FixedPositive,
    FixedNegative,
    SignSelectable,
};

enum class CardSign {
    Positive,
    Negative,
};

class CardDefinition {
public:
    CardDefinition(CardBehavior behavior, int magnitude);

    static CardDefinition fixedPositive(int magnitude);
    static CardDefinition fixedNegative(int magnitude);
    static CardDefinition signSelectable(int magnitude);

    CardBehavior behavior() const { return _behavior; }
    int magnitude() const { return _magnitude; }

    bool operator==(const CardDefinition &other) const;

private:
    CardBehavior _behavior;
    int _magnitude;
};

using SideDeck = std::array<CardDefinition, kSideDeckSize>;
using HandSelection = std::array<size_t, kHandSize>;

class MainDeck {
public:
    explicit MainDeck(std::vector<int> cards);

    static MainDeck standardOrdered();

    const std::vector<int> &cards() const { return _cards; }

    bool operator==(const MainDeck &other) const;

private:
    friend class MatchState;

    int draw();

    std::vector<int> _cards;
    size_t _next {0};
};

struct HandCard {
    CardDefinition definition;
    bool used {false};

    bool operator==(const HandCard &other) const;
};

class ParticipantMatchState {
public:
    ParticipantMatchState(const SideDeck &sideDeck, const HandSelection &selection);

    const std::vector<HandCard> &hand() const { return _hand; }
    uint8_t setWins() const { return _setWins; }

    bool operator==(const ParticipantMatchState &other) const;

private:
    friend class MatchState;

    std::vector<HandCard> _hand;
    uint8_t _setWins {0};
};

enum class PlayedCardSource {
    MainDeck,
    Hand,
};

class PlayedCard {
public:
    PlayedCardSource source() const { return _source; }
    int value() const { return _value; }
    const std::optional<size_t> &handIndex() const { return _handIndex; }

    bool operator==(const PlayedCard &other) const;

private:
    friend class MatchState;

    static PlayedCard mainDeck(int value);
    static PlayedCard hand(size_t handIndex, int value);

    PlayedCard(PlayedCardSource source, int value, std::optional<size_t> handIndex);

    PlayedCardSource _source;
    int _value;
    std::optional<size_t> _handIndex;
};

class ParticipantSetState {
public:
    const std::vector<PlayedCard> &board() const { return _board; }
    bool stood() const { return _stood; }
    bool busted() const { return _busted; }
    bool hasNineCardPriority() const { return _nineCardPriority; }
    bool finished() const { return _stood || _busted || _nineCardPriority; }
    int total() const;

    bool operator==(const ParticipantSetState &other) const;

private:
    friend class MatchState;

    std::vector<PlayedCard> _board;
    bool _stood {false};
    bool _busted {false};
    bool _nineCardPriority {false};
};

enum class TurnStage {
    AwaitingDraw,
    AwaitingAction,
    Complete,
};

enum class SetResult {
    InProgress,
    Tie,
    UnresolvedBothNine,
    ParticipantOneWon,
    ParticipantTwoWon,
};

class SetState {
public:
    /// Invalid participant values are programmer misuse and throw std::invalid_argument.
    const ParticipantSetState &participant(Participant participant) const;
    Participant activeParticipant() const { return _activeParticipant; }
    TurnStage turnStage() const { return _turnStage; }
    bool handCardPlayedThisTurn() const { return _handCardPlayedThisTurn; }
    SetResult result() const { return _result; }

    bool operator==(const SetState &other) const;

private:
    friend class MatchState;

    explicit SetState(Participant firstParticipant);

    std::array<ParticipantSetState, 2> _participants;
    Participant _activeParticipant;
    TurnStage _turnStage {TurnStage::AwaitingDraw};
    bool _handCardPlayedThisTurn {false};
    SetResult _result {SetResult::InProgress};
};

enum class MatchResult {
    InProgress,
    ParticipantOneWon,
    ParticipantTwoWon,
};

struct DrawCommand {
    Participant actor;
};

struct PlayHandCardCommand {
    Participant actor;
    size_t handIndex;
    std::optional<CardSign> sign;
};

struct EndTurnCommand {
    Participant actor;
};

struct StandCommand {
    Participant actor;
};

using Command = std::variant<DrawCommand, PlayHandCardCommand, EndTurnCommand, StandCommand>;

enum class ActionError {
    None,
    InvalidParticipant,
    MatchComplete,
    SetComplete,
    ParticipantFinished,
    ParticipantStanding,
    NotActiveParticipant,
    MustDrawFirst,
    DrawAlreadyPerformed,
    BoardFull,
    InvalidHandIndex,
    HandCardUsed,
    HandCardAlreadyPlayedThisTurn,
    SignRequired,
    SignNotAllowed,
    InvalidSign,
    SetInProgress,
    SetUnresolved,
};

struct LegalActions {
    bool canDraw {false};
    bool canEndTurn {false};
    bool canStand {false};
    std::vector<size_t> playableHandCards;
};

class MatchState {
public:
    MatchState(MainDeck mainDeck,
               ParticipantMatchState participantOne,
               ParticipantMatchState participantTwo,
               Participant firstParticipant);

    const MainDeck &mainDeck() const { return _mainDeck; }
    /// Invalid participant values are programmer misuse and throw std::invalid_argument.
    const ParticipantMatchState &participant(Participant participant) const;
    const SetState &set() const { return _set; }
    MatchResult result() const { return _result; }

    ActionError validate(const Command &command) const;
    LegalActions legalActions(Participant participant) const;
    ActionError apply(const Command &command);

    ActionError startNextSet(MainDeck mainDeck, Participant firstParticipant);

    bool operator==(const MatchState &other) const;

private:
    friend class SetState;

    static bool isValidParticipant(Participant participant);
    static size_t participantIndex(Participant participant);
    static Participant otherParticipant(Participant participant);

    ParticipantSetState &setParticipant(Participant participant);
    ParticipantMatchState &matchParticipant(Participant participant);
    ActionError validateActor(Participant actor) const;
    ActionError validateDraw(const DrawCommand &command) const;
    ActionError validatePlayHandCard(const PlayHandCardCommand &command) const;
    ActionError validateEndTurn(const EndTurnCommand &command) const;
    ActionError validateStand(const StandCommand &command) const;
    void applyValidated(const Command &command);
    void resolveTurn(Participant actor, bool stand);
    void completeSet(SetResult result);

    MainDeck _mainDeck;
    std::array<ParticipantMatchState, 2> _participants;
    SetState _set;
    MatchResult _result {MatchResult::InProgress};
};

} // namespace pazaak

} // namespace game

} // namespace reone
