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

#include "reone/game/object/placeable.h"

#include <algorithm>
#include <limits>

#include "reone/audio/clip.h"
#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/game/animations.h"
#include "reone/game/animationutil.h"
#include "reone/game/attack.h"
#include "reone/game/di/services.h"
#include "reone/game/effect/death.h"
#include "reone/game/event.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/trigger.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"
#include "reone/game/script/runner.h"
#include "reone/game/twodautil.h"
#include "reone/graphics/di/services.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/provider/walkmeshes.h"
#include "reone/resource/resources.h"
#include "reone/resource/strings.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/model.h"
#include "reone/script/types.h"

using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;
using namespace reone::script;

namespace reone {

namespace game {

static constexpr int kDefaultAnimationId = 10000;
static constexpr int kDeadAnimationId = 10072;
static constexpr int kOpenAnimationId = 10075;
static constexpr int kCloseAnimationId = 10076;
// The animations.2da row of a destroyed placeable's pose.
static constexpr int kDestroyedAnimation = 307;
static constexpr int kDamagedEvent = static_cast<int>(SavedSignalEventType::Damaged);
// A container broken open opens for the controlled creature this long after it
// breaks; in TSL only when that creature is within this reach.
static constexpr float kBrokenOpenReach = 3.0f;
static constexpr uint32_t kBrokenOpenDelayMilliseconds = 1000;

void Placeable::loadFromBlueprint(const std::string &resRef) {
    std::shared_ptr<Gff> utp(_services.resource.gffs.get(resRef, ResType::Utp));
    if (!utp) {
        return;
    }
    deserialize(*utp, SerializedIdentityContext::templateResource(resRef));
}

void Placeable::deserialize(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    std::string templateRes;
    if (!identityContext.isSerializedState() &&
        gff.readResRef(templateRes, "TemplateResRef")) {
        if (auto utp = _services.resource.gffs.get(templateRes, ResType::Utp)) {
            deserializeAll(*utp, SerializedIdentityContext::templateResource(templateRes));
            captureAuthoredRecord(*utp, gff);
        }
    }
    deserializeAll(gff, identityContext);
    loadAppearance();
    if (_animation == kDeadAnimationId) {
        enterDestroyedState();
    }
    updateTransform();
}

void Placeable::deserializeAll(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    deserializeRuntimeState(gff, identityContext);
    if (gff.readString(_tag, "Tag")) {
        boost::to_lower(_tag);
    }
    if (gff.readLocString(_locName, "LocName", _services.resource.strings)) {
        _name = _locName.str();
    }
    gff.readBool(_autoRemoveKey, "AutoRemoveKey");
    gff.readEnum(_faction, "Faction");
    gff.readBool(_plot, "Plot");
    gff.readBool(_minOneHP, "Min1HP");
    gff.readByte(_openLockDC, "OpenLockDC");
    gff.readString(_keyName, "KeyName");
    _trap.deserialize(gff);
    // A placeable keeps the DCs its record stores, saved or placed.
    _trap.deserializeArming(gff, _game, false);
    gff.readBool(_usable, "Useable");
    gff.readBool(_static, "Static");
    gff.readBool(_notBlastable, "NotBlastable");
    gff.readBool(_groundPile, "GroundPile");
    gff.readDword(_appearance, "Appearance");
    if (gff.readShort(_hitPoints, "HP")) {
        _maxHitPoints = _hitPoints;
    }
    gff.readShort(_currentHitPoints, "CurrentHP");
    gff.readByte(_hardness, "Hardness");
    gff.readByte(_fort, "Fort");
    gff.readByte(_will, "Will");
    gff.readByte(_ref, "Ref");
    gff.readBool(_lockable, "Lockable");
    gff.readBool(_locked, "Locked");
    gff.readBool(_hasInventory, "HasInventory");
    gff.readBool(_keyRequired, "KeyRequired");
    gff.readByte(_closeLockDC, "CloseLockDC");
    gff.readBool(_open, "Open");
    gff.readBool(_partyInteract, "PartyInteract");
    // The two highest portrait IDs name no row and leave the portrait to its
    // resref.
    gff.readWord(_portraitId, "PortraitId");
    if (_portraitId >= 0xfffe) {
        gff.readResRef(_portrait, "Portrait");
    }
    gff.readResRef(_conversation, "Conversation");
    gff.readByte(_bodyBagId, "BodyBag");
    gff.readBool(_dieWhenEmpty, "DieWhenEmpty");
    gff.readByte(_lightState, "LightState");
    gff.readLocString(_description, "Description", _services.resource.strings);
    gff.readResRef(_onClosed, "OnClosed");
    gff.readResRef(_onDamaged, "OnDamaged");
    gff.readResRef(_onDeath, "OnDeath");
    gff.readResRef(_onHeartbeat, "OnHeartbeat");
    gff.readResRef(_onInvDisturbed, "OnInvDisturbed");
    gff.readResRef(_onFailToOpen, "OnFailToOpen");
    gff.readResRef(_onLock, "OnLock");
    gff.readResRef(_onMeleeAttacked, "OnMeleeAttacked");
    gff.readResRef(_onOpen, "OnOpen");
    gff.readResRef(_onSpellCastAt, "OnSpellCastAt");
    gff.readResRef(_onUnlock, "OnUnlock");
    gff.readResRef(_onUsed, "OnUsed");
    gff.readResRef(_onUserDefined, "OnUserDefined");
    gff.readResRef(_onDialog, "OnDialog");
    gff.readResRef(_onEndDialogue, "OnEndDialogue");
    gff.readInt(_animation, "Animation");
    {
        float bearing;
        if (gff.readFloat(bearing, "Bearing")) {
            _orientation = glm::quat(glm::vec3(0.0f, 0.0f, bearing));
        }
    }
    gff.readFloat(_position[0], "X");
    gff.readFloat(_position[1], "Y");
    gff.readFloat(_position[2], "Z");
    gff.readBool(_isBodyBag, "IsBodyBag");
    gff.readBool(_isBodyBagVisible, "IsBodyBagVisible");
    gff.readBool(_isCorpse, "IsCorpse");
    gff.readBool(_commandable, "Commandable");

    deserializeOwnedItems(
        gff,
        identityContext,
        SaveRecordOriginKind::PlaceableItem,
        true);

    // FIXME: deserialize EffectList, ActionList
}

void Placeable::loadAppearance() {
    std::shared_ptr<TwoDA> placeables(_services.resource.twoDas.get("placeables"));
    std::string modelName(boost::to_lower_copy(placeables->getString(_appearance, "modelname")));
    _hostileAppearance = placeables->getInt(_appearance, "hostile", 0) == 1;
    _preciseUse = placeables->getInt(_appearance, "preciseuse", 0) == 1;
    _appearanceNameStrRef = placeables->getInt(_appearance, "strref", -1);
    auto sounds = getRequiredTwoDA(_services.resource.twoDas, "placeableobjsnds");
    const auto soundType = placeables->getIntOpt(_appearance, "soundapptype");
    _soundArmorType = boost::to_lower_copy(sounds->getString(soundType.value_or(0), "armortype"));
    _openedSound = soundType ? boost::to_lower_copy(sounds->getString(*soundType, "opened")) : std::string();

    auto model = _services.resource.models.get(modelName);
    if (!model) {
        return;
    }
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);

