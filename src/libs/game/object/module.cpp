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

#include "reone/game/object/module.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <utility>

#include <boost/algorithm/string/case_conv.hpp>

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/game/action/attackobject.h"
#include "reone/game/action/opencontainer.h"
#include "reone/game/action/opendoor.h"
#include "reone/game/action/startconversation.h"
#include "reone/game/attack.h"
#include "reone/game/castspell.h"
#include "reone/game/combatfeedback.h"
#include "reone/game/d20/spells.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/gui/sounds.h"
#include "reone/game/location.h"
#include "reone/game/object/door.h"
#include "reone/game/object/encounter.h"
#include "reone/game/object/item.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/trigger.h"
#include "reone/game/party.h"
#include "reone/game/projectiles.h"
#include "reone/game/reputes.h"
#include "reone/game/script/runner.h"
#include "reone/game/script/savedsituation.h"
#include "reone/game/twodautil.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/resources.h"
#include "reone/resource/strings.h"
#include "reone/system/exception/validation.h"
#include "reone/system/logutil.h"

#include "../action/commonactions.h"

using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace game {

namespace detail {
// Pending events are ordered by world deadline and module-lifetime handle.
// Removing the head preserves chronological and FIFO order while cancellation
// uses stable handles. Delivered entries do not remain as tombstones. Delivery
// stops while the world is held.
template <class Events, class Clock, class Deliver, class Held>
void dispatchDueSavedEvents(Events &events, bool &dispatching, Clock now, Deliver deliver, Held held) {
    // Observe appended events after the current callback returns.
    // Do not recursively enter the same delivery pump.
    if (dispatching) {
        return;
    }
    dispatching = true;
    struct Dispatch {
        bool &active;
        ~Dispatch() { active = false; }
    } dispatch {dispatching};
    while (!events.empty() && !held()) {
        const auto next = events.begin();
        if (next->first.first > now()) {
            return;
        }
        auto node = events.extract(next);
        auto event = std::move(node.mapped());
        // Destroy the queue node before invoking any user/game callback.
        node = {};
        deliver(event);
    }
}
} // namespace detail

// A harmful spell at a door or placeable excites its caster for the spell row
// of excitedduration.
static constexpr int kSpellCasterExcitedState = 2;
// A broadcast bolt leaves from its source's impact node, which hand 2 names.
static constexpr int kSafeProjectileHand = 2;

// The delay in milliseconds before a destroyed creature's body fades; blank
// keeps the body as a corpse.
static std::optional<int> readFadeDelayOnDeath(const resource::TwoDA &table, int row) {
    return table.getIntOpt(row, "fadedelayondeath");
}

// The droid power offered against a mine (TSL). Minor mine types allow
// Destroy, else Disable, else Stun; average types Destroy, else Disable; the
// stronger odd types from 17 to 25 Destroy only; any other type none.
std::shared_ptr<Spell> Module::mineForcePower(const Trigger &trigger, const Creature &leader) const {
    // The mine types of each kind, as bits by trap type: minor (0, 3, 6, 9
    // and 14), average (1, 4, 7, 10 and 15), and the stronger odd types.
    static constexpr uint32_t kMinorMineTypes = 0x4249;
    static constexpr uint32_t kAverageMineTypes = 0x8492;
    static constexpr uint32_t kStrongMineTypes = 0x2aa0000;
    static constexpr int kMineTypeBits = 32;
    if (!_game.isTSL()) {
        return nullptr;
    }
    const int trapType = trigger.trapBaseType();
    auto inMask = [trapType](uint32_t mask) { return trapType < kMineTypeBits && ((mask >> trapType) & 1) != 0; };
    std::vector<SpellType> powers;
    if (inMask(kMinorMineTypes)) {
        powers = {SpellType::DroidDestroy, SpellType::DroidDisable, SpellType::DroidStun};
    } else if (inMask(kAverageMineTypes)) {
        powers = {SpellType::DroidDestroy, SpellType::DroidDisable};
    } else if (inMask(kStrongMineTypes)) {
        powers = {SpellType::DroidDestroy};
    }
    for (SpellType power : powers) {
        if (leader.attributes().hasSpell(power)) {
            return _services.game.spells.get(power);
        }
    }
    return nullptr;
}

// Mine kits the leader may set on a locked door or placeable (TSL).
static void addMineKitActions(std::vector<ContextAction> &actions, Game &game, const Area &area,
                              const std::shared_ptr<Creature> &leader) {
    if (!game.isTSL() || !leader->attributes().hasSkill(SkillType::Demolitions)) {
        return;
    }
    if (area.playerRestrictMode()) {
        return;
    }
    auto inventory = game.party().sharedInventoryReceiver(leader);
    if (!inventory) {
        return;
    }
    for (const auto &item : inventory->items()) {
        if (item->isMineKit()) {
            actions.push_back(ContextAction(SkillType::Demolitions, 0, item));
        }
    }
}

static bool canUseSecurityOnPlaceable(const Placeable &placeable, const Creature &actor) {
    return placeable.hasInventory() &&
           placeable.isLocked() &&
           !placeable.isKeyRequired() &&
           actor.attributes().hasSkill(SkillType::Security);
}

// Standing toward the placeable plays no part in whether it can be bashed.
static bool canBashPlaceable(const Placeable &placeable) {
    return placeable.hasInventory() &&
           placeable.isLocked() &&
           placeable.isSelectable() &&
           !placeable.isDead() &&
           !placeable.plotFlag() &&
           !placeable.isNotBlastable() &&
           (placeable.hitPoints() > 0 || placeable.currentHitPoints() > 0);
}

void Module::load(std::string name, const Gff &ifo, bool restoreSavedWorld) {
    auto parsed = resource::generated::parseIFO(ifo);
    if (parsed.Mod_Entry_Area.empty()) {
        throw ValidationException("Mod_Entry_Area must not be empty");
    }
    auto are = _services.resource.gffs.get(parsed.Mod_Entry_Area, ResType::Are);
    if (!are) {
        throw ResourceNotFoundException(
            "Area ARE not found: " + parsed.Mod_Entry_Area);
    }
    auto git = _services.resource.gffs.get(parsed.Mod_Entry_Area, ResType::Git);
    if (!git) {
        throw ResourceNotFoundException(
            "Area GIT not found: " + parsed.Mod_Entry_Area);
    }
    load(std::move(name), ifo, *are, *git, restoreSavedWorld);
    if (!restoreSavedWorld) {
        runSpawnScripts();
    }
}

void Module::load(
    std::string name,
    const Gff &ifo,
    const Gff &are,
    const Gff &git,
    bool restoreSavedWorld) {
    _name = std::move(name);
    if (restoreSavedWorld) {
        _game.captureSaveResourceShadow(
            {SaveResourceKind::ModuleIfo, _name}, ifo);
    }
    _publishedSavedEvents.clear();
    _savedEventsPublished = false;

    auto ifoParsed = resource::generated::parseIFO(ifo);
    _isSaveGame = restoreSavedWorld && ifoParsed.Mod_IsSaveGame != 0;
    _projectilesAwaitingRestore = restoreSavedWorld;
    if (restoreSavedWorld) {
        const auto identityContext =
            SerializedIdentityContext::moduleGraph(_name);
        deserializeRuntimeState(ifo, identityContext);
        deserializeSavedEventQueue(ifo, identityContext);
        _savedProjectiles.clear();
        for (const auto &record : ifo.getList("ProjectileState")) {
            _savedProjectiles.push_back(SavedProjectile::fromGff(*record, identityContext));
        }
        loadLimboCreatures(ifo);
    } else {
        _pendingSavedEvents.clear();
        _savedProjectiles.clear();
    }
    loadInfo(ifoParsed);
    loadArea(ifoParsed, are, git, restoreSavedWorld);
    // Outside a saved game, each creature placed from the area's instance
    // list is announced to the area.
    if (!_isSaveGame) {
        for (const auto &object : _area->getObjectsByType(ObjectType::Creature)) {
            _area->signalEntered(static_cast<Creature &>(*object));
        }
    }

    _area->initCameras(_info.entryPosition, _info.entryFacing);

    loadPlayer();

}

void Module::activate() {
    _area->activate();
}

void Module::loadInfo(const resource::generated::IFO &ifo) {
    // Mod_Name is a localized string: KotOR II modules carry a talk table
    // reference, while KotOR modules usually carry the text inline. LocString
    // resolves whichever form is present, and yields an empty string when
    // neither is.
    _localizedName = LocString(
                         ifo.Mod_Name.first,
                         ifo.Mod_Name.second,
                         _services.resource.strings)
                         .str();

    // Entry location

    _info.entryArea = ifo.Mod_Entry_Area;
    if (_info.entryArea.empty()) {
        throw ValidationException("Mod_Entry_Area must not be empty");
    }

    _info.entryPosition.x = ifo.Mod_Entry_X;
    _info.entryPosition.y = ifo.Mod_Entry_Y;
    _info.entryPosition.z = ifo.Mod_Entry_Z;

    float dirX = ifo.Mod_Entry_Dir_X;
    float dirY = ifo.Mod_Entry_Dir_Y;
    _info.entryFacing = -glm::atan(dirX, dirY);

    _info.onModLoad = boost::to_lower_copy(ifo.Mod_OnModLoad);
    _info.onModStart = boost::to_lower_copy(ifo.Mod_OnModStart);
    _info.onActivateItem = boost::to_lower_copy(ifo.Mod_OnActvtItem);
    _info.onAcquireItem = boost::to_lower_copy(ifo.Mod_OnAcquirItem);
    _info.onUnacquireItem = boost::to_lower_copy(ifo.Mod_OnUnAqreItem);
    _info.onPlayerDeath = boost::to_lower_copy(ifo.Mod_OnPlrDeath);
    _onHeartbeat = boost::to_lower_copy(ifo.Mod_OnHeartbeat);

    _info.dawnHour = ifo.Mod_DawnHour;
    _info.duskHour = ifo.Mod_DuskHour;
}

void Module::loadArea(
    const resource::generated::IFO &ifo,
    const Gff &are,
    const Gff &git,
    bool restoreSavedWorld) {
    reone::info("Load area '" + _info.entryArea + "'");

    const auto identityContext = restoreSavedWorld
                                     ? SerializedIdentityContext::moduleGraph(_name)
                                     : SerializedIdentityContext::templateResource(_name);
    std::shared_ptr<Area> candidateArea;
    std::vector<std::shared_ptr<Object>> noObsolete;
    _game.replaceRuntimeObjectGraph(
        noObsolete,
        [&]() {
            if (restoreSavedWorld) {
                auto area = std::find_if(
                    ifo.Mod_Area_list.begin(),
                    ifo.Mod_Area_list.end(),
                    [this](const auto &entry) {
                        return entry.Area_Name == _info.entryArea;
                    });
                uint32_t areaId = area == ifo.Mod_Area_list.end()
                                      ? 1
                                      : area->ObjectId;
                candidateArea = _game.newSavedArea(areaId, identityContext);
            } else {
                candidateArea = _game.newArea();
            }
            candidateArea->load(
                _info.entryArea, are, git, identityContext);
        },
        [&]() noexcept {
            _area = std::move(candidateArea);
        });

}

void Module::runSpawnScripts() {
    _area->runSpawnScripts();
}
void Module::loadLimboCreatures(const resource::Gff &ifo) {
    _limboCreatures.clear();
    const auto identityContext = SerializedIdentityContext::moduleGraph(_name);
    for (const auto &creatureGff : ifo.getList("Creature List")) {
        auto creature = _game.newCreature(*creatureGff, identityContext);
        creature->captureSaveRecord(
            *creatureGff,
            identityContext,
            {SaveRecordOriginKind::ModuleLimboCreature, _name});
        _limboCreatures.push_back(std::move(creature));
    }
}

void Module::loadPlayer() {
    _player = std::make_unique<Player>(*this, *_area, *_area->getCamera(CameraType::ThirdPerson), _game.party());
}

void Module::loadParty(const std::string &entry, bool preserveSavedPlacement) {
    glm::vec3 position(0.0f);
    float facing = 0.0f;
    getEntryPoint(entry, position, facing);

    _area->loadParty(position, facing, preserveSavedPlacement);
    _area->onPartyLeaderMoved(true);
    _area->update3rdPersonCameraFacing();
    // Placing the party ends its stealth and leaves solo mode.
    _game.party().setSoloMode(false);
}

// The event carries whether a saved game was being loaded when the module
// finished loading.
void Module::signalLoaded() {
    _game.queueScriptEvent(*this, *this,
                           Event(static_cast<int>(SavedSignalEventType::ModuleLoaded), {static_cast<int32_t>(_game.isLoadingFromSaveGame())}, {}, {}, {}));
}

// The load script runs with the load-from-save answer the event carries. The
// arrival of the creature the player controls is then announced to the area
// like any other entry, with the same answer, unless the load script has sent
// the party on to another module.
void Module::receiveLoadedSignal(bool loadFromSaveGame) {
    struct LoadFromSaveScope {
        Game &game;
        bool live;
        ~LoadFromSaveScope() { game.setLoadingFromSaveGame(live); }
    } scope {_game, _game.isLoadingFromSaveGame()};
    _game.setLoadingFromSaveGame(loadFromSaveGame);
    runOnLoadScript();
    if (_game.isModuleTransitionScheduled()) {
        return;
    }
    if (auto leader = _game.party().getLeader()) {
        _area->signalEntered(*leader);
    }
}

void Module::runOnLoadScript() {
    if (_info.onModLoad.empty()) {
        return;
    }
    _game.scriptRunner().run(_info.onModLoad, id());
}

void Module::runOnStartScript() {
    if (_info.onModStart.empty()) {
        return;
    }
    _game.scriptRunner().run(_info.onModStart, id());
}

void Module::getEntryPoint(const std::string &waypoint, glm::vec3 &position, float &facing) const {
    position = _info.entryPosition;
    facing = _info.entryFacing;

    if (!waypoint.empty()) {
        std::shared_ptr<Object> object(
            _area->getObjectByTag(boost::to_lower_copy(waypoint)));
        if (object) {
            position = object->position();
            facing = object->getFacing();
        } else {
            debug("Module entry '" + waypoint + "' not found; using default entry");
        }
    }
}

bool Module::handle(const input::Event &event) {
    if (_player && _player->handle(event))
        return true;
    if (_area->handle(event))
        return true;

    switch (event.type) {
    case input::EventType::MouseMotion:
        if (handleMouseMotion(event.motion))
            return true;
        break;
    case input::EventType::MouseButtonDown:
        if (handleMouseButtonDown(event.button))
            return true;
        break;
    case input::EventType::KeyDown:
        if (handleKeyDown(event.key))
            return true;
        break;
    default:
        break;
    }

    return false;
}

bool Module::handleMouseMotion(const input::MouseMotionEvent &event) {
    CursorType cursor = CursorType::Default;

    auto object = _area->getObjectAt(event.x, event.y);
    if (object && object->isSelectable()) {
        auto objectPtr = _game.getObjectById(object->id());
        _area->hilightObject(objectPtr);

        bool hostile = object->type() == ObjectType::Creature && !object->isDead()
            ? isHostileToPartyLeader(*static_cast<Creature *>(object)) : false;
        cursor = contextualCursor(object->type(), object->isDead(), hostile);
    } else {
        _area->hilightObject(nullptr);
    }

    _game.setCursorType(cursor);

    return true;
}

bool Module::handleMouseButtonDown(const input::MouseButtonEvent &event) {
    if (event.button != input::MouseButton::Left) {
        return false;
    }
    auto object = _area->getObjectAt(event.x, event.y);
    if (!object || !object->isSelectable()) {
        return false;
    }
    auto objectPtr = _game.getObjectById(object->id());
    if (!objectPtr) {
        return false;
    }
    // World-click handling publishes the selected target before
    // interaction. Repeat clicks must rebind it too; hover and programmatic
    // selection are not substitutes for this player-input producer.
    _game.setLastTarget(objectPtr->id());
    auto selectedObject = _area->selectedObject();
    if (objectPtr != selectedObject) {
        _area->selectObject(objectPtr);
        return true;
    }
    onObjectClick(objectPtr);

    return true;
}

void Module::onObjectClick(const std::shared_ptr<Object> &object, bool sound) {
    if (!_game.party().getLeader()) {
        return;
    }
    switch (object->type()) {
    case ObjectType::Creature:
        onCreatureClick(std::static_pointer_cast<Creature>(object), sound);
        break;
    case ObjectType::Door:
        onDoorClick(std::static_pointer_cast<Door>(object));
        break;
    case ObjectType::Placeable:
        onPlaceableClick(std::static_pointer_cast<Placeable>(object));
        break;
    default:
        break;
    }
}

// With no one under control nothing is hostile to the leader.
bool Module::isHostileToPartyLeader(const Creature &creature) const {
    const auto leader = _game.party().getLeader();
    if (!leader || creature.isDead()) {
        return false;
    }
    return _services.game.reputes.getIsEnemy(creature, *leader);
}

void Module::onCreatureClick(const std::shared_ptr<Creature> &creature, bool sound) {
    debug(str(boost::format("Module: click: creature '%s', faction %d") % creature->tag() % static_cast<int>(creature->faction())));

    std::shared_ptr<Creature> partyLeader(_game.party().getLeader());

    // A dead creature is looted through its body bag, never directly.
    if (creature->isDead()) {
        return;
    }
    if (isHostileToPartyLeader(*creature)) {
        // The default action on a hostile creature is the menu attack,
        // which a restricted area does not offer. A click plays its sound.
        if (_area && _area->playerRestrictMode()) {
            return;
        }
        if (sound) {
            if (auto clip = _services.game.guiSounds.getActionAccepted()) {
                _services.audio.mixer.play(std::move(clip), audio::AudioType::Sound);
            }
        }
        if (_game.prepareMenuAttack(*partyLeader, creature)) {
            _game.sendAttack(*partyLeader, creature);
        }
    } else if (!creature->conversation().empty() && !creature->isInCombat()) {
        // A creature in combat does not answer a click to talk, and nobody
        // does while clicks are shut off.
        if (!_game.canClick()) {
            return;
        }
        partyLeader->clearAllActions();
        partyLeader->addAction(_game.newAction<StartConversationAction>(creature, ""));
    }
}

// Opening or using an object is an order a creature that cannot be commanded
// does not take. A door's default action first clears the leader's orders, as
// the player's controls clear them.
void Module::onDoorClick(const std::shared_ptr<Door> &door) {
    _game.combat().clearAllOrders(*_game.party().getLeader());
    if (!_game.party().getLeader()->isCommandable()) {
        return;
    }
    if (!door->linkedToModule().empty() && door->getOnOpen().empty()) {
        std::shared_ptr<Creature> partyLeader(_game.party().getLeader());
        if (door->isLocked()) {
            tryUnlockDoorWithKey(_game, *door, *partyLeader, _game.party());
        }
        if (door->isLocked()) {
            signalFailToOpen(*door, *partyLeader);
            return;
        }
        _game.scheduleModuleTransition(door->linkedToModule(), door->linkedTo());
        return;
    }
    if (!door->isOpen()) {
        std::shared_ptr<Creature> partyLeader(_game.party().getLeader());
        partyLeader->addAction(_game.newAction<OpenDoorAction>(door));
    }
}

void Module::onPlaceableClick(const std::shared_ptr<Placeable> &placeable) {
    std::shared_ptr<Creature> partyLeader(_game.party().getLeader());
    if (!partyLeader->isCommandable() || !_game.canClick()) {
        return;
    }

    if (placeable->hasInventory()) {
        partyLeader->clearAllActions();
        if (!placeable->isLocked()) {
            partyLeader->addAction(_game.newAction<OpenContainerAction>(placeable));
        }
    } else if (!placeable->conversation().empty()) {
        partyLeader->clearAllActions();
        partyLeader->addAction(_game.newAction<StartConversationAction>(placeable, ""));
    } else {
        partyLeader->clearAllActions();
        partyLeader->addAction(_game.newAction<OpenContainerAction>(placeable));
    }
}

static constexpr uint32_t kBodyBagDelayMilliseconds = 500;
static constexpr int kDefaultBodyBagAppearance = 3;

// A dead creature or a destroyed placeable leaves its items in a body bag,
// added to the area half a second later. A creature whose bag row is a
// corpse leaves one even with nothing to hold.
uint32_t Module::spawnBodyBag(Object &source) {
    auto *creature = dyn_cast<Creature>(&source);
    if (creature) {
        creature->stripHandWeapons();
    }
    auto area = source.spatialArea();
    if (!area) {
        return script::kObjectInvalid;
    }
    // A placeable with nothing to hold leaves no bag.
    if (!creature && source.items().empty()) {
        return script::kObjectInvalid;
    }
    const auto bodyBags = getRequiredTwoDA(_services.resource.twoDas, "bodybag");
    std::optional<int> appearance;
    std::optional<int> nameStrRef;
    bool corpse = false;
    if (creature) {
        // A bag row of 0 takes the appearance's row.
        const auto appearances = getRequiredTwoDA(_services.resource.twoDas, "appearance");
        const int appearanceRow = appearances->getIntOpt(creature->appearance(), "body_bag").value_or(0);
        const int row = creature->bodyBagRow() != 0 ? creature->bodyBagRow() : appearanceRow;
        corpse = bodyBags->getIntOpt(row, "corpse").value_or(0) != 0;
        if (!corpse && creature->dropableItems().empty() && creature->gold() == 0) {
            return script::kObjectInvalid;
        }
        appearance = bodyBags->getIntOpt(creature->bodyBagRow(), "appearance");
        if (!appearance) {
            appearance = bodyBags->getIntOpt(appearanceRow, "appearance");
        }
        nameStrRef = bodyBags->getIntOpt(creature->bodyBagRow(), "name");
    } else {
        const auto &placeable = cast<Placeable>(source);
        appearance = bodyBags->getIntOpt(placeable.bodyBagRow(), "appearance");
        nameStrRef = bodyBags->getIntOpt(placeable.bodyBagRow(), "name");
    }
    std::vector<std::shared_ptr<Object>> noObsolete;
    std::shared_ptr<Placeable> bag;
    _game.replaceRuntimeObjectGraph(
        noObsolete,
        [&]() {
            bag = _game.newPlaceable();
            bag->loadBodyBag(appearance.value_or(kDefaultBodyBagAppearance));
        },
        []() noexcept {});
    bag->fillBodyBag(source, corpse, nameStrRef);

    SavedEventRecord event;
    const uint64_t when = _game.worldTimeMilliseconds() + kBodyBagDelayMilliseconds;
    event.day = static_cast<uint32_t>(when / _game.millisecondsPerWorldDay());
    event.time = static_cast<uint32_t>(when % _game.millisecondsPerWorldDay());
    event.object = SavedObjectReference::fromRuntimeId(area->get().id());
    event.caller = SavedObjectReference::fromRuntimeId(source.id());
    event.eventId = static_cast<uint32_t>(SavedEventType::SpawnBodyBag);
    event.payload = SavedBodyBag {SavedObjectReference::fromRuntimeId(bag->id()), source.position()};
    enqueueSaveEvent(std::move(event));
    return bag->id();
}

size_t Module::pendingSavedEventCount() const {
    return _pendingSavedEvents.size();
}

void Module::deserializeSavedEventQueue(
    const resource::Gff &ifo,
    const SerializedIdentityContext &identityContext) {
    auto queue = SavedEventQueue::fromGff(ifo, identityContext);
    _pendingSavedEvents.clear();
    _publishedSavedEvents.clear();
    _savedEventsPublished = false;
    // An event that cannot be delivered is not kept.
    for (auto &event : queue.events) {
        const auto index = _nextSavedEventIndex++;
        if (event.shouldRestore() && event.executionSupport() == SavedExecutionSupport::Executable) {
            _pendingSavedEvents.emplace(index, PendingSavedEvent {std::move(event), false});
        }
    }
}

// Binds every reference of a queued event. Only the target decides whether
// the event can be delivered; nothing else is looked up.
static bool bindForDelivery(SavedEventRecord &event, const Game &game) {
    event.bindObjectReferences(game);
    return event.object.isInvalid() || event.object.boundObject() != nullptr;
}

std::vector<SavedEventRecord> Module::saveEventSnapshot() const {
    std::vector<SavedEventRecord> result;
    result.reserve(_pendingSavedEvents.size());
    for (const auto &[index, pending] : _pendingSavedEvents) {
        result.push_back(pending.record);
        if (!pending.command) {
            continue;
        }
        // A delayed command is saved as a timed event:
        // the script situation of its DoCommand continuation.
        auto savedAction = pending.command->saveFacingState();
        const auto *situation =
            savedAction && savedAction->type() == SavedActionType::DoCommand && savedAction->parameters.size() == 1
                ? std::get_if<SerializedScriptSituation>(&savedAction->parameters.front().payload)
                : nullptr;
        if (!situation) {
            std::ostringstream message;
            message << "live delayed action has no serializable timed-event representation"
                    << ": ownerId=" << pending.record.object.id
                    << " eventIndex=" << index
                    << " actionType=" << static_cast<int>(pending.command->type());
            throw ValidationException(message.str());
        }
        result.back().payload = *situation;
    }
    return result;
}

size_t Module::enqueueSaveEvent(SavedEventRecord event, std::shared_ptr<Action> command, bool keepsCallerFade) {
    // Bind before snapshot/publication, while this registry owns the domain.
    const bool targetBound = bindForDelivery(event, _game);
    return enqueueBoundSaveEvent(std::move(event), targetBound, std::move(command), keepsCallerFade);
}

size_t Module::enqueueBoundSaveEvent(
    SavedEventRecord event, bool targetBound, std::shared_ptr<Action> command, bool keepsCallerFade) {
    // The caller has bound the target already.
    const auto index = _nextSavedEventIndex++;
    PendingSavedEvent pending {std::move(event), targetBound};
    pending.command = std::move(command);
    pending.keepsCallerFade = keepsCallerFade;
    _pendingSavedEvents.emplace(index, std::move(pending));
    if (_savedEventsPublished) {
        publishSavedEvent(index);
    }
    return index;
}

bool Module::cancelSaveEvent(size_t index) {
    const auto pending = _pendingSavedEvents.find(index);
    if (pending == _pendingSavedEvents.end()) {
        return false;
    }
    if (const auto due = pending->second.publishedDueMilliseconds) {
        _publishedSavedEvents.erase(SavedEventDeadline {*due, index});
    }
    _pendingSavedEvents.erase(pending);
    return true;
}

void Module::bindSavedEventQueue() {
    for (auto &[index, pending] : _pendingSavedEvents) {
        pending.targetBound = !pending.record.shouldRestore() ||
                              bindForDelivery(pending.record, _game);
    }
}

void Module::restoreProjectilePresentations() {
    if (!_projectilesAwaitingRestore) {
        return;
    }
    _projectilesAwaitingRestore = false;
    _services.game.projectiles.restorePresentations(std::move(_savedProjectiles), _game, _services);
    _savedProjectiles.clear();
}
void Module::publishSavedEventQueue() {
    if (_savedEventsPublished) {
        return;
    }
    _publishedSavedEvents.clear();
    // Publication may drop the event it publishes.
    for (auto it = _pendingSavedEvents.begin(); it != _pendingSavedEvents.end();) {
        const auto index = it->first;
        ++it;
        publishSavedEvent(index);
    }
    _savedEventsPublished = true;
}

void Module::publishSavedEvent(size_t index) {
    auto &pending = _pendingSavedEvents.find(index)->second;
    const auto &savedEvent = pending.record;
    if (!savedEvent.shouldRestore() || !pending.targetBound) {
        return;
    }
    if (!pending.command && savedEvent.executionSupport() != SavedExecutionSupport::Executable) {
        return;
    }
    PublishedSavedEvent event;
    event.savedIndex = index;
    event.dueMilliseconds = static_cast<uint64_t>(savedEvent.day) * _game.millisecondsPerWorldDay() + savedEvent.time;
    event.command = pending.command;
    event.keepsCallerFade = pending.keepsCallerFade;
    if (!pending.command && savedEvent.eventId == static_cast<uint32_t>(SavedEventType::Timed)) {
        auto situation = std::get_if<SerializedScriptSituation>(&savedEvent.payload);
        if (!situation) {
            return;
        }
        SavedScriptSituationImporter importer(_game, _services.resource.scripts);
        auto imported = importer.import(*situation);
        // A situation that cannot be resumed is dropped with its event.
        if (!imported) {
            warn("Module: dropping a timed event that cannot be resumed: " + imported.message);
            _pendingSavedEvents.erase(index);
            return;
        }
        event.continuation = std::move(imported.continuation);
    }
    const SavedEventDeadline deadline {event.dueMilliseconds, index};
    _publishedSavedEvents.emplace(deadline, std::move(event));
    pending.publishedDueMilliseconds = deadline.first;
}

void Module::dispatchDueSavedEvents() {
    publishSavedEventQueue();
    detail::dispatchDueSavedEvents(_publishedSavedEvents, _dispatchingSavedEvents, [&]() { return _game.worldTimeMilliseconds(); }, [&](PublishedSavedEvent &event) { deliverSavedEvent(event); }, [&]() { return static_cast<bool>(_game.movie()); });
}

void Module::cancelObjectDestruction(const Object &object) {
    for (auto it = _pendingSavedEvents.begin(); it != _pendingSavedEvents.end();) {
        const auto index = it->first;
        const auto &event = it->second.record;
        // Only the death fade is withdrawn. Its records carry no caller; a
        // script's DestroyObject cannot be cancelled.
        const bool matches = event.eventId == static_cast<uint32_t>(SavedEventType::DestroyObject) &&
                             event.caller.isInvalid() &&
                             event.object.boundObject().get() == &object;
        ++it;
        if (matches) {
            cancelSaveEvent(index);
        }
    }
}

void Module::dropPendingEvents(const Object &target) {
    for (auto it = _pendingSavedEvents.begin(); it != _pendingSavedEvents.end();) {
        const auto index = it->first;
        const bool matches = it->second.record.object.boundObject().get() == &target;
        ++it;
        if (matches) {
            cancelSaveEvent(index);
        }
    }
}

void Module::deliverSavedEvent(PublishedSavedEvent &published) {
    // Remove the pending node before callbacks. The local value owns its payload
    // through delivery, including exception unwinding.
    auto node = _pendingSavedEvents.extract(published.savedIndex);
    const auto savedEvent = std::move(node.mapped().record);
    node = {};
    auto target = savedEvent.object.boundObject();
    if (!target) {
        return;
    }

    switch (static_cast<SavedEventType>(savedEvent.eventId)) {
    case SavedEventType::SpellImpact:
    case SavedEventType::ItemOnHitSpellImpact:
        deliverSpellImpact(savedEvent, *target);
        break;
    case SavedEventType::SignalEvent:
        deliverSignalEvent(savedEvent, target);
        break;
    case SavedEventType::OnMeleeAttacked:
        deliverAttackedEvent(savedEvent, *target);
        break;
    case SavedEventType::BroadcastSafeProjectile:
        deliverSafeProjectile(savedEvent);
        break;
    case SavedEventType::DestroyObject:
        deliverDestruction(target, published.keepsCallerFade);
        break;
    case SavedEventType::OpenObject: {
        // The caller opens the placeable's inventory.
        auto placeable = std::dynamic_pointer_cast<Placeable>(target);
        auto opener = std::dynamic_pointer_cast<Creature>(savedEvent.caller.boundObject());
        if (placeable && opener) {
            placeable->openInventory(*opener);
        }
        break;
    }
    case SavedEventType::SpawnBodyBag:
        deliverBodyBag(std::get<SavedBodyBag>(savedEvent.payload), cast<Area>(*target));
        break;
    case SavedEventType::Timed:
        // The situation runs as the target whatever its state:
        // dead, stunned or in a combat round, and without its action queue.
        if (published.command) {
            published.command->execute(published.command, *target, 0.0f);
        } else if (published.continuation) {
            _game.scriptRunner().run(
                *published.continuation, _game, target->id());
        }
        break;
    case SavedEventType::ApplyEffect:
        deliverAppliedEffect(std::get<EffectInstance>(savedEvent.payload), *target);
        break;
    case SavedEventType::RemoveEffect:
        target->removeEffectsById(std::get<EffectInstance>(savedEvent.payload).id);
        break;
    case SavedEventType::FeedbackMessage:
        showCombatFeedbackMessage(_game, savedEvent.caller.boundObject(), *target,
                                  std::get<CombatFeedbackMessage>(savedEvent.payload));
        break;
    default:
        break;
    }
}

void Module::deliverSpellImpact(const SavedEventRecord &savedEvent, Object &target) {
    if (const auto *hit = std::get_if<SavedWeaponImpact>(&savedEvent.payload)) {
        auto source = hit->source.boundObject();
        auto applications = hit->applications;
        for (auto &application : applications) {
            application.creator = source;
        }
        if (!std::dynamic_pointer_cast<Creature>(source)) {
            return;
        }
        applyItemOnHitApplications(std::move(applications), target, _game, _services);
        return;
    }
    const auto *impact = std::get_if<SavedSpellImpact>(&savedEvent.payload);
    if (!impact) {
        return;
    }
    auto caster = impact->caster.boundObject();
    if (!caster) {
        return;
    }
    auto spell = *_services.game.spells.get(static_cast<SpellType>(impact->spellId));
    spell.impactScript = impact->script;
    auto spellTarget = impact->target.boundObject();
    // Uncaptured spell events update spell, target, and cost while retaining
    // the owner's metamagic and casting class. Only captured context replaces those
    // values.
    SpellCastContext context = caster->spellCastContext();
    context.spellId = impact->spellId;
    context.forcePointCost = impact->finalForceCost;
    float targetFacing = scriptFacingFromObject(caster->getFacing());
    if (impact->capturedContext) {
        context.casterLevel = impact->capturedContext->casterLevel;
        context.metaMagic = impact->capturedContext->metaMagic;
        context.castingClass = impact->capturedContext->castingClass;
        targetFacing = impact->capturedContext->targetFacing;
        // A captured absence clears a later item's override.
        // Uncaptured events retain the existing owner context.
        caster->spellScriptContext().levelOverride = impact->capturedContext->levelOverride;
    }
    auto position = impact->targetPosition;
    if (spellTarget && spellTarget->type() < ObjectType::Module &&
        caster->spatialArea() && caster->sharesSpatialArea(*spellTarget)) {
        position = spellTarget->position();
    }
    const auto item = impact->item.boundObject();
    // Uncaptured item events use the zero-initialized item level.
    // Captured events carry that value explicitly.
    const std::optional<int> overrideLevel =
        savedEvent.eventId == static_cast<uint32_t>(SavedEventType::ItemOnHitSpellImpact)
            ? std::optional<int>(impact->capturedContext ? impact->capturedContext->itemCasterLevel : 0)
            : std::nullopt;
    // A creature's on-hit impact measures its flight again, on the default
    // path, to where the impact lands.
    if (overrideLevel && isa<Creature>(caster.get()))
        caster->setLastSpellProjectileMilliseconds(spellProjectileTimeMilliseconds(
            spell, caster->position(), position, ProjectilePathType::Default, _game.isTSL()));
    runSpellImpact(_game, spell, *caster, spellTarget,
                   std::make_shared<Location>(position, targetFacing), context, item, overrideLevel);
}

void Module::deliverSignalEvent(const SavedEventRecord &savedEvent, const std::shared_ptr<Object> &target) {
    using Signal = SavedSignalEventType;
    const auto &event = std::get<SavedScriptEvent>(savedEvent.payload);
    const auto type = static_cast<Signal>(event.type);
    const auto callerId = [&savedEvent]() {
        const auto caller = savedEvent.caller.boundObject();
        return caller ? caller->id() : script::kObjectInvalid;
    };
    const auto subject = [&event]() { return event.objects.empty() ? nullptr : event.objects[0].boundObject(); };
    const bool doorOrPlaceable = isa<Door>(target.get()) || isa<Placeable>(target.get());
    auto placeable = dyn_cast<Placeable>(target);
    auto encounter = dyn_cast<Encounter>(target);
    auto areaOfEffect = dyn_cast<AreaOfEffect>(target);
    if (type == Signal::Damaged) {
        const auto damager = savedEvent.caller.boundObject();
        if (auto door = dyn_cast<Door>(target)) {
            door->receiveDamagedSignal(damager);
        } else if (placeable) {
            placeable->receiveDamagedSignal(damager);
        }
    } else if (type == Signal::Death) {
        if (target.get() == this) {
            receivePlayerDeathEvent(savedEvent.caller);
        } else if (auto door = dyn_cast<Door>(target)) {
            door->receiveDeathSignal(callerId());
        } else if (placeable) {
            placeable->receiveDeathSignal(callerId());
        }
    } else if (placeable && (type == Signal::InventoryOpened || type == Signal::InventoryClosed || type == Signal::Used)) {
        // An inventory opened or closed runs the matching script with the
        // one who opened or closed it; a usable placeable that was used
        // runs its used script.
        if (type == Signal::InventoryOpened) {
            placeable->onOpen(callerId());
        } else if (type == Signal::InventoryClosed) {
            placeable->onClosed(callerId());
        } else if (placeable->isUsable()) {
            placeable->runOnUsed(savedEvent.caller.boundObject());
        }
    } else if (type == Signal::Locked && doorOrPlaceable) {
        // A locked door or placeable runs its script for being locked.
        if (auto door = dyn_cast<Door>(target)) {
            door->onLocked();
        } else {
            placeable->onLocked();
        }
    } else if (type == Signal::FailToOpen && doorOrPlaceable) {
        // A door or placeable that failed to open for its caller runs its
        // script for that; the first integer quiets a door's locked line.
        if (auto door = dyn_cast<Door>(target)) {
            door->onFailToOpen(callerId(), !event.integers.empty() && event.integers[0] != 0);
        } else {
            placeable->onFailToOpen(callerId());
        }
    } else if (target.get() == this &&
               (type == Signal::ItemActivated || type == Signal::ItemAcquired || type == Signal::ItemLost)) {
        receiveItemEvent(event, savedEvent.caller);
    } else if (target.get() == this && type == Signal::ModuleLoaded) {
        receiveLoadedSignal(!event.integers.empty() && event.integers[0] != 0);
    } else if (encounter && (type == Signal::Heartbeat || type == Signal::Entered || type == Signal::Exited ||
                             type == Signal::EncounterExhausted)) {
        if (type == Signal::Heartbeat) {
            encounter->receiveHeartbeatSignal();
        } else if (type == Signal::EncounterExhausted) {
            encounter->receiveExhaustedSignal();
        } else if (type == Signal::Entered) {
            encounter->receiveEnteredSignal(subject());
        } else {
            encounter->receiveExitedSignal(subject());
        }
    } else if (areaOfEffect && (type == Signal::Entered || type == Signal::Exited)) {
        if (type == Signal::Entered) {
            areaOfEffect->receiveEnteredSignal(subject());
        } else {
            areaOfEffect->receiveExitedSignal(subject());
        }
    } else if (auto area = dyn_cast<Area>(target); area && type == Signal::Entered) {
        area->receiveEnteredSignal(savedEvent.caller.boundObject(), !event.integers.empty() && event.integers[0] != 0);
    } else if (type == Signal::UserDefined && !event.integers.empty()) {
        _game.scriptRunner().run(target->getOnUserDefined(), {{script::ArgKind::Caller, script::Variable::ofObject(target->id())},
                                                              {script::ArgKind::UserDefinedEventNumber, script::Variable::ofInt(event.integers[0])}});
    } else if (type == Signal::SpellCastAt && event.integers.size() >= 2) {
        deliverSpellCastAtSignal(event, *target);
    }
}

// The spell-cast-at signal carries the spell and whether it is harmful in its
// integers, and its caster as its first object.
void Module::deliverSpellCastAtSignal(const SavedScriptEvent &event, Object &target) {
    auto caster = event.objects.empty() ? nullptr : event.objects[0].boundObject();
    const uint32_t casterId = caster ? caster->id() : script::kObjectInvalid;
    const int spell = event.integers[0];
    const int harmful = event.integers[1];
    if (auto *creature = dyn_cast<Creature>(&target)) {
        creature->receiveSpellCastAt(casterId, spell, harmful);
        return;
    }
    // A harmful spell at a door or placeable records its caster as the last
    // hostile actor and excites the caster's spell row before the
    // spell-cast-at script runs.
    const bool doorOrPlaceable = isa<Door>(&target) || isa<Placeable>(&target);
    if (doorOrPlaceable && harmful != 0) {
        target.setLastHostileActor(casterId);
        if (auto casterCreature = caster ? dyn_cast<Creature>(caster) : nullptr) {
            casterCreature->setExcitedState(kSpellCasterExcitedState);
            casterCreature->removeCombatInvisibilityEffects();
        }
    }
    _game.scriptRunner().run(target.getOnSpellCastAt(), {{script::ArgKind::Caller, script::Variable::ofObject(target.id())},
                                                         {script::ArgKind::LastSpellCaster, script::Variable::ofObject(casterId)},
                                                         {script::ArgKind::LastSpell, script::Variable::ofInt(spell)},
                                                         {script::ArgKind::LastSpellHarmful, script::Variable::ofInt(harmful)}});
}

void Module::deliverAttackedEvent(const SavedEventRecord &savedEvent, Object &target) {
    const auto *attack = std::get_if<SavedCombatAttack>(&savedEvent.payload);
    auto caller = savedEvent.caller.boundObject();
    const uint32_t attackerId = caller ? caller->id() : script::kObjectInvalid;
    std::optional<std::reference_wrapper<const AttackHistory>> history;
    std::optional<std::reference_wrapper<const AttackEventFields>> fields;
    if (attack) {
        history = *attack->history;
        fields = *attack->fields;
    }
    if (auto *creature = dyn_cast<Creature>(&target)) {
        creature->receiveAttackEvent(history, attackerId, fields);
    } else if (auto *door = dyn_cast<Door>(&target)) {
        door->receiveAttackEvent(attackerId, fields);
    } else if (auto *placeable = dyn_cast<Placeable>(&target)) {
        placeable->receiveAttackEvent(attackerId, fields);
    }
}

// Only a loaded save carries this event. It shows one bolt from the source's
// impact node to the receiver's current position.
void Module::deliverSafeProjectile(const SavedEventRecord &savedEvent) {
    const auto &attack = std::get<SavedCombatAttack>(savedEvent.payload);
    auto source = std::dynamic_pointer_cast<Creature>(savedEvent.caller.boundObject());
    auto receiver = std::dynamic_pointer_cast<Creature>(attack.reactionObject.boundObject());
    auto weapon = std::dynamic_pointer_cast<Item>(attack.ammoItem.boundObject());
    if (!source || !receiver || !weapon || !_area->isObjectResident(*source) || !_area->isObjectResident(*receiver)) {
        return;
    }
    SafeProjectileShot shot;
    shot.result = attack.fields->result;
    shot.endpoint = receiver->position();
    shot.delayMilliseconds = attack.fields->reactionDelay;
    shot.hand = kSafeProjectileHand;
    _services.game.projectiles.launchSafeProjectile(*source, *receiver, *weapon, shot, _game, _services);
}

void Module::deliverDestruction(const std::shared_ptr<Object> &target, bool keepsCallerFade) {
    if ((isa<Door>(target) || isa<Placeable>(target)) && !target->isDestroyable()) {
        return;
    }
    if (auto creature = dyn_cast<Creature>(target)) {
        // A creature that cannot be destroyed, a party member and the
        // player character stay.
        auto &party = _game.party();
        if (!creature->isDestroyable() || party.isMember(*creature) ||
            party.actualPlayer().get() == creature.get())
            return;
        // A dead creature leaves a body bag, whatever fade it takes.
        if (creature->isDead()) {
            creature->setSpawnedBodyBag(spawnBodyBag(*creature));
        }
        // Any destruction but a script's takes the death fade: the
        // appearance's fade delay, or a kept corpse when it has none.
        if (!keepsCallerFade) {
            const auto table = getRequiredTwoDA(_services.resource.twoDas, "appearance");
            if (const auto delay = readFadeDelayOnDeath(*table, creature->appearance())) {
                creature->setFadeOutTime(static_cast<uint32_t>(*delay));
                creature->setKeepCorpse(false);
            } else {
                creature->setKeepCorpse(true);
            }
        }
    }
    if (auto placeable = dyn_cast<Placeable>(target)) {
        _game.closeContainer(*placeable);
        spawnBodyBag(*placeable);
    }
    if (auto item = dyn_cast<Item>(target)) {
        if (auto owner = _game.getObjectById(item->owner())) {
            if (item->isEquipped()) {
                if (auto *creature = dyn_cast<Creature>(owner.get())) {
                    creature->takeEquippedItem(item);
                }
            } else {
                owner->removeItemStack(item);
            }
        }
    }
    _area->destroyObject(*target);
}

// The bag appears where its source stood. A corpse bag takes over its
// creature's body and moves the creatures it lands on out of its way.
void Module::deliverBodyBag(const SavedBodyBag &spawn, Area &area) {
    auto bag = std::dynamic_pointer_cast<Placeable>(spawn.object.boundObject());
    if (!bag) {
        return;
    }
    if (bag->isCorpse()) {
        if (auto body = area.takeCorpseBagBody(bag->id())) {
            bag->adoptCorpseModel(std::move(body));
        }
    }
    bag->setPosition(spawn.position);
    area.add(bag);
    if (bag->isCorpse()) {
        area.budgeCreatures(spawn.position, bag->collisionBounds());
    }
}

void Module::deliverAppliedEffect(const EffectInstance &savedEffect, Object &target) {
    EffectInstance effect(savedEffect);
    // An area keeps no effects: it takes only a visual, played once at the
    // point floats 0-2 name.
    if (auto *area = dyn_cast<Area>(&target)) {
        if (effect.type() == EffectType::Visual) {
            area->presentVisualAt(static_cast<uint16_t>(effect.integerParameter(0)),
                                  glm::vec3(effect.floatParameters[0], effect.floatParameters[1], effect.floatParameters[2]));
        }
        return;
    }
    effect.restoring = false;
    if (effect.hasStableId()) {
        _game.importEffectId(effect.id);
    } else {
        effect.id = _game.allocateEffectId();
    }
    target.applyEffect(std::move(effect));
}

void Module::update(float dt) {
    // A script that plays a movie waits for it to end, and the world with it:
    // once a movie starts, the rest of the update waits for the next one
    // after the movie.
    auto held = [this]() { return static_cast<bool>(_game.movie()); };
    // Process the module object's own action queue so commands assigned to
    // the module by its scripts execute. Delayed commands are module events.
    Object::update(dt);
    if (held()) {
        return;
    }
    updateStampedHeartbeat(_onHeartbeat);
    if (held()) {
        return;
    }
    dispatchDueSavedEvents();
    if (held()) {
        return;
    }
    _game.updateTimeStop();

    if (_game.cameraType() == CameraType::ThirdPerson) {
        _player->update(dt);
    }
    _area->update(dt);
    if (held()) {
        return;
    }
    dispatchDueSavedEvents();
}

// Item events (activated, acquired and lost) record their objects for the
// module's getters, then run the matching module script.
void Module::receiveItemEvent(const SavedScriptEvent &event, const SavedObjectReference &caller) {
    auto objectId = [&event](size_t index) {
        if (index >= event.objects.size()) {
            return script::kObjectInvalid;
        }
        auto object = event.objects[index].boundObject();
        return object ? object->id() : script::kObjectInvalid;
    };
    std::string script;
    switch (static_cast<SavedSignalEventType>(event.type)) {
    case SavedSignalEventType::ItemActivated:
        // The item, its user and its target are the event's first, second and
        // fourth objects; its floats give the point it was used at.
        _itemEvents.activated = objectId(0);
        _itemEvents.activator = objectId(1);
        _itemEvents.activatedTarget = objectId(3);
        _itemEvents.activatedPosition = glm::vec3(0.0f);
        for (size_t i = 0; i < 3 && i < event.floats.size(); ++i) {
            _itemEvents.activatedPosition[i] = event.floats[i];
        }
        script = _info.onActivateItem;
        break;
    case SavedSignalEventType::ItemAcquired:
        _itemEvents.acquired = objectId(0);
        _itemEvents.acquiredFrom = objectId(1);
        script = _info.onAcquireItem;
        break;
    default: {
        _itemEvents.lost = objectId(0);
        auto lostBy = caller.boundObject();
        _itemEvents.lostBy = lostBy ? lostBy->id() : script::kObjectInvalid;
        script = _info.onUnacquireItem;
        break;
    }
    }
    _game.scriptRunner().run(script, {{script::ArgKind::Caller, script::Variable::ofObject(_id)}});
}

// A fallen party member's death event records it for GetLastPlayerDied, then
// runs the module's player-death script.
void Module::receivePlayerDeathEvent(const SavedObjectReference &caller) {
    auto fallen = caller.boundObject();
    _lastPlayerDied = fallen ? fallen->id() : script::kObjectInvalid;
    _game.scriptRunner().run(_info.onPlayerDeath, {{script::ArgKind::Caller, script::Variable::ofObject(_id)}});
}

// A standing of at most this is hostile.
static constexpr int kHighestHostileReputation = 10;

// The usable items of the party's shared inventory, which the player carries
// for whoever leads.
static const ItemAttributes &partyItemAttributes(Game &game, const std::shared_ptr<Creature> &leader) {
    auto receiver = std::dynamic_pointer_cast<Creature>(game.party().sharedInventoryReceiver(leader));
    return (receiver ? *receiver : *leader).itemAttributes();
}

std::vector<ContextAction> Module::getContextActions(const std::shared_ptr<Object> &object) const {
    std::vector<ContextAction> actions;
    // With no one under control there is no one to act.
    const auto leader = _game.party().getLeader();
    if (!leader) {
        return actions;
    }

    switch (object->type()) {
    case ObjectType::Creature: {
        auto creature = std::static_pointer_cast<Creature>(object);
        if (isHostileToPartyLeader(*creature)) {
            actions.push_back(ContextAction(ActionType::AttackObject));
            auto weapon = leader->getEquippedItem(InventorySlots::rightWeapon);
            // Each attack feat chain offers the highest rank the leader holds.
            auto offerChain = [&](FeatType master, FeatType improved, FeatType basic) {
                for (FeatType feat : {master, improved, basic}) {
                    if (leader->hasEffectiveFeat(feat)) {
                        actions.push_back(ContextAction(feat));
                        return;
                    }
                }
            };
            if (weapon && weapon->isRanged()) {
                offerChain(FeatType::MasterPowerBlast, FeatType::ImprovedPowerBlast, FeatType::PowerBlast);
                offerChain(FeatType::MultiShot, FeatType::ImprovedRapidShot, FeatType::RapidShot);
                offerChain(FeatType::MasterSniperShot, FeatType::ImprovedSniperShot, FeatType::SniperShot);
            } else {
                offerChain(FeatType::MasterCriticalStrike, FeatType::ImprovedCriticalStrike, FeatType::CriticalStrike);
                offerChain(FeatType::WhirlwindAttack, FeatType::ImprovedFlurry, FeatType::Flurry);
                offerChain(FeatType::MasterPowerAttack, FeatType::ImprovedPowerAttack, FeatType::PowerAttack);
                if (_game.isTSL() && leader->hasEffectiveFeat(FeatType::ShieldBreaker)) {
                    actions.push_back(ContextAction(FeatType::ShieldBreaker));
                }
            }

            for (auto &power : leader->powerMenuActions(*object, true)) {
                actions.push_back(std::move(power));
            }

            // Hostile items come from the party's inventory.
            for (const auto &[item, spell] : partyItemAttributes(_game, leader).attackingSpells()) {
                actions.emplace_back(item, spell);
            }
        }
        break;
    }
    case ObjectType::Door: {
        auto door = std::static_pointer_cast<Door>(object);
        if (canBashDoor(*door)) {
            actions.push_back(ContextAction(ActionType::AttackObject));
        }
        if (door->isLocked() && !door->isKeyRequired() && leader->attributes().hasSkill(SkillType::Security)) {
            actions.push_back(ContextAction(SkillType::Security));
        }
        if (door->isLocked()) {
            addMineKitActions(actions, _game, *_area, leader);
        }
        break;
    }
    case ObjectType::Placeable: {
        auto placeable = cast<Placeable>(object);
        if (canBashPlaceable(*placeable)) {
            actions.push_back(ContextAction(ActionType::AttackObject));
        }
        if (canUseSecurityOnPlaceable(*placeable, *leader)) {
            actions.push_back(ContextAction(SkillType::Security));
        }
        // A hostile placeable without an inventory that regards the leader as
        // an enemy takes the hostile power menu and, in TSL, the hostile item
        // list in place of its own mine kits (which only TSL has).
        const bool hostileTarget = placeable->isHostileAppearance() && !placeable->hasInventory() &&
                                   leader->getReputationFrom(placeable->faction()) <= kHighestHostileReputation;
        if (hostileTarget) {
            for (auto &power : leader->powerMenuActions(*object, true)) {
                actions.push_back(std::move(power));
            }
            if (_game.isTSL()) {
                for (const auto &[item, spell] : partyItemAttributes(_game, leader).attackingSpells()) {
                    actions.emplace_back(item, spell);
                }
            }
        }
        if (placeable->isLocked() && !hostileTarget) {
            addMineKitActions(actions, _game, *_area, leader);
        }
        break;
    }
    case ObjectType::Trigger: {
        // A mine: disable it when hostile, recover it either way.
        auto trigger = std::static_pointer_cast<Trigger>(object);
        if (!trigger->isTrap()) {
            break;
        }
        if (leader->attributes().hasSkill(SkillType::Demolitions)) {
            if (trigger->isTrapHostileTo(*leader)) {
                actions.push_back(ContextAction(SkillType::Demolitions));
            }
            actions.push_back(ContextAction(SkillType::Demolitions, static_cast<int>(SubSkill::RecoverTrap)));
        }
        // TSL: one droid power the leader knows, chosen by the mine's type.
        if (auto spell = mineForcePower(*trigger, *leader)) {
            actions.emplace_back(spell);
            actions.back().availability = leader->powerMenuStatus(*spell, *trigger);
        }
        break;
    }
    default:
        break;
    }

    return actions;
}

bool Module::handleKeyDown(const input::KeyEvent &event) {
    switch (event.code) {
    case input::KeyCode::Space:
    case input::KeyCode::Pause:
        // Pause keys do not repeat: holding one toggles once.
        if (!event.repeat) {
            _game.pressPauseKey();
        }
        return true;
    case input::KeyCode::G:
        // The stealth key.
        if (!event.repeat) {
            _game.requestStealth();
        }
        return true;
    case input::KeyCode::Y:
        // The clear-one key.
        if (!event.repeat) {
            _game.clearOneAction();
        }
        return true;
    case input::KeyCode::F:
        // The cancel-combat key.
        if (!event.repeat) {
            _game.cancelCombat();
        }
        return true;
    case input::KeyCode::R:
        // The default action key runs the selected target's default action, silently.
        if (event.repeat) {
            return true;
        }
        if (auto selected = _area->selectedObject()) {
            onObjectClick(selected, false);
        }
        return true;
    case input::KeyCode::Q:
    case input::KeyCode::E: {
        // Target cycling: Q steps counter-clockwise, E clockwise.
        if (_game.cameraType() != CameraType::ThirdPerson) {
            return false;
        }
        _game.selectNearestObject(event.code == input::KeyCode::Q ? 0 : 1);
        // In combat mode a new target may pause play, whether or not one was found.
        if (_game.clientCombatMode()) {
            _game.requestAutoPause(AutoPauseReason::NewTargetSelected);
        }
        return true;
    }
    default:
        return false;
    }
}

} // namespace game

} // namespace reone
