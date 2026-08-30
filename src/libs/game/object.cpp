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

#include "reone/game/object.h"

#include <sstream>
#include <typeinfo>

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/item.h"
#include "reone/game/script/savedsituation.h"
#include "reone/resource/provider/scripts.h"
#include "reone/game/room.h"
#include "reone/resource/gff.h"
#include "reone/system/exception/validation.h"
#include "reone/system/logutil.h"

using namespace reone::graphics;
using namespace reone::scene;

namespace reone {

namespace game {

static constexpr float kKeepPathDuration = 1000.0f;
static constexpr float kDefaultMaxObjectDistance = 2.0f;
static constexpr float kMaxConversationDistance = 4.0f;
static constexpr float kDistanceWalk = 4.0f;

void Object::deserialize(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    if (gff.readString(_tag, "Tag")) {
        boost::to_lower(_tag);
    }

    // FIXME: not all of these properties are shared by all object subclasses.
    gff.readResRef(_blueprintResRef, "TemplateResRef");
    gff.readResRef(_conversation, "Conversation");
    gff.readResRef(_onHeartbeat, "ScriptHeartbeat");
    gff.readResRef(_onUserDefined, "ScriptUserDefine");
    gff.readBool(_minOneHP, "Min1HP");
    gff.readBool(_plot, "Plot");
    gff.readBool(_commandable, "Commandable");
    gff.readBool(_interruptable, "Interruptable");
    gff.readShort(_hitPoints, "HitPoints");
    gff.readShort(_maxHitPoints, "MaxHitPoints");
    gff.readShort(_currentHitPoints, "CurrentHitPoints");

    gff.readFloat(_position[0], "X");
    gff.readFloat(_position[0], "XPosition");
    gff.readFloat(_position[1], "Y");
    gff.readFloat(_position[1], "YPosition");
    gff.readFloat(_position[2], "Z");
    gff.readFloat(_position[2], "ZPosition");

    {
        float cosine, sine;
        if (gff.readFloat(cosine, "XOrientation") && gff.readFloat(sine, "YOrientation")) {
            _orientation = glm::quat(glm::vec3(0.0f, 0.0f, -glm::atan(cosine, sine)));
        }

        float bearing;
        if (gff.readFloat(bearing, "Bearing")) {
            _orientation = glm::quat(glm::vec3(0.0f, 0.0f, bearing));
        }
    }

    if (_type != ObjectType::Placeable && _type != ObjectType::Store &&
        _type != ObjectType::Item && dynamic_cast<Creature *>(this) == nullptr) {
        deserializeOwnedItems(
            gff, identityContext, SaveRecordOriginKind::ContainedItem);
    }
    deserializeRuntimeState(gff, identityContext);
}

std::vector<std::shared_ptr<Object>> Object::ownedRuntimeObjects() const {
    return {_items.begin(), _items.end()};
}

void Object::deserializeOwnedItems(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    SaveRecordOriginKind originKind,
    bool forceDropable,
    std::string originOwner) {
    if (!identityContext.isSerializedState()) {
        if (!gff.has("ItemList")) return;
        std::vector<std::shared_ptr<Object>> obsolete(
            _items.begin(), _items.end());
        std::vector<std::shared_ptr<Item>> replacement;
        ItemAttributes replacementAttributes;
        auto creature = dynamic_cast<Creature *>(this);
        _game.replaceRuntimeObjectGraph(
            obsolete,
            [&]() {
                for (const auto &itemGff : gff.getList("ItemList")) {
                    auto item = _game.newOwnedItem(*itemGff, identityContext);
                    if (forceDropable) item->setDropable(true);
                    item->setOwner(_id);
                    replacement.push_back(std::move(item));
                }
                if (creature) {
                    for (const auto &item : replacement) {
                        replacementAttributes.addItem(item, _services.game);
                    }
                }
            },
            [&]() noexcept {
                _items = std::move(replacement);
                if (creature) {
                    creature->itemAttributes() =
                        std::move(replacementAttributes);
                }
            });
        return;
    }

    std::vector<std::shared_ptr<Object>> obsolete(_items.begin(), _items.end());
    std::vector<std::shared_ptr<Item>> replacement;
    ItemAttributes replacementAttributes;
    auto creature = dynamic_cast<Creature *>(this);
    _game.replaceRuntimeObjectGraph(
        obsolete,
        [&]() {
            for (const auto &itemGff : gff.getList("ItemList")) {
                auto item = _game.newOwnedItem(*itemGff, identityContext);
                item->captureSaveRecord(
                    *itemGff,
                    identityContext,
                    {originKind,
                     originOwner.empty() ? std::to_string(_id) : originOwner});
                if (forceDropable) {
                    item->setDropable(true);
                }
                item->setOwner(_id);
                replacement.push_back(std::move(item));
            }
            if (creature) {
                for (const auto &item : replacement) {
                    replacementAttributes.addItem(item, _services.game);
                }
            }
        },
        [&]() noexcept {
            _items = std::move(replacement);
            if (creature) {
                creature->itemAttributes() =
                    std::move(replacementAttributes);
            }
        });
}

void Object::update(float dt) {
    if (!isRuntimeLive()) {
        return;
    }
    updateActions(dt);
    updateEffects(dt);
    if (!_dead && canExecuteActions()) {
        executeActions(dt);
    }
    if (_sceneNode && _sceneNode->type() == SceneNodeType::Model) {
        std::static_pointer_cast<ModelSceneNode>(_sceneNode)->setPickable(isSelectable());
    }
}

bool Object::getLocalBoolean(int index) const {
    auto it = _localBooleans.find(index);
    return it != _localBooleans.end() ? it->second : false;
}

int Object::getLocalNumber(int index) const {
    auto it = _localNumbers.find(index);
    return it != _localNumbers.end() ? it->second : 0;
}

void Object::setLocalBoolean(int index, bool value) {
    _localBooleans[index] = value;
}

void Object::setLocalNumber(int index, int value) {
    _localNumbers[index] = value;
}
void Object::deserializeRuntimeState(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {

    _localBooleans.clear();
    _localNumbers.clear();
    if (auto variables = gff.findStruct("SWVarTable")) {
        const auto bits = variables->getList("BitArray");
        for (size_t word = 0; word < std::min<size_t>(bits.size(), 5); ++word) {
            uint32_t value = bits[word]->getUint("Variable");
            for (int bit = 0; bit < 32; ++bit) {
                if ((value & (1u << bit)) != 0) {
                    _localBooleans[static_cast<int>(word * 32 + bit)] = true;
                }
            }
        }

        const auto bytes = variables->getList("ByteArray");
        for (size_t index = 0; index < std::min<size_t>(bytes.size(), 32); ++index) {
            uint8_t value = 0;
            if (bytes[index]->readByte(value, "Variable") && value != 0) {
                _localNumbers[static_cast<int>(index)] = value;
            }
        }
    }

    _savedEffects.clear();
    _savedActionQueue = SavedActionQueue {};
    _savedRuntimeIdentityContext = identityContext;
    _savedEffectReferencesBound.clear();
    _savedActionReferencesBound.clear();
    _savedRuntimeParsed = gff.has("EffectList") || gff.has("ActionList");
    _savedRuntimePublished = false;
    _loadedSaveActionSlots.clear();
    if (_savedRuntimeParsed) {
        for (const auto &effect : gff.getList("EffectList")) {
            _savedEffects.push_back(EffectInstance::fromGff(
                *effect, identityContext));
        }
        _savedActionQueue = SavedActionQueue::fromGff(
            gff, identityContext);
        _effects.clear();
        _actions.clear();
        _delayed.clear();
        _executingAction.reset();
    }

    _savedReferenceIds.clear();
    _savedReferences.clear();
    static const std::array<std::string_view, 9> referenceFields {
        "AreaId",
        "CreatorId",
        "LastAttacker",
        "LastDamager",
        "LastHostileActor",
        "LastPerceived",
        "MasterID",
        "OwnerId",
        "TargetId",
    };
    for (auto field : referenceFields) {
        uint32_t id = 0;
        if (gff.readDword(id, field)) {
            _savedReferenceIds.emplace(std::string(field), id);
        }
    }

    size_t perceptionIndex = 0;
    for (const auto &perception : gff.getList("PerceptionList")) {
        uint32_t id = 0;
        if (perception->readDword(id, "ObjectId")) {
            _savedReferenceIds.emplace(
                "Perception/" + std::to_string(perceptionIndex), id);
        }
        ++perceptionIndex;
    }
}

void Object::bindSavedRuntimeState() {
    if (!_savedRuntimeParsed) {
        return;
    }
    for (auto &effect : _savedEffects) {
        _savedEffectReferencesBound.push_back(
            _game.bindEffectCreator(effect));
    }
    for (auto &action : _savedActionQueue.actions) {
        _savedActionReferencesBound.push_back(
            action.bindObjectReferences(_game));
    }
}

void Object::publishSavedRuntimeState() {
    if (!_savedRuntimeParsed || _savedRuntimePublished) {
        return;
    }
    SavedScriptSituationImporter importer(
        _game, _services.resource.scripts);

    for (size_t index = 0; index < _savedEffects.size(); ++index) {
        const auto &savedEffect = _savedEffects[index];
        if (index >= _savedEffectReferencesBound.size() ||
            !_savedEffectReferencesBound[index]) {
            continue;
        }
        EffectInstance effect(savedEffect);
        if (effect.durationType() == DurationType::Temporary) {
            auto remaining = _game.remainingEffectDuration(effect);
            if (!remaining || *remaining <= 0.0f) {
                continue;
            }
            effect.remainingDuration = *remaining;
        }
        if (effect.hasStableId()) {
            _game.importEffectId(effect.id);
        } else {
            effect.id = _game.allocateEffectId();
        }
        restoreEffect(std::move(effect));
    }

    for (size_t index = 0; index < _savedActionQueue.actions.size(); ++index) {
        const auto &savedAction = _savedActionQueue.actions[index];
        if (index >= _savedActionReferencesBound.size() ||
            !_savedActionReferencesBound[index]) {
            _loadedSaveActionSlots.push_back(
                LoadedSaveActionSlot {savedAction, {}, true});
            continue;
        }
        auto action = savedAction.toRuntimeAction(_game, &importer);
        if (action) {
            action->attachSavedAction(savedAction);
            addAction(action);
            _loadedSaveActionSlots.push_back(
                LoadedSaveActionSlot {savedAction, action, false});
        } else if (savedAction.executionSupport() ==
                   SavedExecutionSupport::RepresentableButUnsupported) {
            _loadedSaveActionSlots.push_back(
                LoadedSaveActionSlot {savedAction, {}, true});
        }
    }
    _savedRuntimePublished = true;
}

void Object::captureSaveRecord(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    SaveRecordOrigin origin) {
    std::optional<SerializedObjectIdentity> identity;
    uint32_t id = 0;
    if (identityContext.isSerializedState() &&
        gff.readDword(id, "ObjectId")) {
        identity = SerializedObjectIdentity {identityContext, id};
    }
    _saveRecordProvenance = SaveRecordProvenance {
        SaveGffShadow::capture(gff), std::move(origin), std::move(identity)};
}

void Object::assignSerializedObjectIdentity(
    const SerializedObjectIdentity &identity) {
    if (_saveRecordProvenance) {
        _saveRecordProvenance->identity = identity;
    } else {
        _saveRecordProvenance = SaveRecordProvenance {
            SaveGffShadow {}, SaveRecordOrigin {}, identity};
    }
}

std::vector<EffectInstance> Object::saveEffectSnapshot() const {
    // Later orchestration calls this at a stable synchronous frame boundary.
    return {_effects.begin(), _effects.end()};
}

std::vector<SavedActionRecord> Object::saveActionSnapshot() const {
    // Loaded slots retain original order. Executed/cancelled actions cease to
    // be live as soon as they leave (or complete within) the runtime queue.
    std::vector<SavedActionRecord> result;
    std::set<const Action *> represented;
    auto unsupportedAction = [this](const Action &action, size_t queueIndex) {
        std::ostringstream message;
        message << "live queued action has no save-facing representation"
                << ": ownerId=" << id()
                << " ownerType=" << static_cast<int>(type())
                << " ownerTag=\"" << tag() << '"'
                << " ownerBlueprint=\"" << blueprintResRef() << '"'
                << " queueIndex=" << queueIndex
                << " actionType=" << static_cast<int>(action.type())
                << " runtimeClass=" << typeid(action).name()
                << " provenance="
                << (action.originalSavedAction() ? "loaded" : "runtime-created");
        if (action.originalSavedAction()) {
            message << " savedActionId=" << action.originalSavedAction()->actionId
                    << " groupActionId="
                    << action.originalSavedAction()->groupActionId;
        }
        return ValidationException(message.str());
    };
    auto saveAction = [this](const Action &action, size_t queueIndex) {
        try {
            return action.saveFacingState();
        } catch (const std::exception &ex) {
            std::ostringstream message;
            message << ex.what()
                    << "; ownerId=" << id()
                    << " ownerType=" << static_cast<int>(type())
                    << " ownerClass=" << typeid(*this).name()
                    << " ownerTag=\"" << tag() << '\"'
                    << " ownerBlueprint=\"" << blueprintResRef() << '\"'
                    << " queueIndex=" << queueIndex
                    << " actionType=" << static_cast<int>(action.type())
                    << " runtimeClass=" << typeid(action).name()
                    << " actionProvenance="
                    << (action.originalSavedAction() ? "present" : "absent");
            if (action.originalSavedAction()) {
                message << " savedActionId=" << action.originalSavedAction()->actionId
                        << " groupActionId=" << action.originalSavedAction()->groupActionId;
            }
            throw ValidationException(message.str());
        }
    };
    for (const auto &slot : _loadedSaveActionSlots) {
        if (slot.unsupportedPending) {
            result.push_back(slot.original);
            continue;
        }
        auto action = slot.runtimeAction.lock();
        bool queued = action &&
                      std::find(_actions.begin(), _actions.end(), action) != _actions.end();
        if (!queued || action->isCompleted() || action->isCancelled()) {
            continue;
        }
        represented.insert(action.get());
        auto position = std::find(_actions.begin(), _actions.end(), action);
        auto queueIndex = static_cast<size_t>(
            std::distance(_actions.begin(), position));
        if (auto saved = saveAction(*action, queueIndex)) {
            result.push_back(std::move(*saved));
        } else {
            throw unsupportedAction(*action, queueIndex);
        }
    }
    for (size_t index = 0; index < _actions.size(); ++index) {
        const auto &action = _actions[index];
        if (represented.count(action.get()) != 0 ||
            action->isCompleted() || action->isCancelled()) {
            continue;
        }
        if (auto saved = saveAction(*action, index)) {
            result.push_back(std::move(*saved));
        } else {
            throw unsupportedAction(*action, index);
        }
    }
    return result;
}

void Object::retireAreaRuntimeState(
    const std::set<const Object *> &retainedObjects) {
    // The authoritative source snapshot was captured before this boundary.
    // Discard rather than cancel: cancellation callbacks are live gameplay and
    // must not mutate the already-frozen outgoing world.
    for (auto &action : _actions) {
        if (action) action->markCancelled();
    }
    _actions.clear();
    _delayed.clear();
    _executingAction.reset();
    _loadedSaveActionSlots.clear();
    _savedActionQueue = SavedActionQueue {};

    for (auto &effect : _effects) {
        if (effect.effect) {
            effect.effect->retireAreaRuntime(retainedObjects);
        }
        effect.retireAreaRuntimeBindings(retainedObjects);
    }
    _savedEffects.clear();
    _savedRuntimeParsed = false;
    _savedRuntimePublished = false;

    // Rebase object-local bindings exactly as effects are rebased. Master/owner
    // relations between retained session objects remain meaningful; every
    // outgoing Area binding retires. A2 separately owns the saved-graph
    // namespace, translation, and generation.
    std::map<std::string, uint32_t> retainedReferenceIds;
    std::map<std::string, RuntimeObjectRef<Object>> retainedReferences;
    for (const auto &[field, binding] : _savedReferences) {
        auto object = binding.resolve();
        if (!object || retainedObjects.count(object.get()) == 0) continue;
        retainedReferenceIds.emplace(field, object->id());
        retainedReferences.emplace(field, object);
    }
    _savedReferenceIds = std::move(retainedReferenceIds);
    _savedReferences = std::move(retainedReferences);
    _lastHostileActor = script::kObjectInvalid;
}

void Object::resolveSavedReferences(
    const std::function<std::shared_ptr<Object>(uint32_t)> &resolver) {
    _savedReferences.clear();
    for (const auto &[field, id] : _savedReferenceIds) {
        if (auto object = resolver(id)) {
            _savedReferences.emplace(field, object);
        }
    }
}

std::shared_ptr<Object> Object::savedReference(std::string_view field) const {
    auto found = _savedReferences.find(std::string(field));
    return found == _savedReferences.end() ? nullptr : found->second.resolve();
}

void Object::clearAllActions(bool force) {
    for (auto &slot : _loadedSaveActionSlots) {
        slot.unsupportedPending = false;
    }
    // If the current front action clears the queue while it is executing, keep
    // that action and its queued continuation alive instead of trimming from the
    // back and deleting the follow-up it is about to hand off to.
    if (!force) {
        auto executingAction = _executingAction.lock();
        if (executingAction) {
            while (!_actions.empty() && _actions.front() != executingAction) {
                const std::shared_ptr<Action> &action = _actions.front();
                if (action->locked()) {
                    break;
                }
                action->cancel(action, *this);
                action->markCancelled();
                _actions.pop_front();
            }
            if (!_actions.empty() && _actions.front() == executingAction) {
                return;
            }
        }
    }

    while (!_actions.empty()) {
        const std::shared_ptr<Action> &action = _actions.back();
        if (!force && action->locked()) {
            break;
        }
        _actions.back()->cancel(action, *this);
        action->markCancelled();
        _actions.pop_back();
    }
}

void Object::addAction(std::shared_ptr<Action> action) {
    if (!isRuntimeLive()) return;
    _actions.push_back(std::move(action));
}

void Object::addActionOnTop(std::shared_ptr<Action> action) {
    if (!isRuntimeLive()) return;
    _actions.push_front(std::move(action));
}

void Object::delayAction(std::shared_ptr<Action> action, float seconds) {
    if (!isRuntimeLive()) return;
    DelayedAction delayed;
    delayed.action = std::move(action);
    delayed.timer = std::make_unique<Timer>(seconds);
    _delayed.push_back(std::move(delayed));
}

void Object::updateActions(float dt) {
    if (isDead()) {
        clearAllActions(/*force=*/true);
        return;
    }
    removeCompletedActions();
    updateDelayedActions(dt);
}

void Object::removeCompletedActions() {
    while (true) {
        std::shared_ptr<Action> action(getCurrentAction());
        if (!action || !action->isCompleted())
            return;

        _actions.pop_front();
    }
}

void Object::updateDelayedActions(float dt) {
    // Iterate in reverse order, so addActionOnTop keeps the original order.
    for (auto delayed = _delayed.rbegin(), end = _delayed.rend(); delayed != end; ++delayed) {
        delayed->timer->update(dt);
        if (delayed->timer->elapsed()) {
            addActionOnTop(std::move(delayed->action));
        }
    }
    auto delayedToRemove = std::remove_if(
        _delayed.begin(),
        _delayed.end(),
        [](const DelayedAction &delayed) { return delayed.timer->elapsed(); });

    _delayed.erase(delayedToRemove, _delayed.end());
}

void Object::executeActions(float dt) {
    if (_actions.empty()) {
        return;
    }
    std::shared_ptr<Action> action(_actions.front());
    if (!action->runtimeDependenciesLive()) {
        action->complete();
        return;
    }
    _executingAction = action;
    try {
        action->execute(action, *this, dt);
    } catch (...) {
        _executingAction.reset();
        throw;
    }
    _executingAction.reset();
}

bool Object::hasUserActionsPending(const Action *excluded) const {
    // TODO: must only work during combat
    for (const auto &action : _actions) {
        if (action.get() != excluded && action->isUserAction()) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<Action> Object::getCurrentAction() const {
    return _actions.empty() ? nullptr : _actions.front();
}

std::shared_ptr<Item> Object::addItem(const std::string &resRef, int stackSize, bool dropable) {
    std::shared_ptr<Item> result;

    auto maybeItem = std::find_if(_items.begin(), _items.end(), [&resRef](auto &item) {
        return item->tag() == resRef;
    });
    if (maybeItem != _items.end()) {
        result = *maybeItem;
        int prevStackSize = result->stackSize();
        result->setStackSize(prevStackSize + stackSize);

    } else {
        std::vector<std::shared_ptr<Object>> noObsolete;
        std::vector<std::shared_ptr<Item>> replacement(_items);
        ItemAttributes replacementAttributes;
        auto creature = dyn_cast<Creature>(this);
        if (creature) replacementAttributes = creature->itemAttributes();
        _game.replaceRuntimeObjectGraph(
            noObsolete,
            [&]() {
                result = _game.newItemFromBlueprint(resRef);
                result->setStackSize(stackSize);
                result->setDropable(dropable);
                result->setOwner(_id);
                replacement.push_back(result);
                if (creature) {
                    replacementAttributes.addItem(result, _services.game);
                }
            },
            [&]() noexcept {
                _items = std::move(replacement);
                if (creature) {
                    creature->itemAttributes() =
                        std::move(replacementAttributes);
                }
            });
    }

    return result;
}

void Object::addItem(const std::shared_ptr<Item> &item) {
    if (!item || (!item->isRuntimeLive() && !item->isPresentationOnly())) {
        throw ValidationException("Cannot own a non-live runtime item");
    }
    if (item->owner() != 0 && item->owner() != script::kObjectInvalid &&
        item->owner() != _id) {
        throw ValidationException("Runtime item already has another owner");
    }
    auto maybeItem = std::find_if(_items.begin(), _items.end(), [&item](auto &entry) { return entry->tag() == item->tag(); });
    if (maybeItem != _items.end()) {
        // Re-adding an owned stack restores one consumed item; transferred stacks merge fully.
        int stackSize = *maybeItem == item ? 1 : item->stackSize();
        (*maybeItem)->setStackSize((*maybeItem)->stackSize() + stackSize);
        if (*maybeItem != item) {
            item->setOwner(0);
            if (item->isRuntimeLive()) {
                _game.destroyRuntimeObjectGraph(item);
            }
        }
    } else {
        std::vector<std::shared_ptr<Item>> replacement(_items);
        replacement.push_back(item);
        auto creature = dyn_cast<Creature>(this);
        ItemAttributes replacementAttributes;
        if (creature) {
            replacementAttributes = creature->itemAttributes();
            replacementAttributes.addItem(item, _services.game);
        }
        _items = std::move(replacement);
        item->setOwner(_id);
        if (creature) {
            creature->itemAttributes() = std::move(replacementAttributes);
        }
    }
}

bool Object::removeItem(const std::shared_ptr<Item> &item, bool &last) {
    auto maybeItem = find(_items.begin(), _items.end(), item);
    if (maybeItem == _items.end())
        return false;

    last = false;

    int stackSize = (*maybeItem)->stackSize();
    if (stackSize > 1) {
        (*maybeItem)->setStackSize(stackSize - 1);
    } else {
        last = true;
        _items.erase(maybeItem);
        item->setOwner(0);
        if (Creature *creature = dyn_cast<Creature>(this)) {
            creature->itemAttributes().removeItem(item);
        }
    }

    return true;
}

bool Object::removeItemStack(const std::shared_ptr<Item> &item) {
    auto maybeItem = find(_items.begin(), _items.end(), item);
    if (maybeItem == _items.end()) {
        return false;
    }

    _items.erase(maybeItem);
    if (Creature *creature = dyn_cast<Creature>(this)) {
        creature->itemAttributes().removeItem(item);
    }
    item->setOwner(0);

    return true;
}

float Object::getDistanceTo(const glm::vec2 &point) const {
    return glm::distance(glm::vec2(_position), point);
}

float Object::getSquareDistanceTo(const glm::vec2 &point) const {
    return glm::distance2(glm::vec2(_position), point);
}

float Object::getDistanceTo(const glm::vec3 &point) const {
    return glm::distance(_position, point);
}

float Object::getSquareDistanceTo(const glm::vec3 &point) const {
    return glm::distance2(_position, point);
}

float Object::getDistanceTo(const Object &other) const {
    return glm::distance(_position, other._position);
}

float Object::getSquareDistanceTo(const Object &other) const {
    return glm::distance2(_position, other._position);
}

bool Object::contains(const glm::vec3 &point) const {
    if (!_sceneNode)
        return false;

    const AABB &aabb = _sceneNode->aabb();

    return (aabb * _transform).contains(point);
}

void Object::face(const Object &other) {
    if (_id != other._id) {
        face(other._position);
    }
}

void Object::face(const glm::vec3 &point) {
    if (point == _position)
        return;

    glm::vec2 dir(glm::normalize(point - _position));
    _orientation = glm::quat(glm::vec3(0.0f, 0.0f, -glm::atan(dir.x, dir.y)));
    updateTransform();
}

void Object::faceAwayFrom(const Object &other) {
    if (_id == other._id || _position == other.position())
        return;

    glm::vec2 dir(glm::normalize(_position - other.position()));
    _orientation = glm::quat(glm::vec3(0.0f, 0.0f, -glm::atan(dir.x, dir.y)));
    updateTransform();
}

void Object::moveDropableItemsTo(Object &other) {
    bool otherInParty = _game.party().isMember(other);
    for (auto it = _items.begin(); it != _items.end();) {
        if ((*it)->isDropable()) {
            std::shared_ptr<Item> item(*it);
            it = _items.erase(it);
            if (Creature *creature = dyn_cast<Creature>(this)) {
                creature->itemAttributes().removeItem(item);
            }
            // Credits looted by the party feed the shared gold pool instead of
            // the inventory; the stack size is the credit amount.
            if (otherInParty && item->isCredits()) {
                _game.party().giveGold(item->stackSize());
                item->setOwner(0);
                _game.destroyRuntimeObjectGraph(item);
            } else {
                item->setOwner(0);
                other.addItem(item);
            }
        } else {
            ++it;
        }
    }
}

void Object::applyEffect(const std::shared_ptr<Effect> &effect, DurationType durationType, float duration) {
    if (!isRuntimeLive()) return;
    if (auto saved = std::dynamic_pointer_cast<SavedEffectValue>(effect)) {
        EffectInstance instance(saved->instance());
        if (instance.hasStableId()) {
            _game.importEffectId(instance.id);
        } else {
            instance.id = _game.allocateEffectId();
        }

        instance.subType = static_cast<uint16_t>(
            (instance.subType & ~static_cast<uint16_t>(0x7)) |
            static_cast<uint16_t>(durationType));
        instance.duration = duration;
        instance.remainingDuration = durationType == DurationType::Temporary
                                         ? std::optional<float>(duration)
                                         : std::nullopt;
        instance.expiryOrigin = durationType == DurationType::Temporary
                                    ? EffectExpiryOrigin::RuntimeCountdown
                                    : EffectExpiryOrigin::None;
        instance.expiryDay = 0;
        instance.expiryTime = 0;
        instance.skipOnLoad = false;

        // The save-facing instance remains authoritative. Unsupported retail
        // effects stay typed and queryable rather than pretending that the
        // SavedEffectValue wrapper implements their gameplay behavior.
        instance.effect.reset();
        restoreEffect(std::move(instance));
        return;
    }

    EffectInstance instance = effect->saveFacingInstance();
    instance.effect = effect;
    instance.id = _game.allocateEffectId();
    instance.subType = static_cast<uint16_t>(durationType);
    instance.duration = duration;
    if (durationType == DurationType::Temporary) {
        instance.remainingDuration = duration;
        instance.expiryOrigin = EffectExpiryOrigin::RuntimeCountdown;
    }
    instance.exposed = 1;
    restoreEffect(std::move(instance));
}

bool Object::restoreEffect(EffectInstance effect) {
    if (!effect.shouldRestoreOnLoad()) {
        return false;
    }
    if (effect.durationType() == DurationType::Instant && effect.effect) {
        if (effect.effect->onApply(*this)) {
            effect.effect->onRemove(*this);
        }
        return true;
    }
    _effects.push_back(std::move(effect));
    if (_effects.back().effect && !_effects.back().effect->onApply(*this)) {
        _effects.pop_back();
        return false;
    }
    return true;
}

size_t Object::removeEffectsById(EffectId id) {
    size_t removed = 0;
    for (auto it = _effects.begin(); it != _effects.end();) {
        if (it->id == id) {
            std::shared_ptr<Effect> removedEffect = it->effect;
            it = _effects.erase(it);
            if (removedEffect) {
                removedEffect->onRemove(*this);
            }
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

void Object::updateEffects(float dt) {
    for (auto it = _effects.begin(); it != _effects.end();) {
        EffectInstance &effect = *it;
        bool temporary = effect.durationType() == DurationType::Temporary && effect.remainingDuration;
        if (temporary) {
            *effect.remainingDuration = glm::max(0.0f, *effect.remainingDuration - dt);
        }
        if (temporary && *effect.remainingDuration == 0.0f) {
            std::shared_ptr<Effect> removedEffect = effect.effect;
            it = _effects.erase(it);
            if (removedEffect) {
                removedEffect->onRemove(*this);
            }
        } else {
            ++it;
        }
    }
}

void Object::playAnimation(AnimationType animation, AnimationProperties properties) {
}

bool Object::isSelectable() const {
    return false;
}

glm::vec3 Object::getSelectablePosition() const {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    return model ? model->getWorldCenterOfAABB() : _position;
}

void Object::setRoom(Room *room) {
    if (_room) {
        _room->removeTenant(this);
    }
    _room = room;

    if (_room) {
        _room->addTenant(this);
    }
}

void Object::setPosition(const glm::vec3 &position) {
    _position = position;
    updateTransform();
}

void Object::updateTransform() {
    _transform = glm::translate(glm::mat4(1.0f), _position);
    _transform *= glm::mat4_cast(_orientation);

    if (_sceneNode && !_stunt) {
        _sceneNode->setLocalTransform(_transform);
    }
}

void Object::setFacing(float facing) {
    _orientation = glm::quat(glm::vec3(0.0f, 0.0f, facing));
    updateTransform();
}

void Object::setVisible(bool visible) {
    if (_visible == visible)
        return;

    _visible = visible;

    if (_sceneNode) {
        _sceneNode->setEnabled(visible);
    }
}

std::shared_ptr<Item> Object::getFirstItem() {
    _itemIndex = 0;
    return getNextItem();
}

std::shared_ptr<Item> Object::getNextItem() {
    int itemCount = static_cast<int>(_items.size());
    if (itemCount > _itemIndex) {
        return _items[_itemIndex++];
    }
    return nullptr;
}

std::shared_ptr<Item> Object::getItemByTag(const std::string &tag) {
    for (auto &item : _items) {
        if (item->tag() == tag)
            return item;
    }
    return nullptr;
}

void Object::clearAllEffects() {
    std::vector<std::shared_ptr<Effect>> removed;
    removed.reserve(_effects.size());
    for (EffectInstance &effect : _effects) {
        if (effect.effect) {
            removed.push_back(std::move(effect.effect));
        }
    }
    _effects.clear();

    for (const std::shared_ptr<Effect> &effect : removed) {
        effect->onRemove(*this);
    }
    onEffectsCleared();
}

void Object::removeEffect(const std::shared_ptr<Effect> &effect) {
    for (auto it = _effects.begin(); it != _effects.end(); ++it) {
        if (it->effect == effect) {
            std::shared_ptr<Effect> removed = it->effect;
            _effects.erase(it);
            removed->onRemove(*this);
            return;
        }
    }
}

bool Object::hasEffect(EffectType type) const {
    return std::any_of(
        _effects.begin(),
        _effects.end(),
        [type](const EffectInstance &applied) {
            return applied.effect && applied.effect->type() == type;
        });
}

int Object::applyDamageToHitPoints(int amount, int currentHitPoints) {
    bool minimumOne = isMinOneHP();
    int minimumHitPoints = minimumOne ? 1 : 0;
    int adjustedAmount = minimumOne
                             ? std::min(amount, std::max(0, currentHitPoints - minimumHitPoints))
                             : amount;
    _currentHitPoints = std::max(minimumHitPoints, currentHitPoints - amount);
    return adjustedAmount;
}

void Object::damage(int amount, uint32_t damager) {
}

void Object::startStuntMode() {
    if (_sceneNode) {
        _sceneNode->setLocalTransform(glm::mat4(1.0f));
        _sceneNode->setCullingEnabled(false);
    }
    _stunt = true;
}

void Object::stopStuntMode() {
    if (!_stunt)
        return;

    if (_sceneNode) {
        _sceneNode->setLocalTransform(_transform);
        _sceneNode->setCullingEnabled(true);
    }
    _stunt = false;
}

std::shared_ptr<Effect> Object::getFirstEffect() {
    _effectIndex = 1;
    return !_effects.empty() ? _effects[0].effect : nullptr;
}

std::shared_ptr<Effect> Object::getNextEffect() {
    return (_effectIndex < _effects.size()) ? _effects[_effectIndex++].effect : nullptr;
}

} // namespace game

} // namespace reone