    auto sceneNode = sceneGraph.newModel(*model, ModelUsage::Placeable);
    sceneNode->setUser(*this);
    sceneNode->setDrawDistance(_game.options().graphics.drawDistance);
    _sceneNode = std::move(sceneNode);

    auto walkmesh = _services.resource.walkmeshes.get(modelName, ResType::Pwk);
    if (walkmesh) {
        _walkmesh = sceneGraph.newWalkmesh(*walkmesh);
    }
}

void Placeable::damage(
    int amount,
    const std::shared_ptr<Object> &damager) {
    if (isDead() || _plot || _notBlastable) {
        return;
    }
    if (amount <= 0) {
        return;
    }

    setLastDamager(damager);
    uint32_t damagerId = getLastDamager();
    const int hitPointsBefore = currentHitPoints();
    if (amount == std::numeric_limits<int>::max()) {
        _currentHitPoints = isMinOneHP() ? 1 : 0;
    } else {
        int adjustedAmount = applyDamageToHitPoints(amount, hitPointsBefore);
        _game.floatingText().addDamage(
            *this, amount, adjustedAmount, damagerId);
    }

    _game.queueScriptEvent(*this, damager ? std::optional<std::reference_wrapper<const Object>>(*damager) : std::nullopt, Event(kDamagedEvent, {amount}, {}, {}, {}));

    if (!isDead()) {
        return;
    }

    if (!_hasInventory) {
        // Non-container lethal damage uses the Death effect's signal and
        // delayed destruction. Do not also invoke its death script here.
        auto effect = std::make_shared<DeathEffect>(false, true, false);
        effect->setSaveFacingCreator(damager);
        auto instance = effect->saveFacingInstance();
        instance.effect = std::move(effect);
        instance.setDuration(DurationType::Instant, 0.0f);
        applyEffect(std::move(instance));
        return;
    }

    // A container broken open becomes plot and unlocked. It swings open and
    // opens for the controlled creature a second later; in TSL only when that
    // creature is close by.
    _plot = true;
    _locked = false;
    auto leader = _game.party().getLeader();
    if (!leader || (_game.isTSL() && glm::distance(leader->position(), _position) >= kBrokenOpenReach)) {
        return;
    }
    setStateAnimation(kOpenAnimationId);
    SavedEventRecord event;
    const uint64_t when = _game.worldTimeMilliseconds() + kBrokenOpenDelayMilliseconds;
    event.day = static_cast<uint32_t>(when / _game.millisecondsPerWorldDay());
    event.time = static_cast<uint32_t>(when % _game.millisecondsPerWorldDay());
    event.object = SavedObjectReference::fromRuntimeId(_id);
    event.caller = SavedObjectReference::fromRuntimeId(leader->id());
    event.eventId = static_cast<uint32_t>(SavedEventType::OpenObject);
    event.payload = SavedOpenObject {};
    _game.module()->enqueueSaveEvent(std::move(event));
}

void Placeable::onOpen(uint32_t triggererId) {
    if (_onOpen.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onOpen,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastOpenedBy, Variable::ofObject(triggererId)}});
}

