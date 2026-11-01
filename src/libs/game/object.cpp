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

#include <algorithm>
#include <exception>
#include <sstream>
#include <typeinfo>
#include <vector>

#include "reone/game/action/castspell.h"
#include "reone/game/action/startconversation.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/di/services.h"
#include "reone/game/effect/damage.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/room.h"
#include "reone/game/script/savedsituation.h"
#include "reone/resource/gff.h"
#include "reone/resource/provider/scripts.h"
#include "reone/system/exception/validation.h"
#include "reone/system/logutil.h"

using namespace reone::graphics;
using namespace reone::scene;

namespace reone {

namespace game {

namespace detail {
template <class Members, class Apply>
EffectPackageApplicationResult applyEffectPackageMembers(const Members &members, Apply apply) {
    EffectPackageApplicationResult result;
    for (const auto &member : members) {
        if (apply(member)) {
            ++result.accepted;
        } else {
            ++result.rejected;
        }
    }
    return result;
}

struct EffectCollectionUnchanged {
    template <class Record>
    void operator()(const Record &) const {}
};

// Application order, not package ID, identifies the record across callbacks.
// The current record and subsequent siblings remain queryable during removal.
// The after-removal callback receives the record that left the collection.
template <class Collection, class Remove, class AfterRemove = EffectCollectionUnchanged>
bool removeEffectApplication(Collection &effects, uint64_t order,
                             std::vector<uint64_t> &inFlight, Remove remove, AfterRemove afterRemove = {}) {
    const auto find = [&]() { return std::find_if(effects.begin(), effects.end(),
                                                  [order](const auto &record) { return record.applicationOrder == order; }); };
    auto at = find();
    if (at == effects.end() || std::find(inFlight.begin(), inFlight.end(), order) != inFlight.end()) {
        return false;
    }
    const auto record = *at;
    struct Invocation {
        std::vector<uint64_t> &stack;
        Invocation(std::vector<uint64_t> &stack, uint64_t order) :
            stack(stack) { stack.push_back(order); }
        ~Invocation() { stack.pop_back(); }
    } invocation(inFlight, order);
    remove(record);
    at = find();
    if (at == effects.end()) {
        return false;
    }
    effects.erase(at);
    afterRemove(record);
    return true;
}

template <class Collection, class Remove, class AfterRemove = EffectCollectionUnchanged>
size_t removeEffectPackage(Collection &effects, EffectId id, std::vector<uint64_t> &inFlight,
                           Remove remove, AfterRemove afterRemove = {}) {
    size_t count = 0;
    std::vector<uint64_t> visited;
    for (;;) {
        auto at = std::find_if(effects.begin(), effects.end(), [&](const auto &record) {
            return record.id == id &&
                   std::find(visited.begin(), visited.end(), record.applicationOrder) == visited.end() &&
                   std::find(inFlight.begin(), inFlight.end(), record.applicationOrder) == inFlight.end();
        });
        if (at == effects.end()) {
            return count;
        }
        const auto order = at->applicationOrder;
        visited.push_back(order);
        if (removeEffectApplication(effects, order, inFlight, remove, afterRemove)) {
            ++count;
        }
    }
}
} // namespace detail

namespace {

template <class Records>
std::vector<EffectId> selectScriptEffectRemovals(Records &records,
                                                 ScriptEffectRemovalMatch match, const EffectInstance &value) {
    std::vector<EffectId> result;
    for (auto &record : records) {
        const bool selected = match == ScriptEffectRemovalMatch::PackageId
                                  ? record.id == value.id
                              : match == ScriptEffectRemovalMatch::Integer0
                                  ? record.integerParameter(0) == value.integerParameter(0)
                                  : record.serializedType == value.serializedType &&
                                        record.integerParameter(0) == value.integerParameter(0) &&
                                        record.integerParameter(1) == value.integerParameter(1);
        if (!selected) {
            continue;
        }
        // Scripts hide the selected records immediately; removal itself
        // is queued. RemoveEffect queues once after hiding every package member.
        record.exposed = 0;
        if (match == ScriptEffectRemovalMatch::PackageId) {
            result.assign(1, record.id);
        } else {
            result.push_back(record.id);
        }
        if (match == ScriptEffectRemovalMatch::TypeAndFirstTwoIntegers) {
            break;
        }
    }
    return result;
}

} // namespace

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
    const bool hasBaseHitPoints = gff.readShort(_hitPoints, "HitPoints");
    gff.readShort(_maxHitPoints, "MaxHitPoints");
    // A record without current hit points starts at its own base hit points.
    int currentHitPoints = _hitPoints;
    if (gff.readShort(currentHitPoints, "CurrentHitPoints") || hasBaseHitPoints) {
        _currentHitPoints = currentHitPoints;
    }

    deserializePlacement(gff);

