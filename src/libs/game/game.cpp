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

#include "reone/game/game.h"
#include "reone/game/savedruntime.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <numeric>

#include "reone/game/minigame.h"

#include "reone/audio/context.h"
#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/cutsceneattack.h"
#include "reone/game/action/startconversation.h"
#include "reone/game/combat.h"
#include "reone/game/d20/classes.h"
#include "reone/game/d20/spells.h"
#include "reone/game/debug.h"
#include "reone/game/di/services.h"
#include "reone/game/gui/hud.h"
#include "reone/game/gui/sounds.h"
#include "reone/game/location.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"
#include "reone/game/room.h"
#include "reone/game/savewidesnapshot.h"
#include "reone/game/script/routines.h"
#include "reone/game/surfaces.h"
#include "reone/graphics/context.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/font.h"
#include "reone/graphics/format/tgawriter.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/model.h"
#include "reone/graphics/modelnode.h"
#include "reone/graphics/renderbuffer.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/uniforms.h"
#include "reone/gui/gui.h"
#include "reone/movie/format/bikreader.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/director.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/format/erfreader.h"
#include "reone/resource/format/erfwriter.h"
#include "reone/resource/format/gffreader.h"
#include "reone/resource/format/gffwriter.h"
#include "reone/resource/parser/gff/gvt.h"
#include "reone/resource/parser/gff/nfo.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/cursors.h"
#include "reone/resource/provider/dialogs.h"
#include "reone/resource/provider/fonts.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/provider/layouts.h"
#include "reone/resource/provider/lips.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/provider/movies.h"
#include "reone/resource/provider/scripts.h"
#include "reone/resource/provider/soundsets.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/provider/walkmeshes.h"
#include "reone/resource/resources.h"
#include "reone/resource/saveworkingstate.h"
#include "reone/scene/di/services.h"
#include "reone/scene/drawdebug.h"
#include "reone/scene/graphs.h"
#include "reone/scene/render/pipeline.h"
#include "reone/script/di/services.h"
#include "reone/system/binarywriter.h"
#include "reone/system/clock.h"
#include "reone/system/di/services.h"
#include "reone/system/exception/validation.h"
#include "reone/system/fileutil.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"
#include "reone/system/smallset.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/threadutil.h"

#include <imgui.h>

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::movie;
using namespace reone::resource;
using namespace reone::scene;
using namespace reone::script;