void Placeable::onFailToOpen(uint32_t openerId) {
    _lastEnteredId = openerId;
    if (_onFailToOpen.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onFailToOpen,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::ClickingObject, Variable::ofObject(openerId)}});
}

void Placeable::onLocked() {
    if (_onLock.empty()) {
        return;
    }
    _game.scriptRunner().run(_onLock, {{script::ArgKind::Caller, script::Variable::ofObject(_id)}});
}

void Placeable::onClosed(uint32_t closerId) {
    if (_onClosed.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onClosed,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastClosedBy, Variable::ofObject(closerId)}});
}

void Placeable::runOnUsed(std::shared_ptr<Object> usedBy) {
    if (_onUsed.empty()) {
        return;
    }

    std::vector<script::Argument> args;
    args.emplace_back(script::ArgKind::Caller, Variable::ofObject(_id));

    if (usedBy) {
        args.emplace_back(script::ArgKind::LastUsedBy,
                          Variable::ofObject(usedBy->id()));
    }

    _game.scriptRunner().run(_onUsed, args);
}

void Placeable::runOnInvDisturbed(uint32_t triggerrer, InventoryDisturbType type, uint32_t item) {
    if (_onInvDisturbed.empty()) {
        return;
    }

    std::vector<script::Argument> args;
    args.emplace_back();

    // FIXME: implement LastDisturbed
    // triggerrer ? triggerrer->id() : kObjectInvalid

    _game.scriptRunner().run(
        _onInvDisturbed,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastDisturbed, Variable::ofObject(triggerrer)},
         {script::ArgKind::InventoryDisturbItem, Variable::ofObject(item)},
         {script::ArgKind::InventoryDisturbType, Variable::ofInt(static_cast<int>(type))}});
}

void Placeable::runDamagedScript() {
    if (_onDamaged.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onDamaged,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastAttacker, Variable::ofObject(getLastDamager())},
         {script::ArgKind::LastDamager, Variable::ofObject(getLastDamager())}});
}

void Placeable::enterDestroyedState() {
    _game.closeContainer(*this);
    _animation = kDeadAnimationId;
    _shownAnimation = kDeadAnimationId;
    _animationState = kDeadAnimationId;
    if (auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode)) {
        model->playAnimation(_services.game.animations.getNameById(kDestroyedAnimation));
        // A destroyed placeable's lights go out.
        std::vector<scene::SceneNode *> pending {model.get()};
        while (!pending.empty()) {
            auto node = pending.back();
            pending.pop_back();
            if (node->type() == SceneNodeType::Light) {
                node->setEnabled(false);
            }
            for (auto child : node->children()) {
                pending.push_back(child);
            }
        }
    }
}