    if (_type != ObjectType::Placeable && _type != ObjectType::Store &&
        _type != ObjectType::Item && dynamic_cast<Creature *>(this) == nullptr) {
        deserializeOwnedItems(
            gff, identityContext, SaveRecordOriginKind::ContainedItem);
    }
    deserializeRuntimeState(gff, identityContext);
}

void Object::deserializePlacement(const resource::Gff &gff) {
    gff.readFloat(_position[0], "X");
    gff.readFloat(_position[0], "XPosition");
    gff.readFloat(_position[1], "Y");
    gff.readFloat(_position[1], "YPosition");
    gff.readFloat(_position[2], "Z");
    gff.readFloat(_position[2], "ZPosition");

    float cosine, sine;
    if (gff.readFloat(cosine, "XOrientation") && gff.readFloat(sine, "YOrientation")) {
        setFacing(-glm::atan(cosine, sine));
    }

    float bearing;
    if (gff.readFloat(bearing, "Bearing")) {
        setFacing(bearing);
    }
    updateTransform();
}

std::vector<std::shared_ptr<Object>> Object::ownedRuntimeObjects() const {
    return {_items.begin(), _items.end()};
}

std::shared_ptr<Item> Object::appendOwnedItemCandidate(
    std::vector<std::shared_ptr<Item>> &items,
    const std::shared_ptr<Item> &item,
    bool preserveSerializedIdentities) {
    if (!item) {
        throw ValidationException("Cannot stage a null owned Item");
    }
    for (const auto &existing : items) {
        if (!existing || existing.get() == item.get()) {
            continue;
        }
        if (preserveSerializedIdentities &&
            (existing->serializedObjectIdentity() ||
             item->serializedObjectIdentity())) {
            continue;
        }
        if (!existing->isStackCompatibleWith(*item)) {
            continue;
        }
        if (existing->mergeStackFrom(*item)) {
            item->clearOwner();
            _game.discardStagedRuntimeObjects({item});
            return existing;
        }
    }
    items.push_back(item);
    return item;
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
                    // An entry naming a missing template makes no item.
                    if (Item::namesMissingTemplate(*itemGff, _services.resource.gffs)) {
                        continue;
                    }
                    auto item = _game.newOwnedItem(*itemGff, identityContext);
                    if (forceDropable) item->setDropable(true);
                    item->setOwner(_id);
                    appendOwnedItemCandidate(
                        replacement, item, true);
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
                appendOwnedItemCandidate(
                    replacement, item, true);
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
    if (_feedbackTextRemaining > 0.0f) {
        _feedbackTextRemaining = std::max(0.0f, _feedbackTextRemaining - dt);
        if (_feedbackTextRemaining == 0.0f) {
            _feedbackText.clear();
        }
    }
    if (!isRuntimeLive()) {
        return;
    }
    updateActions(dt);
    updateEffects(dt);
    updateShownVisuals(dt);
    executeActionsIfAble(dt);
    if (_sceneNode && _sceneNode->type() == SceneNodeType::Model) {
        std::static_pointer_cast<ModelSceneNode>(_sceneNode)->setPickable(isSelectable());
    }
}

void Object::runActions() {
    if (!isRuntimeLive()) {
        return;
    }
    updateActions(0.0f);
    executeActionsIfAble(0.0f);
}

void Object::executeActionsIfAble(float dt) {
    // An action that can no longer be cleared runs on whatever holds the object.
    const auto current = getCurrentAction();
    if (!isDead() && (takesUpActions() || (current && !current->isClearable()))) {
        executeActions(dt);
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

void Object::setListenPattern(const std::string &pattern, int32_t number) {
    auto expression = std::find_if(_listenExpressions.begin(), _listenExpressions.end(),
                                   [number](const ListenExpression &entry) { return entry.number == number; });
    if (expression == _listenExpressions.end()) {
        expression = _listenExpressions.insert(_listenExpressions.end(), ListenExpression {number, "", std::nullopt});
    }
    // The pattern the entry already has changes nothing; a new entry has the
    // empty pattern and matches nothing.
    const std::string lowered = boost::to_lower_copy(pattern);
    if (lowered == expression->pattern) {
        return;
    }
    // An empty pattern in place of another matches only an empty string.
    if (lowered.empty()) {
        expression->parsed = ListenPattern::emptyString();
        expression->pattern.clear();
        return;
    }
    // A pattern that fails to parse leaves the entry matching nothing, and
    // the last pattern that parsed is still the one saved.
    expression->parsed = ListenPattern::parse(pattern);
    if (expression->parsed) {
        expression->pattern = lowered;
    }
}

std::optional<int32_t> Object::testListenExpressions(const std::string &str, std::vector<std::string> &pieces) const {
    for (const auto &expression : _listenExpressions) {
        if (expression.parsed && expression.parsed->match(str, pieces)) {
            return expression.number;
        }
    }
    return std::nullopt;
}

void Object::receiveConversationEvent(ConversationEvent event) {
    _lastSpeaker = event.speaker;
    _listenPatternNumber = event.patternNumber;
    _matchedSubstrings = std::move(event.matchedPieces);
    runConversationScript();
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
    _savedScheduledActions.clear();
    _savedScheduledReferencesBound.clear();
    _savedScheduledTime = 0.0f;
    _savedOwnerHold = 0.0f;
    _savedOwnerHoldBy = SavedObjectReference {};
    _savedRounds.clear();
    _savedRuntimeIdentityContext = identityContext;
    _savedActionReferencesBound.clear();
    _savedRuntimeParsed = gff.has("EffectList") || gff.has("ActionList") || gff.has("CombatRoundData");
    const auto round = gff.findStruct("CombatRoundData");
    if (round) {
        _savedScheduledTime = round->getInt("Timer") / 1000.0f;
        for (const auto &entry : round->getList("SchedActionList")) {
            _savedScheduledActions.push_back(SavedScheduledAction::fromGff(*entry, identityContext));
        }
        for (const auto &entry : round->getList("RoundList")) {
            SavedCreatureRound saved;
            saved.clock = SavedRoundClock::fromGff(*entry, identityContext);
            saved.spellRound = entry->getUint("SpellCastRound") != 0;
            _savedRounds.push_back(std::move(saved));
        }
        if (round->getUint("RoundPaused") != 0 && round->getInt("PauseTimer") > 0) {
            _savedOwnerHold = round->getInt("PauseTimer") / 1000.0f;
            _savedOwnerHoldBy = SavedObjectReference::fromSerializedId(
                round->getUint("RoundPausedBy", kSavedRuntimeInvalidObjectId), identityContext);
        }
    }
    if (auto *creature = dyn_cast<Creature>(this)) {
        creature->setWeaponIneffectiveReported(round && round->getUint("WeaponSucks") != 0);
    }
    _savedRuntimePublished = false;
    if (_savedRuntimeParsed) {
        for (const auto &effect : gff.getList("EffectList")) {
            _savedEffects.push_back(EffectInstance::fromGff(
                *effect, identityContext));
        }
        _savedActionQueue = SavedActionQueue::fromGff(
            gff, identityContext);
        // The saved effects replace the template's, and lay their counters afresh.
        _effects.clear();
        _damageImmunity.fill(0);
        if (auto *creature = dyn_cast<Creature>(this)) {
            creature->clearArmorClassEffectCache();
            creature->clearVisibilityCounters();
        }
        _actions.clear();
        _publishedSavedActions.clear();
        _executingActionNode.reset();
    }

    const auto spellContext = gff.findStruct("SpellContext");
    _spellScriptContext.restore(spellContext ? std::optional<std::reference_wrapper<const resource::Gff>>(*spellContext)
                                             : std::nullopt);
    if (_type == ObjectType::AreaOfEffect) {
        _effectSpellId = gff.getUint("SpellId", kNoEffectSpellId);
    }
    _savedReferenceIds.clear();
    _savedReferences.clear();
    _lastDamager.reset();
    _lastDamageAmounts.fill(-1);
    _savedLastDamagerId.reset();
    uint32_t lastDamagerId = 0;
    if (gff.readDword(lastDamagerId, "LastDamager")) {
        _savedLastDamagerId = lastDamagerId;
    }
    static const std::array<std::string_view, 8> referenceFields {
        "AreaId",
        "CreatorId",
        "LastAttacker",
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
    _savedPerceptionData.clear();
    for (const auto &perception : gff.getList("PerceptionList")) {
        uint32_t id = 0;
        if (perception->readDword(id, "ObjectId")) {
            _savedReferenceIds.emplace(
                "Perception/" + std::to_string(perceptionIndex), id);
        }
        _savedPerceptionData.push_back(static_cast<uint8_t>(perception->getUint("PerceptionData")));
        ++perceptionIndex;
    }
}

std::shared_ptr<Object> Object::effectSource() const {
    const auto creator = _savedReferences.find("CreatorId");
    return creator == _savedReferences.end() ? nullptr : creator->second.resolve();
}

void Object::bindSavedRuntimeState() {
    if (!_savedRuntimeParsed) {
        return;
    }
    // A reference that no longer resolves leaves that binding empty; the
    // effect itself still comes back.
    for (auto &effect : _savedEffects) {
        if (effect.hasStableId()) {
            _game.importEffectId(effect.id);
        }
        _game.bindEffectCreator(effect);
    }
    for (auto &action : _savedActionQueue.actions) {
        _savedActionReferencesBound.push_back(
            action.bindObjectReferences(_game));
    }
    for (auto &action : _savedScheduledActions) {
        _savedScheduledReferencesBound.push_back(action.bindObjectReferences(_game));
    }
    if (_savedOwnerHold > 0.0f) {
        _game.bindSavedObjectReference(_savedOwnerHoldBy);
    }
    for (auto &saved : _savedRounds) {
        _game.bindSavedObjectReference(saved.clock.pauseOwner);
        _game.bindSavedObjectReference(saved.clock.master);
        _game.bindSavedObjectReference(saved.clock.engaged);
    }
}

void Object::publishSavedRuntimeState() {
    if (!_savedRuntimeParsed || _savedRuntimePublished) {
        return;
    }
    SavedScriptSituationImporter importer(
        _game, _services.resource.scripts);

    for (size_t index = 0; index < _savedEffects.size(); ++index) {
        // An effect whose time ran out while it was saved still comes back;
        // the next update takes it off again.
        auto effect = _savedEffects[index];
        if (effect.hasStableId()) {
            _game.importEffectId(effect.id);
        } else {
            effect.id = _game.allocateEffectId();
        }
        restoreEffect(std::move(effect));
    }

    // A saved action that cannot be built, or whose objects are gone, keeps
    // its place in the queue and is saved again as it was loaded.
    for (size_t index = 0; index < _savedActionQueue.actions.size(); ++index) {
        const auto &savedAction = _savedActionQueue.actions[index];
        auto action = _savedActionReferencesBound.at(index) ? savedAction.toRuntimeAction(_game, &importer) : nullptr;
        if (action) {
            _publishedSavedActions.push_back(PublishedSavedAction {restoreAction(std::move(action)), std::nullopt});
        } else {
            _publishedSavedActions.push_back(PublishedSavedAction {{}, savedAction});
        }
    }
    onEffectsRestored();
    // Saved perception comes back as it was, without notices.
    if (auto *creature = dyn_cast<Creature>(this)) {
        creature->restoreSavedPerception(_savedPerceptionData);
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

void Object::captureAuthoredRecord(const resource::Gff &blueprint, const resource::Gff &placement) {
    auto record = blueprint.deepCopy();
    for (const auto &field : placement.fields()) {
        replaceSaveField(*record, field.deepCopy());
    }
    _saveRecordProvenance = SaveRecordProvenance {SaveGffShadow::capture(*record), SaveRecordOrigin {}, std::nullopt};
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
    std::vector<EffectInstance> result;
    for (const EffectInstance &effect : _effects) {
        // Equipped effects are derived from the authoritative equipment edge.
        // Persisting them independently would duplicate them on reconstruction.
        if (effect.durationType() != DurationType::Equipped) {
            result.push_back(effect);
        }
    }
    return result;
}

EffectInstance *Object::findEffectInstance(const Effect &effect) {
    auto it = std::find_if(
        _effects.begin(),
        _effects.end(),
        [&effect](const EffectInstance &instance) {
            return instance.effect.get() == &effect;
        });
    return it != _effects.end() ? &*it : nullptr;
}

std::vector<SavedActionRecord> Object::saveActionSnapshot() const {
    // The same ordered nodes drive queries, execution and persistence.
    std::vector<SavedActionRecord> result;
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
    // A kept saved action is written back behind the restored node it followed
    // in its saved queue, or behind the nearest restored node before that one
    // still saved; with none, ahead of every live action.
    const auto kept = keptSavedActionAnchors();
    auto saveKeptBehind = [&](const OrdinaryActionQueue::Node &node) {
        for (const auto &[published, after] : kept) {
            if (after == node) {
                result.push_back(*_publishedSavedActions[published].kept);
            }
        }
    };
    saveKeptBehind(nullptr);
    for (size_t index = 0; index < _actions.nodes.size(); ++index) {
        const auto &node = _actions.nodes[index];
        const auto &action = node->action;
        if (action->isCompleted() || action->isCancelled()) {
            continue;
        }
        if (auto saved = saveAction(*action, index)) {
            saved->groupActionId = node->groupId;
            saved->round = _game.combat().saveRound(*action);
            saved->scheduled = action->isScheduledCommand();
            // A restored command keeps the fields of its saved record that are not read.
            if (const auto &original = action->originalSavedAction()) {
                saved->unknownFields = original->unknownFields;
            }
            result.push_back(std::move(*saved));
        } else {
            throw unsupportedAction(*action, index);
        }
        saveKeptBehind(node);
    }
    return result;
}

std::vector<std::pair<size_t, OrdinaryActionQueue::Node>> Object::keptSavedActionAnchors() const {
    auto isSaved = [this](const OrdinaryActionQueue::Node &node) {
        return std::find(_actions.nodes.begin(), _actions.nodes.end(), node) != _actions.nodes.end() &&
               !node->action->isCompleted() && !node->action->isCancelled();
    };
    std::vector<std::pair<size_t, OrdinaryActionQueue::Node>> result;
    OrdinaryActionQueue::Node anchor;
    for (size_t index = 0; index < _publishedSavedActions.size(); ++index) {
        const auto &published = _publishedSavedActions[index];
        if (published.kept) {
            result.emplace_back(index, anchor);
        } else if (auto node = published.restored.lock(); node && isSaved(node)) {
            anchor = std::move(node);
        }
    }
    return result;
}

void Object::endEffectPresentations() {
    for (auto &effect : _effects) {
        if (effect.effect) {
            effect.effect->retireAreaRuntime({});
        }
    }
    _shownVisuals.clear();
}

void Object::retireAreaRuntimeState(
    const std::set<uint32_t> &retainedObjectIds) {
    // The authoritative source snapshot was captured before this boundary.
    // Discard rather than cancel: cancellation callbacks are live gameplay and
    // must not mutate the already-frozen outgoing world.
    for (auto &action : _actions) {
        // Conversation cancellation only retires its presentation admission.
        // Other actions are discarded without invoking gameplay callbacks.
        if (auto conversation = dyn_cast<StartConversationAction>(action)) {
            conversation->cancel(action, *this);
        }
        if (action) action->markCancelled();
    }
    _actions.clear();
    _publishedSavedActions.clear();
    _executingActionNode.reset();
    _savedActionQueue = SavedActionQueue {};
    _savedScheduledActions.clear();
    _savedScheduledReferencesBound.clear();
    _savedScheduledTime = 0.0f;
    _savedOwnerHold = 0.0f;
    _savedOwnerHoldBy = SavedObjectReference {};
    _savedRounds.clear();

    for (auto &effect : _effects) {
        if (effect.effect) {
            effect.effect->retireAreaRuntime(retainedObjectIds);
        }
        effect.retireAreaRuntimeBindings(retainedObjectIds);
    }
    _shownVisuals.clear();
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
        if (!object || retainedObjectIds.count(object->id()) == 0)
            continue;
        retainedReferenceIds.emplace(field, object->id());
        retainedReferences.emplace(field, object);
    }
    _savedReferenceIds = std::move(retainedReferenceIds);
    _savedReferences = std::move(retainedReferences);
    _spellScriptContext.retireAreaRuntime(retainedObjectIds);
    _lastHostileActor.reset();
    auto lastDamager = _lastDamager.resolve();
    if (!lastDamager || retainedObjectIds.count(lastDamager->id()) == 0) {
        _lastDamager.reset();
        _lastDamageAmounts.fill(-1);
        if (_savedLastDamagerId) {
            _savedLastDamagerId = script::kObjectInvalid;
        }
    } else {
        _savedLastDamagerId = lastDamager->id();
    }
}

void Object::resolveSavedReferences(
    const std::function<std::shared_ptr<Object>(uint32_t)> &resolver) {
    _spellScriptContext.bind(resolver);
    _savedReferences.clear();
    for (const auto &[field, id] : _savedReferenceIds) {
        if (auto object = resolver(id)) {
            _savedReferences.emplace(field, object);
        }
    }
    _lastDamager.reset();
    _lastDamageAmounts.fill(-1);
    if (_savedLastDamagerId &&
        *_savedLastDamagerId != script::kObjectInvalid) {
        _lastDamager = resolver(*_savedLastDamagerId);
    }
}

std::shared_ptr<Object> Object::savedReference(std::string_view field) const {
    auto found = _savedReferences.find(std::string(field));
    return found == _savedReferences.end() ? nullptr : found->second.resolve();
}

uint32_t Object::getLastHostileActor() const {
    auto actor = _lastHostileActor.resolve();
    return actor ? actor->id() : script::kObjectInvalid;
}

Object::Object(uint32_t id, ObjectType type, std::string sceneName,
               Game &game, ServicesView &services) :
    _id(id), _type(type), _sceneName(std::move(sceneName)), _game(game), _services(services) {
    _lastDamageAmounts.fill(-1);
    _raiseable = game.isTSL();
}

void Object::setDestroyability(bool destroyable, bool raiseable, bool selectableWhenDead) {
    _destroyable = destroyable;
    _raiseable = raiseable;
    _selectableWhenDead = selectableWhenDead;
    if (auto *creature = dyn_cast<Creature>(this)) {
        creature->onDestroyabilityChanged();
    }
}

void Object::recordReceivedAttack(uint32_t attackerId, int weaponAttackType) {
    _savedReferences["LastAttacker"] = _game.getObjectById(attackerId);
    setLastHostileActor(attackerId);
    auto attacker = _game.getObjectById<Creature>(attackerId);
    if (!attacker) {
        return;
    }
    attacker->removeCombatInvisibilityEffects();
    attacker->recordAttackWeapon(weaponAttackType);
}

void Object::setLastHostileActor(uint32_t actor, bool force) {
    if (force) {
        if (actor == script::kObjectInvalid) {
            _lastHostileActor.reset();
        } else {
            _lastHostileActor = _game.getObjectById(actor);
        }
        return;
    }
    if (_plot) {
        _lastHostileActor.reset();
        return;
    }
    auto object = _game.getObjectById(actor);
    // An actor that is no object leaves the record as it is.
    if (!object) {
        return;
    }
    // An area of effect acts for its creator.
    if (object->type() == ObjectType::AreaOfEffect) {
        object = object->effectSource();
    }
    const auto *creature = object ? dyn_cast<Creature>(object.get()) : nullptr;
    if (!creature || creature == this || creature->isDead() || creature->isTemporarilyDead()) {
        _lastHostileActor.reset();
        return;
    }
    if (const auto *self = dyn_cast<Creature>(this)) {
        if (_game.module()->area()->isObjectResident(*this) && creature->faction() == self->faction()) {
            return;
        }
    }
    _lastHostileActor = object;
}

uint32_t Object::getLastDamager() const {
    auto damager = _lastDamager.resolve();
    return damager ? damager->id() : script::kObjectInvalid;
}

void Object::setLastDamager(const std::shared_ptr<Object> &damager) {
    _lastDamager = damager;
    _savedLastDamagerId = damager
                              ? damager->id()
                              : script::kObjectInvalid;
}

void Object::clearAllActions(bool force, bool evenUncommandable) {
    if (!_commandable && !evenUncommandable) {
        return;
    }
    removeActions(force, true);
}

void Object::teardownActions() {
    // An object leaving play drops its round's pending entries with its actions.
    _game.combat().discardEquipment(*this);
    removeActions(true, false);
}

void Object::removeActions(bool force, bool keepUnclearable) {
    _publishedSavedActions.clear();
    auto &nodes = _actions.nodes;
    // Every node goes that may, the one running included. Cancelling one may
    // take others with it.
    const std::vector<OrdinaryActionQueue::Node> snapshot {nodes.begin(), nodes.end()};
    for (const auto &node : snapshot) {
        if (std::find(nodes.begin(), nodes.end(), node) == nodes.end()) {
            continue;
        }
        // An action that cannot be cleared stays, whatever the force, and so
        // does one its round has taken up, unless forced.
        if (keepUnclearable && node->action && (!node->action->isClearable() || (!force && node->action->locked()))) {
            continue;
        }
        if (node->action) {
            auto action = node->action;
            action->cancel(action, *this);
            action->markCancelled();
        }
        auto position = std::find(nodes.begin(), nodes.end(), node);
        if (position != nodes.end()) {
            nodes.erase(position);
        }
        releaseOrientationLock();
    }
}

void Object::removeActionsOfType(ActionType type) {
    auto &nodes = _actions.nodes;
    const std::vector<OrdinaryActionQueue::Node> snapshot {nodes.begin(), nodes.end()};
    for (const auto &node : snapshot) {
        if (!node->action || node->action->type() != type) {
            continue;
        }
        if (std::find(nodes.begin(), nodes.end(), node) == nodes.end()) {
            continue;
        }
        auto action = node->action;
        action->cancel(action, *this);
        action->markCancelled();
        auto position = std::find(nodes.begin(), nodes.end(), node);
        if (position != nodes.end()) {
            nodes.erase(position);
        }
    }
}

// Clearing an action from a creature's queue releases its orientation lock.
void Object::releaseOrientationLock() {
    if (auto *creature = dyn_cast<Creature>(this)) {
        creature->setOrientationLock(script::kObjectInvalid);
    }
}

void Object::discardHostileActionGroups() {
    std::vector<OrdinaryActionQueue::Node> nodes {_actions.nodes.begin(), _actions.nodes.end()};
    detail::discardCombatActionGroups(nodes, _game.isTSL(), [](const auto &node) { return node->groupId; }, [](const auto &node) { return node->actionId; }, [](uint32_t action, const auto &) {
            const auto type = static_cast<SavedActionType>(action);
            return type == SavedActionType::AttackObject || type == SavedActionType::CastSpell ||
                   type == SavedActionType::ItemCastSpell; }, [this](const OrdinaryActionQueue::Node &node) { discardGroupNode(node); });
}

void Object::discardGroupNode(const OrdinaryActionQueue::Node &node) {
    if (node->action) {
        node->action->markCancelled();
        // Nothing else of the cast is undone; only its conjuring projectile,
        // which belongs to the action, goes with it.
        auto &command = node->action->combatAction();
        if (auto *cast = dyn_cast<CastSpellAction>(&command)) {
            cast->dropPresentation();
        }
    }
    auto &queue = _actions.nodes;
    auto position = std::find(queue.begin(), queue.end(), node);
    if (position != queue.end()) {
        queue.erase(position);
    }
}

bool Object::isHeartbeatDueSince(uint32_t day, uint32_t time) const {
    uint32_t days = 0;
    uint32_t elapsed = 0;
    _game.subtractWorldTimes(_game.worldTimeDay(), _game.worldTimeOfDay(), day, time, days, elapsed);
    return elapsed >= static_cast<uint32_t>(kHeartbeatInterval * 1000.0f);
}

void Object::stampHeartbeat() {
    _heartbeatDay = _game.worldTimeDay();
    _heartbeatTime = _game.worldTimeOfDay();
}

void Object::updateStampedHeartbeat(const std::string &script, bool runnable) {
    if (!isHeartbeatDueSince(_heartbeatDay, _heartbeatTime)) {
        return;
    }
    const bool running = _heartbeatTime != 0 && runnable;
    stampHeartbeat();
    // The script may drop itself while it runs.
    const std::string resRef = script;
    if (running && !resRef.empty()) {
        _game.scriptRunner().run(resRef, _id);
    }
}

int Object::chooseActionGroup(const Action &action, std::optional<int> group) {
    if (group) {
        return *group;
    }
    if (const auto &saved = action.originalSavedAction()) {
        return saved->groupActionId;
    }
    return _actions.newGroup();
}

void Object::addAction(std::shared_ptr<Action> action, std::optional<int> group) {
    if (!isRuntimeLive() || !action) {
        return;
    }
    auto node = std::make_shared<ActionQueueNode>();
    node->action = action;
    node->actionId = action->serializedActionId();
    node->groupId = chooseActionGroup(*action, group);
    if (_game.combat().scheduleEquipment(*this, node)) {
        return;
    }
    _actions.nodes.push_back(node);
    action->onQueued(*this);
}

OrdinaryActionQueue::Node Object::restoreAction(std::shared_ptr<Action> action) {
    if (!isRuntimeLive())
        return nullptr;
    auto node = std::make_shared<ActionQueueNode>();
    node->action = action;
    node->actionId = action->serializedActionId();
    node->groupId = action->originalSavedAction()->groupActionId;
    _actions.nodes.push_back(node);
    action->onQueued(*this);
    return node;
}

void Object::addActionOnTop(std::shared_ptr<Action> action, std::optional<int> group) {
    if (!isRuntimeLive() || !action) {
        return;
    }
    auto node = std::make_shared<ActionQueueNode>();
    node->action = action;
    node->actionId = action->serializedActionId();
    node->groupId = chooseActionGroup(*action, group);
    if (_game.combat().scheduleEquipment(*this, node)) {
        return;
    }
    _actions.nodes.push_front(node);
    action->onQueued(*this);
}

bool Object::addActionBefore(const Action &parent, std::shared_ptr<Action> action) {
    if (!isRuntimeLive() || !action) {
        return false;
    }
    auto &nodes = _actions.nodes;
    auto executing = _executingActionNode.lock();
    auto position = std::find_if(nodes.begin(), nodes.end(), [&](const auto &node) {
        return executing && executing->action.get() == &parent
                   ? node == executing
                   : node->action.get() == &parent;
    });
    if (position == nodes.end()) {
        return false;
    }
    auto node = std::make_shared<ActionQueueNode>();
    node->action = action;
    node->actionId = action->serializedActionId();
    node->groupId = (*position)->groupId;
    nodes.insert(position, node);
    action->onQueued(*this);
    return true;
}

void Object::removeActionsBehind(const Action &action, const std::function<bool(const Action &)> &matches) {
    auto &nodes = _actions.nodes;
    auto position = std::find_if(nodes.begin(), nodes.end(),
                                 [&](const OrdinaryActionQueue::Node &node) { return node->action.get() == &action; });
    if (position == nodes.end()) {
        return;
    }
    ++position;
    while (position != nodes.end() && (*position)->action && matches(*(*position)->action)) {
        position = nodes.erase(position);
    }
}

void Object::cancelActionGroups(const std::function<bool(const Action &)> &matches) {
    auto &nodes = _actions.nodes;
    std::vector<int> groups;
    for (const auto &node : nodes) {
        if (node->action && matches(*node->action)) {
            groups.push_back(node->groupId);
        }
    }
    const std::vector<OrdinaryActionQueue::Node> snapshot {nodes.begin(), nodes.end()};
    for (const auto &node : snapshot) {
        if (std::find(groups.begin(), groups.end(), node->groupId) == groups.end() ||
            std::find(nodes.begin(), nodes.end(), node) == nodes.end() ||
            (node->action && !node->action->isClearable()))
            continue;
        if (node->action) {
            auto action = node->action;
            action->cancel(action, *this);
            action->markCancelled();
        }
        auto position = std::find(nodes.begin(), nodes.end(), node);
        if (position != nodes.end()) {
            nodes.erase(position);
        }
    }
}

void Object::removeActionsBehind(const Action &action) {
    auto &nodes = _actions.nodes;
    auto position = std::find_if(nodes.begin(), nodes.end(),
                                 [&](const OrdinaryActionQueue::Node &node) { return node->action.get() == &action; });
    if (position == nodes.end()) {
        return;
    }
    // The kept saved actions queued behind it go with the rest.
    const auto kept = keptSavedActionAnchors();
    std::vector<size_t> dropped;
    for (const auto &[published, after] : kept) {
        if (after && std::find(position, nodes.end(), after) != nodes.end()) {
            dropped.push_back(published);
        }
    }
    for (auto it = dropped.rbegin(); it != dropped.rend(); ++it) {
        _publishedSavedActions.erase(_publishedSavedActions.begin() + static_cast<std::ptrdiff_t>(*it));
    }
    nodes.erase(std::next(position), nodes.end());
}

void Object::requeueActionNode(const OrdinaryActionQueue::Node &node) {
    if (!isRuntimeLive() || !node) {
        return;
    }
    node->groupId = _actions.newGroup();
    _actions.nodes.push_front(node);
}

void Object::moveActionNodeToBack(const OrdinaryActionQueue::Node &node) {
    const auto moved = node;
    auto &nodes = _actions.nodes;
    auto position = std::find(nodes.begin(), nodes.end(), moved);
    if (position == nodes.end()) {
        return;
    }
    nodes.erase(position);
    nodes.push_back(moved);
}

void Object::delayAction(std::shared_ptr<Action> action, float seconds) {
    if (!isRuntimeLive()) return;
    _game.postDelayedCommand(*this, std::move(action), seconds);
}

void Object::updateActions(float dt) {
    // Nothing a dead object was to do can be done: each of its actions fails
    // as it comes up, whether or not the object can be commanded.
    if (isDead()) {
        teardownActions();
        return;
    }
    removeCompletedActions();
}

void Object::removeCompletedActions() {
    while (true) {
        std::shared_ptr<Action> action(getCurrentAction());
        if (!action || !action->isCompleted())
            return;

        _actions.nodes.pop_front();
    }
}

void Object::executeActions(float dt) {
    if (_actions.nodes.empty()) {
        _spellScriptContext.clearActiveTarget();
        if (auto *creature = dyn_cast<Creature>(this)) {
            creature->setAttemptedAttackTarget(script::kObjectInvalid);
            creature->resetAttemptedMovementTarget();
            creature->resetBlockingCreature();
        }
        return;
    }
    auto node = _actions.nodes.front();
    auto action = node->action;
    auto *creature = dyn_cast<Creature>(this);
    if (creature && _game.combat().blocksOwnerActions(*creature)) {
        return;
    }
    // An action that cannot be cleared is refused only to a party member at
    // zero vitality.
    const bool refused = creature && (action->isClearable() || creature->isTemporarilyDead()) &&
                         !creature->permitsAction(*action);
    // A physical attack its creature is no longer able to make fails, and
    // ends the creature's combat modes.
    Action &command = action->combatAction();
    if (refused && (command.type() == ActionType::AttackObject ||
                    (command.type() == ActionType::UseFeat && isHostileAction(command)))) {
        creature->cancelAllCombatModes();
    }
    if (!action->runtimeDependenciesLive() || refused) {
        action->cancel(action, *this);
        action->markCancelled();
        if (!action->isCompleted()) {
            action->complete();
        }
        return;
    }
    _executingActionNode = node;
    try {
        action->execute(action, *this, dt);
    } catch (...) {
        _executingActionNode.reset();
        throw;
    }
    _executingActionNode.reset();
    // A round an attack asked to settle ends now, before events are delivered.
    _game.combat().settleRequested();
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

int Object::currentScriptAction() const {
    const auto executing = _executingActionNode.lock();
    std::optional<int> group;
    int result = static_cast<int>(ActionType::QueueEmpty);
    for (const auto &node : _actions.nodes) {
        // A finished action has left the queue, whenever it is removed.
        if (node == executing || (node->action && node->action->isCompleted())) {
            continue;
        }
        if (!group) {
            group = node->groupId;
            result = static_cast<int>(ActionType::Invalid);
        } else if (node->groupId != *group) {
            break;
        }
        if (const auto number = scriptActionNumber(node->actionId, _game.isTSL())) {
            result = *number;
        }
    }
    return result;
}

std::shared_ptr<Action> Object::getCurrentAction() const {
    return _actions.nodes.empty() ? nullptr : _actions.nodes.front()->action;
}

std::shared_ptr<Item> Object::addItem(const std::string &resRef, int stackSize, std::optional<bool> dropable) {
    std::shared_ptr<Item> result;
    std::shared_ptr<Item> counted;
    std::vector<std::shared_ptr<Object>> noObsolete;
    std::vector<std::shared_ptr<Item>> replacement(_items);
    ItemAttributes replacementAttributes;
    auto creature = dyn_cast<Creature>(this);
    if (creature) replacementAttributes = creature->itemAttributes();
    _game.replaceRuntimeObjectGraph(
        noObsolete,
        [&]() {
            auto candidate = _game.newItemFromBlueprint(resRef);
            candidate->setStackSize(stackSize);
            if (dropable) {
                candidate->setDropable(*dropable);
            }
            if (creature && _game.party().isCountedItem(*candidate)) {
                counted = candidate;
                _game.discardStagedRuntimeObjects({candidate});
                return;
            }
            candidate->setOwner(_id);
            result = appendOwnedItemCandidate(
                replacement, candidate, false);
            if (creature && result.get() == candidate.get()) {
                replacementAttributes.addItem(candidate, _services.game);
            }
        },
        [&]() noexcept {
            _items = std::move(replacement);
            if (creature) {
                creature->itemAttributes() =
                    std::move(replacementAttributes);
            }
        });
    if (counted) {
        _game.party().acquireCountedItem(*creature, *counted);
    }
    return result;
}

std::shared_ptr<Item> Object::addItem(const std::shared_ptr<Item> &item) {
    if (!item || (!item->isRuntimeLive() && !item->isPresentationOnly())) {
        throw ValidationException("Cannot own a non-live runtime item");
    }
    if (item->isEquipped()) {
        throw ValidationException(
            "Cannot add an Item while it still has an equipment owner edge");
    }
    if (isActiveAreaOwnedItem(_game, item)) {
        throw ValidationException(
            "Cannot add an Item while it still has an Area ownership edge");
    }
    if (item->isHeld() && item->owner() != _id) {
        throw ValidationException("Runtime item already has another owner");
    }
    auto alreadyOwned = std::find(_items.begin(), _items.end(), item);
    if (alreadyOwned != _items.end()) {
        // Re-adding an owned stack restores one consumed item.
        if ((*alreadyOwned)->stackSize() < (*alreadyOwned)->maxStackSize()) {
            (*alreadyOwned)->setStackSize((*alreadyOwned)->stackSize() + 1);
        }
        return *alreadyOwned;
    }
    auto creature = dyn_cast<Creature>(this);
    if (creature && _game.party().isCountedItem(*item)) {
        item->clearOwner();
        _game.party().acquireCountedItem(*creature, *item);
        _game.destroyRuntimeObjectGraph(item);
        return nullptr;
    }
    for (const auto &existing : _items) {
        if (!existing->isStackCompatibleWith(*item)) {
            continue;
        }
        if (existing->mergeStackFrom(*item)) {
            item->clearOwner();
            _game.destroyRuntimeObjectGraph(item);
            return existing;
        }
    }
    std::vector<std::shared_ptr<Item>> replacement(_items);
    replacement.push_back(item);
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
    return item;
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
        item->clearOwner();
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
    item->clearOwner();

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
    setFacing(-glm::atan(dir.x, dir.y));
}

void Object::faceAwayFrom(const Object &other) {
    if (_id == other._id || _position == other.position())
        return;

    glm::vec2 dir(glm::normalize(_position - other.position()));
    setFacing(-glm::atan(dir.x, dir.y));
}

void Object::moveDropableItemsTo(Object &other) {
    for (auto it = _items.begin(); it != _items.end();) {
        if ((*it)->isDropable()) {
            std::shared_ptr<Item> item(*it);
            it = _items.erase(it);
            handOverItem(item, other);
        } else {
            ++it;
        }
    }
}

void Object::moveAllItemsTo(Object &other) {
    while (!_items.empty()) {
        auto item = _items.back();
        _items.pop_back();
        handOverItem(item, other);
    }
}

// An item a party member is handed goes to the party's inventory.
void Object::handOverItem(const std::shared_ptr<Item> &item, Object &other) {
    if (Creature *creature = dyn_cast<Creature>(this)) {
        creature->itemAttributes().removeItem(item);
    }
    item->clearOwner();
    _game.party().sharedInventoryReceiver(_game.getObjectById(other.id()))->addItem(item);
}

int Object::damageImmunity(int flags) const {
    int result = 0;
    for (size_t bit = 0; bit < _damageImmunity.size(); ++bit) {
        if ((flags & (1 << bit)) == 0) {
            continue;
        }
        const int value = _damageImmunity[bit];
        if (result == 0 || value < result) {
            result = value;
        }
    }
    return result;
}

void Object::setDamageImmunity(int flags, int value) {
    for (size_t bit = 0; bit < _damageImmunity.size(); ++bit) {
        if ((flags & (1 << bit)) == 0) {
            continue;
        }
        _damageImmunity[bit] = std::clamp(value, kMinDamageImmunity, kMaxDamageImmunity);
        return;
    }
}

bool Object::applyEffect(const std::shared_ptr<Effect> &effect,
                         DurationType durationType, float duration) {
    EffectInstance instance = effect->saveFacingInstance();
    if (!std::dynamic_pointer_cast<SavedEffectValue>(effect)) {
        instance.effect = effect;
    }
    instance.setDuration(durationType, duration);
    instance.skipOnLoad = false;
    instance.restoring = false;
    if (instance.type() != EffectType::LinkEffects) {
        return applyEffect(std::move(instance));
    }
    if (!instance.hasStableId()) {
        instance.id = _game.allocateEffectId();
    }
    const EffectId id = instance.id;
    _linkAwaitingLastingEffect = true;
    const bool applied = applyEffect(std::move(instance));
    if (_linkAwaitingLastingEffect) {
        removeEffectsById(id);
    }
    _linkAwaitingLastingEffect = false;
    return applied;
}

bool Object::applyEffect(EffectInstance effect) {
    if (!isRuntimeLive()) {
        return false;
    }
    if (effect.hasStableId()) {
        _game.importEffectId(effect.id);
    } else {
        effect.id = _game.allocateEffectId();
    }
    if (!effect.restoring) {
        effect.setDuration(effect.durationType(), effect.duration);
    }
    effect.materialize();
    return admitEffect(std::move(effect));
}

bool Object::restoreEffect(EffectInstance effect) {
    if (!effect.shouldRestoreOnLoad()) {
        return false;
    }
    effect.restoring = true;
    effect.materialize();
    return admitEffect(std::move(effect));
}

bool Object::admitEffect(EffectInstance effect) {
    const uint64_t start = _game.worldTimeMilliseconds();
    effect.skipOnLoad = effect.skipOnLoad || effect.exposed == 0;
    const auto result = effect.effect
                            ? effect.effect->onApply(*this, effect)
                            : EffectApplicationResult::Retained;
    if (result == EffectApplicationResult::Rejected) {
        return false;
    }
    if (result == EffectApplicationResult::Applied) {
        return true;
    }

    if (effect.durationType() == DurationType::Temporary) {
        if (effect.expiryOrigin == EffectExpiryOrigin::RuntimeCountdown ||
            effect.expiryOrigin == EffectExpiryOrigin::None) {
            const auto expiry = getEffectExpiryMilliseconds(start, effect.duration,
                                                            static_cast<uint32_t>(_game.millisecondsPerWorldDay()));
            effect.expiryDay = static_cast<uint32_t>(expiry / _game.millisecondsPerWorldDay());
            effect.expiryTime = static_cast<uint32_t>(expiry % _game.millisecondsPerWorldDay());
            effect.expiryOrigin = EffectExpiryOrigin::RuntimeAbsoluteGameTime;
        }
        effect.remainingDuration = _game.remainingEffectDuration(effect);
    }
    // Effect icons and visuals other than the two lasting ones are not lasting.
    static constexpr int kFirstLastingVisual = 5000;
    static constexpr int kSecondLastingVisual = 5001;
    const bool passing = effect.serializedType == SavedEffectTypes::icon ||
                         (effect.serializedType == SavedEffectTypes::visualEffect && effect.integerParameter(0) != kFirstLastingVisual &&
                          effect.integerParameter(0) != kSecondLastingVisual);
    if (!passing) {
        _linkAwaitingLastingEffect = false;
    }
    // Admission may consume a wrapper, change its duration, or insert children.
    // The order is assigned only after admission; equal types remain stable.
    effect.applicationOrder = _nextEffectApplicationOrder++;
    effect.restoring = false;
    auto position = std::find_if(_effects.begin(), _effects.end(),
                                 [&](const EffectInstance &candidate) {
                                     return candidate.serializedType > effect.serializedType;
                                 });
    auto *creature = dyn_cast<Creature>(this);
    if (creature) {
        creature->addArmorClassEffect(effect);
    }
    // An ability effect moves the current pool by the change in its maximum.
    const auto poolMaximum = creature ? creature->abilityPoolMaximum(effect) : std::nullopt;
    // Visibility consumers consult the canonical collection, so counter
    // effects must become visible there before perception (and OnNotice) runs.
    const bool changesVisibility = effect.type() == EffectType::Invisibility ||
                                   effect.type() == EffectType::SeeInvisible ||
                                   effect.type() == EffectType::Ultravision ||
                                   effect.type() == EffectType::TrueSeeing ||
                                   effect.type() == EffectType::Blindness;
    const auto admitted = _effects.insert(position, std::move(effect));
    if (creature) {
        creature->updateArmorClassEffectCursor();
        if (poolMaximum) {
            creature->followAbilityPool(*admitted, *poolMaximum);
        }
    }
    if (changesVisibility && creature) {
        creature->refreshVisibilityPerception();
    }
    return true;
}

size_t Object::removeEffectsById(EffectId id) {
    bool changesVisibility = false;
    // An ability effect moves the current pool by the change in its maximum.
    std::optional<int> poolMaximum;
    const auto count = detail::removeEffectPackage(_effects, id, _removingEffectApplications, [&](const EffectInstance &value) {
            poolMaximum.reset();
            if (value.effect) {
                value.effect->onRemove(*this, value);
            }
            if (auto *creature = dyn_cast<Creature>(this)) {
                creature->removeArmorClassEffect(value);
                poolMaximum = creature->abilityPoolMaximum(value);
            }
            changesVisibility |=
                value.type() == EffectType::Invisibility || value.type() == EffectType::Blindness; }, [&](const EffectInstance &value) {
            if (auto *creature = dyn_cast<Creature>(this)) {
                creature->updateArmorClassEffectCursor();
                if (poolMaximum) {
                    creature->followAbilityPool(value, *poolMaximum);
                }
            } });
    if (count && changesVisibility) {
        if (auto *creature = dyn_cast<Creature>(this)) {
            creature->refreshVisibilityPerception();
        }
    }
    return count;
}

bool Object::removeEffectApplication(uint64_t order) {
    bool changesVisibility = false;
    // An ability effect moves the current pool by the change in its maximum.
    std::optional<int> poolMaximum;
    const bool removed = detail::removeEffectApplication(_effects, order, _removingEffectApplications, [&](const EffectInstance &value) {
            poolMaximum.reset();
            if (value.effect) {
                value.effect->onRemove(*this, value);
            }
            if (auto *creature = dyn_cast<Creature>(this)) {
                creature->removeArmorClassEffect(value);
                poolMaximum = creature->abilityPoolMaximum(value);
            }
            changesVisibility |=
                value.type() == EffectType::Invisibility || value.type() == EffectType::Blindness; }, [&](const EffectInstance &value) {
            if (auto *creature = dyn_cast<Creature>(this)) {
                creature->updateArmorClassEffectCursor();
                if (poolMaximum) {
                    creature->followAbilityPool(value, *poolMaximum);
                }
            } });
    if (removed && changesVisibility) {
        if (auto *creature = dyn_cast<Creature>(this)) {
            creature->refreshVisibilityPerception();
        }
    }
    return removed;
}

EffectPackageApplicationResult Object::applyEffectPackage(const std::vector<EffectInstance> &members) {
    return detail::applyEffectPackageMembers(members, [&](const EffectInstance &member) {
        return applyEffect(member);
    });
}

void Object::updateEffects(float dt) {
    // The effects are kept sorted by saved type and, within a type, by
    // application order, and are walked in that order. Each step looks for the
    // effect after the last one walked, so an update that removes or adds
    // effects makes the walk neither skip nor repeat one; effects applied
    // during the walk wait for the next update.
    const uint64_t firstNew = _nextEffectApplicationOrder;
    std::optional<std::pair<uint16_t, uint64_t>> walked;
    while (true) {
        const auto next = std::find_if(_effects.begin(), _effects.end(), [&](const EffectInstance &effect) {
            return effect.applicationOrder < firstNew &&
                   (!walked || std::make_pair(effect.serializedType, effect.applicationOrder) > *walked);
        });
        if (next == _effects.end()) {
            break;
        }
        const uint64_t order = next->applicationOrder;
        walked = std::make_pair(next->serializedType, order);
        // The executable is held while it updates, which may remove its record.
        if (const auto executable = next->effect) {
            executable->onUpdate(*this, *next, dt);
        }
        auto found = findEffectApplication(order);
        if (!found) {
            continue;
        }
        auto &effect = found->get();
        const bool retired = effect.durationType() == DurationType::Equipped &&
                             !effect.hasLiveRuntimeSource();
        const bool temporary = effect.durationType() == DurationType::Temporary;
        if (temporary) {
            effect.remainingDuration = _game.remainingEffectDuration(effect);
        }
        const auto expiry = static_cast<uint64_t>(effect.expiryDay) * _game.millisecondsPerWorldDay() +
                            effect.expiryTime;
        // An object acting through a time stop expires its effects on its own clock.
        if (retired || (temporary && _game.activeTimeMilliseconds(*this) > expiry)) {
            const EffectId id = effect.id;
            removeEffectsById(id);
        }
    }
}

void Object::addShownVisual(const EffectInstance &visual, float seconds) {
    ShownVisual shown {visual, seconds};
    shown.visual.applicationOrder = _nextEffectApplicationOrder++;
    _shownVisuals.push_back(std::move(shown));
}

std::vector<std::reference_wrapper<const EffectInstance>> Object::shownVisuals() const {
    std::vector<std::reference_wrapper<const EffectInstance>> visuals;
    for (const auto &shown : _shownVisuals) {
        visuals.push_back(std::cref(shown.visual));
    }
    return visuals;
}

void Object::updateShownVisuals(float dt) {
    for (size_t index = 0; index < _shownVisuals.size();) {
        _shownVisuals[index].visual.effect->onUpdate(*this, _shownVisuals[index].visual, dt);
        _shownVisuals[index].remaining -= dt;
        if (_shownVisuals[index].remaining > 0.0f) {
            ++index;
            continue;
        }
        auto ended = std::move(_shownVisuals[index]);
        _shownVisuals.erase(_shownVisuals.begin() + static_cast<std::ptrdiff_t>(index));
        ended.visual.effect->onRemove(*this, ended.visual);
    }
}

void Object::playAnimation(AnimationType animation, AnimationProperties properties) {
}

bool Object::isSelectable() const {
    return false;
}

// An object is marked at its look-at hook, or at the centre of its body
// without one.
glm::vec3 Object::getSelectablePosition() const {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model) {
        return _position;
    }
    if (auto *hook = model->getNodeByName("lookathook")) {
        return hook->origin();
    }
    return model->getWorldCenterOfAABB();
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
    if (_spatialArea) {
        _spatialArea->updateObjectSpatialIndex(*this);
    }
    updateTransform();
}

void Object::updateTransform() {
    _transform = glm::translate(glm::mat4(1.0f), _position);
    _transform *= glm::mat4_cast(presentedOrientation());

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
        _sceneNode->setEnabled(_visible && _clientPresent);
    }
}

void Object::setClientPresent(bool present) {
    if (_clientPresent == present) {
        return;
    }

    _clientPresent = present;

    if (_sceneNode) {
        _sceneNode->setEnabled(_visible && _clientPresent);
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

void Object::clearAllEffects(bool retainOwn) {
    const bool hadEffects = !_effects.empty();
    // Innate and equipped effects and the internal state record (type 9) stay;
    // the internal state follows the state effects as they are removed.
    const auto clears = [&](const EffectInstance &effect) {
        return effect.durationType() != DurationType::Innate &&
               effect.durationType() != DurationType::Equipped &&
               effect.serializedType != SavedEffectTypes::setStateInternal &&
               !(retainOwn && effect.boundCreator().get() == this);
    };
    // Effects leave one at a time from the end of the list. A removal can add
    // or remove other effects, so every removal starts again from the end.
    std::vector<uint64_t> tried;
    for (;;) {
        auto next = std::find_if(_effects.rbegin(), _effects.rend(), [&](const EffectInstance &effect) {
            return clears(effect) &&
                   std::find(tried.begin(), tried.end(), effect.applicationOrder) == tried.end();
        });
        if (next == _effects.rend()) {
            break;
        }
        tried.push_back(next->applicationOrder);
        removeEffectApplication(next->applicationOrder);
    }
    // Clearing a non-empty effect list leaves the object commandable.
    if (hadEffects) {
        setCommandable(true);
    }
}

void Object::queueScriptEffectRemoval(ScriptEffectRemovalMatch match, const EffectInstance &value) {
    for (EffectId id : selectScriptEffectRemovals(_effects, match, value)) {
        _game.queueEffectRemoval(*this, id);
    }
}

bool Object::hasEffect(EffectType type) const {
    return std::any_of(
        _effects.begin(),
        _effects.end(),
        [type](const EffectInstance &applied) {
            return applied.hasLiveRuntimeSource() &&
                   applied.type() == type;
        });
}

bool Object::hasSpellEffect(int spell) const {
    // No spell is never applied.
    if (spell == -1) {
        return false;
    }
    return std::any_of(
        _effects.begin(),
        _effects.end(),
        [spell](const EffectInstance &applied) {
            return static_cast<int32_t>(applied.spellId) == spell;
        });
}

void Object::replaceEffectState(std::deque<EffectInstance> replacement) noexcept {
    std::vector<uint64_t> departures;
    for (const auto &record : _effects) {
        const bool survives = std::any_of(replacement.begin(), replacement.end(),
                                          [&](const EffectInstance &candidate) {
                                              return candidate.applicationOrder == record.applicationOrder;
                                          });
        if (!survives) {
            departures.push_back(record.applicationOrder);
        }
    }
    for (const auto order : departures) {
        removeEffectApplication(order);
    }
    // The ownership edge was committed by the equipment transaction. Existing
    // canonical records (including callback mutations) stay in place; only the
    // newly materialized equipment entries have no application order yet.
    for (auto &record : replacement) {
        if (record.applicationOrder == 0) {
            admitEffect(std::move(record));
        }
    }
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

void Object::damage(
    int amount,
    const std::shared_ptr<Object> &damager) {
}

void Object::startStuntMode() {
    _placedAtOrigin = false;
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

void Object::setPlacedAtOrigin(bool placed) {
    if (!placed) {
        if (_placedAtOrigin) {
            stopStuntMode();
            _placedAtOrigin = false;
            updateTransform();
        }
        return;
    }
    _playingCutsceneAnimation = true;
    if (!_stunt) {
        startStuntMode();
        _placedAtOrigin = true;
    }
}

bool Object::holdsCutsceneAnimation() const {
    return _playingCutsceneAnimation && _game.isAnimatedCutsceneShowing();
}

std::shared_ptr<Effect> Object::getFirstEffect() {
    _effectIndex = 0;
    return getNextEffect();
}

std::shared_ptr<Effect> Object::getNextEffect() {
    while (_effectIndex < _effects.size()) {
        const auto &record = _effects[_effectIndex++];
        if (record.isScriptEnumerable()) {
            return std::make_shared<SavedEffectValue>(record);
        }
    }
    return nullptr;
}

void Object::applyDamageEffect(
    int amount,
    const std::shared_ptr<Object> &damager,
    std::optional<DamageReaction>) {

    damage(amount, damager);
}

int Object::getLastDamageAmountByType(int damageTypeFlag) const {
    return getDamageAmountByType(_lastDamageAmounts, damageTypeFlag);
}

int Object::getTotalDamageDealt() const {
    return getTotalDamageAmounts(_lastDamageAmounts);
}

std::optional<std::reference_wrapper<EffectInstance>> Object::findEffectApplication(uint64_t applicationOrder) {
    auto it = std::find_if(_effects.begin(), _effects.end(),
                           [applicationOrder](const EffectInstance &instance) {
                               return instance.applicationOrder == applicationOrder;
                           });
    if (it == _effects.end()) {
        return std::nullopt;
    }
    return *it;
}

void Object::setFeedbackText(std::string text, float duration) {
    _feedbackText = std::move(text);
    _feedbackTextRemaining = std::max(0.0f, duration);
    if (_feedbackTextRemaining == 0.0f) {
        _feedbackText.clear();
    }
}

} // namespace game

} // namespace reone