namespace reone {

namespace game {

ModuleLoadContext resolveModuleLoadContext(
    bool initialSaveRestore,
    bool savedModuleSnapshot) {

    if (initialSaveRestore) {
        return ModuleLoadContext::InitialSaveRestore;
    }
    return savedModuleSnapshot
               ? ModuleLoadContext::SavedModuleTransition
               : ModuleLoadContext::FreshModule;
}

bool restoresSavedWorld(ModuleLoadContext context) {
    return context != ModuleLoadContext::FreshModule;
}

bool restoresSavedSession(ModuleLoadContext context) {
    return context == ModuleLoadContext::InitialSaveRestore;
}

bool Game::bindEffectCreator(EffectInstance &effect) const {
    if (effect._runtimeSession &&
        *effect._runtimeSession != _runtimeSessionGeneration) {
        effect.creator.reset();
        for (auto &object : effect.objectParameterObjects) {
            object.reset();
        }
        return false;
    }
    const auto &serializedContext = effect.serializedReferenceContext;
    const bool moduleGraphReferences =
        serializedContext &&
        serializedContext->domain == SerializedIdentityDomain::ModuleGraph;
    if (moduleGraphReferences && effect._savedGraph &&
        *effect._savedGraph != _savedGraphGeneration) {
        effect.creator.reset();
        for (auto &object : effect.objectParameterObjects) {
            object.reset();
        }
        return false;
    }
    if (!effect._runtimeSession) {
        effect._runtimeSession = _runtimeSessionGeneration;
    }
    if (moduleGraphReferences && !effect._savedGraph) {
        effect._savedGraph = _savedGraphGeneration;
    }
    auto resolve = [this, &serializedContext](uint32_t id) {
        return serializedContext
                   ? resolveSerializedObjectReference(id, *serializedContext)
                   : getObjectById(id);
    };
    bool allBound = true;
    if (effect.creatorId != kSavedEffectInvalidObjectId &&
        effect.creatorId != kSavedRuntimeInvalidObjectId) {
        allBound = effect.bindCreator(resolve(effect.creatorId));
    }
    for (size_t index = 0; index < effect.objectParameters.size(); ++index) {
        uint32_t id = effect.objectParameters[index];
        if (id == kSavedEffectInvalidObjectId ||
            id == kSavedRuntimeInvalidObjectId) {
            continue;
        }
        allBound = effect.bindObjectParameter(index, resolve(id)) &&
                   allBound;
    }
    return allBound;
}

bool Game::bindSavedObjectReference(SavedObjectReference &reference) const {
    if (reference._runtimeSession && *reference._runtimeSession != _runtimeSessionGeneration) {
        reference._object.reset();
        return false;
    }
    if (reference.isInvalid()) {
        reference._object.reset();
        return false;
    }
    if (!reference._runtimeSession) {
        reference._runtimeSession = _runtimeSessionGeneration;
    }
    const auto &serializedContext = reference.serializedIdentityContext();
    const bool moduleGraphReference =
        serializedContext &&
        serializedContext->domain == SerializedIdentityDomain::ModuleGraph;
    if (moduleGraphReference && reference._savedGraph &&
        *reference._savedGraph != _savedGraphGeneration) {
        reference._object.reset();
        return false;
    }
    if (moduleGraphReference && !reference._savedGraph) {
        reference._savedGraph = _savedGraphGeneration;
    }

    auto object = serializedContext
                      ? resolveSerializedObjectReference(
                            reference.id, *serializedContext)
                      : getObjectById(reference.id);
    if (!object) {
        reference._object.reset();
        return false;
    }
    reference._object = object;
    return true;
}

std::shared_ptr<Object> Game::resolveSerializedObjectReference(
    uint32_t id,
    const SerializedIdentityContext &identityContext) const {
    switch (identityContext.domain) {
    case SerializedIdentityDomain::ModuleGraph:
        if (!_reservedSavedIdentityNamespace ||
            *_reservedSavedIdentityNamespace != identityContext.identityNamespace) {
            return nullptr;
        }
        return getObjectBySavedId(id);
    case SerializedIdentityDomain::Template:
        // ObjectId-shaped fields in blueprints do not name instances in any
        // live serialized graph. Treating them as module references would
        // recreate the cross-domain equality assumption A1 removed.
        return nullptr;
    case SerializedIdentityDomain::DetachedRecord: {
        std::shared_ptr<Object> result;
        for (const auto &[_, candidate] : _objectById) {
            auto identity = candidate->serializedObjectIdentity();
            if (!identity || !(identity->context == identityContext) ||
                identity->id != id) {
                continue;
            }
            if (result && result.get() != candidate.get()) {
                throw ValidationException(
                    "Ambiguous detached-record object reference " +
                    identityContext.identityNamespace + ":" +
                    std::to_string(id));
            }
            result = candidate;
        }
        return result;
    }
    }
    return nullptr;
}

static constexpr char kDeveloperOverlayToggleHelp[] = "Ctrl+Shift+D";
static constexpr char kDeveloperTriggerToggleHelp[] = "Ctrl+Shift+T";
static constexpr char kDeveloperActorToggleHelp[] = "Ctrl+Shift+A";
static constexpr char kDeveloperActorLongToggleHelp[] = "Ctrl+Shift+L";
static constexpr char kDeveloperWatchToggleHelp[] = "Ctrl+Shift+W";
static constexpr float kDeveloperActorLabelDistance = 32.0f;
static constexpr float kCursorSizeScale = 0.5f;

static std::shared_ptr<Gff> decodeSaveGff(std::optional<Resource> resource) {
    if (!resource) {
        return {};
    }
    MemoryInputStream stream(resource->data);
    GffReader reader(stream);
    reader.load();
    return reader.root();
}

static const char *screenName(Game::Screen screen) {
    switch (screen) {
    case Game::Screen::None:
        return "None";
    case Game::Screen::MainMenu:
        return "MainMenu";
    case Game::Screen::Loading:
        return "Loading";
    case Game::Screen::CharacterGeneration:
        return "CharacterGeneration";
    case Game::Screen::InGame:
        return "InGame";
    case Game::Screen::InGameMenu:
        return "InGameMenu";
    case Game::Screen::Conversation:
        return "Conversation";
    case Game::Screen::Container:
        return "Container";
    case Game::Screen::PartySelection:
        return "PartySelection";
    case Game::Screen::SaveLoad:
        return "SaveLoad";
    case Game::Screen::GalaxyMap:
        return "GalaxyMap";
    case Game::Screen::SwoopRace:
        return "SwoopRace";
    case Game::Screen::PazaakWager:
        return "PazaakWager";
    case Game::Screen::PazaakSetup:
        return "PazaakSetup";
    case Game::Screen::PazaakBoard:
        return "PazaakBoard";
    default:
        return "Unknown";
    }
}

static pazaak::HandSelection randomPazaakHandSelection(const pazaak::SideDeck &) {
    std::array<size_t, pazaak::kSideDeckSize> indices;
    std::iota(indices.begin(), indices.end(), 0);
    for (size_t i = indices.size() - 1; i > 0; --i) {
        size_t swapIndex = static_cast<size_t>(randomInt(0, static_cast<int>(i)));
        std::swap(indices[i], indices[swapIndex]);
    }
    return {indices[0], indices[1], indices[2], indices[3]};
}

// Developer-only KotOR II showcase hand selection. The showcase side deck is
// built in a fixed order, so rotating the four-card window across sets exposes
// every supported card family during a single match while still obeying the
// ordinary ten-card side deck and four-card hand rules.
static PazaakSession::HandSelector showcasePazaakHandSelector() {
    auto set = std::make_shared<size_t>(0);
    return [set](const pazaak::SideDeck &) {
        static const std::array<pazaak::HandSelection, 3> windows {
            pazaak::HandSelection {0, 1, 2, 3},
            pazaak::HandSelection {4, 5, 6, 7},
            pazaak::HandSelection {8, 9, 0, 1},
        };
        pazaak::HandSelection selection = windows[*set % windows.size()];
        ++*set;
        return selection;
    };
}

static pazaak::MainDeck randomPazaakMainDeck() {
    std::vector<int> cards(pazaak::MainDeck::standardOrdered().cards());
    for (size_t i = cards.size() - 1; i > 0; --i) {
        size_t swapIndex = static_cast<size_t>(randomInt(0, static_cast<int>(i)));
        std::swap(cards[i], cards[swapIndex]);
    }
    return pazaak::MainDeck(std::move(cards));
}

static std::optional<pazaak::CardDefinition> k1PazaakCardDefinition(int cardId) {
    if (cardId < 0 || cardId >= 18) {
        return std::nullopt;
    }
    int magnitude = cardId % 6 + 1;
    if (cardId < 6) {
        return pazaak::CardDefinition::fixedPositive(magnitude);
    }
    if (cardId < 12) {
        return pazaak::CardDefinition::fixedNegative(magnitude);
    }
    return pazaak::CardDefinition::signSelectable(magnitude);
}

// KotOR II owns five extra card types after the numbered ones. Their order
// follows the authored side-deck screen: Tiebreaker, Double, Flip 2&4, Flip 3&6
// and Value Change.
static std::optional<pazaak::CardDefinition> k2PazaakCardDefinition(int cardId) {
    if (cardId < 0 || cardId >= 23) {
        return std::nullopt;
    }
    if (cardId < 18) {
        return k1PazaakCardDefinition(cardId);
    }
    switch (cardId) {
    case 18:
        return pazaak::CardDefinition::tiebreaker();
    case 19:
        return pazaak::CardDefinition::doubleCard();
    case 20:
        return pazaak::CardDefinition::flipTwoFour();
    case 21:
        return pazaak::CardDefinition::flipThreeSix();
    default:
        return pazaak::CardDefinition::valueChange();
    }
}

static std::optional<pazaak::CardDefinition> parseK1PazaakDeckCard(
    const std::string &token) {

    if (token.size() != 2 ||
        (token[0] != '+' && token[0] != '-' && token[0] != '*') ||
        token[1] < '1' || token[1] > '6') {
        return std::nullopt;
    }
    int magnitude = token[1] - '0';
    switch (token[0]) {
    case '+':
        return pazaak::CardDefinition::fixedPositive(magnitude);
    case '-':
        return pazaak::CardDefinition::fixedNegative(magnitude);
    case '*':
        return pazaak::CardDefinition::signSelectable(magnitude);
    default:
        return std::nullopt;
    }
}

static std::optional<pazaak::SideDeck> loadK1PazaakOpponentDeck(
    const resource::TwoDA &decks,
    int row) {

    if (row < 0 || row >= decks.getRowCount()) {
        return std::nullopt;
    }
    std::vector<pazaak::CardDefinition> cards;
    cards.reserve(pazaak::kSideDeckSize);
    for (size_t i = 0; i < pazaak::kSideDeckSize; ++i) {
        auto card = parseK1PazaakDeckCard(
            decks.getString(row, "card" + std::to_string(i)));
        if (!card) {
            return std::nullopt;
        }
        cards.push_back(*card);
    }
    return pazaak::SideDeck {
        cards[0], cards[1], cards[2], cards[3], cards[4],
        cards[5], cards[6], cards[7], cards[8], cards[9],
    };
}

static std::optional<pazaak::CardDefinition> parseK2PazaakDeckCard(
    const std::string &token) {

    // KotOR II special-card tokens, verified from shipped card descriptions.
    if (token == "$$") {
        return pazaak::CardDefinition::doubleCard();
    }
    if (token == "F1") {
        return pazaak::CardDefinition::flipTwoFour();
    }
    if (token == "F2") {
        return pazaak::CardDefinition::flipThreeSix();
    }
    if (token == "TT") {
        return pazaak::CardDefinition::tiebreaker();
    }
    if (token == "VV") {
        return pazaak::CardDefinition::valueChange();
    }
    // Otherwise the KotOR I grammar (+N / -N / *N) applies unchanged.
    return parseK1PazaakDeckCard(token);
}

static std::optional<pazaak::SideDeck> loadK2PazaakOpponentDeck(
    const resource::TwoDA &decks,
    int row) {

    if (row < 0 || row >= decks.getRowCount()) {
        return std::nullopt;
    }
    std::vector<pazaak::CardDefinition> cards;
    cards.reserve(pazaak::kSideDeckSize);
    for (size_t i = 0; i < pazaak::kSideDeckSize; ++i) {
        auto card = parseK2PazaakDeckCard(
            decks.getString(row, "card" + std::to_string(i)));
        if (!card) {
            return std::nullopt;
        }
        cards.push_back(*card);
    }
    return pazaak::SideDeck {
        cards[0], cards[1], cards[2], cards[3], cards[4],
        cards[5], cards[6], cards[7], cards[8], cards[9],
    };
}

static const char *cameraTypeName(CameraType type) {
    switch (type) {
    case CameraType::FirstPerson:
        return "FirstPerson";
    case CameraType::ThirdPerson:
        return "ThirdPerson";
    case CameraType::Static:
        return "Static";
    case CameraType::Animated:
        return "Animated";
    case CameraType::Dialog:
        return "Dialog";
    default:
        return "Unknown";
    }
}

static const char *objectTypeName(ObjectType type) {
    switch (type) {
    case ObjectType::Creature:
        return "creature";
    case ObjectType::Item:
        return "item";
    case ObjectType::Trigger:
        return "trigger";
    case ObjectType::Door:
        return "door";
    case ObjectType::Waypoint:
        return "waypoint";
    case ObjectType::Placeable:
        return "placeable";
    case ObjectType::Store:
        return "store";
    case ObjectType::Encounter:
        return "encounter";
    case ObjectType::Sound:
        return "sound";
    case ObjectType::Module:
        return "module";
    case ObjectType::Area:
        return "area";
    case ObjectType::Room:
        return "room";
    case ObjectType::Camera:
        return "camera";
    default:
        return "object";
    }
}

static bool isDeveloperOverlayChord(const input::KeyEvent &event) {
    bool control = (event.mod & input::KeyModifiers::control) != 0;
    bool shift = (event.mod & input::KeyModifiers::shift) != 0;
    return control && shift;
}

static const char *triggerDebugStateName(Trigger::DebugState state) {
    switch (state) {
    case Trigger::DebugState::Entered:
        return "enter";
    case Trigger::DebugState::Inside:
        return "inside";
    case Trigger::DebugState::Tested:
        return "tested";
    default:
        return "default";
    }
}

static int getDebugFaction(const std::shared_ptr<Object> &object) {
    if (!object) {
        return -1;
    }
    if (auto creature = dyn_cast<Creature>(object)) {
        return static_cast<int>(creature->faction());
    }
    if (auto door = dyn_cast<Door>(object)) {
        return static_cast<int>(door->faction());
    }
    if (auto placeable = dyn_cast<Placeable>(object)) {
        return static_cast<int>(placeable->faction());
    }
    return -1;
}

void Game::init() {
    initConsole();
    resetGalaxyMap();
    initLocalServices();
    setSceneSurfaces();
    setCursorType(CursorType::Default);

    _moduleNames = _services.resource.director.moduleNames();
    _saveNames = _services.resource.director.saveNames();

    playVideo("legal");
    openMainMenu();
}

void Game::initJournalNotifications() {
    _journal.setOnQuestChanged([this](const Journal::EntryChange &change) {
        submitStatusSummary(StatusSummaryCategory::Journal);
        awardPlotXPByIndex(change.plotIndex, change.xpPercentage);
    });
}

void Game::registerConsoleCommand(std::string name, std::string description, ConsoleCommandHandler handler) {
    static const std::set<std::string> cheatCommands {
        "playanim", "warp", "kill", "additem", "givexp", "givegold",
        "spawncreature", "spawncompanion", "setfaction", "setposition",
        "professionaltools", "killroom", "setability", "setskill",
        "addfeat", "removefeat", "addspell", "removespell",
        "castspellatobject", "opendoor", "closedoor"};
    bool marksCheatUsed = cheatCommands.count(name) != 0;
    _console.registerCommand(
        name, description,
        [this, handler, marksCheatUsed](const ConsoleArgs &args) {
            (this->*handler)(args);
            if (marksCheatUsed) {
                _cheatUsed = true;
            }
        });
}

void Game::initConsole() {
    registerConsoleCommand("info", "information on selected object", &Game::consoleInfo);
    registerConsoleCommand("listglobals", "list global variables", &Game::consoleListGlobals);
    registerConsoleCommand("listlocals", "list local variables", &Game::consoleListLocals);
    registerConsoleCommand("runscript", "run script", &Game::consoleRunScript);
    registerConsoleCommand("listanim", "list animations of selected object", &Game::consoleListAnim);
    registerConsoleCommand("playanim", "play animation on selected object", &Game::consolePlayAnim);
    registerConsoleCommand("warp", "warp to a module", &Game::consoleWarp);
    registerConsoleCommand("camera", "select camera (free)", &Game::consoleCamera);
    registerConsoleCommand("campos", "set free camera position", &Game::consoleCamPos);
    registerConsoleCommand("camlook", "aim free camera at a point", &Game::consoleCamLook);
    registerConsoleCommand("camstatus", "print free camera viewpoint commands", &Game::consoleCamStatus);
    registerConsoleCommand("openmenu", "open an in-game menu tab", &Game::consoleOpenMenu);
    registerConsoleCommand("openchargen", "open a character-generation screen", &Game::consoleOpenCharacterGeneration);
    registerConsoleCommand("skipmovie", "skip the active movie", &Game::consoleSkipMovie);
    registerConsoleCommand("showbark", "show a timed HUD bark message", &Game::consoleShowBark);
    registerConsoleCommand("showpopup", "show the confirmation popup with an optional icon", &Game::consoleShowPopup);
    registerConsoleCommand("showgallerymode", "open a deterministic gameplay-mode gallery fixture", &Game::consoleShowGalleryMode);
    registerConsoleCommand("graphics", "toggle 3D scene rendering: graphics on|off", &Game::consoleGraphics);
    registerConsoleCommand("seed", "reseed the shared random generator: seed <number>", &Game::consoleSeed);
    registerConsoleCommand("showhud", "open the third-person gameplay HUD for a scripted capture", &Game::consoleShowHUD);
    registerConsoleCommand("showtransition", "show an area-transition banner for a scripted capture", &Game::consoleShowTransition);
    registerConsoleCommand("opencontainer", "open the container screen on the party leader for a scripted capture", &Game::consoleOpenContainer);
    registerConsoleCommand("selectdialogoption", "select a dialog option without activating it for a scripted capture", &Game::consoleSelectDialogOption);
    registerConsoleCommand("kill", "kill selected object", &Game::consoleKill);
    registerConsoleCommand("additem", "add item to selected object", &Game::consoleAddItem);
    registerConsoleCommand("givexp", "give experience to selected creature", &Game::consoleGiveXP);
    registerConsoleCommand("givegold", "give credits to the party", &Game::consoleGiveGold);
    registerConsoleCommand("showaabb", "toggle rendering AABB", &Game::consoleShowAABB);
    registerConsoleCommand("showwalkmesh", "toggle rendering walkmesh", &Game::consoleShowWalkmesh);
    registerConsoleCommand("showtriggers", "toggle rendering triggers", &Game::consoleShowTriggers);
    registerConsoleCommand("spawncreature", "spawn a creature", &Game::consoleSpawnCreature);
    registerConsoleCommand("spawncompanion", "spawn a companion", &Game::consoleSpawnCompanion);
    registerConsoleCommand("addavailablenpc", "add an NPC to the party selection roster", &Game::consoleAddAvailableNpc);
    registerConsoleCommand("selectobjectbyid", "select an object by id", &Game::consoleSelectObjectById);
    registerConsoleCommand("selectobjectbytag", "select an object by tag", &Game::consoleSelectObjectByTag);
    registerConsoleCommand("selectleader", "select the party leader", &Game::consoleSelectLeader);
    registerConsoleCommand("setfaction", "change faction of a creature", &Game::consoleSetFaction);
    registerConsoleCommand("setposition", "change position of a creature", &Game::consoleSetPosition);
    registerConsoleCommand("professionaltools", "add various combat items to the inventory", &Game::consoleProfessionalTools);
    registerConsoleCommand("killroom", "kill all hostile creatures in a room of the selected object", &Game::consoleKillRoom);
    registerConsoleCommand("autoskipenable", "enable auto-skip for conversations", &Game::consoleAutoSkipEnable);
    registerConsoleCommand("autoskipentries", "add a sequence of entries to skip", &Game::consoleAutoSkipEntries);
    registerConsoleCommand("autoskipreplies", "add a sequence of replies to pick", &Game::consoleAutoSkipReplies);
    registerConsoleCommand("startconversation", "start a conversation with the selected object or a DLG resref", &Game::consoleStartConversation);
    registerConsoleCommand("cutsceneattack", "attack an object by id with a pre-determined animation and result", &Game::consoleCutsceneAttack);
    registerConsoleCommand("setability", "set ability value (strength, dexterity, etc.)", &Game::consoleSetAbility);
    registerConsoleCommand("setskill", "set skill value (computer use, repair, etc.)", &Game::consoleSetSkill);
    registerConsoleCommand("addfeat", "add feat by type", &Game::consoleAddOrRemoveFeat);
    registerConsoleCommand("removefeat", "remove feat by type", &Game::consoleAddOrRemoveFeat);
    registerConsoleCommand("addspell", "add spell by type", &Game::consoleAddOrRemoveSpell);
    registerConsoleCommand("removespell", "remove spell by type", &Game::consoleAddOrRemoveSpell);
    registerConsoleCommand("castspellatobject", "cast spell at object", &Game::consoleCastSpellAtObject);
    registerConsoleCommand("opendoor", "open a selected door object", &Game::consoleOpenCloseDoor);
    registerConsoleCommand("closedoor", "close a selected door object", &Game::consoleOpenCloseDoor);
    registerConsoleCommand("listgames", "list savegames", &Game::consoleListGames);
    registerConsoleCommand("loadgame", "load a savegame", &Game::consoleLoadGame);
    registerConsoleCommand("savegame", "save to a semantic slot", &Game::consoleSaveGame);
    registerConsoleCommand("startpazaak", "start a development Pazaak match", &Game::consoleStartPazaak);
    registerConsoleCommand("showpath", "show debug overlay for pathfinding", &Game::consoleShowPath);

    if (_options.game.developer) {
        registerConsoleCommand("minigameinfo", "print minigame metadata for current area", &Game::consoleMiniGameInfo);
        registerConsoleCommand("startswoop", "enter the developer swoop race mode for the current area", &Game::consoleStartSwoop);
        registerConsoleCommand("stopswoop", "exit the developer swoop race mode", &Game::consoleStopSwoop);
        registerConsoleCommand("swoopstate", "print the current swoop race progress/lateral state", &Game::consoleSwoopState);
        registerConsoleCommand("startswooprace", "enter a swoop module from the current one and auto-start the race", &Game::consoleStartSwoopRace);
        registerConsoleCommand("finishswoop", "finish the lifecycle swoop race (forced success) and return to origin", &Game::consoleFinishSwoop);
        registerConsoleCommand("startturret", "enter the turret minigame for the current area", &Game::consoleStartTurret);
        registerConsoleCommand("startturretgame", "enter a turret module from the current one and auto-start the minigame", &Game::consoleStartTurretGame);
        registerConsoleCommand("stopturret", "exit the turret minigame", &Game::consoleStopTurret);
        registerConsoleCommand("turretstate", "print the current turret aim/health/enemy state", &Game::consoleTurretState);
        registerConsoleCommand("showimgui", "open imgui demo", &Game::consoleShowImGui);
    }
}

void Game::initLocalServices() {
    auto routines = std::make_unique<Routines>(_gameId, this, &_services);
    routines->init();
    _routines = std::move(routines);

    _scriptRunner = std::make_unique<ScriptRunner>(*_routines, _services.resource.scripts);

    if (!_saveSeams.captureScreenshot) {
        _saveSeams.captureScreenshot = [this]() {
            return captureSaveScreenshot();
        };
    }

    _map = std::make_unique<Map>(*this, _services);
}

void Game::setSceneSurfaces() {
    auto walkable = _services.game.surfaces.getWalkableSurfaces();
    auto walkcheck = _services.game.surfaces.getWalkcheckSurfaces();
    auto lineOfSight = _services.game.surfaces.getLineOfSightSurfaces();
    for (auto &name : _services.scene.graphs.sceneNames()) {
        auto &scene = _services.scene.graphs.get(name);
        scene.setWalkableSurfaces(walkable);
        scene.setWalkcheckSurfaces(walkcheck);
        scene.setLineOfSightSurfaces(lineOfSight);
    }
}

bool Game::handle(const input::Event &event) {
    if (_confirmPopup && _confirmPopup->isVisible()) {
        _confirmPopup->handle(event);
        return true;
    }

    switch (event.type) {
    case input::EventType::KeyDown:
        if (handleKeyDown(event.key)) {
            return true;
        }
        break;
    case input::EventType::MouseMotion:
        if (handleMouseMotion(event.motion)) {
            return true;
        }
        break;
    case input::EventType::MouseButtonDown:
        if (handleMouseButtonDown(event.button)) {
            return true;
        }
        break;
    case input::EventType::MouseButtonUp:
        if (handleMouseButtonUp(event.button)) {
            return true;
        }
        break;
    default:
        break;
    }

    if (!_movie) {
        auto gui = getScreenGUI();
        if (gui && gui->handle(event)) {
            return true;
        }
        switch (_screen) {
        case Screen::InGame: {
            if (_party.handle(event)) {
                return true;
            }
            auto camera = getActiveCamera();
            if (camera && camera->handle(event)) {
                return true;
            }
            if (_module->handle(event)) {
                return true;
            }
            break;
        }
        case Screen::SwoopRace:
            if (_swoopRace.handle(event)) {
                return true;
            }
            break;
        case Screen::Turret:
            if (_turret.handle(event)) {
                return true;
            }
            break;
        default:
            break;
        }
    }

    return false;
}

bool Game::consumeTimingDiscontinuity() {
    bool discontinuity = _timingDiscontinuity;
    _timingDiscontinuity = false;
    return discontinuity;
}

void Game::update(float frameTime) {
    float dt = frameTime * _gameSpeed;
    if (_movie) {
        updateMovie(dt);
        return;
    }
    updateMusic();

    // Requests made by scripts, console handlers or UI code in the previous
    // update execute only after those call stacks have unwound. This precedes
    // deferred module transition handling, so save+transition in one script
    // deterministically captures the source module first.
    processPendingSave();

    if (_screen == Screen::PazaakBoard && _pazaakSession) {
        static constexpr float kPazaakOpponentEventDelay = 0.45f;
        if (_pazaakSession->advanceResultPresentation(dt)) {
            if (_pazaakBoard) {
                _pazaakBoard->refresh();
            }
            completePazaakIfReady();
        } else if (_pazaakSession && !_pazaakSession->presentationPending()) {
            _pazaakOpponentEventElapsed += dt;
            if (_pazaakOpponentEventElapsed >= kPazaakOpponentEventDelay) {
                _pazaakOpponentEventElapsed = 0.0f;
                PazaakOpponentEvent event = _pazaakSession->advanceOpponentEvent();
                if (event != PazaakOpponentEvent::None) {
                    if (_pazaakBoard) {
                        _pazaakBoard->refresh();
                    }
                    completePazaakIfReady();
                }
            }
        }
    }

    if (!_nextModule.empty()) {
        loadNextModule();
    }
    updateCamera(dt);

    if (_swoopRace.isActive()) {
        _swoopRace.update(dt);

        // Non-blocking auto-finish: when a lifecycle race reaches the finish
        // threshold, force success and return to the origin module. Plain dev
        // races (no lifecycle) keep riding so the dev stays in control.
        if (_swoopLifecycle.active && _swoopRace.finishReached()) {
            debug(str(boost::format("swoop: auto-finish progress=%.1f finish=%.1f forcedSuccess=yes returning=%s")
                      % _swoopRace.progress()
                      % _swoopRace.finishProgress()
                      % _swoopLifecycle.originModule));
            finishSwoopLifecycle(/*success=*/true);
        }
    }

    if (_turret.isActive()) {
        _turret.update(dt);

        // Non-blocking auto-finish: a lifecycle session ends as soon as the
        // turret reaches a win or a loss and returns to the origin module. Plain
        // dev sessions (no lifecycle) keep running so the dev stays in control.
        if (_turretLifecycle.active && _turret.finished()) {
            debug(str(boost::format("turret: auto-finish outcome=%s hp=%d/%d returning=%s")
                      % turretOutcomeName(_turret.outcome())
                      % _turret.hitPoints()
                      % _turret.maxHitPoints()
                      % _turretLifecycle.originModule));
            finishTurretLifecycle(_turret.outcome());
        }
    }

    bool updModule = !_movie && _module && (_screen == Screen::InGame || _screen == Screen::Conversation);
    if (updModule && !_paused) {
        _floatingText.update(dt);
        advanceWorldTime(dt);
        advancePlayedTime(dt);
        _module->update(dt);
        _combat.update(dt);
    }

    auto gui = getScreenGUI();
    if (gui) {
        gui->update(dt);
    }
    if (_confirmPopup && _confirmPopup->isVisible()) {
        _confirmPopup->update(dt);
    }
    updateSceneGraph(dt);
    if (!_paused) {
        updateDrawDebug(dt);
    }
    if (_showImGui) {
        updateImGui(dt);
    }
}

void Game::render() {
    if (_movie) {
        _movie->render();
    } else {
        renderScene();
        renderGUI();
    }
}

bool Game::handleKeyDown(const input::KeyEvent &event) {
    if (event.repeat)
        return false;

    if (handleDeveloperKeyDown(event)) {
        return true;
    }

    switch (event.code) {
    case input::KeyCode::F4:
        if (_screen == Screen::InGame) {
            requestQuickSave();
            return true;
        }
        break;

    case input::KeyCode::Minus:
        if (_options.game.developer && _gameSpeed > 1.0f) {
            _gameSpeed = glm::max(1.0f, _gameSpeed - 1.0f);
            return true;
        }
        break;

    case input::KeyCode::Equals:
        if (_options.game.developer && _gameSpeed < 8.0f) {
            _gameSpeed = glm::min(8.0f, _gameSpeed + 1.0f);
            return true;
        }
        break;

    case input::KeyCode::V:
        if (_options.game.developer && _screen == Screen::InGame) {
            toggleInGameCameraType();
            return true;
        }
        break;

    default:
        break;
    }

    return false;
}

bool Game::handleDeveloperKeyDown(const input::KeyEvent &event) {
    if (!_options.game.developer || _screen != Screen::InGame) {
        return false;
    }
    if (!isDeveloperOverlayChord(event)) {
        return false;
    }

    switch (event.code) {
    case input::KeyCode::D:
        _developerOverlay.visible = !_developerOverlay.visible;
        return true;
    case input::KeyCode::T:
        if (!_developerOverlay.visible) {
            _developerOverlay.visible = true;
            _developerOverlay.triggers = true;
        } else {
            _developerOverlay.triggers = !_developerOverlay.triggers;
        }
        return true;
    case input::KeyCode::A:
        if (!_developerOverlay.visible) {
            _developerOverlay.visible = true;
            _developerOverlay.actorLabels = true;
        } else {
            _developerOverlay.actorLabels = !_developerOverlay.actorLabels;
        }
        return true;
    case input::KeyCode::L:
        if (!_developerOverlay.visible) {
            _developerOverlay.visible = true;
            _developerOverlay.actorLabels = true;
            _developerOverlay.longActorLabels = true;
        } else if (!_developerOverlay.actorLabels) {
            _developerOverlay.actorLabels = true;
            _developerOverlay.longActorLabels = true;
        } else {
            _developerOverlay.longActorLabels = !_developerOverlay.longActorLabels;
        }
        return true;
    case input::KeyCode::W:
        if (!_developerOverlay.visible) {
            _developerOverlay.visible = true;
            _developerOverlay.watchedValues = true;
        } else {
            _developerOverlay.watchedValues = !_developerOverlay.watchedValues;
        }
        return true;
    default:
        return false;
    }
}

bool Game::handleMouseMotion(const input::MouseMotionEvent &event) {
    _cursor->setPosition({event.x, event.y});
    return false;
}

bool Game::handleMouseButtonDown(const input::MouseButtonEvent &event) {
    if (event.button != input::MouseButton::Left) {
        return false;
    }
    _cursor->setPressed(true);
    if (_movie) {
        _movie->finish();
        return true;
    }
    return false;
}

bool Game::handleMouseButtonUp(const input::MouseButtonEvent &event) {
    if (event.button != input::MouseButton::Left) {
        return false;
    }
    _cursor->setPressed(false);
    return false;
}

bool Game::loadModule(const std::string &name, std::string entry, bool initialSaveRestore) {
    info("Loading module '" + name + "'");
    _transitionInProgress = true;
    struct TransitionGuard {
        bool &value;
        ~TransitionGuard() { value = false; }
    } transitionGuard {_transitionInProgress};

    // Restoring a save is the only load an authored script may see as such,
    // and only while it runs: the flag falls away on completion, on failure
    // and on the way out of any exception.
    _loadingFromSaveGame = initialSaveRestore;
    struct LoadFromSaveGuard {
        bool &value;
        ~LoadFromSaveGuard() { value = false; }
    } loadFromSaveGuard {_loadingFromSaveGame};

    // A module transition is a technical Pazaak abort. It must not manufacture
    // a result or invoke the pending continuation.
    abortPazaak();

    // Tear down an active race before the current area (and its camera/scene)
    // is unloaded, so no dangling references survive the transition.
    if (_swoopRace.isActive()) {
        _swoopRace.stop();
        _cameraType = _savedCameraType;
    }
    if (_turret.isActive()) {
        _turret.stop();
        _cameraType = _savedCameraType;
    }
    // A direct module load (e.g. warp) while a lifecycle session is pending
    // means the player navigated away; abandon the pending return.
    // (Lifecycle-managed loads clear the session beforehand, so this only fires
    // on external loads.)
    if (_swoopLifecycle.active) {
        _swoopLifecycle = MinigameLifecycle();
    }
    if (_turretLifecycle.active) {
        _turretLifecycle = MinigameLifecycle();
    }
    // Likewise a scheduled turret session is only good for the module it named:
    // any other load means the transition was superseded, and keeping the
    // request would block startturretgame until the game was reset.
    if (_pendingTurret.active && !boost::iequals(_pendingTurret.targetModule, name)) {
        debug(str(boost::format("turret: scheduled session for '%s' dropped, loading '%s' instead")
                  % _pendingTurret.targetModule % name));
        _pendingTurret = PendingTurretRequest();
    }

    if (_screen == Screen::Conversation && _conversation) {
        _conversation->cleanupForModuleTransition();
    }

    // Exit scripts are part of the source module's last observable state.
    // Freeze that state before party/object teardown or destination mounting.
    // A capture failure aborts with the source runtime graph still alive.
    if (_module) {
        try {
            _module->area()->runOnExitScript();
        } catch (const std::exception &e) {
            error("Source module exit script failed: " + std::string(e.what()));
            return false;
        }
        if (!storeCurrentModuleForTransition()) {
            return false;
        }
    }

    bool loaded = false;

    withLoadingScreen("load_" + name, [this, &name, &entry, initialSaveRestore, &loaded]() {
        loadInGameMenus();

        try {
            if (_module) {
                _module->area()->unloadParty();
                retireActiveModuleRuntime();
            }

            // Do not carry a displayed or pending batch, indicator, or GUI
            // controls across module teardown. OnLoad events below start a new
            // batch for the destination module.
            _floatingText.reset();
            _statusSummary.reset();
            if (_hud) {
                _hud->resetStatusSummaryPresentation();
            }

            _services.resource.director.onModuleLoad(name);

            if (_loadScreen) {
                _loadScreen->setProgress(50);
            }
            render();

            _services.scene.graphs.get(kSceneMain).clear();

            std::shared_ptr<Gff> ifo(_services.resource.gffs.get("module", ResType::Ifo));
            if (!ifo) {
                throw ResourceNotFoundException("Module IFO not found");
            }
            ModuleLoadContext context = resolveModuleLoadContext(
                initialSaveRestore,
                ifo->getBool("Mod_IsSaveGame"));
            bool restoringSavedWorld = restoresSavedWorld(context);
            bool restoringSavedSession = restoresSavedSession(context);

            const auto identityContext =
                restoringSavedWorld
                    ? SerializedIdentityContext::moduleGraph(name)
                    : SerializedIdentityContext::templateResource(name);
            const std::string entryArea = ifo->getString("Mod_Entry_Area");
            auto git = entryArea.empty()
                           ? nullptr
                           : _services.resource.gffs.get(entryArea, ResType::Git);
            const bool preparedForInitialRestore =
                initialSaveRestore && _rosterMaterializationPlan &&
                _rosterMaterializationPlan->identityContext == identityContext;
            if (!preparedForInitialRestore) {
                prepareRosterMaterialization(git.get(), identityContext);
            }

            // The module itself needs a transient runtime identity even though
            // it is not serialized. Keep that allocation clear of identities
            // explicitly owned by the saved IFO graph (notably the Area).
            if (restoringSavedWorld) {
                reserveSavedObjectIds(
                    *ifo,
                    SerializedIdentityContext::moduleGraph(name),
                    SerializedGraphRoot::ModuleIfo);
            }
            _module = restoringSavedWorld ? newSavedModule() : newModule();
            if (restoringSavedWorld) {
                registerSavedModuleReferenceTarget(
                    _module,
                    SerializedIdentityContext::moduleGraph(name));
            }
            _module->load(name, *ifo, restoringSavedWorld);
            // A roster slot only changes binding once every destination object
            // has completed construction. OnLoad and saved action publication
            // therefore observe the reconciled, single-object roster graph.
            commitRosterMaterialization();
            _loadedModules.insert(std::make_pair(name, _module));

            if (_party.isEmpty()) {
                loadDefaultParty();
            }

            // Whether the world came from persisted state decides what gets
            // restored, not whether the module's authored entry hook runs.
            // Content relies on that hook every time it is entered, including
            // when revisiting a module whose world state is restored.
            _module->runOnLoadScript();

            _module->loadParty(entry, restoringSavedSession);

            bindSavedRuntimeState();
            publishSavedRuntimeState();

            info("Module '" + name + "' loaded successfully");

            if (_loadScreen) {
                _loadScreen->setProgress(100);
            }
            render();

            std::string musicName(_module->area()->music());
            playMusic(musicName);

            openInGame();
            loaded = true;
        } catch (const std::exception &e) {
            abortRosterMaterialization();
            error("Failed loading module '" + name + "': " + std::string(e.what()));
            if (initialSaveRestore) {
                // Restoring a save has already retired the session that was
                // running, so there is nothing to fall back to. Retiring alone
                // leaves Screen::None, which renders as a black window over a
                // still-updating engine; send the player somewhere deliberate.
                retireRuntimeSession();
                retireToMainMenu();
            } else {
                retireActiveModuleRuntime();
                _screen = Screen::None;
            }
        }
    });

    // However long that took, none of it was time the game world lived
    // through. Whoever drives the frame clock has to start a new epoch before
    // the next update, or the whole load lands on the world as one enormous
    // step.
    _timingDiscontinuity = true;
    return loaded;
}

void Game::retireActiveModuleRuntime() {
    _lastRenderedSceneOutput = nullptr;
    _runtimeSessionPlayable = false;

    std::set<uint32_t> sessionObjectIds;
    for (const auto &object : _party.runtimeObjects()) {
        if (object) sessionObjectIds.insert(object->id());
    }

    _combat.reset();
    _module.reset();
    _loadedModules.clear();
    std::vector<std::shared_ptr<Object>> retiredObjects;
    for (const auto &[id, object] : _objectById) {
        if (sessionObjectIds.count(id) == 0) {
            retiredObjects.push_back(object);
        }
    }
    for (const auto &object : retiredObjects) {
        destroyRuntimeObjectGraph(object);
    }
    retireSavedObjectGraph();
}

void Game::retireSavedObjectGraph() {
    ++_savedGraphGeneration;
    _reservedSavedObjectIds.clear();
    _reservedSavedIdentityNamespace.reset();
    _reservedSavedObjectIdClaims.clear();
    _objectBySavedId.clear();
    _savedIdsByObject.clear();
}

void Game::retireRuntimeSession() {
    // Stable-frame save execution is synchronous and cannot ordinarily overlap
    // retirement. A re-entrant retirement from an injected/service callback is
    // an invariant violation; the local request owner in processPendingSave()
    // still remains responsible for terminalization.
    if (_saveInProgress) {
        error("Runtime session retirement re-entered synchronous save execution");
    }
    if (_pendingSave) {
        auto request = std::move(*_pendingSave);
        _pendingSave.reset();
        SaveResult cancelled;
        cancelled.status = SaveStatus::Cancelled;
        cancelled.message = "Runtime session retired before save execution";
        finalizeSaveRequest(request, std::move(cancelled));
    }
    ++_runtimeSessionGeneration;
    _runtimeSessionPlayable = false;
    _screen = Screen::None;
    _lastRenderedSceneOutput = nullptr;

    abortPazaak();
    _lastPazaakResult.reset();
    _pazaakContinuationCaller.reset();
    _pazaakDevelopmentSelectedObjectOverride.reset();
    if (_swoopRace.isActive()) {
        _swoopRace.stop();
    }
    if (_turret.isActive()) {
        _turret.stop();
    }
    _pendingTurret = PendingTurretRequest();
    _swoopLifecycle = MinigameLifecycle();
    _turretLifecycle = MinigameLifecycle();

    if (_conversation) {
        _conversation->cleanupForModuleTransition();
        _conversation = nullptr;
    }

    _services.audio.mixer.stopAll();
    _music.reset();
    _movie.reset();
    _musicResRef.clear();
    while (!_moduleTransitionMovies.empty()) {
        _moduleTransitionMovies.pop();
    }
    setCursorType(CursorType::Default);
    _cameraType = CameraType::ThirdPerson;
    _savedCameraType = CameraType::ThirdPerson;
    _paused = false;
    _relativeMouseMode = false;

    _statusSummary.reset();
    if (_hud) {
        _hud->resetStatusSummaryPresentation();
    }

    // Drop GUI-owned object selections, conversation participants and
    // container/party bindings before releasing the runtime graph.
    _hud.reset();
    _inGame.reset();
    _dialog.reset();
    _computer.reset();
    _container.reset();
    _partySelect.reset();

    if (_map) {
        _map->retireRuntimeSession();
    }

    _combat.reset();
    abortRosterMaterialization();
    _party.retireRuntimeSession();

    _services.scene.graphs.get(kSceneMain).clear();
    _module.reset();
    _loadedModules.clear();

    _objectById.clear();
    retireSavedObjectGraph();
    _nextObjectId = kFirstRuntimeObjectId;
    _effectIds.reset();
    _worldTimeMilliseconds = 0;
    _minutesPerHour = 5;
    _worldTimeFraction = 0.0;

    _nextModule.clear();
    _nextEntry.clear();
    _atStableSavePoint = false;
}

void Game::resetGame() {
    retireRuntimeSession();

    _quitRequested = false;
    _globalStrings.clear();
    _globalBooleans.clear();
    _globalNumbers.clear();
    _globalLocations.clear();
    _customTokens.clear();
    _saveResourceShadows.clear();

    _party.reset();
    _journal.reset();
    _messageLog.reset();
    _floatingText.reset();
    _cheatUsed = false;
    _playedTimeFraction = 0.0;
    _services.resource.director.onNewGame();
}

/**
 * Resolve and validate a save without disturbing the running game.
 *
 * Every read goes through the unpublished candidate rather than the director,
 * because the director still answers for the committed session: consulting it
 * here would validate the save that is already loaded. Records that the loader
 * treats as mandatory are proven now, so the failures that used to strand a
 * half-torn-down session are raised while the old one is still authoritative.
 */
Game::PreparedSaveLoad Game::prepareSaveLoad(const resource::SaveSlotDescriptor &slot) {
    PreparedSaveLoad prepared;
    prepared.session = _services.resource.director.prepareGameLoad(slot);

    prepared.saveInfo = decodeSaveGff(
        prepared.session->findMetadata(ResourceId("savenfo", ResType::Res)));
    if (!prepared.saveInfo) {
        throw ResourceNotFoundException("saveinfo.res not found");
    }
    prepared.nfo = resource::parseNFO(*prepared.saveInfo);

    prepared.globalVars = decodeSaveGff(
        prepared.session->findMetadata(ResourceId("globalvars", ResType::Res)));
    if (!prepared.globalVars) {
        throw ResourceNotFoundException("globalvars.res not found");
    }

    // The remaining records are optional to the loader. Decode what the slot
    // carries so restoration does not read it again, but do not invent a
    // requirement: a module IFO absent from the archive still resolves from the
    // module itself once mounted, and rejecting the save here would refuse one
    // that loads correctly today.
    prepared.moduleIfo = decodeSaveGff(
        prepared.session->findWorking(ResourceId("module", ResType::Ifo)));
    prepared.partyTable = decodeSaveGff(
        prepared.session->findMetadata(ResourceId("partytable", ResType::Res)));
    prepared.inventory = decodeSaveGff(
        prepared.session->findWorking(ResourceId("inventory", ResType::Res)));

    return prepared;
}

void Game::loadGame(const resource::SaveSlotDescriptor &slot) {
    info(str(boost::format("Loading savegame '%s'") % slot.directory.filename().string()));

    // Resolve and validate the replacement before anything is given up. A
    // throw here leaves the current session and its mounts untouched, so the
    // player keeps playing instead of being left with nothing to render.
    auto prepared = prepareSaveLoad(slot);

    // Commit. The old runtime and the old mounts retire together, and only
    // then does the candidate become authoritative: no runtime ever observes
    // the other session's resources.
    resetGame();
    _services.resource.director.commitGameLoad(std::move(prepared.session));

    try {
        restoreSaveLoad(std::move(prepared));
    } catch (const std::exception &e) {
        // Past the commit boundary the previous session no longer exists and
        // cannot be restored. Retire whatever was half-built and land on a
        // deliberate screen rather than the blank one an abandoned session
        // leaves behind.
        error("Failed restoring savegame '" +
              slot.directory.filename().string() + "': " + std::string(e.what()));
        retireToMainMenu();
        return;
    }
}

void Game::restoreSaveLoad(PreparedSaveLoad prepared) {
    const NFO &nfo = prepared.nfo;
    _cheatUsed = nfo.cheatUsed;
    captureSaveResourceShadow({SaveResourceKind::Nfo, {}}, *prepared.saveInfo);

    // Add module files to resource resolution. Since all savegame files are
    // already in scope, this is going to resolve to the last module from the
    // save game.
    _services.resource.director.onModuleLoad(nfo.lastModule);

    // Restore the save-wide faction table before any module objects can query
    // disposition. A missing or malformed optional FAC starts from fresh base
    // data; it must never preserve relationships from the previous save.
    std::optional<IReputes::State> reputesState;
    try {
        if (auto reputesGff = decodeSaveGff(
                _services.resource.director.findSaveWorking(
                    ResourceId("repute", ResType::Fac)))) {
            captureSaveResourceShadow(
                {SaveResourceKind::FactionTable, {}}, *reputesGff);
            reputesState = _services.game.reputes.parse(*reputesGff);
        }
    } catch (const std::exception &e) {
        warn("Game: invalid repute.fac: " + std::string(e.what()));
    }
    if (!reputesState) {
        reputesState = _services.game.reputes.baseState();
    }
    _services.game.reputes.replace(std::move(*reputesState));

    // Deserialize global variables, proven present while the candidate was
    // still unpublished.
    deserializeGlobalVariables(*prepared.globalVars);

    // Deserialize party. The archive usually carries its own module IFO; when
    // it does not, the mounted module still supplies one.
    std::shared_ptr<Gff> ifo = prepared.moduleIfo
                                   ? prepared.moduleIfo
                                   : _services.resource.gffs.get("module", ResType::Ifo);
    if (!ifo) {
        throw ResourceNotFoundException("Module IFO not found");
    }
    captureSaveResourceShadow({SaveResourceKind::ModuleIfo, nfo.lastModule}, *ifo);
    replaceCustomTokens(parseCustomTokens(*ifo));
    const auto moduleIdentityContext =
        SerializedIdentityContext::moduleGraph(nfo.lastModule);
    prepareSavedRuntimeNamespace(*ifo, moduleIdentityContext);

    // Detached save-wide records are deliberately not traversed here: their
    // ObjectId fields do not claim identities in the active module graph.
    const std::string entryArea = ifo->getString("Mod_Entry_Area");
    if (!entryArea.empty()) {
        if (auto git = _services.resource.gffs.get(entryArea, ResType::Git)) {
            reserveSavedObjectIds(
                *git,
                moduleIdentityContext,
                SerializedGraphRoot::AreaGit);
        }
    }
    deserializeParty(*ifo, prepared.partyTable, moduleIdentityContext);

    // Once the player is loaded, deserialize player's inventory.
    if (prepared.inventory) {
        captureSaveResourceShadow(
            {SaveResourceKind::Inventory, {}}, *prepared.inventory);
        deserializeInventory(*prepared.inventory);
    }

    // Warp to the last module.
    loadModule(nfo.lastModule, /*entry=*/"", /*fromSave=*/true);
}

std::map<int, std::string> Game::parseCustomTokens(
    const resource::Gff &ifoGff) const {

    std::map<int, std::string> result;
    for (const auto &entry : ifoGff.getList("Mod_Tokens")) {
        uint32_t token = 0;
        if (!entry->readDword(token, "Mod_TokensNumber") ||
            token > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
            continue;
        }
        result[static_cast<int>(token)] = entry->getString("Mod_TokensValue");
    }
    return result;
}

void Game::replaceCustomTokens(std::map<int, std::string> tokens) {
    _customTokens = std::move(tokens);
}

void Game::deserializeGlobalVariables(resource::Gff &gvtGff) {
    captureSaveResourceShadow({SaveResourceKind::GlobalVars, {}}, gvtGff);
    GVT gvt = resource::parseGVT(gvtGff);
    _globalStrings.clear();
    _globalBooleans.clear();
    _globalNumbers.clear();
    _globalLocations.clear();

    for (auto &[name, value] : gvt.strings) {
        setGlobalString(name, value);
    }

    for (auto &[name, value] : gvt.booleans) {
        setGlobalBoolean(name, value);
    }

    for (auto &[name, value] : gvt.numbers) {
        setGlobalNumber(name, value);
    }

    for (auto &[name, value] : gvt.locations) {
        auto &[pos, rot] = value;
        setGlobalLocation(name, std::make_shared<Location>(pos, rot));
    }
}

void Game::deserializeParty(
    resource::Gff &ifoGff,
    const std::shared_ptr<Gff> &ptGff,
    const SerializedIdentityContext &moduleIdentityContext) {
    resetGalaxyMap();

    std::shared_ptr<Gff> pcGff;
    const auto &players = ifoGff.getList("Mod_PlayerList");
    if (!players.empty()) {
        const int controlledNpc =
            ptGff ? parsePartyTable(*ptGff).controlledNpc : -1;
        // Retail K2 may mark the controlled module creature primary even while
        // pc.utc holds a distinct canonical player. PARTYTABLE is authoritative.
        if (controlledNpc != -1) {
            try {
                pcGff = decodeSaveGff(
                    _services.resource.director.findSaveWorking(ResourceId("pc", ResType::Utc)));
            } catch (const std::exception &e) {
                warn("Game: invalid pc.utc: " + std::string(e.what()));
            }
        }
    }

    publishPartyRuntimeState(ifoGff, ptGff, pcGff, moduleIdentityContext);
}

void Game::publishPartyRuntimeState(
    resource::Gff &ifoGff,
    const std::shared_ptr<resource::Gff> &ptGff,
    const std::shared_ptr<resource::Gff> &pcGff,
    const SerializedIdentityContext &moduleIdentityContext) {
    if (ptGff) {
        captureSaveResourceShadow({SaveResourceKind::PartyTable, {}}, *ptGff);
        deserializeGalaxyMap(*ptGff);
        uint32_t gold = 0;
        if (ptGff->readDword(gold, "PT_GOLD")) {
            _party.takeGold(_party.gold());
            _party.giveGold(gold);
        }

        uint32_t xp = 0;
        if (ptGff->readDword(xp, "PT_XP_POOL")) {
            // Populate the shared party XP pool. Members added below are synced to it.
            _party.setXP(xp);
        }

        Party::PersistedState partyState = parsePartyTable(*ptGff);
        replacePartyTable(std::move(partyState));
        deserializePazaakPartyTable(*ptGff);
        const auto &entryArea = ifoGff.getString("Mod_Entry_Area");
        auto git = entryArea.empty()
                       ? nullptr
                       : _services.resource.gffs.get(entryArea, ResType::Git);
        prepareRosterMaterialization(git.get(), moduleIdentityContext);
        // Retail constructs the available/limbo creature records before
        // completing the primary player. Keeping that boundary also ensures
        // every party object exists before saved references are bound.
        deserializeAvailableNpcs();
    }

    const auto &players = ifoGff.getList("Mod_PlayerList");
    if (players.empty()) {
        return;
    }

    auto modulePlayer = newCreature(*players.front(), moduleIdentityContext);
    modulePlayer->deserialize(*players.front(), moduleIdentityContext);
    modulePlayer->captureSaveRecord(
        *players.front(), moduleIdentityContext, {SaveRecordOriginKind::ModulePlayer, {}});
    modulePlayer->setTag(kObjectTagPlayer);

    auto actualPlayer = modulePlayer;
    const auto &partyState = _party.persistedState();
    // PT_CONTROLLED_NP, not Mod_IsPrimaryPlr, defines whether pc.utc is the
    // canonical player distinct from the currently controlled module creature.
    if (partyState.controlledNpc != -1 && pcGff) {
        const auto pcIdentityContext =
            SerializedIdentityContext::detachedRecord("pc.utc");
        actualPlayer = newCreature(*pcGff, pcIdentityContext);
        actualPlayer->deserialize(*pcGff, pcIdentityContext);
        actualPlayer->captureSaveRecord(
            *pcGff, pcIdentityContext, {SaveRecordOriginKind::PrimaryPlayerUtc, {}});
    }

    // Retail K1 and K2 complete primary-player BIC publication by assigning
    // the derived maximum HP after creature load. Keep this explicit and
    // separate from generic creature deserialization: corpses, party NPCs and
    // unrelated serialized PCs must retain their archived health state.
    actualPlayer->restorePrimaryPlayerHitPoints();

    _party.setPlayer(modulePlayer);
    _party.setActualPlayer(actualPlayer);
    if (partyState.controlledNpc != -1 &&
        !_party.bindRosterCreature(
            {RosterKind::Npc, partyState.controlledNpc}, modulePlayer)) {
        throw ValidationException(
            "Controlled NPC could not bind its logical roster slot");
    }

    if (ptGff) {
        deserializePartyMembers(*ptGff);
        deserializeJournal(*ptGff);
    } else {
        _party.addMember(kNpcPlayer, actualPlayer);
    }
}

Party::PersistedState Game::parsePartyTable(const resource::Gff &ptGff) const {
    Party::PersistedState state;
    state.pcName = ptGff.getString("PT_PCNAME");
    // GFF labels are capped at sixteen bytes. Retail KotOR II stores the
    // component count under this exact truncated label.
    state.itemComponent = ptGff.getUint("PT_ITEM_COMPONEN");
    if (!ptGff.has("PT_ITEM_COMPONEN")) {
        state.itemComponent = ptGff.getUint("PT_ITEM_COMPONENT");
    }
    state.itemChemical = ptGff.getUint("PT_ITEM_CHEMICAL");
    state.swoopUpgrades[0] = ptGff.getUint("PT_SWOOP1");
    state.swoopUpgrades[1] = ptGff.getUint("PT_SWOOP2");
    state.swoopUpgrades[2] = ptGff.getUint("PT_SWOOP3");
    state.playedSeconds = ptGff.getUint("PT_PLAYEDSECONDS");
    uint32_t playedMinutes = ptGff.getUint("PT_PLAYEDMINUTES");
    if (playedMinutes != 0) {
        state.playedSeconds = playedMinutes * 60;
    }
    state.controlledNpc = ptGff.getInt("PT_CONTROLLED_NP", -1);
    state.soloMode = ptGff.getBool("PT_SOLOMODE");

    const auto memberList = ptGff.getList("PT_MEMBERS");
    size_t memberCount = std::min<size_t>(
        std::min<size_t>(ptGff.getUint("PT_NUM_MEMBERS"), memberList.size()), 2);
    for (size_t index = 0; index < memberCount; ++index) {
        int npc = memberList[index]->getInt("PT_MEMBER_ID", -1);
        state.memberIds.push_back(npc);
        if (memberList[index]->getBool("PT_IS_LEADER")) {
            state.leader = npc;
        }
    }

    const auto puppetList = ptGff.getList("PT_PUPPETS");
    size_t puppetCount = std::min<size_t>(
        std::min<size_t>(ptGff.getUint("PT_NUM_PUPPETS"), puppetList.size()),
        Party::kMaxPuppetCount);
    for (size_t index = 0; index < puppetCount; ++index) {
        state.puppetIds.push_back(puppetList[index]->getInt("PT_PUPPET_ID", -1));
    }

    const auto availableNpcs = ptGff.getList("PT_AVAIL_NPCS");
    size_t npcCount = std::min(
        availableNpcs.size(), isTSL() ? Party::kK2NpcCount : Party::kK1NpcCount);
    for (size_t npc = 0; npc < npcCount; ++npc) {
        state.npcAvailable[npc] = availableNpcs[npc]->getBool("PT_NPC_AVAIL");
        state.npcSelectable[npc] = availableNpcs[npc]->getBool("PT_NPC_SELECT", true);
    }

    const auto influences = ptGff.getList("PT_INFLUENCE");
    for (size_t npc = 0; npc < std::min(influences.size(), Party::kMaxNpcCount); ++npc) {
        state.influence[npc] = influences[npc]->getInt("PT_NPC_INFLUENCE", -1);
    }

    const auto availablePuppets = ptGff.getList("PT_AVAIL_PUPS");
    for (size_t puppet = 0;
         puppet < std::min(availablePuppets.size(), Party::kMaxPuppetCount);
         ++puppet) {
        state.puppetAvailable[puppet] =
            availablePuppets[puppet]->getBool("PT_PUP_AVAIL");
        state.puppetSelectable[puppet] =
            availablePuppets[puppet]->getBool("PT_PUP_SELECT", true);
    }

    state.aiState = ptGff.getInt("PT_AISTATE");
    state.followState = ptGff.getInt("PT_FOLLOWSTATE");
    if (auto galaxy = ptGff.findStruct("GlxyMap")) {
        state.galaxyPointCount = galaxy->getUint("GlxyMapNumPnts");
        uint32_t mask = galaxy->getUint("GlxyMapPlntMsk");
        for (size_t planet = 0; planet < Party::kGalaxyPlanetCount; ++planet) {
            state.planetAvailable[planet] = (mask & (1u << planet)) != 0;
            state.planetSelectable[planet] = (mask & (1u << (planet + 16))) != 0;
        }
        state.selectedPlanet = galaxy->getInt("GlxyMapSelPnt", -1);
    }
    state.mapDisabled = ptGff.getBool("PT_DISABLEMAP");
    state.regenerationDisabled = ptGff.getBool("PT_DISABLEREGEN");

    for (const auto &entry : ptGff.getList("PT_DLG_MSG_LIST")) {
        Party::SavedDialogMessage message;
        message.speaker = entry->getString("PT_DLG_MSG_SPKR");
        message.text = entry->getString("PT_DLG_MSG_MSG");
        state.dialogMessages.push_back(std::move(message));
    }
    for (const auto &entry : ptGff.getList("PT_FB_MSG_LIST")) {
        Party::SavedLogMessage message;
        entry->readByte(message.color, "PT_FB_MSG_COLOR");
        entry->readDword(message.type, "PT_FB_MSG_TYPE");
        message.text = entry->getString("PT_FB_MSG_MSG");
        state.feedbackMessages.push_back(std::move(message));
    }
    for (const auto &entry : ptGff.getList("PT_COM_MSG_LIST")) {
        Party::SavedLogMessage message;
        entry->readByte(message.color, "PT_COM_MSG_COOR");
        entry->readDword(message.type, "PT_COM_MSG_TYPE");
        message.text = entry->getString("PT_COM_MSG_MSG");
        state.combatMessages.push_back(std::move(message));
    }
    return state;
}

void Game::replacePartyTable(Party::PersistedState state) {
    _party.setPersistedState(std::move(state));
}

void Game::resetGalaxyMap() {
    // K1 takes its planet count from content; K2 ignores the table and always
    // carries sixteen rows.
    auto planetary = _services.resource.twoDas.get("planetary");
    _party.galaxyMap().reset(_gameId, planetary ? planetary->getRowCount() : 0);
}

void Game::deserializeGalaxyMap(resource::Gff &ptGff) {
    _party.galaxyMap().loadFromPartyTable(ptGff);
}

void Game::deserializePazaakPartyTable(resource::Gff &ptGff) {
    const auto &pazaakCards = ptGff.getList("PT_PAZAAKCARDS");
    const auto &pazaakSide = ptGff.getList("PT_PAZSIDELIST");
    // Each title stores its own number of ownership entries, so the saved table
    // is accepted at either authored length and the card-type ID range follows
    // from it.
    size_t expectedCards = isTSL() ? Party::kK2PazaakCardCount : Party::kK1PazaakCardCount;
    size_t cardTypes = expectedCards - 1;
    if (pazaakCards.size() == expectedCards &&
        pazaakSide.size() == Party::kK1PazaakSideDeckSize) {
        Party::PazaakCardCounts counts {};
        Party::PazaakSideDeck sideDeck;
        bool valid = true;
        for (size_t i = 0; i < expectedCards; ++i) {
            counts[i] = pazaakCards[i]->getInt("PT_PAZAAKCOUNT", -1);
            valid = valid && counts[i] >= 0 && counts[i] <= 255;
        }
        bool allEmpty = true;
        bool allSelected = true;
        Party::PazaakCardCounts selectedCounts {};
        for (size_t i = 0; i < sideDeck.size(); ++i) {
            sideDeck[i] = pazaakSide[i]->getInt("PT_PAZSIDECARD", -2);
            allEmpty = allEmpty && sideDeck[i] == -1;
            bool owned = sideDeck[i] >= 0 && static_cast<size_t>(sideDeck[i]) < cardTypes;
            allSelected = allSelected && owned;
            if (owned) {
                ++selectedCounts[sideDeck[i]];
            }
        }
        valid = valid && (allEmpty || allSelected);
        if (allSelected) {
            for (size_t i = 0; i < cardTypes; ++i) {
                valid = valid && selectedCounts[i] <= counts[i];
            }
        }
        if (valid) {
            _party.setPazaakData(std::move(counts), std::move(sideDeck), expectedCards);
        } else {
            warn("Game: invalid Pazaak state in PARTYTABLE.res");
        }
    } else if (!pazaakCards.empty() || !pazaakSide.empty()) {
        warn("Game: invalid Pazaak list sizes in PARTYTABLE.res");
    }

}

void Game::saveNpcState(int npc) {
    // Retail bounds the roster flat, then treats an empty slot as nothing to
    // save rather than an error.
    if (npc < 0 || npc >= static_cast<int>(Party::kMaxNpcCount)) {
        return;
    }
    auto creature = _party.getAvailableMember(npc);
    if (!creature) {
        return;
    }

    // The same record a save writes for this roster slot, produced by the same
    // serializer, so a companion persisted here reads back exactly as one
    // persisted by saving would.
    auto candidate = resource::SaveWorkingStateCandidate::fromCommitted(
        _services.resource.director.committedSaveWorkingState());
    candidate.put(
        ResourceId("availnpc" + std::to_string(npc), ResType::Utc),
        SaveWideSnapshotBuilder::availableNpcRecord(*this, *creature));
    _services.resource.director.adoptSaveWorkingState(candidate.freeze());
}

Game::RosterRepresentationKey Game::rosterRepresentationKey(
    const resource::Gff &gff) const {
    RosterRepresentationKey key;
    key.tag = boost::to_lower_copy(gff.getString("Tag"));
    return key;
}

Game::RosterRepresentationKey Game::rosterRepresentationKey(
    const Creature &creature) const {
    RosterRepresentationKey key;
    key.tag = boost::to_lower_copy(creature.tag());
    return key;
}

bool Game::sameRosterRepresentation(
    const RosterRepresentationKey &lhs,
    const RosterRepresentationKey &rhs) const {
    // Retail's RebuildPartyTable resolves authored module creatures by Tag,
    // then binds the resulting runtime object to the explicit PartyTable slot.
    // AVAILNPC/AVAILPUP files retain that same tag but no durable ObjectId or
    // slot field. Appearance, name, portrait and template provenance are live
    // character state and therefore cannot strengthen this authored contract.
    return lhs.valid() && rhs.valid() && lhs.tag == rhs.tag;
}

void Game::prepareRosterMaterialization(
    const resource::Gff *git,
    const SerializedIdentityContext &identityContext) {
    RosterMaterializationPlan plan;
    plan.identityContext = identityContext;

    const auto &persisted = _party.persistedState();
    auto isActive = [&persisted](const RosterIdentity &identity) {
        const auto &active = identity.kind == RosterKind::Npc
                                 ? persisted.memberIds
                                 : persisted.puppetIds;
        return (identity.kind == RosterKind::Npc &&
                persisted.controlledNpc == identity.slot) ||
               std::find(active.begin(), active.end(), identity.slot) !=
                   active.end();
    };
    auto loadRecord = [this](const RosterIdentity &identity) {
        auto bound = _party.rosterCreature(identity);
        if (bound && bound->saveRecordProvenance() &&
            bound->saveRecordProvenance()->shadow) {
            return bound->saveRecordProvenance()->shadow.cloneForMerge();
        }

        const std::string prefix =
            identity.kind == RosterKind::Npc ? "availnpc" : "availpup";
        const std::string resref = prefix + std::to_string(identity.slot);
        try {
            return decodeSaveGff(
                _services.resource.director.findSaveWorking(
                    ResourceId(resref, ResType::Utc)));
        } catch (const std::exception &e) {
            warn("Game: invalid " + resref + ".utc: " + std::string(e.what()));
            return std::shared_ptr<Gff> {};
        }
    };

    const size_t npcCount = isTSL() ? Party::kK2NpcCount : Party::kK1NpcCount;
    for (size_t slot = 0; slot < npcCount; ++slot) {
        if (!persisted.npcAvailable[slot]) {
            continue;
        }
        RosterIdentity identity {RosterKind::Npc, static_cast<int>(slot)};
        auto bound = _party.rosterCreature(identity);
        auto record = loadRecord(identity);
        if (!record && !bound) {
            continue;
        }
        auto key = record ? rosterRepresentationKey(*record)
                          : rosterRepresentationKey(*bound);
        plan.records.emplace(
            identity,
            PlannedRosterRecord {
                std::move(record), std::move(key), isActive(identity), false});
    }
    if (isTSL()) {
        for (size_t slot = 0; slot < Party::kMaxPuppetCount; ++slot) {
            if (!persisted.puppetAvailable[slot]) {
                continue;
            }
            RosterIdentity identity {RosterKind::Puppet, static_cast<int>(slot)};
            auto bound = _party.rosterCreature(identity);
            auto record = loadRecord(identity);
            if (!record && !bound) {
                continue;
            }
            auto key = record ? rosterRepresentationKey(*record)
                              : rosterRepresentationKey(*bound);
            plan.records.emplace(
                identity,
                PlannedRosterRecord {
                    std::move(record), std::move(key), isActive(identity), false});
        }
    }

    for (auto first = plan.records.begin(); first != plan.records.end(); ++first) {
        for (auto second = std::next(first); second != plan.records.end(); ++second) {
            if (sameRosterRepresentation(
                    first->second.key, second->second.key)) {
                throw ValidationException(
                    "Ambiguous logical roster representation signatures");
            }
        }
    }

    if (git) {
        for (const auto &creature : git->getList("Creature List")) {
            auto key = rosterRepresentationKey(*creature);
            std::optional<RosterIdentity> logical;
            for (const auto &[identity, record] : plan.records) {
                if (!sameRosterRepresentation(key, record.key)) {
                    continue;
                }
                if (logical) {
                    throw ValidationException(
                        "GIT creature matches multiple logical roster slots");
                }
                logical = identity;
            }
            if (!logical) {
                continue;
            }
            if (std::any_of(
                    plan.gitBindings.begin(), plan.gitBindings.end(),
                    [&logical](const auto &binding) {
                        return binding.second == *logical;
                    })) {
                throw ValidationException(
                    "Multiple GIT creatures represent one logical roster slot");
            }
            plan.gitBindings.emplace_back(std::move(key), *logical);
            plan.records.at(*logical).representedInGit = true;
        }
    }

    _rosterMaterializationPlan = std::move(plan);
}

RosterGitMaterialization Game::rosterGitMaterialization(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    if (!_rosterMaterializationPlan ||
        !(_rosterMaterializationPlan->identityContext == identityContext)) {
        return {};
    }
    auto key = rosterRepresentationKey(gff);
    auto found = std::find_if(
        _rosterMaterializationPlan->gitBindings.begin(),
        _rosterMaterializationPlan->gitBindings.end(),
        [this, &key](const auto &binding) {
            return sameRosterRepresentation(key, binding.first);
        });
    if (found == _rosterMaterializationPlan->gitBindings.end()) {
        return {};
    }

    const auto &identity = found->second;
    const auto &record = _rosterMaterializationPlan->records.at(identity);
    if (!record.active) {
        return {RosterGitAction::MaterializeAndBind, identity, {}};
    }

    auto existing = _party.rosterCreature(identity);
    if (!existing) {
        throw ValidationException(
            "Active roster slot has no materialized creature binding");
    }
    if (identityContext.hasAuthoritativeObjectIds()) {
        registerSavedObjectIdentity(savedObjectId(gff), existing, identityContext);
    }
    return {RosterGitAction::OmitAndReuse, identity, std::move(existing)};
}

void Game::stageRosterGitCreature(
    const RosterIdentity &identity,
    const std::shared_ptr<Creature> &creature) {
    if (!_rosterMaterializationPlan || !creature ||
        !_rosterMaterializationPlan->records.count(identity) ||
        !_rosterMaterializationPlan->stagedBindings.emplace(
            identity, creature).second) {
        throw ValidationException("Invalid staged roster creature binding");
    }
}

void Game::commitRosterMaterialization() {
    if (!_rosterMaterializationPlan) {
        return;
    }

    std::set<const Creature *> stagedCreatures;
    std::vector<std::shared_ptr<Creature>> retiredBindings;
    for (const auto &[identity, creature] :
         _rosterMaterializationPlan->stagedBindings) {
        if (!stagedCreatures.insert(creature.get()).second) {
            throw ValidationException(
                "One runtime creature was staged for multiple roster slots");
        }
        auto existing = _party.rosterIdentity(*creature);
        if (existing && *existing != identity) {
            throw ValidationException(
                "Staged creature is bound to a different logical roster slot");
        }
        auto previous = _party.rosterCreature(identity);
        if (previous && previous != creature) {
            if (_party.isMember(*previous) || previous == _party.player() ||
                previous == _party.actualPlayer()) {
                throw ValidationException(
                    "Cannot replace an active roster creature binding");
            }
            retiredBindings.push_back(std::move(previous));
        }
    }
    for (const auto &[identity, creature] :
         _rosterMaterializationPlan->stagedBindings) {
        if (!_party.bindRosterCreature(identity, creature)) {
            throw ValidationException("Failed to publish roster creature binding");
        }
    }
    // The old inactive representation was deliberately retained until every
    // destination object had constructed. Once all logical bindings publish,
    // end its complete runtime-object lifetime through the generic registry
    // finalizer; A3 remains responsible only for the logical binding choice.
    for (const auto &retired : retiredBindings) {
        destroyRuntimeObjectGraph(retired);
    }
    _rosterMaterializationPlan.reset();
}

void Game::abortRosterMaterialization() {
    _rosterMaterializationPlan.reset();
}

void Game::deserializeAvailableNpcs() {
    const auto &persisted = _party.persistedState();
    size_t npcCount = isTSL() ? Party::kK2NpcCount : Party::kK1NpcCount;
    for (size_t npc = 0; npc < npcCount; ++npc) {
        if (!persisted.npcAvailable[npc]) {
            continue;
        }
        std::string utc = str(boost::format("availnpc%d") % npc);

        const RosterIdentity identity {RosterKind::Npc, static_cast<int>(npc)};
        std::shared_ptr<Gff> utcGff;
        if (_rosterMaterializationPlan) {
            auto planned = _rosterMaterializationPlan->records.find(identity);
            if (planned != _rosterMaterializationPlan->records.end()) {
                if (planned->second.representedInGit && !planned->second.active) {
                    continue;
                }
                utcGff = planned->second.detached;
            }
        }
        if (!utcGff) {
            try {
                utcGff = decodeSaveGff(
                    _services.resource.director.findSaveWorking(ResourceId(utc, ResType::Utc)));
            } catch (const std::exception &e) {
                warn("Game: invalid " + utc + ".utc: " + std::string(e.what()));
                continue;
            }
        }
        if (!utcGff) {
            warn("Game: missing " + utc + ".utc");
            continue;
        }

        const auto identityContext =
            SerializedIdentityContext::detachedRecord(utc + ".utc");
        auto creature = newCreature(*utcGff, identityContext);
        creature->deserialize(*utcGff, identityContext);
        creature->captureSaveRecord(
            *utcGff,
            identityContext,
            {SaveRecordOriginKind::AvailableNpc, std::to_string(npc)});

        _party.addAvailableMember(static_cast<int>(npc), creature);
    }

    if (!isTSL()) {
        return;
    }
    for (size_t puppet = 0; puppet < Party::kMaxPuppetCount; ++puppet) {
        if (!persisted.puppetAvailable[puppet]) {
            continue;
        }
        std::string utc = str(boost::format("availpup%d") % puppet);

        const RosterIdentity identity {
            RosterKind::Puppet, static_cast<int>(puppet)};
        std::shared_ptr<Gff> utcGff;
        if (_rosterMaterializationPlan) {
            auto planned = _rosterMaterializationPlan->records.find(identity);
            if (planned != _rosterMaterializationPlan->records.end()) {
                if (planned->second.representedInGit && !planned->second.active) {
                    continue;
                }
                utcGff = planned->second.detached;
            }
        }
        if (!utcGff) {
            try {
                utcGff = decodeSaveGff(
                    _services.resource.director.findSaveWorking(ResourceId(utc, ResType::Utc)));
            } catch (const std::exception &e) {
                warn("Game: invalid " + utc + ".utc: " + std::string(e.what()));
                continue;
            }
        }
        if (!utcGff) {
            warn("Game: missing " + utc + ".utc");
            continue;
        }

        const auto identityContext =
            SerializedIdentityContext::detachedRecord(utc + ".utc");
        auto creature = newCreature(*utcGff, identityContext);
        creature->deserialize(*utcGff, identityContext);
        creature->captureSaveRecord(
            *utcGff,
            identityContext,
            {SaveRecordOriginKind::AvailablePuppet, std::to_string(puppet)});
        _party.addAvailablePuppet(static_cast<int>(puppet), std::move(creature));
    }
}

void Game::deserializePartyMembers(resource::Gff &ptGff) {
    auto savedMembers = ptGff.getList("PT_MEMBERS");
    size_t memberCount = std::min<size_t>(
        std::min<size_t>(ptGff.getUint("PT_NUM_MEMBERS"), savedMembers.size()), 2);
    savedMembers.resize(memberCount);
    auto leader = std::find_if(savedMembers.begin(), savedMembers.end(), [](auto &member) {
        return member->getBool("PT_IS_LEADER");
    });

    auto addMember = [&](resource::Gff &memberGff) {
        int32_t npc = -1;
        if (!memberGff.readInt(npc, "PT_MEMBER_ID")) {
            warn("Game: missing PT_MEMBER_ID");
            return;
        }
        if (_party.isMember(npc)) {
            return;
        }

        auto member =
            npc == _party.persistedState().controlledNpc &&
                    _party.player() != _party.actualPlayer()
                ? _party.player()
                : _party.getAvailableMember(npc);
        if (!member) {
            warn("Game: NPC is not available: " + std::to_string(npc));
            return;
        }

        _party.addMember(npc, member);
    };

    // Party leader is the first runtime member. A controlled companion is the
    // module player while pc.utc remains the actual player in limbo.
    if (leader != savedMembers.end()) {
        addMember(**leader);
    }

    auto actualPlayer = _party.actualPlayer();
    // A zero-member controlled-NPC PARTYTABLE is used by retail K2 prologue
    // saves for an NPC operating alone while the canonical PC remains in
    // limbo. Non-empty lists retain the usual implicit canonical PC member.
    const bool canonicalPlayerIsActive =
        _party.persistedState().controlledNpc == -1 || !savedMembers.empty();
    if (canonicalPlayerIsActive && actualPlayer && !_party.isMember(*actualPlayer)) {
        _party.addMember(kNpcPlayer, actualPlayer);
    }

    if (_party.player() != actualPlayer &&
        !_party.isMember(_party.persistedState().controlledNpc)) {
        _party.addMember(_party.persistedState().controlledNpc, _party.player());
    }

    for (auto &savedMember : savedMembers) {
        if (leader == savedMembers.end() || savedMember != *leader) {
            addMember(*savedMember);
        }
    }
}

void Game::deserializeJournal(const resource::Gff &ptGff) {
    for (const auto &jnlEntry : ptGff.getList("JNL_Entries")) {
        std::string plotId(jnlEntry->getString("JNL_PlotID"));
        if (plotId.empty()) {
            warn("Game: missing JNL_PlotID");
            continue;
        }
        int state = jnlEntry->getInt("JNL_State");
        uint32_t date = jnlEntry->getUint("JNL_Date");
        uint32_t time = jnlEntry->getUint("JNL_Time");
        _journal.restoreEntry(std::move(plotId), state, date, time);
    }
}

void Game::deserializeInventory(resource::Gff &inventoryGff) {
    std::shared_ptr<Creature> player = _party.actualPlayer();
    if (!player) {
        return;
    }
    player->deserializeOwnedItems(
        inventoryGff,
        SerializedIdentityContext::detachedRecord("inventory.res"),
        SaveRecordOriginKind::PartyInventoryItem,
        false,
        "inventory");
}

bool Game::loadParty() {
    std::shared_ptr<Gff> ifo(_services.resource.gffs.get("module", ResType::Ifo));
    if (!ifo) {
        throw ResourceNotFoundException("module.ifo not found");
    }

    return true;
}

void Game::loadDefaultParty() {
    // A new game starts with the authored basic collection for the running title.
    _party.setDefaultPazaakData(
        isTSL() ? Party::kK2PazaakCardCount : Party::kK1PazaakCardCount);
    std::string member1, member2, member3;
    _party.defaultMembers(member1, member2, member3);

    if (!member1.empty()) {
        std::shared_ptr<Creature> player = newCreature();
        player->loadFromBlueprint(member1);
        player->setTag(kObjectTagPlayer);
        player->setImmortal(true);
        _party.addMember(kNpcPlayer, player);
        _party.setPlayer(player);
        _party.setActualPlayer(player);
    }
    if (!member2.empty()) {
        std::shared_ptr<Creature> companion = newCreature();
        companion->loadFromBlueprint(member2);
        companion->setImmortal(true);
        companion->equip("g_w_dblsbr001");
        _party.addMember(0, companion);
    }
    if (!member3.empty()) {
        std::shared_ptr<Creature> companion = newCreature();
        companion->loadFromBlueprint(member3);
        companion->setImmortal(true);
        _party.addMember(1, companion);
    }
}

void Game::setCursorType(CursorType type) {
    if (_cursorType == type) {
        return;
    }
    if (type == CursorType::None) {
        _cursor.reset();
    } else {
        _cursor = _services.resource.cursors.get(type);
    }
    _cursorType = type;
}

void Game::playVideo(const std::string &name) {
    _moduleTransitionMovies = std::queue<std::string>();
    startVideo(name);
}

void Game::playMusic(const std::string &resRef) {
    if (_musicResRef == resRef) {
        return;
    }
    if (_music) {
        _music->stop();
        _music.reset();
    }
    _musicResRef = resRef;
}

void Game::renderScene() {
    if (!_module || !_options.graphics.sceneRender) {
        _lastRenderedSceneOutput = nullptr;
        return;
    }
    auto &scene = _services.scene.graphs.get(kSceneMain);
    auto &output = scene.render({_options.graphics.width, _options.graphics.height});
    _lastRenderedSceneOutput = &output;
    _services.graphics.uniforms.setLocals(std::bind(&LocalUniforms::reset, std::placeholders::_1));
    _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::ndcTexture));
    _services.graphics.context.bindTexture(output);
    _services.graphics.meshRegistry.get(MeshName::quadNDC).draw(_services.graphics.statistic);
}