void Placeable::runConversationScript() {
    // A placeable with no conversation script, or "default", runs the
    // fallback one, which stays its own.
    if (_onDialog.empty() || _onDialog == "default") {
        _onDialog = kFallbackConversationScript;
    }
    _game.scriptRunner().run(_onDialog, _id);
}

void Placeable::runEndDialogScript() {
    _game.scriptRunner().run(_onEndDialogue, id());
}

void Placeable::receiveDamagedSignal(const std::shared_ptr<Object> &damager) {
    setLastDamager(damager);
    setLastHostileActor(damager ? damager->id() : script::kObjectInvalid);
    runDamagedScript();
}

void Placeable::receiveAttackEvent(uint32_t attackerId, std::optional<std::reference_wrapper<const AttackEventFields>> fields) {
    recordReceivedAttack(attackerId, fields ? fields->get().weaponAttackType : 0);
    if (!isRuntimeLive()) {
        return;
    }
    // The melee-attacked script runs whether or not the object is destroyed.
    _game.scriptRunner().run(_onMeleeAttacked, id());
    _trap.springOnStrike(*this, _faction, attackerId);
}

void Placeable::receiveDeathSignal(uint32_t killerId) {
    setLastKiller(killerId);
    _game.scriptRunner().run(_onDeath, {{script::ArgKind::Caller, Variable::ofObject(_id)}});
}

void Placeable::updateTransform() {
    Object::updateTransform();

    if (_walkmesh) {
        _walkmesh->setLocalTransform(_transform);
    }
}

void Placeable::applyDamageEffect(
    int amount,
    const std::shared_ptr<Object> &damager,
    std::optional<DamageReaction>) {
    if (amount == 0) {
        _game.floatingText().addDamage(*this, 0, 0, getLastDamager());
        _game.queueScriptEvent(*this, damager ? std::optional<std::reference_wrapper<const Object>>(*damager) : std::nullopt, Event(kDamagedEvent, {amount}, {}, {}, {}));
        return;
    }

    damage(amount, damager);
}

int Placeable::savingThrow(SavingThrow save) const {
    switch (save) {
    case SavingThrow::Fortitude:
        return _fort;
    case SavingThrow::Reflex:
        return _ref;
    case SavingThrow::Will:
        return _will;
    default:
        return 0;
    }
}

// Traps

// The setter's Demolitions rank counts at a twentieth in a mine's blast at a
// placeable.
static constexpr int kMineBlastRankDivisor = 20;

void Placeable::triggerTrap(const std::shared_ptr<Creature> &caller, bool force) {
    if (caller) {
        _trap.spring(*this, _faction, *caller, force);
    }
}

void Placeable::armMine(int trapType, int detectDC, int disarmDC, int ownerDemolitionsSkill,
                        const std::shared_ptr<Trigger> &linkedMine,
                        const std::shared_ptr<Creature> &setter, int blastBonus, bool blast) {
    _trap.armMine(_game, trapType, detectDC, disarmDC, ownerDemolitionsSkill,
                  linkedMine, setter, blastBonus, blast);
}

// A blast that breaks the lock unlocks the placeable at once.
void Placeable::updateMineBlast(float dt) {
    auto blast = _trap.updateMineBlast(dt, kMineBlastRankDivisor, _openLockDC);
    if (!blast) {
        return;
    }
    if (blast->opens) {
        _locked = false;
        _game.scriptRunner().run(_onUnlock, {{script::ArgKind::Caller, script::Variable::ofObject(_id)}});
    }
    _trap.reportMineBlast(_game, *blast);
}

// END Traps

void Placeable::update(float dt) {
    updateHeartbeat();
    Object::update(dt);
    updateMineBlast(dt);
    updateOpenStateAnimation();
    if (_isBodyBag) {
        updateBodyBagPresentation();
    }
}

