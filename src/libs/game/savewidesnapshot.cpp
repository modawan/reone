/* Copyright (c) 2026 The reone project contributors */

#include "reone/game/savewidesnapshot.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/journal.h"
#include "reone/game/location.h"
#include "reone/game/modulesnapshot.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"
#include "reone/game/saveprovenance.h"
#include "reone/resource/format/gffreader.h"
#include "reone/resource/format/gffwriter.h"
#include "reone/resource/gff.h"
#include "reone/resource/parser/gff/gvt.h"
#include "reone/resource/saveworkingstate.h"
#include "reone/system/binarywriter.h"
#include "reone/system/exception/validation.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/stream/memoryoutput.h"

namespace reone {
namespace game {

namespace {

using resource::Gff;
using resource::ResType;
using resource::ResourceId;

constexpr size_t kMaxGlobalBooleans = 900;
constexpr size_t kMaxGlobalNumbers = 1000;
constexpr size_t kMaxGlobalLocations = 100;
constexpr size_t kMaxGlobalStrings = 5;
constexpr size_t kGlobalLocationBytes = 2400;

void put(Gff &record, Gff::Field field) {
    replaceSaveField(record, std::move(field));
}

std::shared_ptr<Gff> rootFromShadow(
    const Game &game, SaveResourceKind kind) {
    if (const auto *shadow = game.saveResourceShadows().find({kind, {}})) {
        auto root = shadow->cloneForMerge();
        root->setType(0xffffffff);
        return root;
    }
    return Gff::Builder().type(0xffffffff).build();
}

std::shared_ptr<Gff> emptyRecord(uint32_t type = 0) {
    return Gff::Builder().type(type).build();
}

std::shared_ptr<Gff> readGff(const ByteBuffer &bytes) {
    ByteBuffer copy(bytes);
    MemoryInputStream stream(copy);
    resource::GffReader reader(stream);
    reader.load();
    return reader.root();
}

ByteBuffer encode(const std::string &signature, const Gff &gff) {
    return resource::GffWriter(
        resource::GffFileFormat::v32(signature), gff).toBytes();
}

template <typename T>
std::vector<std::shared_ptr<Gff>> namedList(const T &values) {
    std::vector<std::shared_ptr<Gff>> result;
    result.reserve(values.size());
    for (const auto &[name, value] : values) {
        (void)value;
        result.push_back(Gff::Builder().type(0)
                             .field(Gff::Field::newCExoString("Name", name))
                             .build());
    }
    return result;
}

std::set<ResourceId> managedOuterResourceIds() {
    std::set<ResourceId> result {
        {"inventory", ResType::Res},
        {"repute", ResType::Fac},
        {"pc", ResType::Utc},
    };
    for (size_t npc = 0; npc < Party::kMaxNpcCount; ++npc) {
        result.emplace("availnpc" + std::to_string(npc), ResType::Utc);
    }
    for (size_t puppet = 0; puppet < Party::kMaxPuppetCount; ++puppet) {
        result.emplace("availpup" + std::to_string(puppet), ResType::Utc);
    }
    return result;
}

std::vector<int> currentSavedMembers(const Party &party) {
    std::vector<int> result;
    const int controlled = party.persistedState().controlledNpc;
    for (const auto &member : party.members()) {
        if (member.npc == kNpcPlayer || member.npc == controlled) continue;
        if (std::find(result.begin(), result.end(), member.npc) != result.end()) {
            throw ValidationException("party contains a duplicate companion member");
        }
        result.push_back(member.npc);
    }
    if (result.size() > 2) {
        throw ValidationException("party has more than two saved companion members");
    }
    return result;
}

} // namespace

void SaveWideSnapshot::applyTo(
    resource::SaveWorkingStateCandidate &candidate) const {
    for (const auto &id : managedOuterResources) {
        auto current = outerWorkingResources.find(id);
        if (current == outerWorkingResources.end()) {
            candidate.erase(id);
        } else {
            candidate.put(id, current->second);
        }
    }
}

std::shared_ptr<Gff> SaveWideSnapshotBuilder::buildGlobals() const {
    const auto &booleans = _game.globalBooleans();
    const auto &numbers = _game.globalNumbers();
    const auto &strings = _game.globalStrings();
    const auto &locations = _game.globalLocations();
    if (booleans.size() > kMaxGlobalBooleans ||
        numbers.size() > kMaxGlobalNumbers ||
        strings.size() > kMaxGlobalStrings ||
        locations.size() > kMaxGlobalLocations) {
        throw ValidationException("global-variable category exceeds retail capacity");
    }

    ByteBuffer booleanValues(booleans.size() / 8 + 1, 0);
    size_t booleanIndex = 0;
    for (const auto &[name, value] : booleans) {
        (void)name;
        if (value) {
            booleanValues[booleanIndex / 8] |=
                static_cast<uint8_t>(1u << (7 - booleanIndex % 8));
        }
        ++booleanIndex;
    }

    ByteBuffer numberValues;
    numberValues.reserve(numbers.size());
    for (const auto &[name, value] : numbers) {
        (void)name;
        if (value < -128 || value > 127) {
            throw ValidationException("global number is outside retail signed-byte range");
        }
        // Conversion to uint8_t is defined modulo 256 and therefore preserves
        // the retail two's-complement byte for every validated signed value.
        numberValues.push_back(static_cast<uint8_t>(value));
    }

    std::vector<std::shared_ptr<Gff>> stringValues;
    for (const auto &[name, value] : strings) {
        (void)name;
        stringValues.push_back(Gff::Builder().type(0)
                                   .field(Gff::Field::newCExoString("String", value))
                                   .build());
    }

    ByteBuffer locationValues;
    locationValues.reserve(kGlobalLocationBytes);
    MemoryOutputStream locationStream(locationValues);
    BinaryWriter locationWriter(locationStream);
    for (const auto &[name, location] : locations) {
        (void)name;
        if (!location) {
            throw ValidationException("global location is null");
        }
        const auto &position = location->position();
        const auto orientation = location->saveOrientation();
        locationWriter.writeFloat(position.x);
        locationWriter.writeFloat(position.y);
        locationWriter.writeFloat(position.z);
        locationWriter.writeFloat(orientation.x);
        locationWriter.writeFloat(orientation.y);
        locationWriter.writeFloat(orientation.z);
    }
    locationValues.resize(kGlobalLocationBytes, 0);

    auto result = rootFromShadow(_game, SaveResourceKind::GlobalVars);
    put(*result, Gff::Field::newList("CatBoolean", namedList(booleans)));
    put(*result, Gff::Field::newVoid("ValBoolean", std::move(booleanValues)));
    put(*result, Gff::Field::newList("CatNumber", namedList(numbers)));
    put(*result, Gff::Field::newVoid("ValNumber", std::move(numberValues)));
    put(*result, Gff::Field::newList("CatString", namedList(strings)));
    put(*result, Gff::Field::newList("ValString", std::move(stringValues)));
    put(*result, Gff::Field::newList("CatLocation", namedList(locations)));
    put(*result, Gff::Field::newVoid("ValLocation", std::move(locationValues)));
    return result;
}

std::shared_ptr<Gff> SaveWideSnapshotBuilder::buildPartyTable() const {
    const auto &party = _game._party;
    const auto &state = party.persistedState();
    const bool tsl = _game.isTSL();
    const size_t npcCount = tsl ? Party::kK2NpcCount : Party::kK1NpcCount;
    const size_t cardCount = tsl ? Party::kK2PazaakCardCount
                                 : Party::kK1PazaakCardCount;
    if (!party.hasValidPazaakData() || party.pazaakCardCount() != cardCount) {
        throw ValidationException("party has no valid title-specific Pazaak state");
    }

    auto result = rootFromShadow(_game, SaveResourceKind::PartyTable);
    if (tsl) {
        put(*result, Gff::Field::newCExoString("PT_PCNAME", state.pcName));
        put(*result, Gff::Field::newDword("PT_ITEM_COMPONEN", state.itemComponent));
        put(*result, Gff::Field::newDword("PT_ITEM_CHEMICAL", state.itemChemical));
        for (size_t i = 0; i < state.swoopUpgrades.size(); ++i) {
            put(*result, Gff::Field::newDword(
                "PT_SWOOP" + std::to_string(i + 1), state.swoopUpgrades[i]));
        }
    } else {
        static const std::array<const char *, 10> k2Only {
            "PT_PCNAME", "PT_ITEM_COMPONEN", "PT_ITEM_COMPONENT",
            "PT_ITEM_CHEMICAL", "PT_SWOOP1", "PT_SWOOP2", "PT_SWOOP3",
            "PT_NUM_PUPPETS", "PT_PUPPETS", "PT_AVAIL_PUPS"};
        for (const char *field : k2Only) removeSaveField(*result, field);
        removeSaveField(*result, "PT_INFLUENCE");
        removeSaveField(*result, "PT_COM_MSG_LIST");
        removeSaveField(*result, "PT_DISABLEMAP");
        removeSaveField(*result, "PT_DISABLEREGEN");
    }

    put(*result, Gff::Field::newDword(
        "PT_GOLD", static_cast<uint32_t>(std::max(0, party.gold()))));
    put(*result, Gff::Field::newInt("PT_XP_POOL", party.xp()));
    put(*result, Gff::Field::newDword("PT_PLAYEDSECONDS", state.playedSeconds));
    put(*result, Gff::Field::newInt("PT_CONTROLLED_NP", state.controlledNpc));
    put(*result, Gff::Field::newByte("PT_SOLOMODE", party.isSoloMode()));
    put(*result, Gff::Field::newByte("PT_CHEAT_USED", _metadata.cheatUsed));

    const auto savedMembers = currentSavedMembers(party);
    std::vector<std::shared_ptr<Gff>> members;
    const int runtimeLeader = party.members().empty() ? -1 : party.members().front().npc;
    for (int npc : savedMembers) {
        if (npc < 0 || static_cast<size_t>(npc) >= npcCount) {
            throw ValidationException("party member is outside title NPC range");
        }
        members.push_back(Gff::Builder().type(0)
                              .field(Gff::Field::newInt("PT_MEMBER_ID", npc))
                              .field(Gff::Field::newByte(
                                  "PT_IS_LEADER", npc == runtimeLeader))
                              .build());
    }
    put(*result, Gff::Field::newByte(
        "PT_NUM_MEMBERS", static_cast<uint32_t>(members.size())));
    put(*result, Gff::Field::newList("PT_MEMBERS", std::move(members)));

    std::vector<std::shared_ptr<Gff>> available;
    for (size_t npc = 0; npc < npcCount; ++npc) {
        available.push_back(Gff::Builder().type(0)
                                .field(Gff::Field::newByte(
                                    "PT_NPC_AVAIL", state.npcAvailable[npc]))
                                .field(Gff::Field::newByte(
                                    "PT_NPC_SELECT", state.npcSelectable[npc]))
                                .build());
    }
    put(*result, Gff::Field::newList("PT_AVAIL_NPCS", std::move(available)));

    if (tsl) {
        std::vector<std::shared_ptr<Gff>> puppets;
        if (state.puppetIds.size() > 2) {
            throw ValidationException("party has more than two active puppets");
        }
        std::set<int> puppetIds;
        for (int puppet : state.puppetIds) {
            if (puppet < 0 || puppet >= static_cast<int>(Party::kMaxPuppetCount)) {
                throw ValidationException("party puppet is outside retail range");
            }
            if (!puppetIds.insert(puppet).second) {
                throw ValidationException("party contains a duplicate active puppet");
            }
            puppets.push_back(Gff::Builder().type(0)
                                  .field(Gff::Field::newInt("PT_PUPPET_ID", puppet))
                                  .build());
        }
        put(*result, Gff::Field::newByte(
            "PT_NUM_PUPPETS", static_cast<uint32_t>(puppets.size())));
        put(*result, Gff::Field::newList("PT_PUPPETS", std::move(puppets)));

        std::vector<std::shared_ptr<Gff>> availablePuppets;
        for (size_t puppet = 0; puppet < Party::kMaxPuppetCount; ++puppet) {
            availablePuppets.push_back(Gff::Builder().type(0)
                                           .field(Gff::Field::newByte(
                                               "PT_PUP_AVAIL", state.puppetAvailable[puppet]))
                                           .field(Gff::Field::newByte(
                                               "PT_PUP_SELECT", state.puppetSelectable[puppet]))
                                           .build());
        }
        put(*result, Gff::Field::newList(
            "PT_AVAIL_PUPS", std::move(availablePuppets)));

        std::vector<std::shared_ptr<Gff>> influence;
        for (size_t npc = 0; npc < npcCount; ++npc) {
            influence.push_back(Gff::Builder().type(0)
                                    .field(Gff::Field::newInt(
                                        "PT_NPC_INFLUENCE", state.influence[npc]))
                                    .build());
        }
        put(*result, Gff::Field::newList("PT_INFLUENCE", std::move(influence)));
        put(*result, Gff::Field::newInt("PT_DISABLEMAP", state.mapDisabled));
        put(*result, Gff::Field::newInt(
            "PT_DISABLEREGEN", state.regenerationDisabled));
    }

    put(*result, Gff::Field::newInt("PT_AISTATE", state.aiState));
    put(*result, Gff::Field::newInt("PT_FOLLOWSTATE", state.followState));
    const auto &galaxyState = party.galaxyMap();
    uint32_t planetMask = 0;
    for (int planet = 0; planet < GalaxyMapState::kPlanetMaskBits; ++planet) {
        if (galaxyState.available(planet)) planetMask |= 1u << planet;
        if (galaxyState.selectable(planet)) {
            planetMask |= 1u << (planet + GalaxyMapState::kPlanetMaskBits);
        }
    }
    auto galaxy = result->findStruct("GlxyMap");
    if (!galaxy) galaxy = emptyRecord();
    put(*galaxy, Gff::Field::newDword("GlxyMapNumPnts", galaxyState.rowCount()));
    put(*galaxy, Gff::Field::newDword("GlxyMapPlntMsk", planetMask));
    put(*galaxy, Gff::Field::newInt("GlxyMapSelPnt", galaxyState.selectedPlanet()));
    put(*result, Gff::Field::newStruct("GlxyMap", std::move(galaxy)));

    std::vector<std::shared_ptr<Gff>> cards;
    for (size_t card = 0; card < cardCount; ++card) {
        int count = party.pazaakCardCounts()[card];
        if (count < 0 || count > 255) {
            throw ValidationException("Pazaak ownership count is outside byte range");
        }
        cards.push_back(Gff::Builder().type(0)
                            .field(Gff::Field::newByte("PT_PAZAAKCOUNT", count))
                            .build());
    }
    put(*result, Gff::Field::newList("PT_PAZAAKCARDS", std::move(cards)));
    std::vector<std::shared_ptr<Gff>> sideDeck;
    bool allEmpty = true;
    bool allSelected = true;
    Party::PazaakCardCounts selectedCounts {};
    for (int card : party.pazaakSideDeck()) {
        allEmpty = allEmpty && card == -1;
        bool selected = card >= 0 && static_cast<size_t>(card) < cardCount - 1;
        allSelected = allSelected && selected;
        if (selected) ++selectedCounts[static_cast<size_t>(card)];
        sideDeck.push_back(Gff::Builder().type(0)
                               .field(Gff::Field::newInt("PT_PAZSIDECARD", card))
                               .build());
    }
    if (!allEmpty && !allSelected) {
        throw ValidationException("Pazaak side deck is partially or invalidly selected");
    }
    if (allSelected) {
        for (size_t card = 0; card < cardCount - 1; ++card) {
            if (selectedCounts[card] > party.pazaakCardCounts()[card]) {
                throw ValidationException("Pazaak side deck exceeds owned card counts");
            }
        }
    }
    put(*result, Gff::Field::newList("PT_PAZSIDELIST", std::move(sideDeck)));

    std::vector<std::shared_ptr<Gff>> journal;
    for (const auto &quest : _game._journal.quests()) {
        journal.push_back(Gff::Builder().type(0)
                              .field(Gff::Field::newCExoString("JNL_PlotID", quest.plotId))
                              .field(Gff::Field::newInt("JNL_State", quest.state))
                              .field(Gff::Field::newDword("JNL_Date", quest.date))
                              .field(Gff::Field::newDword("JNL_Time", quest.time))
                              .build());
    }
    put(*result, Gff::Field::newList("JNL_Entries", std::move(journal)));

    std::vector<std::shared_ptr<Gff>> dialogMessages;
    for (const auto &message : state.dialogMessages) {
        dialogMessages.push_back(Gff::Builder().type(0)
                                     .field(Gff::Field::newCExoString(
                                         "PT_DLG_MSG_SPKR", message.speaker))
                                     .field(Gff::Field::newCExoString(
                                         "PT_DLG_MSG_MSG", message.text))
                                     .build());
    }
    put(*result, Gff::Field::newList("PT_DLG_MSG_LIST", std::move(dialogMessages)));
    auto logMessages = [](const auto &messages, const std::string &colorLabel) {
        std::vector<std::shared_ptr<Gff>> records;
        for (const auto &message : messages) {
            records.push_back(Gff::Builder().type(0)
                                  .field(Gff::Field::newByte(colorLabel, message.color))
                                  .field(Gff::Field::newDword("PT_FB_MSG_TYPE", message.type))
                                  .field(Gff::Field::newCExoString(
                                      "PT_FB_MSG_MSG", message.text))
                                  .build());
        }
        return records;
    };
    put(*result, Gff::Field::newList(
        "PT_FB_MSG_LIST", logMessages(state.feedbackMessages, "PT_FB_MSG_COLOR")));
    if (tsl) {
        auto combat = logMessages(state.combatMessages, "PT_COM_MSG_COOR");
        for (auto &record : combat) {
            auto type = record->getUint("PT_FB_MSG_TYPE");
            auto text = record->getString("PT_FB_MSG_MSG");
            removeSaveField(*record, "PT_FB_MSG_TYPE");
            removeSaveField(*record, "PT_FB_MSG_MSG");
            put(*record, Gff::Field::newDword("PT_COM_MSG_TYPE", type));
            put(*record, Gff::Field::newCExoString("PT_COM_MSG_MSG", text));
        }
        put(*result, Gff::Field::newList("PT_COM_MSG_LIST", std::move(combat)));
    }
    return result;
}

std::shared_ptr<Gff> SaveWideSnapshotBuilder::buildInventory() const {
    auto result = rootFromShadow(_game, SaveResourceKind::Inventory);
    auto player = _game._party.actualPlayer();
    if (!player) throw ValidationException("party has no actual player inventory owner");

    std::set<const Item *> equipped;
    for (const auto &[slot, item] : player->equipment()) {
        (void)slot;
        if (item) equipped.insert(item.get());
    }
    ModuleSnapshotBuilder records(_game, {});
    std::vector<std::shared_ptr<Gff>> items;
    for (const auto &item : player->items()) {
        if (!item || equipped.count(item.get()) != 0) continue;
        items.push_back(records.writeItem(*item, 0, std::nullopt));
    }
    put(*result, Gff::Field::newList("ItemList", std::move(items)));
    return result;
}

std::shared_ptr<Gff> SaveWideSnapshotBuilder::buildFactions() const {
    auto state = _game._services.game.reputes.state();
    if (state.factions.size() != state.values.size()) {
        throw ValidationException("faction definition and matrix sizes differ");
    }
    for (const auto &row : state.values) {
        if (row.size() != state.factions.size()) {
            throw ValidationException("faction reputation matrix is not square");
        }
    }
    // Retail FAC cannot apply records whose source faction is player (ID2=0),
    // but the authored base table can legitimately give that runtime row
    // non-100 values. Omit the derived base row; reject only an actual runtime
    // mutation that a retail reload would lose.
    auto baseState = _game._services.game.reputes.baseState();
    if (!state.values.empty()) {
        for (size_t target = 0; target < state.values.front().size(); ++target) {
            int baseReputation = 100;
            if (!baseState.values.empty() &&
                target < baseState.values.front().size()) {
                baseReputation = baseState.values.front()[target];
            }
            if (state.values.front()[target] != baseReputation) {
                throw ValidationException(
                    "modified player-source reputation cannot be represented by retail FAC");
            }
        }
    }

    auto result = rootFromShadow(_game, SaveResourceKind::FactionTable);
    const auto oldFactions = result->getList("FactionList");
    std::vector<std::shared_ptr<Gff>> factions;
    for (size_t id = 0; id < state.factions.size(); ++id) {
        auto record = id < oldFactions.size() ? oldFactions[id]->deepCopy()
                                              : emptyRecord(static_cast<uint32_t>(id));
        const auto &faction = state.factions[id];
        put(*record, Gff::Field::newCExoString("FactionName", faction.name));
        put(*record, Gff::Field::newDword("FactionParentID", faction.parentId));
        put(*record, Gff::Field::newWord("FactionGlobal", faction.global));
        factions.push_back(std::move(record));
    }
    put(*result, Gff::Field::newList("FactionList", std::move(factions)));

    std::vector<std::shared_ptr<Gff>> reputations;
    for (size_t target = 0; target < state.factions.size(); ++target) {
        for (size_t source = 1; source < state.factions.size(); ++source) {
            int reputation = state.values[source][target];
            if (reputation < 0 || reputation > 100) {
                throw ValidationException("faction reputation is outside 0..100");
            }
            if (reputation == 100) continue;
            reputations.push_back(Gff::Builder()
                                      .type(static_cast<uint32_t>(reputations.size()))
                                      .field(Gff::Field::newDword(
                                          "FactionID1", static_cast<uint32_t>(target)))
                                      .field(Gff::Field::newDword(
                                          "FactionID2", static_cast<uint32_t>(source)))
                                      .field(Gff::Field::newDword(
                                          "FactionRep", static_cast<uint32_t>(reputation)))
                                      .build());
        }
    }
    put(*result, Gff::Field::newList("RepList", std::move(reputations)));
    return result;
}

std::shared_ptr<Gff> SaveWideSnapshotBuilder::buildNfo() const {
    auto result = rootFromShadow(_game, SaveResourceKind::Nfo);
    const auto &state = _game._party.persistedState();
    const auto &area = *_game._module->area();
    const std::string areaName = area.localizedName().empty()
                                     ? area.name()
                                     : area.localizedName();
    put(*result, Gff::Field::newCExoString("AREANAME", areaName));
    put(*result, Gff::Field::newCExoString("LASTMODULE", _game._module->name()));
    put(*result, Gff::Field::newDword("TIMEPLAYED", state.playedSeconds));
    put(*result, Gff::Field::newByte("CHEATUSED", _metadata.cheatUsed));
    put(*result, Gff::Field::newCExoString("SAVEGAMENAME", _metadata.displayName));
    put(*result, Gff::Field::newByte("GAMEPLAYHINT", _metadata.gameplayHint));
    for (size_t i = 0; i < _metadata.liveContentNames.size(); ++i) {
        put(*result, Gff::Field::newCExoString(
            "LIVE" + std::to_string(i + 1), _metadata.liveContentNames[i]));
    }
    put(*result, Gff::Field::newByte("LIVECONTENT", _metadata.liveContent));
    for (size_t i = 0; i < _metadata.portraits.size(); ++i) {
        const std::string field = "PORTRAIT" + std::to_string(i);
        if (_metadata.portraits[i].empty()) {
            removeSaveField(*result, field);
        } else {
            put(*result, Gff::Field::newResRef(field, _metadata.portraits[i]));
        }
    }
    removeSaveField(*result, "PCAUTOSAVE");
    removeSaveField(*result, "AUTOSAVEPARAMS");

    if (_game.isTSL()) {
        removeSaveField(*result, "STORYHINT");
        put(*result, Gff::Field::newDword64("TIMESTAMP", _metadata.timestamp));
        std::string pcName = state.pcName;
        if (pcName.empty() && _game._party.actualPlayer()) {
            pcName = _game._party.actualPlayer()->name();
        }
        put(*result, Gff::Field::newCExoString("PCNAME", pcName));
        put(*result, Gff::Field::newDword("SAVENUMBER", _metadata.saveNumber));
        for (size_t i = 0; i < _metadata.storyHints.size(); ++i) {
            put(*result, Gff::Field::newByte(
                "STORYHINT" + std::to_string(i), _metadata.storyHints[i]));
        }
    } else {
        put(*result, Gff::Field::newByte("STORYHINT", _metadata.storyHint));
        removeSaveField(*result, "TIMESTAMP");
        removeSaveField(*result, "PCNAME");
        removeSaveField(*result, "SAVENUMBER");
        for (size_t i = 0; i < 10; ++i) {
            removeSaveField(*result, "STORYHINT" + std::to_string(i));
        }
    }
    return result;
}

SaveWideSnapshotResult SaveWideSnapshotBuilder::build() const noexcept {
    SaveWideSnapshotResult result;
    if (!_game._module || !_game._module->area() || !_game._runtimeSessionPlayable) {
        result.error = SaveWideSnapshotError::NoPlayableModule;
        result.message = "there is no stable playable module for save-wide capture";
        return result;
    }
    if (!_game._party.player() || !_game._party.actualPlayer()) {
        result.error = SaveWideSnapshotError::InvalidRuntimeGraph;
        result.message = "party player topology is incomplete";
        return result;
    }

    try {
        const auto &partyState = _game._party.persistedState();
        const size_t npcCount = _game.isTSL() ? Party::kK2NpcCount
                                              : Party::kK1NpcCount;
        if (partyState.controlledNpc < -1 ||
            partyState.controlledNpc >= static_cast<int>(npcCount)) {
            throw ValidationException("controlled NPC is outside title range");
        }
        if ((partyState.controlledNpc == -1) !=
            (_game._party.player() == _game._party.actualPlayer())) {
            const auto &controlled = _game._party.player();
            const auto &actual = _game._party.actualPlayer();
            throw ValidationException(
                "controlled and actual player topology contradict PARTYTABLE: "
                "PT_CONTROLLED_NP=" + std::to_string(partyState.controlledNpc) +
                ", controlled=" + controlled->tag() + "#" +
                std::to_string(controlled->id()) + ", actual=" + actual->tag() +
                "#" + std::to_string(actual->id()) +
                ", same=" + (controlled == actual ? "1" : "0"));
        }

        SaveWideSnapshot snapshot;
        snapshot.gameId = _game._gameId;
        snapshot.moduleName = _game._module->name();
        snapshot.areaName = _game._module->area()->name();
        snapshot.managedOuterResources = managedOuterResourceIds();

        auto add = [&](ResourceId id, std::shared_ptr<Gff> gff,
                       const std::string &signature, bool loose) {
            ByteBuffer bytes = encode(signature, *gff);
            snapshot.semanticResources.emplace(id, std::move(gff));
            auto &resources = loose ? snapshot.looseSlotResources
                                    : snapshot.outerWorkingResources;
            if (!resources.emplace(std::move(id), std::move(bytes)).second) {
                throw ValidationException("duplicate save-wide resource identity");
            }
        };

        add({"globalvars", ResType::Res}, buildGlobals(), "GVT ", true);
        add({"partytable", ResType::Res}, buildPartyTable(), "PT  ", true);
        add({"savenfo", ResType::Res}, buildNfo(), "NFO ", true);
        add({"inventory", ResType::Res}, buildInventory(), "INV ", false);
        add({"repute", ResType::Fac}, buildFactions(), "FAC ", false);

        ModuleSnapshotBuilder records(_game, {});
        auto addCreature = [&](const std::string &name,
                               const std::shared_ptr<Creature> &creature,
                               bool sharedInventoryOwner = false) {
            if (!creature) {
                throw ValidationException("available save-wide creature is missing");
            }
            auto utc = records.writeCreature(
                *creature, 0xffffffff, std::nullopt);
            // Reone models the retail party repository as the actual player's
            // non-equipped ItemList. inventory.res owns that topology; pc.utc
            // retains equipment but must not duplicate the shared repository.
            if (sharedInventoryOwner) removeSaveField(*utc, "ItemList");
            add({name, ResType::Utc}, std::move(utc), "UTC ", false);
        };

        if (_game.isTSL() || partyState.controlledNpc != -1) {
            addCreature("pc", _game._party.actualPlayer(), true);
        }
        for (size_t npc = 0; npc < npcCount; ++npc) {
            if (partyState.npcAvailable[npc]) {
                addCreature(
                    "availnpc" + std::to_string(npc),
                    _game._party.getAvailableMember(static_cast<int>(npc)));
            }
        }
        if (_game.isTSL()) {
            for (size_t puppet = 0; puppet < Party::kMaxPuppetCount; ++puppet) {
                if (partyState.puppetAvailable[puppet]) {
                    addCreature(
                        "availpup" + std::to_string(puppet),
                        _game._party.getAvailablePuppet(static_cast<int>(puppet)));
                }
            }
        }

        try {
            validate(snapshot);
        } catch (const std::exception &ex) {
            result.error = SaveWideSnapshotError::ValidationFailure;
            result.message = ex.what();
            return result;
        }
        result.snapshot = std::move(snapshot);
        return result;
    } catch (const ValidationException &ex) {
        result.error = SaveWideSnapshotError::UnsupportedLiveState;
        result.message = ex.what();
    } catch (const std::exception &ex) {
        result.error = SaveWideSnapshotError::EncodingFailure;
        result.message = ex.what();
    }
    return result;
}

void SaveWideSnapshotBuilder::validate(const SaveWideSnapshot &snapshot) const {
    static const std::map<ResourceId, std::string> required {
        {{"globalvars", ResType::Res}, "GVT V3.2"},
        {{"partytable", ResType::Res}, "PT  V3.2"},
        {{"savenfo", ResType::Res}, "NFO V3.2"},
        {{"inventory", ResType::Res}, "INV V3.2"},
        {{"repute", ResType::Fac}, "FAC V3.2"},
    };
    for (const auto &[id, signature] : required) {
        const auto &resources = id.resRef.value() == "globalvars" ||
                                        id.resRef.value() == "partytable" ||
                                        id.resRef.value() == "savenfo"
                                    ? snapshot.looseSlotResources
                                    : snapshot.outerWorkingResources;
        auto found = resources.find(id);
        if (found == resources.end()) {
            throw ValidationException("required save-wide resource is absent: " + id.string());
        }
        auto reopened = readGff(found->second);
        if (!reopened || reopened->signature() != signature) {
            throw ValidationException("save-wide GFF signature mismatch: " + id.string());
        }
    }
    if (snapshot.looseSlotResources.size() != 3) {
        throw ValidationException("loose save-wide resource placement is invalid");
    }
    if (snapshot.semanticResources.size() !=
        snapshot.looseSlotResources.size() + snapshot.outerWorkingResources.size()) {
        throw ValidationException("semantic and encoded save-wide resources differ");
    }
    std::set<ResourceId> encodedIds;
    for (const auto &[id, bytes] : snapshot.looseSlotResources) {
        (void)bytes;
        encodedIds.insert(id);
    }
    for (const auto &[id, bytes] : snapshot.outerWorkingResources) {
        (void)bytes;
        if (!snapshot.managedOuterResources.count(id)) {
            throw ValidationException("unmanaged resource entered E3e outer output");
        }
        encodedIds.insert(id);
    }
    std::set<ResourceId> semanticIds;
    for (const auto &[id, gff] : snapshot.semanticResources) {
        if (!gff) throw ValidationException("null semantic save-wide resource");
        semanticIds.insert(id);
    }
    if (semanticIds != encodedIds) {
        throw ValidationException("semantic and encoded save-wide identities differ");
    }

    auto globals = readGff(snapshot.looseSlotResources.at({"globalvars", ResType::Res}));
    if (globals->getData("ValLocation").size() != kGlobalLocationBytes) {
        throw ValidationException("global location blob is not 2400 bytes");
    }
    const auto parsedGlobals = resource::parseGVT(*globals);
    if (parsedGlobals.booleans.size() != _game.globalBooleans().size() ||
        parsedGlobals.numbers.size() != _game.globalNumbers().size() ||
        parsedGlobals.strings.size() != _game.globalStrings().size() ||
        parsedGlobals.locations.size() != _game.globalLocations().size()) {
        throw ValidationException("generated GLOBALVARS does not round-trip");
    }

    auto party = readGff(snapshot.looseSlotResources.at({"partytable", ResType::Res}));
    const size_t npcCount = _game.isTSL() ? Party::kK2NpcCount
                                          : Party::kK1NpcCount;
    const size_t cardCount = _game.isTSL() ? Party::kK2PazaakCardCount
                                           : Party::kK1PazaakCardCount;
    if (party->getList("PT_AVAIL_NPCS").size() != npcCount ||
        party->getList("PT_PAZAAKCARDS").size() != cardCount ||
        party->getList("PT_PAZSIDELIST").size() != Party::kK1PazaakSideDeckSize) {
        throw ValidationException("generated PARTYTABLE title topology is invalid");
    }
    if (!_game.isTSL() &&
        (party->has("PT_AVAIL_PUPS") || party->has("PT_INFLUENCE"))) {
        throw ValidationException("K1 PARTYTABLE contains K2-only state");
    }
    if (party->getInt("PT_CONTROLLED_NP", -2) !=
        _game._party.persistedState().controlledNpc) {
        throw ValidationException("PARTYTABLE controlled player is inconsistent");
    }
    const auto expectedMembers = currentSavedMembers(_game._party);
    const auto memberRecords = party->getList("PT_MEMBERS");
    if (memberRecords.size() != expectedMembers.size()) {
        throw ValidationException("PARTYTABLE member count is inconsistent");
    }
    for (size_t index = 0; index < expectedMembers.size(); ++index) {
        if (memberRecords[index]->getInt("PT_MEMBER_ID", -1) !=
            expectedMembers[index]) {
            throw ValidationException("PARTYTABLE member order is inconsistent");
        }
    }

    auto inventory = readGff(snapshot.outerWorkingResources.at({"inventory", ResType::Res}));
    size_t expectedInventoryItems = 0;
    std::set<const Item *> equipped;
    for (const auto &[slot, item] : _game._party.actualPlayer()->equipment()) {
        (void)slot;
        if (item) equipped.insert(item.get());
    }
    for (const auto &item : _game._party.actualPlayer()->items()) {
        if (item && !equipped.count(item.get())) ++expectedInventoryItems;
    }
    if (inventory->getList("ItemList").size() != expectedInventoryItems) {
        throw ValidationException("party inventory membership is inconsistent");
    }
    for (const auto &item : inventory->getList("ItemList")) {
        if (item->has("ObjectId")) {
            throw ValidationException("party inventory item has a world ObjectId");
        }
    }
    auto factions = readGff(snapshot.outerWorkingResources.at({"repute", ResType::Fac}));
    std::set<std::pair<uint32_t, uint32_t>> pairs;
    const size_t factionCount = factions->getList("FactionList").size();
    for (const auto &entry : factions->getList("RepList")) {
        uint32_t target = entry->getUint("FactionID1", UINT32_MAX);
        uint32_t source = entry->getUint("FactionID2", UINT32_MAX);
        if (target >= factionCount || source == 0 || source >= factionCount ||
            !pairs.emplace(target, source).second) {
            throw ValidationException("FAC contains an invalid or duplicate pair");
        }
    }

    for (const auto &[id, bytes] : snapshot.outerWorkingResources) {
        if (id.type != ResType::Utc) continue;
        auto utc = readGff(bytes);
        if (utc->signature() != "UTC V3.2" || utc->has("ObjectId")) {
            throw ValidationException("detached UTC topology is invalid: " + id.string());
        }
    }
    const bool pcExpected = _game.isTSL() ||
                            _game._party.persistedState().controlledNpc != -1;
    if (snapshot.outerWorkingResources.count({"pc", ResType::Utc}) !=
        static_cast<size_t>(pcExpected)) {
        throw ValidationException("pc.utc presence contradicts title/player topology");
    }
    for (size_t npc = 0; npc < Party::kMaxNpcCount; ++npc) {
        bool expected = npc < npcCount &&
                        _game._party.persistedState().npcAvailable[npc];
        bool present = snapshot.outerWorkingResources.count(
                           {"availnpc" + std::to_string(npc), ResType::Utc}) != 0;
        if (present != expected) {
            throw ValidationException("availnpc presence contradicts PARTYTABLE");
        }
    }
    for (size_t puppet = 0; puppet < Party::kMaxPuppetCount; ++puppet) {
        bool expected = _game.isTSL() &&
                        _game._party.persistedState().puppetAvailable[puppet];
        bool present = snapshot.outerWorkingResources.count(
                           {"availpup" + std::to_string(puppet), ResType::Utc}) != 0;
        if (present != expected) {
            throw ValidationException("availpup presence contradicts PARTYTABLE");
        }
    }
    auto nfo = readGff(snapshot.looseSlotResources.at({"savenfo", ResType::Res}));
    const std::string areaDisplayName = _game._module->area()->localizedName().empty()
                                            ? _game._module->area()->name()
                                            : _game._module->area()->localizedName();
    if (nfo->getString("LASTMODULE") != snapshot.moduleName ||
        nfo->getString("AREANAME") != areaDisplayName ||
        nfo->getUint("TIMEPLAYED") != _game._party.persistedState().playedSeconds) {
        throw ValidationException("NFO session identity is inconsistent");
    }
}

} // namespace game
} // namespace reone