void Game::toggleInGameCameraType() {
    switch (_cameraType) {
    case CameraType::FirstPerson:
        if (_party.getLeader()) {
            _cameraType = CameraType::ThirdPerson;
        }
        break;
    case CameraType::ThirdPerson: {
        _module->player().stopMovement();
        std::shared_ptr<Area> area(_module->area());
        auto thirdPerson = area->getCamera<ThirdPersonCamera>(CameraType::ThirdPerson);
        auto firstPerson = area->getCamera<FirstPersonCamera>(CameraType::FirstPerson);
        firstPerson->setPosition(thirdPerson->sceneNode()->origin());
        firstPerson->setFacing(thirdPerson->facing());
        _cameraType = CameraType::FirstPerson;
        break;
    }
    default:
        break;
    }

    setRelativeMouseMode(_cameraType == CameraType::FirstPerson);

    _module->area()->updateRoomVisibility();
}

Camera *Game::getActiveCamera() const {
    if (!_module) {
        return nullptr;
    }
    std::shared_ptr<Area> area(_module->area());
    if (!area) {
        return nullptr;
    }
    return area->getCamera(_cameraType);
}

std::shared_ptr<Object> Game::getObjectById(uint32_t id) const {
    switch (id) {
    case kObjectSelf:
        throw std::invalid_argument("Invalid id: " + std::to_string(id));
    case kObjectInvalid:
        return nullptr;
    default: {
        auto it = _objectById.find(id);
        return it != _objectById.end() ? it->second : nullptr;
    }
    }
}