// A due heartbeat runs unless it is the first check or the placeable is dead.
// In TSL a placeable whose heartbeat has not been stamped runs it at once when
// the interval has not passed yet.
void Placeable::updateHeartbeat() {
    if (isHeartbeatDueSince(_heartbeatDay, _heartbeatTime) || !_game.isTSL() || _heartbeatTime != 0) {
        updateStampedHeartbeat(_onHeartbeat, !isDead());
        return;
    }
    if (isDead()) {
        return;
    }
    _heartbeatTime = 1;
    const std::string script = _onHeartbeat;
    if (!script.empty()) {
        _game.scriptRunner().run(script, _id);
    }
}

// Body bags

static constexpr int kBodyBagHitPoints = 15;
static constexpr int kBodyBagDisarmDC = 15;
static constexpr int kBodyBagOpenLockDC = 18;
static constexpr int kBodyBagFortitude = 16;
static constexpr int kBodyBagHardness = 5;
static constexpr int kRancorCorpseHardness = 1;
static constexpr int kBodyBagDescriptionStrRef = 38612;
static constexpr uint32_t kRancorCorpseAppearance = 217;
static const std::string kBodyBagPortrait {"po_plc_b04_"};
static const std::string kCorpseReticleNode {"rootdummy"};

bool Placeable::isBodyBagHidden() const {
    return _isBodyBag && !_isBodyBagVisible && _spatialArea && _spatialArea->hasCorpseWithBodyBag(_id);
}

void Placeable::loadBodyBag(int appearance) {
    auto &strings = _services.resource.strings;
    const auto placeables = getRequiredTwoDA(_services.resource.twoDas, "placeables");
    _locName = LocString(placeables->getInt(appearance, "strref", 0), "", strings);
    _name = _locName.str();
    _tag = "body bag";
    _faction = Faction::Hostile1;
    _trap.setDisarmDC(kBodyBagDisarmDC);
    _openLockDC = kBodyBagOpenLockDC;
    _appearance = appearance;
    _hitPoints = _maxHitPoints = kBodyBagHitPoints;
    _currentHitPoints = kBodyBagHitPoints;
    _hardness = _appearance == kRancorCorpseAppearance ? kRancorCorpseHardness : kBodyBagHardness;
    _fort = kBodyBagFortitude;
    _usable = true;
    _hasInventory = true;
    _portrait = kBodyBagPortrait;
    _description = LocString(kBodyBagDescriptionStrRef, "", strings);
    _animation = kDefaultAnimationId;
    loadAppearance();
}

void Placeable::fillBodyBag(Object &source, bool corpse, std::optional<int> nameStrRef) {
    acquireItemsFrom(source);
    _isBodyBag = true;
    // A creature's bag waits hidden behind its kept corpse until shown.
    if (isa<Creature>(source)) {
        _isBodyBagVisible = false;
    }
    if (nameStrRef) {
        _locName = LocString(*nameStrRef, "", _services.resource.strings);
        _name = _locName.str();
    }
    _isCorpse = corpse;
    _dieWhenEmpty = !corpse;
    if (corpse && _items.empty()) {
        _hasInventory = false;
    }
    _plot = !corpse;
    _partyInteract = true;
    setFacing(source.getFacing());
}

// A creature gives up its droppable items, equipment first, and all its gold
// as a stack of credits; a placeable gives up everything.
void Placeable::acquireItemsFrom(Object &source) {
    if (auto *creature = dyn_cast<Creature>(&source)) {
        for (const auto &item : creature->dropableItems()) {
            // The body keeps its look while its equipment is taken.
            if (item->isEquipped()) {
                creature->takeEquippedItem(item, false);
            } else {
                creature->removeItemStack(item);
            }
            addItem(item);
        }
        if (const int gold = creature->gold()) {
            addItem(Item::kCreditsResRef, gold);
            creature->takeGold(gold);
        }
        return;
    }
    const auto items = source.items();
    for (const auto &item : items) {
        source.removeItemStack(item);
        addItem(item);
    }
}

void Placeable::adoptCorpseModel(std::shared_ptr<ModelSceneNode> model) {
    model->clearAnimationEventListener();
    model->setUser(*this);
    _sceneNode = std::move(model);
    updateTransform();
}

// A hidden bag is fully faded. It is still picked like any usable placeable,
// and its corpse passes picks to it.
void Placeable::updateBodyBagPresentation() {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model) {
        return;
    }
    model->setFadeAlpha(isBodyBagHidden() ? 0.0f : 1.0f);
}

