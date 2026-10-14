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

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <deque>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "runtimeref.h"
#include "savedruntime.h"
#include "types.h"

namespace reone {

namespace game {

struct ServicesView;

class AttackBuffer;
class Creature;
class Game;
class Object;

/** The type numbers of saved command parameters, as SavedActionParameter keeps them. */
constexpr uint32_t kSavedIntegerParameter = static_cast<uint32_t>(SavedActionParameterType::Integer);
constexpr uint32_t kSavedFloatParameter = static_cast<uint32_t>(SavedActionParameterType::Float);
constexpr uint32_t kSavedObjectParameter = static_cast<uint32_t>(SavedActionParameterType::Object);

class Action : boost::noncopyable {
public:
    virtual ~Action() = default;

    static bool classof(Action *from) {
        return true;
    }

    /**
     * Execute the action. This function is called every update until it is
     * either complete(), or cancelled, or the actor it belongs to is dead.
     */
    virtual void execute(std::shared_ptr<Action> self, Object &actor, float dt);

    /**
     * Perform cancellation cleanup while the node is still linked. Command
     * clearing erases the node only if this returns true; forced teardown uses
     * its own policy.
     */
    virtual bool cancel(std::shared_ptr<Action> self, Object &actor) { return true; }

    /** Ordinary queue insertion, distinct from combat-round registration. */
    virtual void onQueued(Object &actor) {}
    /** Retire attack-record state without forcing a surviving ordinary action away. */
    virtual void retireCombatRound() {}
    /** Strip the special-attack type from every attack record of the current round. */
    virtual void clearSpecialAttacks() {}
    /** Current physical record, distinct from the last-used feat. */
    virtual int currentCombatAttackType() const { return 0; }
    /** Result held by the current physical record. */
    virtual AttackResultType currentCombatAttackResult() const { return AttackResultType::Invalid; }
    /** The attack records of this action's round, when it runs one. */
    virtual std::optional<std::reference_wrapper<AttackBuffer>> combatAttacks() { return std::nullopt; }
    /**
     * The command this action carries out. A wrapper keeps its queue
     * identity while it exposes the command it runs.
     */
    virtual Action &combatAction() { return *this; }
    virtual const Action &combatAction() const { return *this; }
    /** The action keeps its round from ending. */
    virtual bool holdsCombatRound() const { return false; }
    /** The action has taken its round's one attack, cast or item use. */
    virtual bool tookRoundAction() const { return false; }
    /**
     * The command runs inside a round its creature is still in, rather than
     * starting the round afresh.
     */
    virtual bool joinsRunningRound() const { return false; }
    /** The round of this action ends without its end-of-round script. */
    virtual bool suppressesEndRoundScript() const { return _cutsceneAttack; }
    bool isCutsceneAttack() const { return _cutsceneAttack; }
    void setCutsceneAttack(bool value) { _cutsceneAttack = value; }

    /** Clearing leaves an action that is not clearable running; only teardown removes it. */
    bool isClearable() const { return _clearable; }
    void setClearable(bool value) { _clearable = value; }

    /**
     * Actions must call complete() once they are done. Completed actions are
     * removed from the action queue, allowing subsequent actions to execute.
     */
    void complete() { _completed = true; }

    /**
     * Locked action cannot be cancelled, unless the creature it belongs to is dead.
     */
    bool locked() const { return _locked; }

    /**
     * Lock the action to prevent it from being cancelled. AI scripts may
     * request to cancel all actions. Actions that are already in progress
     * (attack, open lock, etc.) should be locked.
     */
    void lock() { _locked = true; }

    ActionType type() const { return _type; }
    /** Ordinary action ID as saved, distinct from ActionType. */
    virtual uint32_t serializedActionId() const;

    bool isUserAction() const { return _userAction; }
    bool isCompleted() const { return _completed; }
    bool isCancelled() const { return _cancelled; }

    void setUserAction(bool val) { _userAction = val; }
    void markCancelled() { _cancelled = true; }

    /** The action was issued by a scheduled round entry. */
    bool isScheduledCommand() const { return _scheduledCommand; }
    void setScheduledCommand(bool value) { _scheduledCommand = value; }
    /** The action was restored from \p record of a save. */
    void attachSavedAction(SavedActionRecord record) {
        _scheduledCommand = record.scheduled;
        _savedAction = std::move(record);
    }
    /** The saved record the action was restored from, if it was. */
    const std::optional<SavedActionRecord> &originalSavedAction() const {
        return _savedAction;
    }
    /**
     * The action as a save records it. An action that keeps no state of its
     * own beyond its saved parameters is saved as it was restored.
     */
    virtual std::optional<SavedActionRecord> saveFacingState() const {
        return _savedAction;
    }

    /**
     * True while every non-owning gameplay object used by this action still
     * denotes the exact live incarnation captured by the action.
     */
    bool runtimeDependenciesLive() const;

protected:
    const float kDistanceWalk = 4.0f;

    Game &_game;
    ServicesView &_services;
    ActionType _type;

    bool _userAction {false};
    bool _completed {false};
    bool _cancelled {false};
    bool _locked {false};
    bool _clearable {true};
    bool _cutsceneAttack {false};
    bool _scheduledCommand {false};
    std::optional<SavedActionRecord> _savedAction;
    std::vector<RuntimeObjectRef<Object>> _runtimeDependencies;

    void requireRuntimeObject(const std::shared_ptr<Object> &object);