bool Game::isRuntimeObjectLive(const Object &object) const {
    auto found = _objectById.find(object.id());
    return found != _objectById.end() && found->second.get() == &object;
}

void Game::unregisterRuntimeObject(const std::shared_ptr<Object> &object) {
    if (!object) {
        return;
    }
    auto registered = _objectById.find(object->id());
    if (registered != _objectById.end() &&
        registered->second.get() == object.get()) {
        _objectById.erase(registered);
    }

    // Include the structural Module alias, which deliberately has no reverse
    // serialized identity on the Module object itself.
    for (auto it = _objectBySavedId.begin(); it != _objectBySavedId.end();) {
        if (it->second.lock().get() == object.get()) {
            it = _objectBySavedId.erase(it);
        } else {
            ++it;
        }
    }
    _savedIdsByObject.erase(object.get());
}

void Game::destroyRuntimeObjectGraph(const std::shared_ptr<Object> &object) {
    if (!object) {
        return;
    }
    std::vector<std::shared_ptr<Object>> pending {object};
    std::set<const Object *> seen;
    std::vector<std::shared_ptr<Object>> graph;
    while (!pending.empty()) {
        auto current = std::move(pending.back());
        pending.pop_back();
        if (!current || !seen.insert(current.get()).second) {
            continue;
        }
        graph.push_back(current);
        for (auto &owned : current->ownedRuntimeObjects()) {
            pending.push_back(std::move(owned));
        }
    }
    // Children cease to exist before their owner. The pointer guard makes this
    // idempotent and protects a newer object if an explicit ID was reused.
    for (auto it = graph.rbegin(); it != graph.rend(); ++it) {
        unregisterRuntimeObject(*it);
    }
}

void Game::beginRuntimeObjectGraphReplacement(
    const std::vector<std::shared_ptr<Object>> &obsoleteObjects) {
    if (_stagedRuntimeObjectGraph) {
        throw ValidationException("Nested runtime object graph replacement");
    }
    _stagedRuntimeObjectGraph.emplace();
    _stagedRuntimeObjectGraph->initialNextObjectId = _nextObjectId;

    std::vector<std::shared_ptr<Object>> pending(obsoleteObjects);
    while (!pending.empty()) {
        auto object = std::move(pending.back());
        pending.pop_back();
        if (!object ||
            !_stagedRuntimeObjectGraph->replaceableObjects.insert(
                object.get()).second) {
            continue;
        }
        for (auto &owned : object->ownedRuntimeObjects()) {
            pending.push_back(std::move(owned));
        }
    }
}

void Game::commitRuntimeObjectGraphReplacement(
    const std::vector<std::shared_ptr<Object>> &obsoleteObjects) {
    if (!_stagedRuntimeObjectGraph) {
        throw ValidationException("No runtime object graph replacement is active");
    }

    auto staged = std::move(*_stagedRuntimeObjectGraph);
    _stagedRuntimeObjectGraph.reset();

    // Publication ownership has already changed through a no-throw swap. Old
    // objects and aliases now retire before candidate map nodes are merged.
    for (const auto &object : obsoleteObjects) {
        destroyRuntimeObjectGraph(object);
    }
    _objectById.merge(staged.objectById);
    _objectBySavedId.merge(staged.objectBySavedId);
    _savedIdsByObject.merge(staged.savedIdsByObject);
    for (uint32_t id : staged.reservedSavedObjectIdsToRelease) {
        _reservedSavedObjectIds.erase(id);
    }
}

void Game::abortRuntimeObjectGraphReplacement() {
    if (!_stagedRuntimeObjectGraph) {
        return;
    }
    _nextObjectId = _stagedRuntimeObjectGraph->initialNextObjectId;
    _stagedRuntimeObjectGraph.reset();
}

std::shared_ptr<Object> Game::getObjectBySavedId(uint32_t id) const {
    auto found = _objectBySavedId.find(id);
    return found == _objectBySavedId.end() ? nullptr : found->second.lock();
}

uint32_t Game::savedObjectId(const resource::Gff &gff) const {
    uint32_t id = 0;
    if (!gff.readDword(id, "ObjectId")) {
        throw ValidationException("Saved runtime object is missing ObjectId");
    }
    return id;
}

void Game::registerObject(
    const std::shared_ptr<Object> &object,
    bool allowReserved) {
    uint32_t id = object->id();
    if (id == std::numeric_limits<uint32_t>::max()) {
        throw ValidationException("Invalid saved ObjectId");
    }
    if (!allowReserved && id < kFirstRuntimeObjectId) {
        throw ValidationException("Reserved saved ObjectId: " + std::to_string(id));
    }
    if (_objectById.count(id) != 0) {
        throw ValidationException("Duplicate runtime ObjectId: " + std::to_string(id));
    }
    if (_stagedRuntimeObjectGraph) {
        if (!_stagedRuntimeObjectGraph->objectById.emplace(id, object).second) {
            throw ValidationException(
                "Duplicate staged runtime ObjectId: " + std::to_string(id));
        }
        _stagedRuntimeObjectGraph->reservedSavedObjectIdsToRelease.insert(id);
    } else {
        _objectById.emplace(id, object);
        _reservedSavedObjectIds.erase(id);
    }
}

void Game::registerSavedObjectIdentity(
    uint32_t id,
    const std::shared_ptr<Object> &object,
    const SerializedIdentityContext &identityContext) {
    if (!identityContext.hasAuthoritativeObjectIds()) {
        throw ValidationException(
            "Cannot register a non-authoritative saved object identity");
    }
    if (!_reservedSavedIdentityNamespace) {
        _reservedSavedIdentityNamespace = identityContext.identityNamespace;
    } else if (*_reservedSavedIdentityNamespace !=
               identityContext.identityNamespace) {
        throw ValidationException("Saved object identity namespace is not active");
    }
    if (id == std::numeric_limits<uint32_t>::max() || !object) {
        throw ValidationException("Invalid saved ObjectId mapping");
    }
    auto existing = getObjectBySavedId(id);
    if (existing && existing.get() != object.get() &&
        (!_stagedRuntimeObjectGraph ||
         !_stagedRuntimeObjectGraph->replaceableObjects.count(existing.get()))) {
        throw ValidationException(
            "Duplicate authoritative saved ObjectId mapping: " +
            std::to_string(id));
    }
    if (_stagedRuntimeObjectGraph) {
        auto staged = _stagedRuntimeObjectGraph->objectBySavedId.find(id);
        if (staged != _stagedRuntimeObjectGraph->objectBySavedId.end() &&
            staged->second.lock().get() != object.get()) {
            throw ValidationException(
                "Duplicate authoritative saved ObjectId mapping: " +
                std::to_string(id));
        }
    }
    const auto &reverseMap = _stagedRuntimeObjectGraph
                                 ? _stagedRuntimeObjectGraph->savedIdsByObject
                                 : _savedIdsByObject;
    auto reverse = reverseMap.find(object.get());
    if (reverse != reverseMap.end() && !reverse->second.count(id)) {
        auto creature = std::dynamic_pointer_cast<Creature>(object);
        if (!creature || !_party.rosterIdentity(*creature)) {
            throw ValidationException("Runtime object has multiple saved ObjectIds");
        }
    }
    auto &savedMap = _stagedRuntimeObjectGraph
                         ? _stagedRuntimeObjectGraph->objectBySavedId
                         : _objectBySavedId;
    savedMap[id] = object;
    auto &aliases = _stagedRuntimeObjectGraph
                        ? _stagedRuntimeObjectGraph->savedIdsByObject[object.get()]
                        : _savedIdsByObject[object.get()];
    aliases.insert(id);
    // The first authoritative view remains the object's serialization
    // provenance. Additional roster views are resolution aliases only.
    if (aliases.size() == 1) {
        object->assignSerializedObjectIdentity({identityContext, id});
    }
}

void Game::registerSavedModuleReferenceTarget(
    const std::shared_ptr<Module> &module,
    const SerializedIdentityContext &identityContext) {
    if (!identityContext.hasAuthoritativeObjectIds() || !module) {
        throw ValidationException("Invalid saved structural Module target");
    }
    if (!_reservedSavedIdentityNamespace) {
        _reservedSavedIdentityNamespace = identityContext.identityNamespace;
    } else if (*_reservedSavedIdentityNamespace !=
               identityContext.identityNamespace) {
        throw ValidationException("Saved object identity namespace is not active");
    }
    if (_reservedSavedObjectIds.count(kSavedRuntimeModuleObjectId) != 0) {
        throw ValidationException(
            "Saved ObjectId 0 collides with the structural Module target");
    }
    auto [found, inserted] = _objectBySavedId.emplace(
        kSavedRuntimeModuleObjectId, module);
    if (!inserted) {
        auto existing = found->second.lock();
        if (!existing || existing.get() != module.get()) {
            throw ValidationException(
                "Duplicate saved structural Module target");
        }
    }
}

std::shared_ptr<Item> Game::newItem(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    return newObjectFromGff<Item>(gff, identityContext, *this, _services);
}

std::shared_ptr<Item> Game::newOwnedItem(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    auto item = newItem();
    uint32_t id = 0;
    if (identityContext.hasAuthoritativeObjectIds() &&
        gff.readDword(id, "ObjectId")) {
        registerSavedObjectIdentity(id, item, identityContext);
    }
    return item;
}

std::shared_ptr<Area> Game::newSavedArea(
    uint32_t id,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    auto area = newArea(std::move(sceneName));
    registerSavedObjectIdentity(id, area, identityContext);
    return area;
}