// END Body bags

// Inventory

static constexpr uint32_t kCloseToOpenRow = 312;
static constexpr int kInventoryOpenedEvent = 22;
static constexpr int kInventoryClosedEvent = 23;
// A use of a placeable with no body waits this long for it to open.
static constexpr float kBodilessOpeningWait = 0.5f;

// The opened sound is the placeable sounds row named by the appearance's
// sound type.
static std::shared_ptr<audio::AudioClip> findOpenedSound(ServicesView &services, const std::string &resRef) {
    return resRef.empty() ? nullptr : services.resource.audioClips.get(resRef);
}

// The wait is the swing's length, or the opened sound's when that is longer
// and the swing has a length at all.
float Placeable::beginOpeningInventory() {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    float wait = kBodilessOpeningWait;
    if (model) {
        auto swing = model->model().getAnimation(_services.game.animations.getNameById(kCloseToOpenRow));
        wait = swing ? swing->length() : 0.0f;
    }
    auto sound = findOpenedSound(_services, _openedSound);
    if (sound) {
        _services.audio.mixer.play(sound, audio::AudioType::Sound, 1.0f, false, _position);
        if (sound->duration() > wait && wait != 0.0f) {
            wait = sound->duration();
        }
    }
    setStateAnimation(kOpenAnimationId);
    _inventoryOpenPending = true;
    return wait;
}

void Placeable::completeOpeningInventory(Object &opener) {
    openInventory(opener);
    _inventoryOpenPending = false;
}

void Placeable::playOpenedSound() {
    _inventoryOpenPending = false;
    if (auto sound = findOpenedSound(_services, _openedSound)) {
        _services.audio.mixer.play(sound, audio::AudioType::Sound, 1.0f, false, _position);
    }
}

void Placeable::openInventory(Object &opener) {
    if (_open || !_hasInventory) {
        return;
    }
    if (&opener != _game.party().getLeader().get()) {
        setStateAnimation(kCloseAnimationId);
        return;
    }
    _game.openContainer(_game.getObjectById(_id));
    _game.queueScriptEvent(*this, opener, Event(kInventoryOpenedEvent));
    _open = true;
}

void Placeable::closeInventory(Object &closer, bool takeAll) {
    if (!_open) {
        return;
    }
    if (takeAll && &closer == _game.party().getLeader().get()) {
        moveAllItemsTo(closer);
    }
    _game.queueScriptEvent(*this, closer, Event(kInventoryClosedEvent));
    setStateAnimation(kCloseAnimationId);
    _open = false;
    if (!_hasInventory || !_dieWhenEmpty || !_items.empty()) {
        return;
    }
    _usable = false;
    _game.postObjectDestruction(*this, *this, 0.0f, false);
}

// The clip an animation ID shows on a placeable: the one its row names, else
// its cut clip or the default pose.
static std::string getPlaceableClip(const IAnimations &animations, int id) {
    const int row = placeableAnimationRow(id);
    std::string clip = row >= 0 ? animations.getNameById(static_cast<uint32_t>(row)) : std::string();
    return clip.empty() ? placeableFallbackClip(id) : clip;
}

// A loop the model lacks leaves the placeable showing what it shows, and a
// swing the model lacks is passed over.
void Placeable::setStateAnimation(int animation) {
    _animation = animation;
    // A world-space animation of an animated cutscene holds the placeable.
    if (holdsCutsceneAnimation()) {
        return;
    }
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (_isCorpse || !model || animation == _shownAnimation) {
        return;
    }
    auto &animations = _services.game.animations;
    const std::string loop = getPlaceableClip(animations, animation);
    if (!model->model().getAnimation(loop)) {
        return;
    }
    const auto swingRow = stateLoopSwingRow(_shownAnimation, _shownAnimation, animation, false);
    _shownAnimation = animation;
    _openStateLoop = loop;
    const std::string swing = swingRow ? animations.getNameById(static_cast<uint32_t>(*swingRow)) : std::string();
    if (!swing.empty() && model->model().getAnimation(swing)) {
        model->playAnimation(swing);
        return;
    }
    updateOpenStateAnimation();
}

float Placeable::animationLength(int id) const {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model) {
        return 0.0f;
    }
    auto clip = model->model().getAnimation(getPlaceableClip(_services.game.animations, id));
    return clip ? clip->length() : 0.0f;
}