    Action(
        Game &game,
        ServicesView &services,
        ActionType type) :
        _game(game),
        _services(services),
        _type(type) {
    }
};

/**
 * Returns true for attacks, feats, some spells (force powers, grenades).
 */
bool isHostileAction(Action &action);

/**
 * The number scripts know a queued action by (the ACTION_* constants), for an
 * ordinary action ID; nothing for an action scripts cannot name.
 */
std::optional<int> scriptActionNumber(uint32_t actionId, bool tsl);

/**
 * One entry of an action queue: the action, its saved action ID and its
 * group. The entry's identity is not the identity of its action, which a
 * round may replace.
 */
struct ActionQueueNode {
    std::shared_ptr<Action> action;
    uint32_t actionId {static_cast<uint32_t>(SavedActionType::Invalid)};
    /** The action group the node belongs to. */
    int groupId {0};

    SavedActionType type() const { return static_cast<SavedActionType>(actionId); }
};

/**
 * An object's queue of actions. The node list is authoritative: execution,
 * saving and group queries use the nodes, while iteration yields the actions
 * of the nodes that hold one.
 */
class OrdinaryActionQueue {
public:
    using Node = std::shared_ptr<ActionQueueNode>;
    using Nodes = std::deque<Node>;

    class const_iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::shared_ptr<Action>;
        using difference_type = std::ptrdiff_t;
        using pointer = const value_type *;
        using reference = const value_type &;

        const_iterator() = default;
        const_iterator(Nodes::const_iterator position, Nodes::const_iterator end) :
            _position(position), _end(end) { skipEmpty(); }
        reference operator*() const { return (*_position)->action; }
        pointer operator->() const { return &(*_position)->action; }
        const_iterator &operator++() {
            ++_position;
            skipEmpty();
            return *this;
        }
        const_iterator operator++(int) {
            auto old = *this;
            ++*this;
            return old;
        }
        bool operator==(const const_iterator &other) const { return _position == other._position; }
        bool operator!=(const const_iterator &other) const { return !(*this == other); }

    private:
        Nodes::const_iterator _position;
        Nodes::const_iterator _end;
        void skipEmpty() {
            while (_position != _end && !(*_position)->action) {
                ++_position;
            }
        }
    };

    const_iterator begin() const { return {nodes.begin(), nodes.end()}; }
    const_iterator end() const { return {nodes.end(), nodes.end()}; }
    bool empty() const { return begin() == end(); }
    size_t size() const { return static_cast<size_t>(std::distance(begin(), end())); }
    const std::shared_ptr<Action> &front() const {
        assert(!empty());
        return *begin();
    }
    const std::shared_ptr<Action> &back() const {
        auto position = std::find_if(nodes.rbegin(), nodes.rend(),
                                     [](const auto &node) { return node->action != nullptr; });
        assert(position != nodes.rend());
        return (*position)->action;
    }
    const std::shared_ptr<Action> &operator[](size_t index) const {
        auto position = begin();
        while (index != 0 && position != end()) {
            --index;
            ++position;
        }
        assert(position != end());
        return *position;
    }
    bool operator==(const OrdinaryActionQueue &other) const {
        return std::equal(begin(), end(), other.begin(), other.end());
    }
    bool operator!=(const OrdinaryActionQueue &other) const { return !(*this == other); }
    void clear() { nodes.clear(); }

    /** Allocates a new action group. */
    int newGroup() {
        _lastGroup = _nextGroup++;
        return _lastGroup;
    }
    /** The action group allocated last. */
    int lastGroup() const { return _lastGroup; }

    Nodes nodes;

private:
    int _nextGroup {0};
    int _lastGroup {0};
};

namespace detail {

/** The commands that decide whether their action group goes: those scripts can name. */
inline bool isAcceptableGroupAction(uint32_t action, bool tsl) {
    return scriptActionNumber(action, tsl).has_value();
}

/**
 * Classify the first contiguous run of each queried group using its last acceptable node.
 * \p accept decides from that action ID and node whether the group goes. Group removal
 * deletes all nodes with that identity; iteration resumes at group index one.
 */
template <class Node, class Group, class ActionId, class Accept, class Remove>
void discardCombatActionGroups(std::vector<Node> &nodes, bool tsl,
                               Group group, ActionId actionId, Accept accept, Remove remove) {
    auto countGroups = [&]() {
        size_t count = 0;
        for (size_t i = 0; i < nodes.size(); ++i)
            if (i == 0 || group(nodes[i]) != group(nodes[i - 1])) {
                ++count;
            }
        return count;
    };
    const size_t originalCount = countGroups();
    for (size_t index = 0; index < originalCount; ++index) {
        size_t start = 0, current = 0;
        while (start < nodes.size() && current < index) {
            const auto key = group(nodes[start]);
            do {
                ++start;
            } while (start < nodes.size() && group(nodes[start]) == key);
            ++current;
        }
        if (start == nodes.size()) {
            continue;
        }
        const auto key = group(nodes[start]);
        auto first = std::find_if(nodes.begin(), nodes.end(),
                                  [&](const auto &node) { return group(node) == key; });
        uint32_t selected = static_cast<uint32_t>(SavedActionType::Invalid);
        auto representative = first;
        for (auto it = first; it != nodes.end() && group(*it) == key; ++it) {
            const auto id = actionId(*it);
            if (isAcceptableGroupAction(id, tsl)) {
                selected = id;
                representative = it;
            }
        }
        if (selected == static_cast<uint32_t>(SavedActionType::Invalid) || !accept(selected, *representative)) {
            continue;
        }
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const auto &node) {
                        if (group(node) != key) {
                            return false;
                        }
                        remove(node);
                        return true;
                    }),
                    nodes.end());
        index = 0;
    }
}

} // namespace detail

} // namespace game

} // namespace reone