std::shared_ptr<Creature> Game::newCreature(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Creature>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<Placeable> Game::newPlaceable(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Placeable>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<Door> Game::newDoor(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Door>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<Waypoint> Game::newWaypoint(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Waypoint>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<Trigger> Game::newTrigger(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Trigger>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<Sound> Game::newSound(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Sound>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<Encounter> Game::newEncounter(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Encounter>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<Store> Game::newStore(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Store>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

void Game::prepareSavedRuntimeNamespace(
    const resource::Gff &ifo,
    const SerializedIdentityContext &identityContext) {
    reserveSavedObjectIds(ifo, identityContext, SerializedGraphRoot::ModuleIfo);

    uint32_t nextObjectId = kFirstRuntimeObjectId;
    if (ifo.readDword(nextObjectId, "Mod_NextObjId0") &&
        nextObjectId != 0 &&
        nextObjectId < kFirstRuntimeObjectId) {
        throw ValidationException("Invalid Mod_NextObjId0");
    }
    _nextObjectId = std::max(nextObjectId, kFirstRuntimeObjectId);

    uint64_t nextEffectId = 0;
    if (ifo.readDword64(nextEffectId, "Mod_Effect_NxtId")) {
        if (!setNextEffectId(nextEffectId)) {
            throw ValidationException("Invalid Mod_Effect_NxtId");
        }
    }
    // Mod_MinPerHour first: it defines the day length that Mod_TimeOfDay is
    // measured against.
    uint32_t minutesPerHour = ifo.getUint("Mod_MinPerHour");
    if (minutesPerHour > std::numeric_limits<uint8_t>::max()) {
        throw ValidationException("Invalid Mod_MinPerHour");
    }
    _minutesPerHour = minutesPerHour == 0
                          ? 5
                          : static_cast<uint8_t>(minutesPerHour);

    // Compose the canonical clock from the retail day/time pair. An oversized
    // time of day carries into later days rather than being rejected, as
    // CWorldTimer::GetWorldTime does: saves written before the day length
    // became Mod_MinPerHour-derived hold a time of day on the old fixed
    // 24-hour scale, and must still load. Both fields are Dwords, so the
    // composition cannot overflow the 64-bit clock.
    uint64_t day = ifo.getUint("Mod_CalendarDay");
    uint64_t timeOfDay = ifo.getUint("Mod_TimeOfDay");
    _worldTimeMilliseconds = day * millisecondsPerWorldDay() + timeOfDay;
    _worldTimeFraction = 0.0;
}

void Game::reserveSavedObjectIds(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    SerializedGraphRoot graphRoot) {
    if (!identityContext.hasAuthoritativeObjectIds()) {
        return;
    }
    if (!_reservedSavedIdentityNamespace) {
        _reservedSavedIdentityNamespace = identityContext.identityNamespace;
    } else if (*_reservedSavedIdentityNamespace != identityContext.identityNamespace) {
        throw ValidationException("Cannot mix authoritative saved object namespaces");
    }
    for (auto &claim : collectSerializedObjectIdClaims(gff, identityContext, graphRoot)) {
        if (claim.id == std::numeric_limits<uint32_t>::max()) {
            throw ValidationException("Invalid saved ObjectId");
        }
        auto [found, inserted] = _reservedSavedObjectIdClaims.emplace(claim.id, claim.path);
        if (!inserted && found->second != claim.path) {
            throw ValidationException(
                "Duplicate authoritative saved ObjectId " +
                std::to_string(claim.id) + " at " + found->second +
                " and " + claim.path);
        }
        _reservedSavedObjectIds.insert(claim.id);
    }
}

void Game::resolveSavedObjectReferences() {
    for (const auto &[_, object] : _objectById) {
        const auto identityContext = object->_savedRuntimeIdentityContext;
        object->resolveSavedReferences(
            [this, identityContext](uint32_t id) {
                return resolveSerializedObjectReference(id, identityContext);
            });
    }
}

void Game::bindSavedRuntimeState() {
    if (!_module) {
        return;
    }
    resolveSavedObjectReferences();
    for (const auto &[_, object] : _objectById) {
        object->bindSavedRuntimeState();
    }
    _module->bindSavedEventQueue();
}

void Game::publishSavedRuntimeState() {
    if (!_module) {
        return;
    }
    for (const auto &[_, object] : _objectById) {
        object->publishSavedRuntimeState();
    }
    _module->publishSavedEventQueue();
}

void Game::advanceWorldTime(float dt) {
    if (dt <= 0.0f) {
        return;
    }
    // One second of simulation is one thousand world milliseconds.
    // Mod_MinPerHour shortens the day, it does not accelerate the clock:
    // CWorldTimer accumulates elapsed time into m_nSnapshotTime and only
    // derives the day boundary from m_nMillisecondsInDay. Scaling the rate
    // here instead made every duration measured in world time short by
    // 60 / Mod_MinPerHour.
    double gameMilliseconds =
        _worldTimeFraction + static_cast<double>(dt) * 1000.0;
    uint64_t wholeMilliseconds =
        static_cast<uint64_t>(std::floor(gameMilliseconds));
    _worldTimeFraction =
        gameMilliseconds - static_cast<double>(wholeMilliseconds);

    _worldTimeMilliseconds += wholeMilliseconds;
}

std::optional<float> Game::remainingEffectDuration(
    const EffectInstance &effect) const {
    if (effect.durationType() != DurationType::Temporary) {
        return std::nullopt;
    }
    if (effect.expiryDay == 0 && effect.expiryTime == 0) {
        return std::max(0.0f, effect.duration);
    }

    // Save-facing expiry provenance, converted once here at the restoration
    // boundary. Live effects then count down in world seconds and never
    // reconstruct a calendar day again.
    uint64_t expiry =
        static_cast<uint64_t>(effect.expiryDay) * millisecondsPerWorldDay() +
        effect.expiryTime;
    if (expiry <= _worldTimeMilliseconds) {
        return 0.0f;
    }
    double remainingSeconds =
        static_cast<double>(expiry - _worldTimeMilliseconds) / 1000.0;
    return static_cast<float>(remainingSeconds);
}

void Game::renderGUI() {
    _services.graphics.uniforms.setGlobals([this](auto &globals) {
        globals.reset();
        globals.projection = glm::ortho(
            0.0f,
            static_cast<float>(_options.graphics.width),
            static_cast<float>(_options.graphics.height),
            0.0f, 0.0f, 100.0f);
        globals.projectionInv = glm::inverse(globals.projection);
    });
    switch (_screen) {
    case Screen::InGame:
        if (_cameraType == CameraType::ThirdPerson) {
            renderHUD();
        }
        break;

    default: {
        auto gui = getScreenGUI();
        if (gui) {
            gui->render();
        }
        break;
    }
    }
    if (_confirmPopup && _confirmPopup->isVisible()) {
        _confirmPopup->render();
    }
    if (_cursor && !_relativeMouseMode) {
        const auto &graphics = _options.graphics;
        float cursorScale = std::min(graphics.width / 800.0f, graphics.height / 600.0f) *
                            graphics.guiScale * kCursorSizeScale;
        _cursor->render(cursorScale);
    }
    renderDeveloperOverlay();
}

void Game::renderDeveloperOverlay() {
    if (!_options.game.developer || !_developerOverlay.visible || !_module || _screen != Screen::InGame) {
        return;
    }
    if (!_developerFont) {
        _developerFont = _services.resource.fonts.get("fnt_console");
    }
    if (!_developerFont) {
        return;
    }

    auto camera = getActiveCamera();
    bool hasCamera = camera != nullptr;
    glm::mat4 projection(1.0f);
    glm::mat4 view(1.0f);
    if (camera) {
        projection = camera->cameraSceneNode()->camera()->projection();
        view = camera->cameraSceneNode()->camera()->view();
    }

    _services.graphics.uniforms.setGlobals([this](auto &globals) {
        globals.reset();
        globals.projection = glm::ortho(
            0.0f,
            static_cast<float>(_options.graphics.width),
            static_cast<float>(_options.graphics.height),
            0.0f, 0.0f, 100.0f);
        globals.projectionInv = glm::inverse(globals.projection);
    });
    _services.graphics.context.withBlendMode(BlendMode::Normal, [this]() {
        renderDeveloperBanner();
    });
    _services.graphics.context.withBlendMode(BlendMode::Normal, [this, hasCamera, &projection, &view]() {
        if (_developerOverlay.triggers && hasCamera) {
            renderDeveloperTriggerOverlay(projection, view);
        }
        if (_developerOverlay.actorLabels && hasCamera) {
            renderDeveloperActorLabels(projection, view);
        }
        if (_developerOverlay.watchedValues) {
            renderDeveloperWatchedValues();
        }
    });
}

void Game::renderDeveloperBanner() {
    std::vector<std::string> lines;
    lines.push_back("DEV OBSERVABILITY");
    lines.push_back(str(boost::format("%s overlay | %s triggers") %
                        kDeveloperOverlayToggleHelp %
                        kDeveloperTriggerToggleHelp));
    lines.push_back(str(boost::format("%s labels (%s) | %s verbose") %
                        kDeveloperActorToggleHelp %
                        (_developerOverlay.longActorLabels ? "long" : "short") %
                        kDeveloperActorLongToggleHelp));
    lines.push_back(str(boost::format("%s watch | ` console | F5 profiler") %
                        kDeveloperWatchToggleHelp));
    lines.push_back("V camera | +/- speed");

    float maxWidth = 0.0f;
    for (const auto &line : lines) {
        maxWidth = glm::max(maxWidth, _developerFont->measure(line));
    }
    renderDeveloperPanel(
        lines,
        glm::vec2(0.5f * (static_cast<float>(_options.graphics.width) - maxWidth - 14.0f), 12.0f),
        glm::vec3(0.58f, 1.0f, 0.58f));
}

void Game::renderDeveloperTriggerOverlay(const glm::mat4 &projection, const glm::mat4 &view) {
    static glm::vec4 viewport(0.0f, 0.0f, 1.0f, 1.0f);
    auto area = _module ? _module->area() : nullptr;
    if (!area) {
        return;
    }

    const auto &opts = _options.graphics;
    for (const auto &object : area->getObjectsByType(ObjectType::Trigger)) {
        auto trigger = dyn_cast<Trigger>(object);
        if (!trigger) {
            continue;
        }
        const auto &geometry = trigger->geometry();
        if (geometry.empty()) {
            continue;
        }

        glm::vec3 centroid(0.0f);
        for (const auto &localPoint : geometry) {
            centroid += trigger->position() + localPoint;
        }

        // Trigger geometry now renders through the main scene pipeline; the overlay only adds labels.
        auto state = trigger->debugState();
        glm::vec4 color = trigger->debugColor();
        centroid /= static_cast<float>(geometry.size());
        glm::vec3 labelScreen = glm::project(centroid, view, projection, viewport);
        if (labelScreen.z >= 0.0f && labelScreen.z < 1.0f) {
            std::string label = str(boost::format("#%u %s") %
                                    trigger->id() %
                                    trigger->tag());
            if (!trigger->blueprintResRef().empty()) {
                label += " " + trigger->blueprintResRef();
            }
            label += str(boost::format(" [%s]") % triggerDebugStateName(state));
            glm::vec3 position(opts.width * labelScreen.x, opts.height * (1.0f - labelScreen.y), 0.0f);
            renderDeveloperText(label, position, glm::vec3(color), TextGravity::CenterBottom);
        }
    }
}

void Game::renderDeveloperActorLabels(const glm::mat4 &projection, const glm::mat4 &view) {
    auto area = _module ? _module->area() : nullptr;
    auto leader = _party.getLeader();
    if (!area || !leader) {
        return;
    }

    const auto &opts = _options.graphics;
    int rendered = 0;
    for (const auto &object : area->objects()) {
        bool supported = object->type() == ObjectType::Creature ||
                         object->type() == ObjectType::Door ||
                         object->type() == ObjectType::Placeable;
        bool inspected = object == area->hilightedObject() || object == area->selectedObject();
        if (!supported && !inspected) {
            continue;
        }

        float distance = object->getDistanceTo(*leader);
        if (!inspected && distance > kDeveloperActorLabelDistance) {
            continue;
        }

        glm::vec3 screen = area->getSelectableScreenCoords(object, projection, view);
        if (screen.z >= 1.0f) {
            continue;
        }

        int faction = getDebugFaction(object);
        bool hostile = false;
        auto creature = dyn_cast<Creature>(object);
        if (creature) {
            hostile = !creature->isDead() && _services.game.reputes.getIsEnemy(*leader, *creature);
        }

        glm::vec3 color = inspected ? glm::vec3(1.0f, 1.0f, 1.0f) : (hostile ? glm::vec3(1.0f, 0.42f, 0.36f) : glm::vec3(0.68f, 0.92f, 1.0f));
        std::string label;
        if (_developerOverlay.longActorLabels) {
            label = str(boost::format("#%u %s %s f=%d H=%d sel=%d cmd=%d vis=%d plot=%d") %
                        object->id() %
                        object->tag() %
                        object->blueprintResRef() %
                        faction %
                        static_cast<int>(hostile) %
                        static_cast<int>(object->isSelectable()) %
                        static_cast<int>(object->isCommandable()) %
                        static_cast<int>(object->visible()) %
                        static_cast<int>(object->plotFlag()));
        } else {
            label = str(boost::format("#%u %s") %
                        object->id() %
                        object->tag());
            if (!object->blueprintResRef().empty()) {
                label += " " + object->blueprintResRef();
            }
            if (hostile) {
                label += " [enemy]";
            }
            if (inspected) {
                label += str(boost::format(" [%s") % objectTypeName(object->type()));
                if (faction >= 0) {
                    label += str(boost::format(" f=%d") % faction);
                }
                label += "]";
            }
        }

        glm::vec3 position(opts.width * screen.x, opts.height * (1.0f - screen.y) - 18.0f - (rendered % 2) * 10.0f, 0.0f);
        renderDeveloperText(label, position, color, TextGravity::CenterBottom);
        if (++rendered >= 16) {
            break;
        }
    }
}

void Game::renderDeveloperWatchedValues() {
    auto area = _module ? _module->area() : nullptr;
    auto leader = _party.getLeader();
    auto selected = area ? area->selectedObject() : nullptr;
    auto hover = area ? area->hilightedObject() : nullptr;
    std::string room = leader && leader->room() ? leader->room()->name() : "-";
    glm::vec3 position = leader ? leader->position() : glm::vec3(0.0f);

    std::vector<std::string> lines;
    lines.push_back(str(boost::format("Watch (%s)") % kDeveloperWatchToggleHelp));
    lines.push_back(str(boost::format("screen=%s module=%s area=%s camera=%s") %
                        screenName(_screen) %
                        (_module ? _module->name() : "-") %
                        (area ? area->localizedName() : "-") %
                        cameraTypeName(_cameraType)));
    lines.push_back(str(boost::format("speed=%.1fx paused=%d relativeMouse=%d room=%s") %
                        _gameSpeed %
                        static_cast<int>(_paused) %
                        static_cast<int>(_relativeMouseMode) %
                        room));
    lines.push_back(str(boost::format("leader=#%u %s hp=%d/%d pos=%.2f,%.2f,%.2f") %
                        (leader ? leader->id() : 0) %
                        (leader ? leader->tag() : "-") %
                        (leader ? leader->currentHitPoints() : -1) %
                        (leader ? leader->maxHitPoints() : -1) %
                        position.x %
                        position.y %
                        position.z));
    lines.push_back(str(boost::format("selected=#%u %s/%s type=%s hp=%d/%d") %
                        (selected ? selected->id() : 0) %
                        (selected ? selected->tag() : "-") %
                        (selected ? selected->blueprintResRef() : "-") %
                        (selected ? objectTypeName(selected->type()) : "-") %
                        (selected ? selected->currentHitPoints() : -1) %
                        (selected ? selected->maxHitPoints() : -1)));
    lines.push_back(str(boost::format("hover=#%u %s/%s type=%s hp=%d/%d") %
                        (hover ? hover->id() : 0) %
                        (hover ? hover->tag() : "-") %
                        (hover ? hover->blueprintResRef() : "-") %
                        (hover ? objectTypeName(hover->type()) : "-") %
                        (hover ? hover->currentHitPoints() : -1) %
                        (hover ? hover->maxHitPoints() : -1)));

    float maxWidth = 0.0f;
    for (const auto &line : lines) {
        maxWidth = glm::max(maxWidth, _developerFont->measure(line));
    }
    float panelWidth = maxWidth + 14.0f;
    float panelHeight = (_developerFont->height() + 2.0f) * static_cast<float>(lines.size()) + 10.0f;
    renderDeveloperPanel(
        lines,
        glm::vec2(static_cast<float>(_options.graphics.width) - panelWidth - 4.0f, 16.0f + panelHeight),
        glm::vec3(0.92f));
}

void Game::renderDeveloperText(const std::string &text, const glm::vec3 &position, const glm::vec3 &color, TextGravity gravity) {
    if (!_developerFont) {
        return;
    }
    _developerFont->render(text, position + glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(0.0f), gravity);
    _developerFont->render(text, position, color, gravity);
}

void Game::renderDeveloperPanel(const std::vector<std::string> &lines, glm::vec2 position, glm::vec3 color) {
    if (!_developerFont || lines.empty()) {
        return;
    }

    float maxWidth = 0.0f;
    for (const auto &line : lines) {
        maxWidth = glm::max(maxWidth, _developerFont->measure(line));
    }
    float lineHeight = _developerFont->height() + 2.0f;
    glm::vec2 size(maxWidth + 14.0f, lineHeight * lines.size() + 10.0f);
    position.x = glm::clamp(position.x, 4.0f, static_cast<float>(_options.graphics.width) - size.x - 4.0f);
    position.y = glm::clamp(position.y, 4.0f, static_cast<float>(_options.graphics.height) - size.y - 4.0f);

    renderDeveloperRect(position, size, glm::vec4(0.0f, 0.0f, 0.0f, 0.58f));
    glm::vec3 textPosition(position.x + 7.0f, position.y + 5.0f, 0.0f);
    for (const auto &line : lines) {
        renderDeveloperText(line, textPosition, color, TextGravity::RightBottom);
        textPosition.y += lineHeight;
    }
}

void Game::renderDeveloperRect(glm::vec2 position, glm::vec2 size, glm::vec4 color) {
    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, glm::vec3(position.x, position.y, 0.0f));
    transform = glm::scale(transform, glm::vec3(size.x, size.y, 1.0f));

    _services.graphics.uniforms.setLocals([transform, color](auto &locals) {
        locals.reset();
        locals.model = transform;
        locals.color = color;
    });
    _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::mvpColor));
    _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);
}

void Game::updateMusic() {
    if (_musicResRef.empty()) {
        return;
    }
    if (_music && _music->isPlaying()) {
        return;
    }
    auto clip = _services.resource.audioClips.get(_musicResRef);
    _music = _services.audio.mixer.play(std::move(clip), AudioType::Music);
}

void Game::loadNextModule() {
    std::string target(_nextModule);

    // Capture the origin (current module + leader location) before the deferred
    // transition runs, so a swoop module entered from a script can return here.
    std::string originModule;
    glm::vec3 originPosition(0.0f);
    float originFacing = 0.0f;
    bool haveOrigin = false;
    if (_module) {
        originModule = _module->name();
        if (auto leader = _party.getLeader()) {
            originPosition = leader->position();
            originFacing = leader->getFacing();
            haveOrigin = true;
        }
    }
    bool wasLifecycleActive = _swoopLifecycle.active || _turretLifecycle.active;

    bool loaded = loadModule(_nextModule, _nextEntry);

    _nextModule.clear();
    _nextEntry.clear();
    if (!loaded) {
        return;
    }

    // Vanilla K1 minigame entry: a dialogue node or cutscene script calls
    // StartNewModule("<*mg>"); the engine auto-enters the minigame on load (no
    // dedicated start routine). Route that generic transition through the
    // forced-success lifecycle harness when the loaded area declares one.
    // A session scheduled by startturretgame carries its own origin, captured
    // before the transition was queued.
    bool pendingTurret = _pendingTurret.active && boost::iequals(_pendingTurret.targetModule, target);
    if (pendingTurret) {
        originModule = _pendingTurret.originModule;
        originPosition = _pendingTurret.originPosition;
        originFacing = _pendingTurret.originFacing;
        haveOrigin = _pendingTurret.haveOrigin;
    }

    if (wasLifecycleActive || _swoopLifecycle.active || _turretLifecycle.active) {
        return;
    }
    if (originModule.empty() || boost::iequals(originModule, target)) {
        if (pendingTurret) {
            abandonPendingTurret("no origin module");
        }
        return;
    }
    auto mod = _module;
    auto area = mod ? mod->area() : nullptr;
    bool hasMinigame = area && area->hasMinigame();
    MinigameType minigameType = hasMinigame ? area->miniGame().type : MinigameType::None;

    auto resolution = resolveTurretRequest(pendingTurret, hasMinigame, minigameType);
    if (resolution == TurretRequestResolution::AbortNoMinigame ||
        resolution == TurretRequestResolution::AbortWrongType) {
        abandonPendingTurret(turretRequestResolutionMessage(resolution));
        return;
    }

    if (!hasMinigame) {
        return;
    }
    if (minigameType == MinigameType::Turret) {
        openTurret();
        if (_turret.isActive()) {
            _turretLifecycle = MinigameLifecycle();
            _turretLifecycle.active = true;
            _turretLifecycle.haveOrigin = haveOrigin;
            _turretLifecycle.originModule = originModule;
            _turretLifecycle.originPosition = originPosition;
            _turretLifecycle.originFacing = originFacing;
            _turretLifecycle.forcedSuccess = true;
            _pendingTurret = PendingTurretRequest();
            debug(str(boost::format("turret: lifecycle start origin=%s target=%s hook=%s")
                      % originModule % target
                      % (pendingTurret ? "startturretgame" : "StartNewModule")));
        } else if (pendingTurret) {
            abandonPendingTurret("turret failed to start");
        }
        return;
    }
    if (minigameType != MinigameType::SwoopRace) {
        return;
    }

    openSwoopRace();
    if (_swoopRace.isActive()) {
        _swoopLifecycle = MinigameLifecycle();
        _swoopLifecycle.active = true;
        _swoopLifecycle.haveOrigin = haveOrigin;
        _swoopLifecycle.originModule = originModule;
        _swoopLifecycle.originPosition = originPosition;
        _swoopLifecycle.originFacing = originFacing;
        _swoopLifecycle.forcedSuccess = true;
        debug(str(boost::format("swoop: script lifecycle start origin=%s target=%s forcedSuccess=yes hook=StartNewModule")
                  % originModule % target));
    }
}

void Game::stopMovement() {
    // Reached with no module while one is being swapped in: loadGame resets the
    // game before the destination module is up, and the menus that call this
    // outlive that reset. There is no player to halt and no in-game camera for
    // getActiveCamera to find, so there is nothing to stop.
    if (!_module) {
        return;
    }

    auto camera = getActiveCamera();
    if (camera) {
        camera->stopMovement();
    }
    _module->player().stopMovement();
}

void Game::scheduleModuleTransition(const std::string &moduleName, const std::string &entry) {
    _nextModule = moduleName;
    _nextEntry = entry;
    _moduleTransitionMovies = std::queue<std::string>();
}

void Game::scheduleModuleTransitionWithMovies(const std::string &moduleName, const std::string &entry, std::vector<std::string> movies) {
    _nextModule = moduleName;
    _nextEntry = entry;
    _moduleTransitionMovies = std::queue<std::string>();
    for (auto &movie : movies) {
        _moduleTransitionMovies.push(std::move(movie));
    }

    if (!_movie) {
        playNextModuleTransitionMovie();
    }
}

bool Game::startVideo(const std::string &name) {
    _services.audio.mixer.stopAll();
    _music.reset();

    _movie = _services.resource.movies.get(name);
    if (!_movie) {
        return false;
    }

    return true;
}

bool Game::playNextModuleTransitionMovie() {
    while (!_moduleTransitionMovies.empty()) {
        auto name = std::move(_moduleTransitionMovies.front());
        _moduleTransitionMovies.pop();

        if (startVideo(name)) {
            return true;
        }
    }
    return false;
}

void Game::updateMovie(float dt) {
    _movie->update(dt);

    if (_movie->isFinished()) {
        _movie.reset();
        playNextModuleTransitionMovie();
    }
}

void Game::updateCamera(float dt) {
    switch (_screen) {
    case Screen::Conversation: {
        int cameraId;
        CameraType cameraType = getConversationCamera(cameraId);
        if (cameraType == CameraType::Static) {
            _module->area()->setStaticCamera(cameraId);
        }
        _cameraType = cameraType;
        break;
    }
    case Screen::InGame:
        if (_cameraType != CameraType::FirstPerson && _cameraType != CameraType::ThirdPerson) {
            _cameraType = CameraType::ThirdPerson;
        }
        break;
    default:
        break;
    }
    Camera *camera = getActiveCamera();
    if (camera) {
        camera->update(dt);

        glm::vec3 listenerPosition;
        if (_cameraType == CameraType::ThirdPerson) {
            std::shared_ptr<Creature> partyLeader(_party.getLeader());
            if (partyLeader) {
                listenerPosition = partyLeader->position() + glm::vec3 {0.0f, 0.0f, 1.7f}; // TODO: height based on appearance
            }
        } else {
            listenerPosition = camera->sceneNode()->origin();
        }
        _services.audio.context.setListenerPosition(std::move(listenerPosition));
    }
}

void Game::updateSceneGraph(float dt) {
    auto camera = getActiveCamera();
    if (!camera) {
        return;
    }
    auto &sceneGraph = _services.scene.graphs.get(kSceneMain);
    sceneGraph.setActiveCamera(camera->cameraSceneNode().get());
    sceneGraph.setUpdateRoots(!_paused);
    sceneGraph.setRenderAABB(isShowAABBEnabled());
    sceneGraph.setRenderWalkmeshes(isShowWalkmeshEnabled());
    bool renderDeveloperTriggers = _options.game.developer &&
                                   _screen == Screen::InGame &&
                                   _developerOverlay.visible &&
                                   _developerOverlay.triggers;
    sceneGraph.setRenderTriggers(isShowTriggersEnabled() || renderDeveloperTriggers);
    sceneGraph.update(dt);
}

bool Game::getGlobalBoolean(const std::string &name) const {
    auto it = _globalBooleans.find(name);
    return it != _globalBooleans.end() ? it->second : false;
}

int Game::getGlobalNumber(const std::string &name) const {
    auto it = _globalNumbers.find(name);
    return it != _globalNumbers.end() ? it->second : 0;
}

std::string Game::getGlobalString(const std::string &name) const {
    auto it = _globalStrings.find(name);
    return it != _globalStrings.end() ? it->second : "";
}

std::shared_ptr<Location> Game::getGlobalLocation(const std::string &name) const {
    auto it = _globalLocations.find(name);
    return it != _globalLocations.end() ? it->second : nullptr;
}

void Game::setCustomToken(int token, std::string value) {
    _customTokens[token] = std::move(value);
}

static std::string substituteCustomTokensFromMap(
    std::string str,
    const std::map<int, std::string> &customTokens) {

    size_t start = 0;
    while ((start = str.find("<CUSTOM", start)) != std::string::npos) {
        size_t digitsStart = start + 7;
        size_t digitsEnd = digitsStart;
        while (digitsEnd < str.size() && std::isdigit(static_cast<unsigned char>(str[digitsEnd]))) {
            ++digitsEnd;
        }
        if (digitsEnd == digitsStart || digitsEnd >= str.size() || str[digitsEnd] != '>') {
            start = digitsStart;
            continue;
        }
        int token = 0;
        try {
            token = std::stoi(str.substr(digitsStart, digitsEnd - digitsStart));
        } catch (const std::exception &) {
            start = digitsEnd + 1;
            continue;
        }
        auto it = customTokens.find(token);
        if (it == customTokens.end()) {
            start = digitsEnd + 1;
            continue;
        }
        str.replace(start, digitsEnd - start + 1, it->second);
        start += it->second.size();
    }
    return str;
}

std::string Game::substituteCustomTokens(std::string str) const {
    return substituteCustomTokensFromMap(std::move(str), _customTokens);
}

std::string Game::substituteCustomToken(std::string str, int token, std::string value) const {
    auto customTokens = _customTokens;
    customTokens[token] = std::move(value);
    return substituteCustomTokensFromMap(std::move(str), customTokens);
}

void Game::setGlobalBoolean(const std::string &name, bool value) {
    _globalBooleans[name] = value;
}

void Game::setGlobalNumber(const std::string &name, int value) {
    // Retail SetGlobalNumber stores the low byte in its signed-char table.
    // Express that conversion portably instead of relying on plain-char
    // signedness or an implementation-defined narrowing conversion.
    uint8_t raw = static_cast<uint8_t>(value);
    _globalNumbers[name] = raw <= 0x7f ? static_cast<int>(raw)
                                       : static_cast<int>(raw) - 0x100;
}

std::vector<SavedGame> Game::savedGames() const {
    return discoverSavedGames(_path);
}

void Game::setGlobalString(const std::string &name, const std::string &value) {
    _globalStrings[name] = value;
}

void Game::setGlobalLocation(const std::string &name, const std::shared_ptr<Location> &location) {
    _globalLocations[name] = location;
}

void Game::setPaused(bool paused) {
    _paused = paused;
}

void Game::setRelativeMouseMode(bool relative) {
    _relativeMouseMode = relative;
}

void Game::withLoadingScreen(const std::string &imageResRef, const std::function<void()> &block) {
    if (!_loadScreen) {
        _loadScreen = tryLoadGUI<LoadingScreen>();
    }
    if (_loadScreen) {
        _loadScreen->setImage(imageResRef);
        _loadScreen->setProgress(0);
    }
    changeScreen(Screen::Loading);
    render();
    block();
}

/**
 * Terminal destination for a load that failed after the commit boundary.
 *
 * openMainMenu retires whatever was half-built and drops the candidate mounts
 * with it, so nothing of either session survives. It gives up early when the
 * menu GUI cannot be loaded, which would leave the screen wherever the
 * abandoned session left it; record the intended destination regardless, so a
 * missing menu resource is its own visible failure rather than an engine that
 * renders nothing while still running.
 */
void Game::retireToMainMenu() {
    openMainMenu();
    if (_screen == Screen::None) {
        _screen = Screen::MainMenu;
    }
}

void Game::openMainMenu() {
    resetGame();
    if (!_mainMenu) {
        _mainMenu = tryLoadGUI<MainMenu>();
    }
    if (!_mainMenu) {
        return;
    }
    if (!_saveLoad) {
        _saveLoad = tryLoadGUI<SaveLoad>();
    }
    playMusic(_mainMenu->musicResRef());
    changeScreen(Screen::MainMenu);
}

void Game::openInGame() {
    _runtimeSessionPlayable = static_cast<bool>(_module);
    changeScreen(Screen::InGame);
}

namespace {

// Result of trying to anchor the race to the authored player track.
struct SwoopTrackFrame {
    glm::vec3 position {0.0f};
    float facing {0.0f};
    std::string mode {"fallback"}; // "lyt-track", "track-model", or "fallback"
    std::string reason;            // why fallback was used
    std::string info;              // concise track inspection details
};

// Max 2D distance (world units) the track's start hook may be from the party
// leader to be trusted WITHOUT an LYT placement. With an LYT track placement the
// hook is in module/world space, so this guard does not apply; without one a
// standalone-loaded track model may sit in a different frame and would otherwise
// teleport the bike into the void, so the proven leader anchor is kept.
constexpr float kTrackFrameMaxDistance = 64.0f;

// PR1 non-blocking finish threshold (forward-progress units). The race finishes
// a margin past the furthest mapped obstacle, or at a conservative fallback
// distance when no obstacle placements exist.
constexpr float kSwoopFinishMargin = 500.0f;
constexpr float kSwoopFallbackFinishProgress = 4000.0f;

// Derive the bike start frame from the player track model's "modelhook" node
// (vanilla parents the player to this node). When an LYT track placement is
// supplied, the hook is placed into module/world space and used unconditionally;
// otherwise it is trusted only if it already resolves near the party leader,
// falling back to the leader frame.
SwoopTrackFrame deriveSwoopTrackFrame(const std::shared_ptr<graphics::Model> &trackModel,
                                      const std::string &trackResRef,
                                      const glm::vec3 *lytTrackPos,
                                      const glm::vec3 &leaderPos,
                                      float leaderFacing) {
    SwoopTrackFrame frame;
    frame.position = leaderPos;
    frame.facing = leaderFacing;

    if (trackResRef.empty()) {
        frame.reason = "no-track-ref";
        return frame;
    }
    if (!trackModel) {
        frame.reason = "track-model-missing";
        return frame;
    }

    size_t animCount = trackModel->getAnimationNames().size();
    auto hook = trackModel->getNodeByNameRecursive("modelhook");
    if (!hook) {
        frame.reason = "no-modelhook";
        frame.info = str(boost::format("modelhook=no placement=%s anims=%zu")
                         % (lytTrackPos ? "yes" : "no") % animCount);
        return frame;
    }

    const glm::mat4 &abs = hook->absoluteTransform();
    glm::vec3 hookLocal(abs[3]);
    // Engine facing convention: forward = (-sin f, cos f). The LYT track
    // placement carries no rotation, so the hook's model-space orientation is
    // also its world orientation.
    glm::vec3 forward = glm::normalize(glm::vec3(glm::mat3(abs) * glm::vec3(0.0f, 1.0f, 0.0f)));
    float facing = glm::atan(-forward.x, forward.y);

    if (lytTrackPos) {
        // Combine the LYT placement (translation only) with the modelhook's
        // model-local transform to get the hook in module/world space.
        glm::vec3 start = *lytTrackPos + hookLocal;
        frame.position = start;
        frame.facing = facing;
        frame.mode = "lyt-track";
        frame.info = str(boost::format("modelhook=yes placement=yes anims=%zu lyt=[%.1f,%.1f,%.1f] hook=[%.1f,%.1f,%.1f] start=[%.1f,%.1f,%.1f]")
                         % animCount
                         % lytTrackPos->x % lytTrackPos->y % lytTrackPos->z
                         % hookLocal.x % hookLocal.y % hookLocal.z
                         % start.x % start.y % start.z);
        return frame;
    }

    float dist = glm::distance(glm::vec2(hookLocal), glm::vec2(leaderPos));
    frame.info = str(boost::format("modelhook=yes placement=no anims=%zu hook=[%.1f,%.1f,%.1f] dist=%.1f")
                     % animCount % hookLocal.x % hookLocal.y % hookLocal.z % dist);

    if (dist > kTrackFrameMaxDistance) {
        // No LYT placement and the hook is not in the party's world frame.
        frame.reason = "no-lyt-track-placement";
        return frame;
    }

    frame.position = hookLocal;
    frame.facing = facing;
    frame.mode = "track-model";
    return frame;
}

} // namespace

void Game::openSwoopRace() {
    if (_swoopRace.isActive()) {
        _console.printLine("swoop: already running");
        return;
    }
    if (!_module || !_module->area()) {
        _console.printLine("swoop: no module loaded");
        return;
    }
    auto area = _module->area();
    if (!area->hasMinigame() || area->miniGame().type != MinigameType::SwoopRace) {
        _console.printLine("swoop: current area has no swoop minigame");
        return;
    }
    auto leader = _party.getLeader();
    if (!leader) {
        _console.printLine("swoop: no party leader to anchor the race");
        return;
    }

    const auto &mg = area->miniGame();
    auto camera = area->getCamera<FirstPersonCamera>(CameraType::FirstPerson);
    if (camera) {
        camera->stopMovement();
    }

    // Load the whole player model set, not just the first entry. Vanilla loads
    // every Player.Models entry and hides the one that is the camera mount
    // (player.cameraResRef). We skip that mount so areas whose visible bike is
    // in a later entry (e.g. Tatooine) still show a body.
    auto &sceneGraph = _services.scene.graphs.get(kSceneMain);
    std::shared_ptr<ModelSceneNode> bikeRoot;
    std::vector<std::shared_ptr<ModelSceneNode>> bikeChildNodes;
    std::vector<std::string> modelDiag;
    bool anyMissing = false;
    for (const auto &modelSpec : mg.player.models) {
        const auto &resRef = modelSpec.resRef;
        if (resRef.empty()) {
            continue;
        }
        if (!mg.player.cameraResRef.empty() && boost::iequals(resRef, mg.player.cameraResRef)) {
            modelDiag.push_back(resRef + " camera-skip");
            continue;
        }
        auto model = _services.resource.models.get(resRef);
        if (!model) {
            modelDiag.push_back(resRef + " missing");
            anyMissing = true;
            continue;
        }
        auto node = sceneGraph.newModel(*model, ModelUsage::Placeable);
        node->setDrawDistance(_options.graphics.drawDistance);
        if (!bikeRoot) {
            bikeRoot = std::move(node);
        } else {
            bikeRoot->addChild(*node);
            bikeChildNodes.push_back(std::move(node));
        }
        modelDiag.push_back(resRef + " loaded");
    }
    size_t loadedCount = bikeRoot ? 1 + bikeChildNodes.size() : 0;
    if (!bikeRoot) {
        _console.printLine("swoop: no visible bike models loaded");
        for (size_t i = 0; i < modelDiag.size(); ++i) {
            debug(str(boost::format("  model[%zu]=%s") % i % modelDiag[i]));
        }
        return;
    }
    sceneGraph.addRoot(bikeRoot);

    // Anchor the race to the authored player track. Prefer the LYT track
    // placement (module/world space); otherwise fall back as before.
    std::shared_ptr<graphics::Model> trackModel;
    if (!mg.player.trackResRef.empty()) {
        trackModel = _services.resource.models.get(mg.player.trackResRef);
    }
    auto layout = _services.resource.layouts.get(area->name());
    glm::vec3 lytTrackPos(0.0f);
    bool haveLytTrackPos = false;
    if (layout && !mg.player.trackResRef.empty()) {
        if (auto placement = layout->findTrackByName(mg.player.trackResRef)) {
            lytTrackPos = placement->get().position;
            haveLytTrackPos = true;
        }
    }
    SwoopTrackFrame trackFrame = deriveSwoopTrackFrame(
        trackModel, mg.player.trackResRef, haveLytTrackPos ? &lytTrackPos : nullptr,
        leader->position(), leader->getFacing());

    // Choose a non-blocking finish threshold (PR1). Vanilla loop/finish is
    // script-driven and not yet implemented, so use the furthest mapped LYT
    // obstacle (the obstacle field spans the playable track) plus a margin, or
    // a conservative fallback distance when no obstacle placements are present.
    glm::vec3 frameForward(-glm::sin(trackFrame.facing), glm::cos(trackFrame.facing), 0.0f);
    float maxObstacleProgress = 0.0f;
    if (layout) {
        for (const auto &obs : layout->obstacles) {
            float p = glm::dot(obs.position - trackFrame.position, frameForward);
            maxObstacleProgress = glm::max(maxObstacleProgress, p);
        }
    }
    float finishProgress = maxObstacleProgress > 0.0f
                               ? maxObstacleProgress + kSwoopFinishMargin
                               : kSwoopFallbackFinishProgress;

    _savedCameraType = _cameraType;
    _swoopRace.start(mg, camera, std::move(bikeRoot), std::move(bikeChildNodes), trackFrame.position, trackFrame.facing, finishProgress);

    _cameraType = CameraType::FirstPerson;
    setRelativeMouseMode(false);
    changeScreen(Screen::SwoopRace);

    // The minigame is taking ownership of the party now, so discard any actions
    // the party queued before the race. In particular the swoop entry dialogue
    // queues a pre-race walk-off (e.g. a MoveToObject to a "flee" waypoint) on
    // the player; left in place it survives the module transitions and, on
    // return, sits in front of the post-race actions the result scripts queue,
    // blocking them. This is scoped to swoop/minigame entry: ordinary module
    // loads never reach openSwoopRace, so other scripted transitions (e.g. the
    // Endar Spire Trask/Bandon cutscene) keep their queued party actions.
    for (auto &member : _party.members()) {
        if (member.creature) {
            member.creature->clearAllActions(/*force=*/true);
        }
    }

    // Hide the normal party while the minigame runs. Vanilla does not add the
    // party to the scene in a minigame module (the swoop bike actor represents
    // the player); otherwise the frozen-but-rendered party leader appears on the
    // track. Restored on exit (or naturally re-spawned on the return module).
    setPartyVisible(false);

    debug(str(boost::format("swoop: started type=%s track=%s models=%zu loaded=%zu camera=chase movePerSec=%.0f lataccel=%.0f camfov=%.0f")
              % minigameTypeName(mg.type)
              % mg.player.trackResRef
              % mg.player.models.size()
              % loadedCount
              % mg.movementPerSec
              % mg.lateralAccel
              % mg.cameraViewAngle));

    // Track frame: lyt-track/track-model/fallback mode and how the start frame
    // was chosen (see deriveSwoopTrackFrame).
    std::string trackLabel(mg.player.trackResRef.empty() ? std::string("<none>") : mg.player.trackResRef);
    if (trackFrame.mode == "fallback") {
        debug(str(boost::format("swoop: track=%s mode=fallback reason=%s%s")
                  % trackLabel
                  % trackFrame.reason
                  % (trackFrame.info.empty() ? std::string() : (" " + trackFrame.info))));
    } else {
        debug(str(boost::format("swoop: track=%s mode=%s %s startFacing=%.2f")
                  % trackLabel
                  % trackFrame.mode
                  % trackFrame.info
                  % trackFrame.facing));
    }

    // Movement model: track-relative progress + lateral strafe (no turning).
    debug(str(boost::format("swoop: movement=track-progress strafeOnly=yes progressAxis=trackForward lateralAxis=trackRight anim=deferred start=[%.1f,%.1f,%.1f] facing=%.2f finish=%.1f")
              % trackFrame.position.x % trackFrame.position.y % trackFrame.position.z
              % trackFrame.facing
              % finishProgress));

    // Lateral bounds chosen for the strafe (see SwoopRace::computeLateralBounds).
    debug(str(boost::format("swoop: bounds lateral=[-%.1f,+%.1f] source=%s tunnelX=[%.1f,%.1f]")
              % _swoopRace.lateralLeftBound()
              % _swoopRace.lateralRightBound()
              % _swoopRace.lateralBoundSource()
              % mg.player.tunnelXNeg
              % mg.player.tunnelXPos));

    // Map authored LYT obstacle placements into the current track frame
    // (progress = down-course distance, lateral = strafe offset). Diagnostic
    // only: no damage/collision is applied in this slice. The "match" count is
    // how many .are MiniGame obstacles have a same-name LYT placement.
    if (layout) {
        glm::vec3 fwd(-glm::sin(trackFrame.facing), glm::cos(trackFrame.facing), 0.0f);
        glm::vec3 right(glm::cos(trackFrame.facing), glm::sin(trackFrame.facing), 0.0f);
        size_t areMatched = 0;
        for (const auto &obs : mg.obstacles) {
            if (layout->findObstacleByName(obs.name)) {
                ++areMatched;
            }
        }
        debug(str(boost::format("swoop: lyt obstacles=%zu areObstacles=%zu matched=%zu")
                  % layout->obstacles.size() % mg.obstacles.size() % areMatched));
        constexpr size_t kMaxObstacleDiag = 6;
        for (size_t i = 0; i < layout->obstacles.size() && i < kMaxObstacleDiag; ++i) {
            const auto &obs = layout->obstacles[i];
            glm::vec3 d = obs.position - trackFrame.position;
            float progress = glm::dot(d, fwd);
            float lateral = glm::dot(d, right);
            debug(str(boost::format("  swoopobj[%zu] name=%s pos=[%.1f,%.1f,%.1f] progress=%.1f lateral=%.1f type=obstacle")
                      % i % obs.name
                      % obs.position.x % obs.position.y % obs.position.z
                      % progress % lateral));
        }
    }

    // Print the per-model breakdown when nothing loaded or a load failed; it is
    // a one-shot dev diagnostic, so avoid spam on the common success path.
    if (loadedCount == 0 || anyMissing) {
        for (size_t i = 0; i < modelDiag.size(); ++i) {
            debug(str(boost::format("  model[%zu]=%s") % i % modelDiag[i]));
        }
    }
}

void Game::closeSwoopRace() {
    if (!_swoopRace.isActive()) {
        debug("swoop: closeSwoopRace called but race not active");
        return;
    }
    auto &sceneGraph = _services.scene.graphs.get(kSceneMain);
    auto bikeRoot = _swoopRace.bikeRoot();
    if (bikeRoot) {
        sceneGraph.removeRoot(*bikeRoot);
    }
    _swoopRace.stop();
    setPartyVisible(true);
    _cameraType = _savedCameraType;
    setRelativeMouseMode(_cameraType == CameraType::FirstPerson);
    openInGame();
    debug("swoop: stopped (race ended, party restored, camera reset)");
}

void Game::setPartyVisible(bool visible) {
    for (auto &member : _party.members()) {
        if (member.creature) {
            member.creature->setVisible(visible);
        }
    }
}

void Game::exitSwoopRace() {
    // Escape / stopswoop entry point. If a lifecycle race is in progress, return
    // to the origin module; otherwise just stop the dev race in place.
    if (_swoopLifecycle.active) {
        finishSwoopLifecycle(/*success=*/true);
    } else {
        closeSwoopRace();
    }
}

void Game::finishSwoopLifecycle(bool success) {
    if (!_swoopLifecycle.active) {
        return;
    }
    // Capture and clear the session first so the upcoming module load does not
    // re-enter this path. The current module (before returning) is the race
    // module, which selects the planet-specific result contract.
    MinigameLifecycle session = _swoopLifecycle;
    _swoopLifecycle = MinigameLifecycle();
    std::string raceModule = _module ? _module->name() : "";

    // Stop the race (removes bike models, restores camera/FOV/input, screen).
    closeSwoopRace();

    // Return to the originating module. Prefer the vanilla race-return waypoint
    // (e.g. Taris heartbeat returns to tar_m03af at tar03_wpmechanic) so the
    // leader lands on the authored return spot and naturally occupies the
    // post-race trigger; fall back to the saved pre-race position otherwise.
    const auto returnWaypoint = swoopReturnWaypoint(raceModule);
    if (!returnWaypoint.empty()) {
        debug(str(boost::format("swoop: return waypoint=%s") % returnWaypoint));
        loadModule(session.originModule, returnWaypoint);
    } else {
        loadModule(session.originModule);
        if (session.haveOrigin) {
            if (auto mod = _module) {
                if (auto area = mod->area()) {
                    if (auto leader = _party.getLeader()) {
                        leader->setPosition(session.originPosition);
                        leader->setFacing(session.originFacing);
                        area->determineObjectRoom(*leader);
                        area->onPartyLeaderMoved(/*roomChanged=*/true);
                    }
                }
            }
        }
    }

    if (success) {
        applySwoopForcedSuccessResult(raceModule);
    }

    debug(str(boost::format("swoop: finished forcedSuccess=%s returning=%s")
              % (success ? "yes" : "no")
              % session.originModule));
}

std::string Game::swoopReturnWaypoint(const std::string &raceModule) const {
    // Vanilla race-end transition target (StartNewModule waypoint), confirmed
    // from assets. K1 Taris: heartbeat.ncs returns to tar_m03af at the
    // tar03_wpmechanic waypoint, which sits inside the tar03_postrace trigger.
    // Other planets are not yet wired (empty = use the saved pre-race position).
    if (boost::iequals(raceModule, "tar_m03mg")) {
        return "tar03_wpmechanic";
    }
    return "";
}

void Game::applyTarisForcedWinningTime() {
    // Read the current heat's time-to-beat. k_ptar_racefirst sets these to
    // MIN_BEAT=0 / SEC_BEAT=38 / MSEC_BEAT=43 for heat 1 (total=3843 in the
    // vanilla comparison unit: MIN*10000 + SEC*100 + MSEC, confirmed by
    // disassembly of k_ptar_postswoop.ncs subroutine at 0x0524).
    //
    // We must win (playerTotal < beatTotal) AND keep playerTotal > 25 so that
    // the win-handler's beat update (k_ptar_postswoop 0x0572,
    // new_beat = playerTime - 25cs) stays strictly positive. Setting
    // player=0:00.00 underflows to MIN_BEAT=-1 (total=-4025), making every
    // subsequent heat unwinnable.
    int beatMin  = getGlobalNumber("TAR_SWOOP_MIN_BEAT");
    int beatSec  = getGlobalNumber("TAR_SWOOP_SEC_BEAT");
    int beatMsec = getGlobalNumber("TAR_SWOOP_MSEC_BEAT");
    int beatTotal = beatMin * 10000 + beatSec * 100 + beatMsec;

    // Choose a player time that wins with comfortable headroom. A 50cs margin
    // keeps enough distance from the beat target while the next beat
    // (playerTotal - 25) stays well above zero.
    //   - Normal case (beatTotal > 150): subtract the full 50cs margin;
    //     guarantees playerTotal > 100 and next beat stays positive.
    //   - Low beat (26-150): win by 1cs, floor at 26 so next beat stays > 0.
    //   - Degenerate beat (<= 25): use the asset-confirmed heat-1 reference
    //     (3793 = 3843 - 50); this path should not occur in normal Taris flow.
    static constexpr int kMargin = 50;
    static constexpr int kMinSafe = 26;                     // next beat = playerTotal - 25 > 0
    static constexpr int kMinSafePlayerTime = 100;          // floor for the comfortable-margin branch
    static constexpr int kNormalThreshold = kMinSafePlayerTime + kMargin; // 150
    static constexpr int kFallback = 3793;                  // k_ptar_racefirst heat-1 beat (3843) - 50

    int playerTotal;
    if (beatTotal > kNormalThreshold) {
        playerTotal = beatTotal - kMargin;
    } else if (beatTotal > 25) {
        playerTotal = std::max(beatTotal - 1, kMinSafe);
    } else {
        playerTotal = kFallback;
    }

    int playerMin  = playerTotal / 10000;
    int playerSec  = (playerTotal % 10000) / 100;
    int playerMsec = playerTotal % 100;
    setGlobalNumber("TAR_SWOOP_MIN",  playerMin);
    setGlobalNumber("TAR_SWOOP_SEC",  playerSec);
    setGlobalNumber("TAR_SWOOP_MSEC", playerMsec);

    _console.printLine(str(boost::format(
        "swoop: result forcedSuccess=yes planet=taris TAR_SWOOP_RUN=1"
        " beat=%d:%d.%d time=%d:%d.%d margin=%d") %
        beatMin % beatSec % beatMsec %
        playerMin % playerSec % playerMsec %
        (beatTotal - playerTotal)));
}

void Game::applySwoopForcedSuccessResult(const std::string &raceModule) {
    // K1 Taris swoop result contract, confirmed from local assets:
    //   tar_m03mg.are -> player OnHeartbeat = "heartbeat" is the race brain. At
    //     race start it sets the boolean global TAR_SWOOP_RUN = TRUE
    //     (SetGlobalBoolean) and records the run time in TAR_SWOOP_MIN / _SEC /
    //     _MSEC; reone substitutes the race and never runs heartbeat.
    //   The post-race scene is the "tar03_postrace" trigger, whose ScriptOnEnter
    //     is k_ptar_postswoop. Disassembly of k_ptar_postswoop.ncs shows it
    //     begins with: if (!GetGlobalBoolean("TAR_SWOOP_RUN")) return; then
    //     SetGlobalBoolean("TAR_SWOOP_RUN", FALSE); compares the run time vs the
    //     TAR_SWOOP_*_BEAT targets; SetGlobalNumber("Tar_SwoopStatus", 2) when
    //     the player time is lower (won) else 1; increments Tar_SwoopRaceCounter;
    //     and starts the announcer/Brejik scene (ActionStartConversation, using
    //     the entering PC).
    // So forced success must reproduce heartbeat's race-state outputs: set
    // TAR_SWOOP_RUN = TRUE so postswoop passes its guard, then choose a player
    // time strictly less than TAR_SWOOP_*_BEAT so postswoop computes a win.
    // Win state (Tar_SwoopStatus) and the scene are produced by the vanilla
    // trigger->postswoop chain (see Area::updateLeaderTriggerOccupancy and the
    // return-waypoint placement). No result/winner globals are set here.
    // Other planets are not yet wired.
    if (!boost::iequals(raceModule, "tar_m03mg")) {
        return;
    }
    setGlobalBoolean("TAR_SWOOP_RUN", true);
    applyTarisForcedWinningTime();
}

void Game::openTurret() {
    if (_turret.isActive()) {
        _console.printLine("turret: already running");
        return;
    }
    if (!_module || !_module->area()) {
        _console.printLine("turret: no module loaded");
        return;
    }
    auto area = _module->area();
    if (!area->hasMinigame() || area->miniGame().type != MinigameType::Turret) {
        _console.printLine("turret: current area has no turret minigame");
        return;
    }

    const auto &mg = area->miniGame();
    auto camera = area->getCamera<FirstPersonCamera>(CameraType::FirstPerson);
    if (camera) {
        camera->stopMovement();
    }

    _savedCameraType = _cameraType;
    if (!_turret.start(mg, camera, area->name())) {
        _console.printLine("turret: failed to start (see log)");
        return;
    }

    _cameraType = CameraType::FirstPerson;
    setRelativeMouseMode(true);
    changeScreen(Screen::Turret);

    // Every started session is a lifecycle session, so a win or a loss is
    // consumed the same way however the turret was entered. A session started
    // in place - the startturret developer command, or any entry that did not
    // come from a module transition - captures no origin; finishing one falls
    // through to the authored return module when the turret area names one and
    // otherwise leaves the player where they are. A transition-driven entry
    // overwrites this in onModuleLoaded with the origin it captured.
    _turretLifecycle = MinigameLifecycle();
    _turretLifecycle.active = true;

    // The minigame owns the party now; drop any actions queued before entry so
    // they do not survive the module transitions (mirrors the swoop entry).
    for (auto &member : _party.members()) {
        if (member.creature) {
            member.creature->clearAllActions(/*force=*/true);
        }
    }

    // Vanilla does not add the party to the scene in a minigame module; the
    // turret actor represents the player. Restored on exit.
    setPartyVisible(false);

    if (!mg.music.empty()) {
        playMusic(mg.music);
    }

    debug(str(boost::format("turret: started track=%s anchor=%s models=%zu banks=%zu enemies=%zu hp=%d camfov=%.0f clip=[%.2f,%.0f]")
              % mg.player.trackResRef
              % _turret.anchorSource()
              % mg.player.models.size()
              % _turret.gunBankCount()
              % _turret.enemyCount()
              % _turret.hitPoints()
              % mg.cameraViewAngle
              % mg.nearClip
              % mg.farClip));
    debug(str(boost::format("turret: hud gauge=%s radar=%s healthState=%d(%s) heading=%d contacts=%zu radarChannels=%zu alarm=%d")
              % (_turret.haveHealthHud() ? "mgf_hud02" : "<missing>")
              % (_turret.haveRadarHud() ? "mgf_hud01" : "<missing>")
              % _turret.healthState()
              % turretHealthAnimation(_turret.healthState())
              % _turret.headingState()
              % _turret.contactsLive()
              % _turret.radarChannelCount()
              % static_cast<int>(_turret.alarmActive())));
    debug(str(boost::format("turret: camera mount=%s hook=%s eyeOffset=[%.3f,%.3f,%.3f] targetOffset=[%.1f,%.1f,%.1f] rotate=%d")
              % (mg.player.cameraResRef.empty() ? "<none>" : mg.player.cameraResRef)
              % (_turret.haveCameraHook() ? "camerahook" : "<missing>")
              % _turret.cameraHookOffset().x
              % _turret.cameraHookOffset().y
              % _turret.cameraHookOffset().z
              % mg.player.targetOffset.x
              % mg.player.targetOffset.y
              % mg.player.targetOffset.z
              % static_cast<int>(mg.player.cameraRotate)));
    debug(str(boost::format("turret: aim pitch=[%.1f,%.1f]%s yaw=[%.1f,%.1f]%s authoredStart=[%.1f,%.1f,%.1f] startPitch=%.1f startYaw=%.1f")
              % glm::degrees(_turret.aim().minPitch())
              % glm::degrees(_turret.aim().maxPitch())
              % (_turret.aim().pitchBounded() ? "" : " (infinite)")
              % glm::degrees(_turret.aim().minYaw())
              % glm::degrees(_turret.aim().maxYaw())
              % (_turret.aim().yawBounded() ? "" : " (infinite)")
              % mg.player.startOffset.x
              % mg.player.startOffset.y
              % mg.player.startOffset.z
              % glm::degrees(_turret.aim().startPitch())
              % glm::degrees(_turret.aim().startYaw())));
}

void Game::closeTurret() {
    if (!_turret.isActive()) {
        debug("turret: closeTurret called but turret not active");
        return;
    }
    _turret.stop();
    setPartyVisible(true);
    _cameraType = _savedCameraType;
    setRelativeMouseMode(_cameraType == CameraType::FirstPerson);
    openInGame();
    debug("turret: stopped (party restored, camera reset)");
}

void Game::exitTurret() {
    // Escape / stopturret entry point. If a lifecycle session is in progress,
    // return to the origin module; otherwise just stop the dev session in place.
    if (_turretLifecycle.active) {
        // A session abandoned mid-run is still InProgress; it returns to the
        // origin but is neither a win nor a loss.
        finishTurretLifecycle(_turret.outcome());
    } else if (_pendingTurret.active) {
        // Scheduled but never started: drop the request rather than leaving it
        // to fire on a later transition into the same module.
        abandonPendingTurret("cancelled");
    } else {
        closeTurret();
    }
}

void Game::returnToLifecycleOrigin(const std::string &module,
                                   bool haveOrigin,
                                   const glm::vec3 &position,
                                   float facing) {
    if (module.empty()) {
        return;
    }
    loadModule(module);
    if (!haveOrigin) {
        return;
    }
    auto mod = _module;
    if (!mod || !mod->area()) {
        return;
    }
    auto leader = _party.getLeader();
    if (!leader) {
        return;
    }
    leader->setPosition(position);
    leader->setFacing(facing);
    mod->area()->determineObjectRoom(*leader);
    mod->area()->onPartyLeaderMoved(/*roomChanged=*/true);
}

void Game::abandonPendingTurret(const std::string &reason) {
    if (!_pendingTurret.active) {
        return;
    }
    PendingTurretRequest request = _pendingTurret;
    _pendingTurret = PendingTurretRequest();
    _console.printLine(str(boost::format("turret: lifecycle aborted (%s), returning to origin=%s")
                           % reason % request.originModule));
    returnToLifecycleOrigin(request.originModule,
                            request.haveOrigin,
                            request.originPosition,
                            request.originFacing);
}

void Game::finishTurretLifecycle(Turret::Outcome outcome) {
    // Repeated calls are no-ops: the session is cleared below before anything
    // else runs, so neither the return nor the completion state can be emitted
    // twice for one session.
    if (!_turretLifecycle.active) {
        return;
    }
    // Capture and clear the session first so the upcoming module load does not
    // re-enter this path. The current module (before returning) is the turret
    // module, which selects the return contract.
    MinigameLifecycle session = _turretLifecycle;
    _turretLifecycle = MinigameLifecycle();
    std::string turretModule = _module ? _module->name() : "";

    closeTurret();

    // Prefer the vanilla return module (the StartNewModule target the turret's
    // end scripts use); fall back to the module the player came from.
    std::string returnModule = game::turretReturnModule(turretModule, session.originModule);
    if (returnModule.empty()) {
        // Nothing authored and nothing captured: stay put rather than schedule
        // a transition to an empty module name.
        debug("turret: no return module, staying put");
        applyTurretResult(turretModule, outcome);
        return;
    }
    bool returningToOrigin = boost::iequals(returnModule, session.originModule);
    returnToLifecycleOrigin(returnModule,
                            returningToOrigin && session.haveOrigin,
                            session.originPosition,
                            session.originFacing);

    applyTurretResult(turretModule, outcome);

    debug(str(boost::format("turret: finished outcome=%s returning=%s")
              % turretOutcomeName(outcome)
              % returnModule));
}

void Game::applyTurretResult(const std::string &turretModule, Turret::Outcome outcome) {
    // K1 M12ab result contract, confirmed from local assets: k_pebo_mgload seeds
    // the globals ebo_num_fighters (Number) and ebo_turret_done (Boolean); each
    // enemy death script decrements ebo_num_fighters and, on the last kill, sets
    // ebo_turret_done before returning to ebo_m12aa. reone substitutes the
    // minigame and never runs those scripts, so reproduce their outputs here.
    //
    // Only the last kill writes them in vanilla, so only a victory writes them
    // here: a defeat or an abandoned session leaves the turret outstanding.
    if (!boost::iequals(turretModule, "m12ab")) {
        return;
    }
    if (!turretSessionSucceeded(outcome)) {
        _console.printLine(str(boost::format(
            "turret: result module=m12ab outcome=%s (no completion state written)")
            % turretOutcomeName(outcome)));
        return;
    }
    setGlobalNumber("ebo_num_fighters", 0);
    setGlobalBoolean("ebo_turret_done", true);
    _console.printLine(
        "turret: result module=m12ab outcome=won ebo_turret_done=1 ebo_num_fighters=0");
}

void Game::openInGameMenu(InGameMenuTab tab) {
    setCursorType(CursorType::Default);
    switch (tab) {
    case InGameMenuTab::Equipment:
        _inGame->openEquipment();
        break;
    case InGameMenuTab::Inventory:
        _inGame->openInventory();
        break;
    case InGameMenuTab::Character:
        _inGame->openCharacter();
        break;
    case InGameMenuTab::Abilities:
        _inGame->openAbilities();
        break;
    case InGameMenuTab::Party:
        _inGame->openPartySelection();
        break;
    case InGameMenuTab::Messages:
        _inGame->openMessages();
        break;
    case InGameMenuTab::Journal:
        _inGame->openJournal();
        break;
    case InGameMenuTab::Map:
        _inGame->openMap();
        break;
    case InGameMenuTab::Options:
        _inGame->openOptions();
        break;
    default:
        break;
    }
    changeScreen(Screen::InGameMenu);
}

void Game::openContainer(const std::shared_ptr<Object> &container) {
    stopMovement();
    setRelativeMouseMode(false);
    setCursorType(CursorType::Default);
    _container->open(container);
    changeScreen(Screen::Container);
}

void Game::openPartySelection(const PartySelectionContext &ctx) {
    stopMovement();
    setRelativeMouseMode(false);
    setCursorType(CursorType::Default);
    _partySelect->prepare(ctx);
    changeScreen(Screen::PartySelection);
}

void Game::openSaveLoad(SaveLoadMode mode) {
    setRelativeMouseMode(false);
    setCursorType(CursorType::Default);
    _saveLoad->setMode(mode);
    _saveLoad->refresh();
    changeScreen(Screen::SaveLoad);
}

bool Game::canOpenGalaxyMapFrom(Screen screen) {
    switch (screen) {
    case Screen::None:
    case Screen::InGame:
    case Screen::InGameMenu:
    case Screen::Conversation:
        return true;
    default:
        // Every other screen owns the whole display and has somewhere of its
        // own to return to. Taking it over would strand it.
        return false;
    }
}

void Game::openGalaxyMap(int initialPlanet) {
    if (!canOpenGalaxyMapFrom(_screen)) {
        return;
    }
    if (_galaxyMap && _galaxyMap->isRunningTravelScript()) {
        // The travel script this panel dispatched must not reopen it.
        return;
    }
    if (!_galaxyMap) {
        _galaxyMap = tryLoadGUI<GalaxyMap>();
    }
    if (!_galaxyMap) {
        // A panel that will not load must not take the screen away from
        // whatever is on it.
        return;
    }
    stopMovement();
    setRelativeMouseMode(false);
    setCursorType(CursorType::Default);
    // The panel decides what the routine's planet means: K2 has to record
    // where the party already is before anything can move the selection.
    _galaxyMap->prepare(initialPlanet);
    changeScreen(Screen::GalaxyMap);
}

void Game::serializePazaakPartyTable(resource::Gff &ptGff) const {
    auto replaceField = [&ptGff](resource::Gff::Field replacement) {
        auto &fields = ptGff.fields();
        auto found = std::find_if(fields.begin(), fields.end(), [&replacement](const auto &field) {
            return field.label == replacement.label;
        });
        if (found == fields.end()) {
            fields.push_back(std::move(replacement));
        } else {
            *found = std::move(replacement);
        }
    };

    replaceField(resource::Gff::Field::newDword(
        "PT_GOLD",
        static_cast<uint32_t>(std::max(0, _party.gold()))));
    if (!_party.hasValidPazaakData()) {
        return;
    }

    // Only the entries the running title actually stores are written back.
    std::vector<std::shared_ptr<resource::Gff>> cardEntries;
    const auto &savedCounts = _party.pazaakCardCounts();
    for (size_t i = 0; i < _party.pazaakCardCount(); ++i) {
        int count = savedCounts[i];
        cardEntries.push_back(
            resource::Gff::Builder()
                .field(resource::Gff::Field::newByte(
                    "PT_PAZAAKCOUNT",
                    static_cast<uint32_t>(count)))
                .build());
    }
    replaceField(resource::Gff::Field::newList(
        "PT_PAZAAKCARDS",
        std::move(cardEntries)));

    std::vector<std::shared_ptr<resource::Gff>> sideEntries;
    for (int cardId : _party.pazaakSideDeck()) {
        sideEntries.push_back(
            resource::Gff::Builder()
                .field(resource::Gff::Field::newInt(
                    "PT_PAZSIDECARD",
                    cardId))
                .build());
    }
    replaceField(resource::Gff::Field::newList(
        "PT_PAZSIDELIST",
        std::move(sideEntries)));
}

bool Game::playPazaak(
    int opponentDeck,
    std::string continuationScript,
    int maximumWager,
    bool tutorialRequested,
    const std::shared_ptr<Object> &opponent) {

    if (!opponent) {
        return false;
    }

    PazaakSessionParams params;
    params.opponentDeck = opponentDeck;
    params.continuationScript = std::move(continuationScript);
    params.maximumWager = maximumWager;
    params.tutorialRequested = tutorialRequested;
    params.opponentId = opponent->id();
    params.opponentName = opponent->name().empty() ? opponent->tag() : opponent->name();

    // A native match always plays with the cards the player actually owns, read
    // from PARTYTABLE.res. Only the developer command uses temporary cards.
    if (!_party.hasValidPazaakData()) {
        error("Unable to start Pazaak: PARTYTABLE.res has no valid Pazaak data");
        return false;
    }
    int cardTypes = static_cast<int>(
        (isTSL() ? Party::kK2PazaakCardCount : Party::kK1PazaakCardCount) - 1);
    std::array<std::optional<size_t>, Party::kMaxPazaakCardCount> collectionIndex;
    const auto &counts = _party.pazaakCardCounts();
    size_t ownedCards = 0;
    for (int cardId = 0; cardId < cardTypes; ++cardId) {
        if (counts[cardId] == 0) {
            continue;
        }
        auto definition = isTSL() ? k2PazaakCardDefinition(cardId)
                                  : k1PazaakCardDefinition(cardId);
        if (!definition) {
            error("Unable to start Pazaak: invalid player collection card ID");
            return false;
        }
        collectionIndex[cardId] = params.collection.size();
        params.collection.push_back(
            {*definition, static_cast<size_t>(counts[cardId]), cardId});
        ownedCards += static_cast<size_t>(counts[cardId]);
    }
    if (ownedCards < pazaak::kSideDeckSize) {
        error("Unable to start Pazaak: player owns fewer than ten side-deck cards");
        return false;
    }

    const auto &savedSideDeck = _party.pazaakSideDeck();
    if (std::all_of(savedSideDeck.begin(), savedSideDeck.end(), [](int id) {
            return id >= 0;
        })) {
        for (int cardId : savedSideDeck) {
            if (cardId >= cardTypes || !collectionIndex[cardId]) {
                error("Unable to start Pazaak: saved side deck is not owned");
                return false;
            }
            params.initialChosenCards.push_back(*collectionIndex[cardId]);
        }
    }

    if (_pazaakOpponentDeckOverride) {
        params.opponentSideDeck = _pazaakOpponentDeckOverride;
    } else {
        try {
            auto decks = _services.resource.twoDas.get("pazaakdecks");
            if (!decks) {
                error("Unable to start Pazaak: pazaakdecks.2da is missing");
                return false;
            }
            params.opponentSideDeck = isTSL()
                                          ? loadK2PazaakOpponentDeck(*decks, opponentDeck)
                                          : loadK1PazaakOpponentDeck(*decks, opponentDeck);
        } catch (const std::exception &e) {
            error("Unable to read pazaakdecks.2da: " + std::string(e.what()));
            return false;
        }
        if (!params.opponentSideDeck) {
            error("Unable to start Pazaak: invalid opponent deck row in pazaakdecks.2da");
            return false;
        }
    }
    return startPazaakFlow(std::move(params), opponent, false);
}

bool Game::startDevelopmentPazaak(std::string opponentName, int maximumWager) {
    PazaakSessionParams params;
    params.opponentDeck = 0;
    params.maximumWager = maximumWager;
    params.opponentName = opponentName.empty() ? "Pazaak Opponent" : std::move(opponentName);
    // The developer route never touches save-owned cards or credits: it uses a
    // temporary, title-appropriate collection and opponent deck only.
    if (isTSL()) {
        params.collection = PazaakSession::k2DefaultCollection();
        params.opponentSideDeck = PazaakSession::temporaryK2OpponentSideDeck();
        // Deterministic showcase deck covering every KotOR II family. The first
        // four entries become the opening hand: a Value Change card, a
        // sign-selectable card, a fixed card and a non-switchable special.
        params.initialChosenCards = PazaakSession::k2ShowcaseChosenCards();
        _pazaakShowcaseHands = true;
    } else {
        params.collection = PazaakSession::temporaryK1TestCollection();
        params.opponentSideDeck = PazaakSession::temporaryK1OpponentSideDeck();
    }
    return startPazaakFlow(std::move(params), nullptr, true);
}

bool Game::startPazaakFlow(
    PazaakSessionParams params,
    const std::shared_ptr<Object> &continuationCaller,
    bool developmentLaunch) {

    if (_pazaakSession) {
        return false;
    }

    _pazaakOriginScreen = _screen;
    _pazaakContinuationCaller = continuationCaller;
    _pazaakDevelopmentLaunch = developmentLaunch;
    _pazaakSelectionPersisted = false;
    _pazaakSettlementApplied = false;
    _pazaakOpponentEventElapsed = 0.0f;
    params.availableCredits = _party.gold();
    params.paceAutomaticDraws = _pazaakPaceAutomaticDraws;
    if (auto player = _party.player()) {
        params.playerName = player->name();
    }

    auto playerSelector = _pazaakPlayerHandSelector
                              ? _pazaakPlayerHandSelector
                              : (_pazaakShowcaseHands
                                     ? showcasePazaakHandSelector()
                                     : PazaakSession::HandSelector(randomPazaakHandSelection));
    auto opponentSelector = _pazaakOpponentHandSelector
                                ? _pazaakOpponentHandSelector
                                : PazaakSession::HandSelector(randomPazaakHandSelection);
    auto mainDeckFactory = _pazaakMainDeckFactory
                               ? _pazaakMainDeckFactory
                               : PazaakSession::MainDeckFactory(randomPazaakMainDeck);
    pazaak::Participant firstParticipant =
        randomInt(0, 1) == 0 ? pazaak::Participant::One : pazaak::Participant::Two;
    auto firstParticipantSelector = _pazaakFirstParticipantSelector
                                        ? _pazaakFirstParticipantSelector
                                        : PazaakSession::FirstParticipantSelector(
                                              [firstParticipant](size_t setIndex) {
                                                  bool useInitial = setIndex % 2 == 0;
                                                  if (useInitial) {
                                                      return firstParticipant;
                                                  }
                                                  return firstParticipant == pazaak::Participant::One
                                                             ? pazaak::Participant::Two
                                                             : pazaak::Participant::One;
                                              });

    try {
        _pazaakSession = std::make_unique<PazaakSession>(
            std::move(params),
            std::move(playerSelector),
            std::move(opponentSelector),
            std::move(mainDeckFactory),
            std::move(firstParticipantSelector));
    } catch (const std::exception &e) {
        error("Unable to create Pazaak session: " + std::string(e.what()));
        releasePazaakFlow(true);
        return false;
    }

    if (!loadPazaakGUIs()) {
        releasePazaakFlow(true);
        return false;
    }

    if (_module && _module->area()) {
        stopMovement();
    }
    setRelativeMouseMode(false);
    setCursorType(CursorType::Default);
    if (_pazaakSession->screen() == PazaakFlowScreen::Wager) {
        if (_pazaakWager) {
            _pazaakWager->refresh();
        }
        changeScreen(Screen::PazaakWager);
    } else {
        showPazaakSetup();
    }
    return true;
}

bool Game::loadPazaakGUIs() {
    if (_pazaakGuiLoadOverride) {
        _pazaakGUIsReady = _pazaakGuiLoadOverride();
        return _pazaakGUIsReady;
    }

    _pazaakWager = tryLoadGUI<PazaakWagerGUI>();
    _pazaakSetup = tryLoadGUI<PazaakSetupGUI>();
    _pazaakBoard = tryLoadGUI<PazaakBoardGUI>();
    _pazaakGUIsReady = _pazaakWager && _pazaakSetup && _pazaakBoard;
    if (!_pazaakGUIsReady) {
        _pazaakWager.reset();
        _pazaakSetup.reset();
        _pazaakBoard.reset();
    }
    return _pazaakGUIsReady;
}

void Game::showPazaakSetup() {
    if (!_pazaakSession ||
        !_pazaakGUIsReady ||
        _pazaakSession->screen() != PazaakFlowScreen::Setup) {
        return;
    }
    if (_pazaakSetup) {
        _pazaakSetup->refresh();
    }
    changeScreen(Screen::PazaakSetup);
}

void Game::showPazaakBoard() {
    if (!_pazaakSession ||
        !_pazaakGUIsReady ||
        _pazaakSession->screen() != PazaakFlowScreen::Board ||
        !_pazaakSession->match()) {
        return;
    }
    if (!_pazaakDevelopmentLaunch && !_pazaakSelectionPersisted) {
        Party::PazaakSideDeck selected;
        const auto &collection = _pazaakSession->collection();
        const auto &chosen = _pazaakSession->chosenCards();
        if (chosen.size() != selected.size()) {
            error("Unable to persist Pazaak side deck: selection is incomplete");
            return;
        }
        for (size_t i = 0; i < chosen.size(); ++i) {
            if (chosen[i] >= collection.size() ||
                collection[chosen[i]].persistentId < 0) {
                error("Unable to persist Pazaak side deck: invalid collection mapping");
                return;
            }
            selected[i] = collection[chosen[i]].persistentId;
        }
        _party.setPazaakSideDeck(std::move(selected));
        _pazaakSelectionPersisted = true;
    }
    if (_pazaakBoard) {
        _pazaakBoard->refresh();
    }
    changeScreen(Screen::PazaakBoard);
    completePazaakIfReady();
}

void Game::cancelPazaak() {
    if (!_pazaakSession || _pazaakSession->screen() == PazaakFlowScreen::Board) {
        return;
    }
    bool developmentLaunch = _pazaakDevelopmentLaunch;
    releasePazaakFlow(true);
    if (developmentLaunch) {
        _console.printLine("pazaak: development match cancelled");
    }
}

void Game::abortPazaak() {
    if (!_pazaakSession) {
        return;
    }
    releasePazaakFlow(true);
}

void Game::completePazaakIfReady() {
    if (!_pazaakSession ||
        !_pazaakSession->completedResult() ||
        _pazaakSession->presentationPending()) {
        return;
    }
    finishPazaak(*_pazaakSession->completedResult());
}

void Game::finishPazaak(PazaakCompletedResult result) {
    if (!_pazaakSession) {
        return;
    }

    std::string continuation(_pazaakSession->continuationScript());
    uint32_t opponentId = _pazaakSession->opponentId();
    std::shared_ptr<Object> continuationCaller(_pazaakContinuationCaller.lock());
    bool developmentLaunch = _pazaakDevelopmentLaunch;
    int wager = _pazaakSession->wager();
    bool callerValid = continuationCaller &&
                       continuationCaller->id() == opponentId &&
                       getObjectById(opponentId) == continuationCaller;
    _lastPazaakResult = result;

    if (!developmentLaunch && !_pazaakSettlementApplied) {
        if (result == PazaakCompletedResult::PlayerWon) {
            _party.giveGold(wager);
        } else {
            _party.takeGold(wager);
        }
        _pazaakSettlementApplied = true;
    }

    // Release ownership before external script execution so re-entrant or
    // repeated completion cannot invoke the continuation twice.
    releasePazaakFlow(true);

    if (developmentLaunch) {
        switch (result) {
        case PazaakCompletedResult::PlayerWon:
            _console.printLine("pazaak: development match completed - player won");
            break;
        case PazaakCompletedResult::OpponentWon:
            _console.printLine("pazaak: development match completed - opponent won");
            break;
        case PazaakCompletedResult::PlayerForfeited:
            _console.printLine("pazaak: development match completed - player forfeited");
            break;
        }
    }

    if (continuation.empty()) {
        return;
    }
    if (!callerValid) {
        error("Pazaak continuation skipped because its caller is no longer valid");
        return;
    }
    if (_pazaakContinuationOverride) {
        _pazaakContinuationOverride(continuation, opponentId);
    } else if (_scriptRunner) {
        _scriptRunner->run(continuation, opponentId);
    }
}

Game::Screen Game::safePazaakOriginScreen() const {
    switch (_pazaakOriginScreen) {
    case Screen::PazaakWager:
    case Screen::PazaakSetup:
    case Screen::PazaakBoard:
        return _module ? Screen::InGame : Screen::None;
    case Screen::Conversation:
        // The dialogue may finish after its action script opens Pazaak.
        // Returning to the world cannot strand an ended conversation GUI.
        return _module ? Screen::InGame : Screen::None;
    default:
        return _pazaakOriginScreen;
    }
}

void Game::releasePazaakFlow(bool restoreOrigin) {
    Screen restore = safePazaakOriginScreen();
    _pazaakSession.reset();
    _pazaakWager.reset();
    _pazaakSetup.reset();
    _pazaakBoard.reset();
    _pazaakGUIsReady = false;
    _pazaakContinuationCaller.reset();
    _pazaakDevelopmentLaunch = false;
    _pazaakSelectionPersisted = false;
    _pazaakSettlementApplied = false;
    _pazaakShowcaseHands = false;
    _pazaakOpponentEventElapsed = 0.0f;
    if (restoreOrigin) {
        changeScreen(restore);
    }
    _pazaakOriginScreen = Screen::None;
}

void Game::openLevelUp() {
    if (!_charGen) {
        _charGen = tryLoadGUI<CharacterGeneration>();
    }
    if (!_charGen) {
        return;
    }

    setRelativeMouseMode(false);
    setCursorType(CursorType::Default);
    _charGen->startLevelUp();
    changeScreen(Screen::CharacterGeneration);
}

void Game::notifyLevelUpPending(const Creature &creature) {
    if (!_party.isMember(creature)) {
        return;
    }
    _services.audio.mixer.play(_services.game.guiSounds.getOnLevelUpNotify(), AudioType::Sound);
}

void Game::startCharacterGeneration() {
    resetGame();
    if (!_charGen) {
        _charGen = tryLoadGUI<CharacterGeneration>();
    }
    if (!_charGen) {
        return;
    }
    withLoadingScreen(_charGen->loadScreenResRef(), [this]() {
        _loadScreen->setProgress(100);
        render();
        playMusic(_charGen->musicResRef());
        changeScreen(Screen::CharacterGeneration);
    });
}

void Game::startDialog(const std::shared_ptr<Object> &owner, const std::string &resRef) {
    if (_captureHUDPresentation) {
        return;
    }
    std::shared_ptr<Gff> dlg(_services.resource.gffs.get(resRef, ResType::Dlg));
    if (!dlg) {
        warn("Game: conversation not found: " + resRef);
        return;
    }

    stopMovement();
    setRelativeMouseMode(false);
    setCursorType(CursorType::Default);
    changeScreen(Screen::Conversation);

    auto dialog = _services.resource.dialogs.get(resRef);
    bool computerConversation = dialog->conversationType == ConversationType::Computer;
    _conversation = computerConversation ? _computer.get() : static_cast<Conversation *>(_dialog.get());
    _conversation->setAutoSkip(&_conversationAutoSkip);
    _conversation->start(dialog, owner);
}

void Game::resumeConversation() {
    if (!_conversation || !isConversationActive()) {
        return;
    }
    _conversation->resume();
}

void Game::pauseConversation() {
    if (!_conversation || !isConversationActive()) {
        return;
    }
    _conversation->pause();
}

void Game::loadInGameMenus() {
    if (!_hud) {
        _hud = tryLoadGUI<HUD>();
    }
    if (!_inGame) {
        _inGame = tryLoadGUI<InGameMenu>();
    }
    if (!_dialog) {
        _dialog = tryLoadGUI<DialogGUI>();
    }
    if (!_computer) {
        _computer = tryLoadGUI<ComputerGUI>();
    }
    if (!_container) {
        _container = tryLoadGUI<ContainerGUI>();
    }
    if (!_partySelect) {
        _partySelect = tryLoadGUI<PartySelection>();
    }
}

void Game::changeScreen(Screen screen) {
    auto gui = getScreenGUI();
    if (gui) {
        gui->clearSelection();
    }
    if (_confirmPopup) {
        _confirmPopup->hide();
    }
    _screen = screen;
}

GameGUI *Game::getScreenGUI() const {
    switch (_screen) {
    case Screen::MainMenu:
        return _mainMenu.get();
    case Screen::Loading:
        return _loadScreen.get();
    case Screen::CharacterGeneration:
        return _charGen.get();
    case Screen::InGame:
        return _cameraType == game::CameraType::ThirdPerson ? _hud.get() : nullptr;
    case Screen::InGameMenu:
        return _inGame.get();
    case Screen::Conversation:
        return _conversation;
    case Screen::Container:
        return _container.get();
    case Screen::PartySelection:
        return _partySelect.get();
    case Screen::SaveLoad:
        return _saveLoad.get();
    case Screen::GalaxyMap:
        return _galaxyMap.get();
    case Screen::SwoopRace:
        return nullptr; // race skeleton has no HUD yet
    case Screen::PazaakWager:
        return _pazaakWager.get();
    case Screen::PazaakSetup:
        return _pazaakSetup.get();
    case Screen::PazaakBoard:
        return _pazaakBoard.get();
    case Screen::Turret:
        return nullptr; // the turret HUD is part of the player model set
    default:
        return nullptr;
    }
}

void Game::setBarkBubbleText(std::string text, float duration) {
    _hud->barkBubble().setBarkText(text, duration);
}

void Game::submitStatusSummary(
    StatusSummaryCategory category,
    int amount,
    std::vector<std::string> items) {

    // Suppression and the Status Summary preference belong at this single
    // submission boundary when those vanilla behaviours are implemented.
    _statusSummary.submit(category, amount, std::move(items));
    if (_hud) {
        _hud->activateStatusSummaryIndicator(category);
    }
}

int Game::getPlotXP(const std::string &plotName) {
    std::shared_ptr<TwoDA> plotTable(_services.resource.twoDas.get("plot"));
    if (!plotTable) {
        return 0;
    }
    for (int row = 0; row < plotTable->getRowCount(); ++row) {
        if (boost::iequals(plotTable->getString(row, "label"), plotName)) {
            return plotTable->getInt(row, "xp");
        }
    }
    return 0;
}

int Game::getPlotXPByIndex(int plotIndex) {
    std::shared_ptr<TwoDA> plotTable(_services.resource.twoDas.get("plot"));
    if (!plotTable || plotIndex < 0 || plotIndex >= plotTable->getRowCount()) {
        return 0;
    }
    return plotTable->getInt(plotIndex, "xp");
}

void Game::awardPlotXP(const std::string &plotName, int percentage) {
    if (plotName.empty() || percentage == 0) {
        return;
    }
    int baseXP = getPlotXP(plotName);
    if (baseXP == 0) {
        return;
    }
    int amount = static_cast<int>((static_cast<int64_t>(baseXP) * percentage) / 100);
    _party.awardXP(amount, XPSource::Plot);
}

void Game::awardPlotXPByIndex(int plotIndex, float fraction) {
    if (plotIndex < 0 || fraction == 0.0f) {
        return;
    }
    int baseXP = getPlotXPByIndex(plotIndex);
    if (baseXP == 0) {
        return;
    }
    int amount = static_cast<int>(baseXP * fraction);
    _party.awardXP(amount, XPSource::Plot);
}

void Game::onModuleSelected(const std::string &module) {
    _mainMenu->onModuleSelected(module);
}

void Game::renderHUD() {
    _hud->render();
}

CameraType Game::getConversationCamera(int &cameraId) const {
    return _conversation->getCamera(cameraId);
}

void Game::updateImGui(float dt) {
    ImGui::ShowDemoWindow(&_showImGui);
}

std::shared_ptr<Object> Game::getConsoleTargetObject() {
    auto object = getConsoleArea()->selectedObject();
    if (!object) {
        object = party().getLeader();
    }
    if (object) {
        return object;
    }
    throw std::runtime_error("No object is selected");
}

std::shared_ptr<Creature> Game::getConsoleTargetCreature() {
    if (auto object = getConsoleArea()->selectedObject()) {
        if (auto creature = dyn_cast<Creature>(object)) {
            return creature;
        }
        throw std::runtime_error("Selected object must be a creature");
    }

    return getConsoleLeader();
}

std::shared_ptr<Creature> Game::getConsoleLeader() {
    if (std::shared_ptr<Creature> leader = _party.getLeader()) {
        return leader;
    }
    throw std::runtime_error("No party leader");
}

std::shared_ptr<Area> Game::getConsoleArea() {
    std::shared_ptr<Module> mod = module();
    if (!mod) {
        throw std::runtime_error("Module is not loaded");
    }

    if (std::shared_ptr<Area> area = mod->area()) {
        return area;
    }
    throw std::runtime_error("Area is not loaded");
}

static void consoleCheckUsage(const ConsoleArgs &args,
                              size_t minArgs, size_t maxArgs,
                              std::string_view usage) {
    size_t numArgs = args.size() - 1;
    if (numArgs < minArgs || numArgs > maxArgs) {
        throw std::runtime_error(str(boost::format("Usage: %s %s") % args[0].value() % usage));
    }
}

void Game::consoleInfo(const ConsoleArgs &args) {
    auto object = getConsoleTargetObject();
    glm::vec3 position(object->position());

    std::stringstream ss;
    ss << std::setprecision(2) << std::fixed
       << "id=" << object->id()
       << " "
       << "tag=\"" << object->tag() << "\""
       << " "
       << "tpl=\"" << object->blueprintResRef() << "\""
       << " "
       << "pos=[" << position.x << ", " << position.y << ", " << position.z << "]";

    if (auto creature = dyn_cast<Creature>(object)) {
        ss << " "
           << "app=" << creature->appearance()
           << " "
           << "fac=" << static_cast<int>(creature->faction());
    } else if (auto placeable = dyn_cast<Placeable>(object)) {
        ss << " "
           << "app=" << placeable->appearance();
    }

    _console.printLine(ss.str());
}

void Game::consoleListGlobals(const ConsoleArgs &args) {
    auto &strings = globalStrings();
    for (auto &var : strings) {
        _console.printLine(var.first + " = " + var.second);
    }

    auto &booleans = globalBooleans();
    for (auto &var : booleans) {
        _console.printLine(var.first + " = " + (var.second ? "true" : "false"));
    }

    auto &numbers = globalNumbers();
    for (auto &var : numbers) {
        _console.printLine(var.first + " = " + std::to_string(var.second));
    }

    auto &locations = globalLocations();
    for (auto &var : locations) {
        _console.printLine(str(boost::format("%s = (%.04f, %.04f, %.04f, %.04f") %
                               var.first %
                               var.second->position().x %
                               var.second->position().y %
                               var.second->position().z %
                               var.second->facing()));
    }
}

void Game::consoleListLocals(const ConsoleArgs &args) {
    auto object = getConsoleTargetObject();

    auto &booleans = object->localBooleans();
    for (auto &var : booleans) {
        _console.printLine(std::to_string(var.first) + " -> " + (var.second ? "true" : "false"));
    }

    auto &numbers = object->localNumbers();
    for (auto &var : numbers) {
        _console.printLine(std::to_string(var.first) + " -> " + std::to_string(var.second));
    }
}

void Game::consoleListAnim(const ConsoleArgs &args) {
    auto object = getConsoleTargetObject();
    auto substr = args[1];

    auto model = std::static_pointer_cast<ModelSceneNode>(object->sceneNode());
    std::vector<std::string> anims(model->model().getAnimationNames());
    sort(anims.begin(), anims.end());

    for (auto &anim : anims) {
        if (!substr || boost::contains(anim, substr.value())) {
            _console.printLine(anim);
        }
    }
}

void Game::consolePlayAnim(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "anim_name");
    std::string anim(args[1].value());

    auto object = getConsoleTargetObject();
    auto model = std::static_pointer_cast<ModelSceneNode>(object->sceneNode());
    model->playAnimation(anim, nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
}

void Game::consoleKill(const ConsoleArgs &args) {
    auto object = getConsoleTargetObject();
    auto effect = newEffect<DamageEffect>(
        100000,
        DamageType::Universal,
        DamagePower::Normal,
        /*damager=*/ 0);
    object->applyEffect(std::move(effect), DurationType::Instant);
}

void Game::consoleAddItem(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 2, "item_tpl [size]");
    auto object = getConsoleTargetObject();
    int stackSize = args.get<int>(2).value_or(1);
    auto receiver = _party.sharedInventoryReceiver(object);
    receiver->addItem(std::string(args[1].value()), stackSize);
}

void Game::consoleGiveXP(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "amount");
    auto creature = getConsoleTargetCreature();
    int amount = args.get<int>(1).value();
    if (_party.isMember(*creature)) {
        _party.awardXP(amount, XPSource::Console);
    } else {
        creature->giveXP(amount);
    }
}

void Game::consoleGiveGold(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "amount");
    _party.giveGold(args.get<int>(1).value());
    _console.printLine(str(boost::format("party gold: %d") % _party.gold()));
}

// The free camera is the first-person camera flown off the player: WASD/QZ
// move it, the mouse aims it. These commands exist so a viewpoint found by
// hand can be replayed exactly from a commands file - camstatus prints the
// line to paste - and the same commands drive other builds of the engine,
// which keeps captures comparable between them.
void Game::consoleCamera(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "free");
    if (args[1].value() != "free") {
        throw std::runtime_error("Unknown camera: " + std::string(args[1].value()));
    }
    if (_screen != Screen::InGame) {
        throw std::runtime_error("The free camera needs the in-game screen");
    }
    if (_cameraType != CameraType::FirstPerson) {
        toggleInGameCameraType();
    }
}