bool Placeable::playsAnimationOnce(int id) const {
    const int row = placeableAnimationRow(id);
    return isFireForgetCutClip(id) || (row >= 0 && _services.game.animations.isFireForgetById(static_cast<uint32_t>(row)));
}

// The loop that follows a one-shot stands where the placeable does.
void Placeable::updateOpenStateAnimation() {
    // A world-space animation of an animated cutscene holds the placeable.
    if (holdsCutsceneAnimation()) {
        return;
    }
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (_openStateLoop.empty() || !model) {
        return;
    }
    const bool swinging = std::any_of(model->animationChannels().begin(), model->animationChannels().end(),
                                      [](const auto &channel) { return !channel.finished && !(channel.properties.flags & AnimationFlags::loop); });
    if (swinging) {
        return;
    }
    setPlacedAtOrigin(false);
    model->playAnimation(_openStateLoop, nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
    _openStateLoop.clear();
}

bool Placeable::showAnimationLoop(int id) {
    // A world-space animation of an animated cutscene holds the placeable.
    if (holdsCutsceneAnimation()) {
        return false;
    }
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    const std::string clip = getPlaceableClip(_services.game.animations, id);
    if (_isCorpse || !model || clip.empty() || !model->model().getAnimation(clip)) {
        return false;
    }
    setPlacedAtOrigin(isWorldSpaceCutClip(id));
    _openStateLoop.clear();
    _shownAnimation = id;
    model->playAnimation(clip, nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
    return true;
}

// The default pose comes first: a loop the model lacks leaves it, and a
// one-shot returns to it.
void Placeable::playDialogAnimation(int id, bool looping) {
    // A world-space animation of an animated cutscene holds the placeable.
    if (holdsCutsceneAnimation()) {
        return;
    }
    if (looping) {
        if (!showAnimationLoop(id)) {
            showAnimationLoop(kDefaultAnimationId);
        }
        return;
    }
    showAnimationLoop(kDefaultAnimationId);
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    const std::string clip = getPlaceableClip(_services.game.animations, id);
    if (_isCorpse || !model || clip.empty() || !model->model().getAnimation(clip)) {
        return;
    }
    setPlacedAtOrigin(isWorldSpaceCutClip(id));
    model->playAnimation(clip);
    _openStateLoop = getPlaceableClip(_services.game.animations, kDefaultAnimationId);
}

void Placeable::showStateAnimation() {
    showAnimationLoop(_animation);
}

// END Inventory

glm::vec3 Placeable::nearestActionPoint(const glm::vec3 &from) const {
    if (!_walkmesh) {
        return _position;
    }
    const graphics::Walkmesh &walkmesh = _walkmesh->walkmesh();
    auto world = [&](const glm::vec3 &local) {
        return glm::vec3(_transform * glm::vec4(local + walkmesh.position, 1.0f));
    };
    // A point that lies exactly at the world origin counts as no point: the
    // first falls back to the placeable's position, the second to the first.
    const glm::vec3 first = world(walkmesh.relativeUsePositions[0]);
    if (first == glm::vec3(0.0f)) {
        return _position;
    }
    const glm::vec3 second = world(walkmesh.relativeUsePositions[1]);
    if (second == glm::vec3(0.0f)) {
        return first;
    }
    const glm::vec3 toFirst {from - first};
    const glm::vec3 toSecond {from - second};
    return glm::dot(toSecond, toSecond) > glm::dot(toFirst, toFirst) ? first : second;
}

graphics::AABB Placeable::collisionBounds() const {
    graphics::AABB bounds;
    if (!_walkmesh) {
        return bounds;
    }
    const glm::mat4 &transform = _walkmesh->absoluteTransform();
    for (const auto &vertex : _walkmesh->walkmesh().vertices) {
        bounds.expand(glm::vec3(transform * glm::vec4(vertex, 1.0f)));
    }
    return bounds;
}

// A corpse bag is marked at the root dummy of the body it took over.
glm::vec3 Placeable::getSelectablePosition() const {
    if (_isCorpse) {
        auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
        if (auto *root = model ? model->getNodeByName(kCorpseReticleNode) : nullptr) {
            return root->origin();
        }
    }
    return Object::getSelectablePosition();
}

} // namespace game

} // namespace reone