void Game::consoleCamPos(const ConsoleArgs &args) {
    consoleCheckUsage(args, 3, 3, "x y z");
    auto camera = getConsoleArea()->getCamera<FirstPersonCamera>(CameraType::FirstPerson);
    camera->setPosition({args.get<float>(1).value(), args.get<float>(2).value(), args.get<float>(3).value()});
}

void Game::consoleCamLook(const ConsoleArgs &args) {
    consoleCheckUsage(args, 3, 3, "x y z");
    auto camera = getConsoleArea()->getCamera<FirstPersonCamera>(CameraType::FirstPerson);
    camera->setLookAt({args.get<float>(1).value(), args.get<float>(2).value(), args.get<float>(3).value()});
}

void Game::consoleCamStatus(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");
    auto camera = getConsoleArea()->getCamera<FirstPersonCamera>(CameraType::FirstPerson);
    glm::vec3 pos = camera->position();
    glm::vec3 forward(-glm::sin(camera->facing()) * glm::cos(camera->pitch()),
                      glm::cos(camera->facing()) * glm::cos(camera->pitch()),
                      glm::sin(camera->pitch()));
    glm::vec3 target = pos + forward;
    std::string result = str(boost::format("camera free; campos %.6f %.6f %.6f; camlook %.6f %.6f %.6f") %
                             pos.x % pos.y % pos.z % target.x % target.y % target.z);
    _console.printLine(result);
    info(result);
}

void Game::consoleWarp(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "module");
    // Gallery states share an engine process for speed. A warp is their scene
    // boundary, so fixture-only HUD state must not bleed into the next image.
    _captureHUDPresentation = false;
    if (_hud) {
        _hud->clearCapturePresentation();
    }
    loadModule(std::string(args[1].value()));
}

void Game::consoleOpenMenu(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "equipment|equipment-items|inventory|character|abilities|party|messages|journal|map|options");

    std::string_view name(args[1].value());
    if (boost::iequals(name, "equipment-items")) {
        setCursorType(CursorType::Default);
        _inGame->openEquipmentItems();
        changeScreen(Screen::InGameMenu);
        return;
    }

    static const std::array<std::pair<std::string_view, InGameMenuTab>, 9> kTabs {{
        {"equipment", InGameMenuTab::Equipment},
        {"inventory", InGameMenuTab::Inventory},
        {"character", InGameMenuTab::Character},
        {"abilities", InGameMenuTab::Abilities},
        {"party", InGameMenuTab::Party},
        {"messages", InGameMenuTab::Messages},
        {"journal", InGameMenuTab::Journal},
        {"map", InGameMenuTab::Map},
        {"options", InGameMenuTab::Options},
    }};
    for (const auto &[tabName, tab] : kTabs) {
        if (boost::iequals(name, tabName)) {
            openInGameMenu(tab);
            return;
        }
    }
    throw std::runtime_error("Unknown in-game menu tab: " + std::string(name));
}

static std::string joinConsoleArgs(const ConsoleArgs &args, size_t first) {
    std::string result;
    for (size_t i = first; i < args.size(); ++i) {
        if (!result.empty()) {
            result += ' ';
        }
        result += args[i].value();
    }
    return result;
}

void Game::consoleOpenCharacterGeneration(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 2, "class|quick-or-custom|quick|portrait|name|custom|abilities|skills|feats|powers|level-up [select]");

    std::string_view screen(args[1].value());
    bool selectFirst = args.size() > 2 && boost::iequals(std::string(args[2].value()), "select");
    if (boost::iequals(screen, "class")) {
        startCharacterGeneration();
        if (_charGen) {
            _charGen->openClassSelection();
        }
        return;
    }

    if (!_module || !_party.getLeader()) {
        throw std::runtime_error("Character-generation capture screens require a loaded party");
    }
    openLevelUp();
    if (!_charGen) {
        throw std::runtime_error("Character generation GUI is unavailable");
    }
    if (boost::iequals(screen, "level-up")) {
        _charGen->openLevelUp();
        return;
    }

    Character character(_charGen->character());
    ClassType captureClass = boost::iequals(screen, "powers")
                                 ? ClassType::JediConsular
                                 : (isTSL() ? ClassType::JediGuardian : ClassType::Soldier);
    std::shared_ptr<CreatureClass> clazz(_services.game.classes.get(captureClass));
    if (!clazz) {
        throw std::runtime_error("Starting class is unavailable");
    }
    character.attributes = clazz->defaultAttributes();
    _charGen->setCharacter(std::move(character));

    if (boost::iequals(screen, "quick")) {
        _charGen->startQuick();
        return;
    }
    _charGen->startCustom();
    if (boost::iequals(screen, "quick-or-custom")) {
        _charGen->openQuickOrCustom();
        return;
    }
    if (boost::iequals(screen, "portrait")) {
        _charGen->openPortraitSelection();
        return;
    }
    if (boost::iequals(screen, "name")) {
        _charGen->openNameEntry();
        return;
    }
    if (boost::iequals(screen, "custom")) {
        _charGen->openCustom();
        return;
    }
    if (boost::iequals(screen, "abilities")) {
        _charGen->openAbilities();
        return;
    }
    if (boost::iequals(screen, "skills")) {
        _charGen->openSkills();
        if (selectFirst) {
            _charGen->skills().selectFirstEntryForCapture();
        }
        return;
    }
    if (boost::iequals(screen, "feats")) {
        _charGen->openFeats();
        if (selectFirst) {
            _charGen->feats().selectFirstEntryForCapture();
        }
        return;
    }
    if (boost::iequals(screen, "powers")) {
        _charGen->openPowers();
        if (selectFirst) {
            _charGen->powers().selectFirstEntryForCapture();
        }
        return;
    }
    throw std::runtime_error("Unknown character-generation screen: " + std::string(screen));
}

void Game::consoleShowBark(const ConsoleArgs &args) {
    consoleCheckUsage(args, 2, 1024, "seconds message ...");
    auto duration = args.get<float>(1);
    if (!duration || *duration <= 0.0f) {
        throw std::invalid_argument("showbark duration must be positive");
    }
    if (!_hud) {
        throw std::runtime_error("HUD is unavailable; load a module first");
    }
    setBarkBubbleText(joinConsoleArgs(args, 2), *duration);
}

void Game::consoleSkipMovie(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");
    _movie.reset();
}

void Game::consoleShowPopup(const ConsoleArgs &args) {
    consoleCheckUsage(args, 2, 1024, "icon|none message ...");
    if (!_confirmPopup) {
        _confirmPopup = tryLoadGUI<ConfirmPopup>();
    }
    if (!_confirmPopup) {
        throw std::runtime_error("Confirmation popup GUI is unavailable");
    }

    std::shared_ptr<Texture> icon;
    std::string_view iconResRef(args[1].value());
    if (!boost::iequals(iconResRef, "none")) {
        icon = _services.resource.textures.get(std::string(iconResRef), TextureUsage::GUI);
    }
    _confirmPopup->show(joinConsoleArgs(args, 2), std::move(icon));
}

void Game::consoleSeed(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "number");
    setRandomSeed(static_cast<uint32_t>(args.get<int>(1).value()));
}

void Game::consoleGraphics(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "on|off");
    auto mode = std::string(args[1].value());
    if (boost::iequals(mode, "on")) {
        _options.graphics.sceneRender = true;
    } else if (boost::iequals(mode, "off")) {
        _options.graphics.sceneRender = false;
    } else {
        throw std::runtime_error("Expected on or off");
    }
}

void Game::consoleOpenContainer(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");
    auto leader = getConsoleLeader();
    if (!leader || !_container) {
        throw std::runtime_error("Container fixture requires a loaded module");
    }
    openContainer(leader);
}

void Game::consoleShowHUD(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 1, "[combat]");
    if (!_module || !_hud) {
        throw std::runtime_error("HUD capture fixture requires a loaded module");
    }
    bool combat = args.size() > 1 && boost::iequals(std::string(args[1].value()), "combat");
    if (args.size() > 1 && !combat) {
        throw std::runtime_error("Unknown HUD capture presentation: " + std::string(args[1].value()));
    }
    _captureHUDPresentation = true;
    _cameraType = CameraType::ThirdPerson;
    openInGame();
    _hud->showCapturePresentation(combat);
}

void Game::consoleShowTransition(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1024, "destination ...");
    if (!_module || !_hud) {
        throw std::runtime_error("Area-transition capture fixture requires a loaded module");
    }
    _captureHUDPresentation = true;
    _cameraType = CameraType::ThirdPerson;
    openInGame();
    _hud->showCapturePresentation(false);
    _hud->showTransitionCapturePresentation(joinConsoleArgs(args, 1));
}

void Game::consoleSelectDialogOption(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "index");
    if (_screen != Screen::Conversation || _conversation != _dialog.get()) {
        throw std::runtime_error("Dialog selection fixture requires an active character conversation");
    }
    _dialog->selectReplyForCapture(args.get<int>(1).value());
}

void Game::consoleShowGalleryMode(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 2, "swoop|pazaak wager|setup|board");

    std::string_view mode(args[1].value());
    if (boost::iequals(mode, "swoop")) {
        if (!_swoopRace.isActive()) {
            openSwoopRace();
        }
        if (!_swoopRace.isActive()) {
            throw std::runtime_error("Swoop gallery fixture requires a loaded swoop minigame module");
        }
        return;
    }

    if (!boost::iequals(mode, "pazaak") || args.size() != 3) {
        throw std::runtime_error("Unknown gallery mode; expected swoop or pazaak wager|setup|board");
    }
    if (!_module) {
        throw std::runtime_error("Pazaak gallery fixture requires a loaded module");
    }

    std::string_view screen(args[2].value());
    bool showWager = boost::iequals(screen, "wager");
    bool showSetup = boost::iequals(screen, "setup");
    bool showBoard = boost::iequals(screen, "board");
    if (!showWager && !showSetup && !showBoard) {
        throw std::runtime_error("Unknown Pazaak gallery screen: " + std::string(screen));
    }

    abortPazaak();
    if (!startDevelopmentPazaak("Gallery Opponent", showWager ? 100 : 0)) {
        throw std::runtime_error("Unable to start Pazaak gallery fixture");
    }
    if (showWager || showSetup) {
        return;
    }

    for (size_t collectionIndex = 0;
         collectionIndex < _pazaakSession->collection().size() &&
         _pazaakSession->chosenCards().size() < pazaak::kSideDeckSize;
         ++collectionIndex) {
        while (_pazaakSession->remainingCopies(collectionIndex) > 0 &&
               _pazaakSession->chosenCards().size() < pazaak::kSideDeckSize) {
            _pazaakSession->selectCard(collectionIndex);
        }
    }
    if (!_pazaakSession->confirmSetup()) {
        throw std::runtime_error("Unable to prepare Pazaak board gallery fixture");
    }
    showPazaakBoard();
}

void Game::consoleRunScript(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1024, "resref [kind:value ...]");

    std::string resRef(args[1].value());
    std::vector<script::Argument> vars;
    for (size_t i = 2; i < args.size(); ++i) {
        vars.push_back(script::Argument::fromString(std::string(args[i].value())));
    }

    int result = scriptRunner().run(resRef, vars);
    _console.printLine(str(boost::format("%s -> %d") % resRef % result));
}

void Game::consoleShowAABB(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "1|0");
    bool show = args.get<int>(1).value();
    setShowAABB(show);
}

void Game::consoleShowWalkmesh(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "1|0");
    bool show = args.get<int>(1).value();
    setShowWalkmesh(show);
}

void Game::consoleShowTriggers(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "1|0");
    bool show = args.get<int>(1).value();
    setShowTriggers(show);
}

void Game::consoleSpawnCreature(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 2, "res [id]");

    std::string res(args[1].value());
    std::optional<uint32_t> id = args.get<uint32_t>(2);

    auto area = getConsoleArea();
    auto leader = getConsoleLeader();

    std::shared_ptr<Creature> creature;
    if (auto id = args.get<uint32_t>(2)) {
        if (getObjectById(id.value())) {
            throw std::runtime_error("Object already exists");
        }
        creature = newObjectAtId<Creature>(
            id.value(), false, kSceneMain, *this, _services);
    } else {
        creature = newCreature();
    }

    creature->loadFromBlueprint(res);
    creature->setPosition(leader->position());
    creature->setFacing(leader->getFacing());
    creature->setFaction(Faction::Neutral);

    area->landObject(*creature);
    area->add(creature);
    creature->runSpawnScript();
}

void Game::consoleSpawnCompanion(const ConsoleArgs &args) {
    consoleCheckUsage(args, 2, 3, "res npcindex [id]");

    std::string res(args[1].value());
    int npc = args.get<int>(2).value();
    std::optional<uint32_t> id = args.get<uint32_t>(3);

    auto leader = getConsoleLeader();
    auto area = getConsoleArea();

    std::shared_ptr<Creature> companion;
    if (id) {
        if (getObjectById(id.value())) {
            throw std::runtime_error("Object already exists");
        }
        companion = newObjectAtId<Creature>(
            id.value(), false, kSceneMain, *this, _services);
    } else {
        companion = newCreature();
    }

    companion->loadFromBlueprint(res);
    companion->setPosition(leader->position());
    companion->setFacing(leader->getFacing());
    companion->setFaction(leader->faction());

    area->landObject(*companion);
    area->add(companion);
    companion->runSpawnScript();
    _party.addMember(npc, companion);
}

void Game::consoleAddAvailableNpc(const ConsoleArgs &args) {
    consoleCheckUsage(args, 2, 2, "npcindex blueprint");

    int npc = args.get<int>(1).value();
    std::string blueprint(args[2].value());

    if (!_party.addAvailableMember(npc, blueprint)) {
        throw std::runtime_error("NPC is already available: " + std::to_string(npc));
    }
}

void Game::consoleSelectObjectById(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "id");
    int id = args.get<int>(1).value();

    std::shared_ptr<Object> object = getObjectById(id);
    if (!object) {
        throw std::runtime_error("Object not found");
    }

    getConsoleArea()->selectObject(object, /*force=*/true);
}

void Game::consoleSelectObjectByTag(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "tag");
    std::string_view tag = args[1].value();

    auto area = getConsoleArea();
    for (auto &object : area->objects()) {
        if (object->tag() == tag) {
            area->selectObject(object, /*force=*/true);
            return;
        }
    }

    throw std::runtime_error("Object not found");
}

void Game::consoleSelectLeader(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");
    getConsoleArea()->selectObject(getConsoleLeader());
}

void Game::consoleSetFaction(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "number");
    Faction faction = args.getEnum<Faction>(1).value();
    getConsoleTargetCreature()->setFaction(faction);
}

void Game::consoleSetPosition(const ConsoleArgs &args) {
    consoleCheckUsage(args, 3, 3, "x y z");

    glm::vec3 pos(args.get<float>(1).value(),
                  args.get<float>(2).value(),
                  args.get<float>(3).value());

    std::shared_ptr<Creature> creature = getConsoleTargetCreature();
    std::shared_ptr<Area> area = getConsoleArea();

    creature->setPosition(pos);
    area->determineObjectRoom(*creature);

    auto leader = party().getLeader();
    if (&*creature == &*leader) {
        area->onPartyLeaderMoved(/*roomChanged=*/true);
    }
}

void Game::consoleProfessionalTools(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");

    std::vector<std::pair<std::string, int>> items = {
        // Ranged weapons
        {"g_w_blstrcrbn001", 1},
        {"g_w_blstrpstl001", 2},
        {"g_w_blstrrfl001", 1},
        {"g_w_bowcstr001", 1},
        {"g_w_dsrptpstl001", 2},
        {"g_w_dsrptrfl001", 1},
        {"g_w_ionblstr02", 2},
        {"g_w_ionrfl01", 1},
        {"g_w_rptnblstr01", 1},
        {"g_w_sonicpstl01", 2},
        {"g_w_sonicrfl01", 1},

        // Melee weapons
        {"g_w_dblsbr001", 1},
        {"g_w_dblswrd001", 1},
        {"g_w_gaffi001", 1},
        {"g_w_lghtsbr01", 2},
        {"g_w_lngswrd01", 2},
        {"g_w_stunbaton01", 1},
        {"g_w_waraxe001", 1},

        // Grenades
        {"g_w_adhsvgren001", 10},
        {"g_w_cryobgren001", 10},
        {"g_w_firegren001", 10},
        {"g_w_flashgren001", 10},
        {"g_w_fraggren01", 10},
        {"g_w_iongren01", 10},
        {"g_w_poisngren01", 10},
        {"g_w_sonicgren01", 10},
        {"g_w_stungren01", 10},
        {"g_w_thermldet01", 10},

        // Mines
        {"g_i_trapkit001", 10},
        {"g_i_trapkit004", 10},
        {"g_i_trapkit007", 10},
        {"g_i_trapkit010", 10},

        // Consumables
        {"g_i_frarmbnds01", 10},
        {"g_i_medeqpmnt01", 10},
        {"g_i_medeqpmnt04", 10},
        {"g_i_adrnaline001", 10},
        {"g_i_adrnaline002", 10},
        {"g_i_adrnaline003", 10},
    };

    std::shared_ptr<Creature> creature = getConsoleTargetCreature();
    if (!creature) {
        return;
    }
    for (auto &kv : items) {
        creature->addItem(kv.first, kv.second);
    }
}

void Game::consoleKillRoom(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");

    std::shared_ptr<Creature> target = getConsoleTargetCreature();
    Room *room = target->room();
    if (!room) {
        throw std::runtime_error("No room found for the selected creature");
    }

    auto leader = party().getLeader();
    bool killEnemies = &*target == &*leader;

    SmallSet<Creature *, 16> targets;
    for (Object *object : room->tenants()) {
        Creature *creature = dyn_cast<Creature>(object);
        if (!creature || creature->isDead()) {
            continue;
        }

        if (killEnemies) {
            // Kill enemies of the leader
            if (_services.game.reputes.getIsEnemy(*target, *creature)) {
                targets.insert(creature);
            }
        } else {
            // Kill all creatures with the same faction as the selected target
            if (target->faction() == creature->faction()) {
                targets.insert(creature);
            }
        }
    }

    for (Creature *creature : targets) {
        creature->damage(std::numeric_limits<int>::max(), 0);
    }
}

void Game::consoleAutoSkipEnable(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "1|0");
    _conversationAutoSkip.enabled = args.get<int>(1).value();
}

void Game::consoleAutoSkipEntries(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 1024, "1|0 ...");

    auto &entries = _conversationAutoSkip.entries;
    entries = std::queue<bool>();

    if (args.size() <= 1) {
        return;
    }

    for (size_t i = 1; i < args.size(); ++i) {
        entries.push(args.get<int>(i).value());
    }
}

void Game::consoleAutoSkipReplies(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 1024, "number|? ...");

    auto &replies = _conversationAutoSkip.replies;
    replies = std::queue<std::optional<int>>();

    if (args.size() <= 1) {
        return;
    }

    for (size_t i = 1; i < args.size(); ++i) {
        int val = args.get<int>(i).value();
        if (!val) {
            replies.push(std::optional<int>());
            continue;
        }
        replies.push(val - 1);
    }
}

void Game::consoleStartConversation(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 1, "[dlg_resref]");

    auto leader = getConsoleLeader();
    _captureHUDPresentation = false;
    auto resRef = args[1];
    if (resRef) {
        startDialog(leader, std::string(resRef.value()));
        return;
    }

    auto target = getConsoleTargetObject();

    auto action = newAction<StartConversationAction>(target, "");
    leader->addAction(std::move(action));
}

void Game::consoleCutsceneAttack(const ConsoleArgs &args) {
    consoleCheckUsage(args, 4, 4, "target_id animation_id result damage");

    std::shared_ptr<Creature> actor = getConsoleTargetCreature();

    std::shared_ptr<Object> target = getObjectById(args.get<uint32_t>(1).value());
    if (!target) {
        throw std::runtime_error("Target not found");
    }

    int anim = args.get<int>(2).value();
    AttackResultType result = args.getEnum<AttackResultType>(3).value();
    int damage = args.get<int>(4).value();

    auto action = newAction<CutsceneAttackAction>(
        std::move(target), anim, result, damage);
    actor->addAction(std::move(action));
}

void Game::consoleSetAbility(const ConsoleArgs &args) {
    consoleCheckUsage(args, 2, 2, "ability value");
    std::shared_ptr<Creature> actor = getConsoleTargetCreature();
    std::optional<Ability> ability = args.getEnum<Ability>(1);
    if (!ability) {
        throw std::runtime_error("Invalid ability: must be a number");
    }
    std::optional<int> value = args.get<int>(2);
    if (!value) {
        throw std::runtime_error("Invalid value");
    }
    actor->attributes().setAbilityScore(ability.value(), value.value());
}

void Game::consoleSetSkill(const ConsoleArgs &args) {
    consoleCheckUsage(args, 2, 2, "skill value");
    std::shared_ptr<Creature> actor = getConsoleTargetCreature();
    std::optional<SkillType> skill = args.getEnum<SkillType>(1);
    if (!skill) {
        throw std::runtime_error("Invalid skill: must be a number");
    }

    std::optional<int> value = args.get<int>(2);
    if (!value) {
        throw std::runtime_error("Invalid value");
    }
    actor->attributes().setSkillRank(skill.value(), value.value());
}

void Game::consoleAddOrRemoveFeat(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "feat");
    std::shared_ptr<Creature> actor = getConsoleTargetCreature();
    std::optional<FeatType> feat = args.getEnum<FeatType>(1);
    if (!feat) {
        throw std::runtime_error("Invalid feat: must be a number");
    }

    CreatureAttributes &attrs = actor->attributes();
    if (args[0].value() == "addfeat") {
        attrs.addFeat(feat.value());
    } else {
        attrs.removeFeat(feat.value());
    }
}

void Game::consoleAddOrRemoveSpell(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "spell");
    std::shared_ptr<Creature> actor = getConsoleTargetCreature();
    std::optional<SpellType> spell = args.getEnum<SpellType>(1);
    if (!spell) {
        throw std::runtime_error("Invalid spell: must be a number");
    }

    CreatureAttributes &attrs = actor->attributes();
    if (args[0].value() == "addspell") {
        attrs.addSpell(spell.value());
    } else {
        attrs.removeSpell(spell.value());
    }
}

void Game::consoleCastSpellAtObject(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 3, "spell ischeat item");

    auto leader = getConsoleLeader();
    auto target = getConsoleTargetObject();

    std::optional<SpellType> spellType = args.getEnum<SpellType>(1);
    if (!spellType) {
        throw std::runtime_error("Invalid spell: must be a number");
    }

    std::shared_ptr<Spell> spell = _services.game.spells.get(spellType.value());
    if (!spell) {
        throw std::runtime_error("Unknown spell");
    }

    bool cheat = args.get<int>(2).value_or(false);
    std::optional<std::string_view> spellItem = args[3];
    std::optional<std::shared_ptr<Item>> item;
    if (spellItem) {
        for (const std::shared_ptr<Item> &inventoryItem : leader->items()) {
            if (inventoryItem->tag() == spellItem.value()) {
                item = inventoryItem;
                break;
            }
        }
        if (!cheat && !item) {
            throw std::runtime_error("Item is not in the inventory");
        }
    }

    auto action = newAction<CastSpellAtObjectAction>(
        spell, std::move(target), std::move(item), cheat);

    leader->addAction(std::move(action));
}

void Game::consoleOpenCloseDoor(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 1, "[triggerer_id]");

    auto target = dyn_cast<Door>(getConsoleTargetObject());
    if (!target) {
        throw std::runtime_error("Selected object must be a door");
    }

    auto triggerer_id = args.get<uint32_t>(1);
    std::shared_ptr<Object> triggerer;
    if (triggerer_id) {
        if (uint32_t id = triggerer_id.value()) {
            triggerer = getObjectById(id);
        }
    } else {
        triggerer = getConsoleLeader();
    }

    if (args[0].value() == "opendoor") {
        target->open();
        if (triggerer) {
            target->onOpen(triggerer->id());
        }
    } else {
        target->close();
        // There is no Door::onClose yet
    }
}

void Game::consoleListGames(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");

    // Indices must address the same list consoleLoadGame indexes.
    std::stringstream ss;
    unsigned index = 0;
    const char *newline = "";
    for (const auto &save : savedGames()) {
        ss << newline << "[" << index++ << "] "
           << save.descriptor.directory.filename().string();
        newline = "\n";
    }
    _console.printLine(ss.str());
}

void Game::consoleLoadGame(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "save_id");
    size_t id = *args.get<size_t>(1);
    auto saves = savedGames();
    if (id >= saves.size()) {
        throw std::runtime_error("Invalid savegame id");
    }
    loadGame(saves[id].descriptor);
}

void Game::consoleSaveGame(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, std::numeric_limits<size_t>::max(), "slot [name]");
    auto slot = args.get<uint32_t>(1);
    if (!slot) {
        throw std::runtime_error("Invalid save slot");
    }
    std::string name;
    for (size_t i = 2; i < args.size(); ++i) {
        if (!name.empty()) {
            name += " ";
        }
        name += std::string(*args[i]);
    }
    auto result = requestSave(
        {SaveKind::Developer, *slot, std::move(name), true});
    _console.printLine(result.message);
}

void Game::consoleStartPazaak(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");
    if (!_options.game.developer) {
        _console.printLine("pazaak: developer mode required");
        return;
    }
    if (_pazaakSession) {
        _console.printLine("pazaak: already active");
        return;
    }
    if (!_module || _screen != Screen::InGame) {
        _console.printLine("pazaak: no active in-game module");
        return;
    }

    std::shared_ptr<Object> selected(_pazaakDevelopmentSelectedObjectOverride.lock());
    if (!selected) {
        if (auto area = _module->area()) {
            selected = area->selectedObject();
        }
    }

    std::string opponentName("Pazaak Opponent");
    if (selected && selected->type() == ObjectType::Creature) {
        if (!selected->name().empty()) {
            opponentName = selected->name();
        } else if (!selected->tag().empty()) {
            opponentName = selected->tag();
        }
    }

    if (!startDevelopmentPazaak(opponentName)) {
        _console.printLine("pazaak: development launch failed");
        return;
    }
    _console.printLine("pazaak: development match started against " + opponentName);
}

void Game::consoleMiniGameInfo(const ConsoleArgs &args) {
    auto area = getConsoleArea();
    if (!area->hasMinigame()) {
        _console.printLine("minigame: none");
        return;
    }
    const auto &mg = area->miniGame();
    _console.printLine(str(boost::format("minigame: type=%s camfov=%.1f lataccel=%.3f movePerSec=%.3f inertia=%d bumpPlane=%u doBumping=%d")
                           % minigameTypeName(mg.type)
                           % mg.cameraViewAngle
                           % mg.lateralAccel
                           % mg.movementPerSec
                           % static_cast<int>(mg.useInertia)
                           % mg.bumpPlane
                           % static_cast<int>(mg.doBumping)));
    _console.printLine(str(boost::format("  player: cam=%s track=%s spd=[%.1f,%.1f] accel=%.3f hp=%u models=%zu")
                           % mg.player.cameraResRef
                           % mg.player.trackResRef
                           % mg.player.minimumSpeed
                           % mg.player.maximumSpeed
                           % mg.player.accelSecs
                           % mg.player.hitPoints
                           % mg.player.models.size()));
    _console.printLine(str(boost::format("  tunnel (deg): X=[%.1f,%.1f] Y=[%.1f,%.1f] Z=[%.1f,%.1f]")
                           % mg.player.tunnelXNeg % mg.player.tunnelXPos
                           % mg.player.tunnelYNeg % mg.player.tunnelYPos
                           % mg.player.tunnelZNeg % mg.player.tunnelZPos));
    _console.printLine(str(boost::format("  tracks=%zu enemies=%zu obstacles=%zu")
                           % mg.trackResRefs.size()
                           % mg.enemies.size()
                           % mg.obstacles.size()));
    for (size_t i = 0; i < mg.trackResRefs.size(); ++i) {
        _console.printLine(str(boost::format("    track[%zu] %s") % i % mg.trackResRefs[i]));
    }
    for (size_t i = 0; i < mg.enemies.size(); ++i) {
        const auto &e = mg.enemies[i];
        _console.printLine(str(boost::format("    enemy[%zu] track=%s hp=%u models=%zu")
                               % i % e.trackResRef % e.hitPoints % e.models.size()));
    }
    for (size_t i = 0; i < mg.obstacles.size(); ++i) {
        _console.printLine(str(boost::format("    obstacle[%zu] name=%s") % i % mg.obstacles[i].name));
    }
    if (auto layout = _services.resource.layouts.get(area->name())) {
        auto placement = layout->findTrackByName(mg.player.trackResRef);
        if (placement) {
            const auto &p = placement->get().position;
            _console.printLine(str(boost::format("  lyt tracks=%zu playerTrack=%s pos=[%.1f,%.1f,%.1f]")
                                   % layout->tracks.size() % mg.player.trackResRef % p.x % p.y % p.z));
        } else {
            _console.printLine(str(boost::format("  lyt tracks=%zu playerTrack=%s pos=<not found>")
                                   % layout->tracks.size() % mg.player.trackResRef));
        }
        size_t obstaclesMatched = 0;
        for (const auto &obs : mg.obstacles) {
            if (layout->findObstacleByName(obs.name)) {
                ++obstaclesMatched;
            }
        }
        _console.printLine(str(boost::format("  lyt obstacles=%zu (matched %zu of %zu .are obstacles)")
                               % layout->obstacles.size() % obstaclesMatched % mg.obstacles.size()));
    }
    const auto &sc = mg.player.scripts;
    if (!sc.onCreate.empty() || !sc.onDeath.empty() || !sc.onTrackLoop.empty()) {
        _console.printLine(str(boost::format("  scripts: create=%s death=%s loop=%s damage=%s")
                               % sc.onCreate % sc.onDeath % sc.onTrackLoop % sc.onDamage));
    }
}

void Game::consoleStartSwoop(const ConsoleArgs &args) {
    openSwoopRace();
}

void Game::consoleStopSwoop(const ConsoleArgs &args) {
    // If a lifecycle race is active, return to origin safely; otherwise stop in place.
    exitSwoopRace();
}

void Game::consoleStartSwoopRace(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "module");

    if (_swoopLifecycle.active) {
        _console.printLine("swoop: lifecycle already active");
        return;
    }
    if (!_module) {
        _console.printLine("swoop: no origin module loaded");
        return;
    }
    std::string target(boost::to_lower_copy(std::string(args[1].value())));
    if (_moduleNames.count(target) == 0) {
        _console.printLine("swoop: unknown module '" + target + "'");
        return;
    }

    // Capture origin module/state before transitioning.
    MinigameLifecycle session;
    session.originModule = _module->name();
    session.forcedSuccess = true;
    if (auto leader = _party.getLeader()) {
        session.originPosition = leader->position();
        session.originFacing = leader->getFacing();
        session.haveOrigin = true;
    }

    // Transition to the target swoop module and auto-start the race.
    loadModule(target);
    openSwoopRace();

    if (!_swoopRace.isActive()) {
        // Target loaded but is not a swoop minigame (openSwoopRace printed why).
        // Return to origin so the failed attempt does not strand the player.
        _console.printLine("swoop: lifecycle aborted, returning to origin=" + session.originModule);
        loadModule(session.originModule);
        if (session.haveOrigin) {
            if (auto mod = _module) {
                if (auto area = mod->area()) {
                    if (auto leader = _party.getLeader()) {
                        leader->setPosition(session.originPosition);
                        leader->setFacing(session.originFacing);
                        area->determineObjectRoom(*leader);
                        area->onPartyLeaderMoved(/*roomChanged=*/true);
                    }
                }
            }
        }
        return;
    }

    _swoopLifecycle = session;
    _swoopLifecycle.active = true;
    _console.printLine(str(boost::format("swoop: lifecycle start origin=%s target=%s forcedSuccess=yes")
                           % session.originModule % target));
}

void Game::consoleFinishSwoop(const ConsoleArgs &args) {
    if (!_swoopLifecycle.active) {
        _console.printLine("swoop: no lifecycle race active");
        return;
    }
    finishSwoopLifecycle(/*success=*/true);
}

void Game::consoleSwoopState(const ConsoleArgs &args) {
    if (!_swoopRace.isActive()) {
        _console.printLine("swoop: not active");
        return;
    }
    glm::vec3 pos = _swoopRace.position();
    _console.printLine(str(boost::format("swoop: progress=%.1f finish=%.1f lateral=%.2f speed=%.1f elapsed=%.1f pos=[%.1f,%.1f,%.1f] bounds=[-%.1f,+%.1f] mode=track-progress")
                           % _swoopRace.progress()
                           % _swoopRace.finishProgress()
                           % _swoopRace.lateralOffset()
                           % _swoopRace.speed()
                           % _swoopRace.elapsed()
                           % pos.x % pos.y % pos.z
                           % _swoopRace.lateralLeftBound()
                           % _swoopRace.lateralRightBound()));
}

void Game::consoleStartTurret(const ConsoleArgs &args) {
    openTurret();
}

void Game::consoleStartTurretGame(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "module");

    std::string target(boost::to_lower_copy(std::string(args[1].value())));
    std::string originModule(_module ? _module->name() : "");
    bool alreadyActive = _turretLifecycle.active || _pendingTurret.active || _turret.isActive();

    auto error = validateTurretRequest(target,
                                       originModule,
                                       _moduleNames.count(target) > 0,
                                       alreadyActive);
    if (error != TurretRequestError::None) {
        _console.printLine(str(boost::format("turret: %s") % turretRequestErrorMessage(error)));
        return;
    }

    // Capture the origin now: the transition is deferred, and by the time it
    // runs the current module is already gone.
    _pendingTurret = PendingTurretRequest();
    _pendingTurret.active = true;
    _pendingTurret.targetModule = target;
    _pendingTurret.originModule = originModule;
    if (auto leader = _party.getLeader()) {
        _pendingTurret.originPosition = leader->position();
        _pendingTurret.originFacing = leader->getFacing();
        _pendingTurret.haveOrigin = true;
    }

    // Go through the normal deferred transition so the Type 2 detection in
    // loadNextModule starts the minigame, exactly as a script entry would.
    scheduleModuleTransition(target, "");

    _console.printLine(str(boost::format("turret: lifecycle scheduled origin=%s target=%s")
                           % originModule % target));
}

void Game::consoleStopTurret(const ConsoleArgs &args) {
    // If a lifecycle session is active, return to origin safely; otherwise stop
    // in place.
    exitTurret();
}

void Game::consoleTurretState(const ConsoleArgs &args) {
    if (!_turret.isActive()) {
        _console.printLine("turret: not active");
        return;
    }
    glm::vec3 pos = _turret.position();
    const char *outcome = "in-progress";
    switch (_turret.outcome()) {
    case Turret::Outcome::Won:
        outcome = "won";
        break;
    case Turret::Outcome::Lost:
        outcome = "lost";
        break;
    default:
        break;
    }
    _console.printLine(str(boost::format("turret: pitch=%.1f yaw=%.1f hp=%d/%d enemies=%zu/%zu bullets=%zu elapsed=%.1f pos=[%.1f,%.1f,%.1f] outcome=%s")
                           % glm::degrees(_turret.aim().pitch())
                           % glm::degrees(_turret.aim().yaw())
                           % _turret.hitPoints()
                           % _turret.maxHitPoints()
                           % _turret.enemiesAlive()
                           % _turret.enemyCount()
                           % _turret.bulletCount()
                           % _turret.elapsed()
                           % pos.x % pos.y % pos.z
                           % outcome));
    _console.printLine(str(boost::format("  hud: health=%d(%s) heading=%d alarm=%d contacts=%zu/%zu gauge=%d radar=%d radarChannels=%zu")
                           % _turret.healthState()
                           % turretHealthAnimation(_turret.healthState())
                           % _turret.headingState()
                           % static_cast<int>(_turret.alarmActive())
                           % _turret.contactsLive()
                           % kTurretContactCount
                           % static_cast<int>(_turret.haveHealthHud())
                           % static_cast<int>(_turret.haveRadarHud())
                           % _turret.radarChannelCount()));
}

void Game::consoleShowImGui(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "1|0");
    bool show = args.get<int>(1).value();
    _showImGui = show;
}

void Game::consoleShowPath(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "1|0");
    bool show = args.get<int>(1).value();
    setShowPath(show);
}

} // namespace game

} // namespace reone
