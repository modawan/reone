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

#include "reone/game/minigame.h"

#include "reone/audio/context.h"
#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/cutsceneattack.h"
#include "reone/game/action/startconversation.h"
#include "reone/game/combat.h"
#include "reone/game/d20/spells.h"
#include "reone/game/debug.h"
#include "reone/game/di/services.h"
#include "reone/game/gui/sounds.h"
#include "reone/game/location.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"
#include "reone/game/room.h"
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
#include "reone/graphics/uniforms.h"
#include "reone/gui/gui.h"
#include "reone/movie/format/bikreader.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/director.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/format/erfreader.h"
#include "reone/resource/format/erfwriter.h"
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
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/render/pipeline.h"
#include "reone/script/di/services.h"
#include "reone/system/binarywriter.h"
#include "reone/system/clock.h"
#include "reone/system/di/services.h"
#include "reone/system/exception/validation.h"
#include "reone/system/fileutil.h"
#include "reone/system/logutil.h"
#include "reone/system/smallset.h"
#include "reone/system/threadutil.h"

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::movie;
using namespace reone::resource;
using namespace reone::scene;
using namespace reone::script;

namespace reone {

namespace game {

static constexpr char kDeveloperOverlayToggleHelp[] = "Ctrl+Shift+D";
static constexpr char kDeveloperTriggerToggleHelp[] = "Ctrl+Shift+T";
static constexpr char kDeveloperActorToggleHelp[] = "Ctrl+Shift+A";
static constexpr char kDeveloperActorLongToggleHelp[] = "Ctrl+Shift+L";
static constexpr char kDeveloperWatchToggleHelp[] = "Ctrl+Shift+W";
static constexpr float kDeveloperActorLabelDistance = 32.0f;

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
    default:
        return "Unknown";
    }
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
    initLocalServices();
    setSceneSurfaces();
    setCursorType(CursorType::Default);

    _moduleNames = _services.resource.director.moduleNames();
    _saveNames = _services.resource.director.saveNames();

    playVideo("legal");
    openMainMenu();
}

void Game::registerConsoleCommand(std::string name, std::string description, ConsoleCommandHandler handler) {
    _console.registerCommand(name, description, std::bind(handler, this, std::placeholders::_1));
}

void Game::initConsole() {
    registerConsoleCommand("info", "information on selected object", &Game::consoleInfo);
    registerConsoleCommand("listglobals", "list global variables", &Game::consoleListGlobals);
    registerConsoleCommand("listlocals", "list local variables", &Game::consoleListLocals);
    registerConsoleCommand("runscript", "run script", &Game::consoleRunScript);
    registerConsoleCommand("listanim", "list animations of selected object", &Game::consoleListAnim);
    registerConsoleCommand("playanim", "play animation on selected object", &Game::consolePlayAnim);
    registerConsoleCommand("warp", "warp to a module", &Game::consoleWarp);
    registerConsoleCommand("kill", "kill selected object", &Game::consoleKill);
    registerConsoleCommand("additem", "add item to selected object", &Game::consoleAddItem);
    registerConsoleCommand("givexp", "give experience to selected creature", &Game::consoleGiveXP);
    registerConsoleCommand("givegold", "give credits to the party", &Game::consoleGiveGold);
    registerConsoleCommand("showaabb", "toggle rendering AABB", &Game::consoleShowAABB);
    registerConsoleCommand("showwalkmesh", "toggle rendering walkmesh", &Game::consoleShowWalkmesh);
    registerConsoleCommand("showtriggers", "toggle rendering triggers", &Game::consoleShowTriggers);
    registerConsoleCommand("spawncreature", "spawn a creature", &Game::consoleSpawnCreature);
    registerConsoleCommand("spawncompanion", "spawn a companion", &Game::consoleSpawnCompanion);
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
    registerConsoleCommand("startconversation", "starts a conversation with the selected object", &Game::consoleStartConversation);
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
    registerConsoleCommand("minigameinfo", "print minigame metadata for current area", &Game::consoleMiniGameInfo);
    registerConsoleCommand("startswoop", "enter the developer swoop race mode for the current area", &Game::consoleStartSwoop);
    registerConsoleCommand("stopswoop", "exit the developer swoop race mode", &Game::consoleStopSwoop);
    registerConsoleCommand("swoopstate", "print the current swoop race progress/lateral state", &Game::consoleSwoopState);
    registerConsoleCommand("startswooprace", "enter a swoop module from the current one and auto-start the race", &Game::consoleStartSwoopRace);
    registerConsoleCommand("finishswoop", "finish the lifecycle swoop race (forced success) and return to origin", &Game::consoleFinishSwoop);
}

void Game::initLocalServices() {
    auto routines = std::make_unique<Routines>(_gameId, this, &_services);
    routines->init();
    _routines = std::move(routines);

    _scriptRunner = std::make_unique<ScriptRunner>(*_routines, _services.resource.scripts);

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
        default:
            break;
        }
    }

    return false;
}

void Game::update(float frameTime) {
    float dt = frameTime * _gameSpeed;
    if (_movie) {
        updateMovie(dt);
        return;
    }
    updateMusic();

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
            _console.printLine(str(boost::format("swoop: auto-finish progress=%.1f finish=%.1f forcedSuccess=yes returning=%s")
                                   % _swoopRace.progress()
                                   % _swoopRace.finishProgress()
                                   % _swoopLifecycle.originModule));
            finishSwoopLifecycle(/*success=*/true);
        }
    }

    bool updModule = !_movie && _module && (_screen == Screen::InGame || _screen == Screen::Conversation);
    if (updModule && !_paused) {
        _module->update(dt);
        _combat.update(dt);
    }

    auto gui = getScreenGUI();
    if (gui) {
        gui->update(dt);
    }
    updateSceneGraph(dt);
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

void Game::loadModule(const std::string &name, std::string entry, bool fromSave) {
    info("Loading module '" + name + "'");

    // Tear down an active race before the current area (and its camera/scene)
    // is unloaded, so no dangling references survive the transition.
    if (_swoopRace.isActive()) {
        _swoopRace.stop();
        _cameraType = _savedCameraType;
    }
    // A direct module load (e.g. warp) while a lifecycle race is pending means
    // the player navigated away; abandon the pending return. (Lifecycle-managed
    // loads clear the session beforehand, so this only fires on external loads.)
    if (_swoopLifecycle.active) {
        _swoopLifecycle = SwoopLifecycle();
    }

    if (_screen == Screen::Conversation && _conversation) {
        _conversation->cleanupForModuleTransition();
    }

    withLoadingScreen("load_" + name, [this, &name, &entry, fromSave]() {
        loadInGameMenus();

        try {
            if (_module) {
                _module->area()->runOnExitScript();
                _module->area()->unloadParty();
            }

            _services.resource.director.onModuleLoad(name);

            if (_loadScreen) {
                _loadScreen->setProgress(50);
            }
            render();

            _services.scene.graphs.get(kSceneMain).clear();

            auto maybeModule = _loadedModules.find(name);
            if (maybeModule != _loadedModules.end()) {
                _module = maybeModule->second;
                _module->activate();
            } else {
                _module = newModule();
                _objectById.insert(std::make_pair(_module->id(), _module));

                std::shared_ptr<Gff> ifo(_services.resource.gffs.get("module", ResType::Ifo));
                if (!ifo) {
                    throw ResourceNotFoundException("Module IFO not found");
                }

                _module->load(name, *ifo, fromSave);
                _loadedModules.insert(std::make_pair(name, _module));
            }

            if (_party.isEmpty()) {
                if (!loadParty()) {
                    loadDefaultParty();
                }
            }

            _module->loadParty(entry, fromSave);

            info("Module '" + name + "' loaded successfully");

            if (_loadScreen) {
                _loadScreen->setProgress(100);
            }
            render();

            std::string musicName(_module->area()->music());
            playMusic(musicName);

            //_ticks = _services.system.clock.ticks();
            openInGame();
        } catch (const std::exception &e) {
            error("Failed loading module '" + name + "': " + std::string(e.what()));
        }
    });
}

void Game::loadGame(std::string_view name) {
    info(str(boost::format("Loading savegame '%s'") % name));

    _services.resource.director.onGameLoad(name);
    std::shared_ptr<Gff> globalVars(_services.resource.gffs.get("globalvars", ResType::Res));
    if (!globalVars) {
        throw ResourceNotFoundException("globalvars.res not found");
    }
    deserializeGlobalVariables(*globalVars);

    std::shared_ptr<Gff> saveInfo(_services.resource.gffs.get("savenfo", ResType::Res));
    if (!saveInfo) {
        throw ResourceNotFoundException("saveinfo.res not found");
    }

    NFO nfo = resource::parseNFO(*saveInfo);
    loadModule(nfo.lastModule, /*entry=*/"", /*fromSave=*/true);

    // Inventory is serialized into a separate file. Once the player is loaded,
    // deserialize it into the player's inventory.
    if (auto inventoryGff = _services.resource.gffs.get("inventory", ResType::Res)) {
        deserializeInventory(*inventoryGff);
    }
}

void Game::deserializeGlobalVariables(resource::Gff &gvtGff) {
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
        float facing = glm::half_pi<float>() - glm::atan(rot.x, rot.y);
        setGlobalLocation(name, std::make_shared<Location>(pos, facing));
    }
}

void Game::deserializeInventory(resource::Gff &inventoryGff) {
    std::shared_ptr<Creature> player = _party.player();
    if (!player) {
        return;
    }

    for (const auto &itemGff : inventoryGff.getList("ItemList")) {
        std::shared_ptr<Item> item = newItem();
        item->deserialize(*itemGff);
        player->addItem(item);
    }
}

bool Game::loadParty() {
    std::shared_ptr<Gff> ifo(_services.resource.gffs.get("module", ResType::Ifo));
    if (!ifo) {
        throw ResourceNotFoundException("module.ifo not found");
    }

    const auto &players = ifo->getList("Mod_PlayerList");
    if (players.empty()) {
        return false;
    }

    std::shared_ptr<Creature> player = newCreature();
    _objectById.insert(std::make_pair(player->id(), player));
    player->deserialize(*players.front());
    player->setTag(kObjectTagPlayer);
    _party.addMember(kNpcPlayer, player);
    _party.setPlayer(player);

    return true;
}

void Game::loadDefaultParty() {
    std::string member1, member2, member3;
    _party.defaultMembers(member1, member2, member3);

    if (!member1.empty()) {
        std::shared_ptr<Creature> player = newCreature();
        _objectById.insert(std::make_pair(player->id(), player));
        player->loadFromBlueprint(member1);
        player->setTag(kObjectTagPlayer);
        player->setImmortal(true);
        _party.addMember(kNpcPlayer, player);
        _party.setPlayer(player);
    }
    if (!member2.empty()) {
        std::shared_ptr<Creature> companion = newCreature();
        _objectById.insert(std::make_pair(companion->id(), companion));
        companion->loadFromBlueprint(member2);
        companion->setImmortal(true);
        companion->equip("g_w_dblsbr001");
        _party.addMember(0, companion);
    }
    if (!member3.empty()) {
        std::shared_ptr<Creature> companion = newCreature();
        _objectById.insert(std::make_pair(companion->id(), companion));
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
    if (!_module) {
        return;
    }
    auto &scene = _services.scene.graphs.get(kSceneMain);
    auto &output = scene.render({_options.graphics.width, _options.graphics.height});
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
    if (_cursor && !_relativeMouseMode) {
        _cursor->render();
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
    bool wasLifecycleActive = _swoopLifecycle.active;

    loadModule(_nextModule, _nextEntry);

    _nextModule.clear();
    _nextEntry.clear();

    // Vanilla K1 swoop entry: a dialogue node fires a script that calls
    // StartNewModule("<*mg>"); the engine auto-enters the minigame on load (no
    // dedicated start routine). Route that generic transition through the
    // forced-success lifecycle harness when the loaded area is a swoop minigame.
    if (wasLifecycleActive || _swoopLifecycle.active) {
        return;
    }
    if (originModule.empty() || boost::iequals(originModule, target)) {
        return;
    }
    auto mod = _module;
    if (!mod || !mod->area() || !mod->area()->hasMinigame()) {
        return;
    }
    if (mod->area()->miniGame().type != MinigameType::SwoopRace) {
        return;
    }

    openSwoopRace();
    if (_swoopRace.isActive()) {
        _swoopLifecycle = SwoopLifecycle();
        _swoopLifecycle.active = true;
        _swoopLifecycle.haveOrigin = haveOrigin;
        _swoopLifecycle.originModule = originModule;
        _swoopLifecycle.originPosition = originPosition;
        _swoopLifecycle.originFacing = originFacing;
        _swoopLifecycle.forcedSuccess = true;
        _console.printLine(str(boost::format("swoop: script lifecycle start origin=%s target=%s forcedSuccess=yes hook=StartNewModule")
                               % originModule % target));
    }
}

void Game::stopMovement() {
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

void Game::setGlobalBoolean(const std::string &name, bool value) {
    _globalBooleans[name] = value;
}

void Game::setGlobalNumber(const std::string &name, int value) {
    _globalNumbers[name] = value;
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

void Game::openMainMenu() {
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
    std::vector<std::shared_ptr<ModelSceneNode>> bikeNodes;
    std::vector<std::string> modelDiag;
    bool anyMissing = false;
    for (const auto &resRef : mg.player.modelResRefs) {
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
        sceneGraph.addRoot(node);
        bikeNodes.push_back(std::move(node));
        modelDiag.push_back(resRef + " loaded");
    }

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
    size_t loadedCount = bikeNodes.size();
    _swoopRace.start(mg, camera, std::move(bikeNodes), trackFrame.position, trackFrame.facing, finishProgress);

    _cameraType = CameraType::FirstPerson;
    setRelativeMouseMode(false);
    changeScreen(Screen::SwoopRace);

    // Hide the normal party while the minigame runs. Vanilla does not add the
    // party to the scene in a minigame module (the swoop bike actor represents
    // the player); otherwise the frozen-but-rendered party leader appears on the
    // track. Restored on exit (or naturally re-spawned on the return module).
    setPartyVisible(false);

    _console.printLine(str(boost::format("swoop: started type=%s track=%s models=%zu loaded=%zu camera=chase movePerSec=%.0f lataccel=%.0f camfov=%.0f")
                           % minigameTypeName(mg.type)
                           % mg.player.trackResRef
                           % mg.player.modelResRefs.size()
                           % loadedCount
                           % mg.movementPerSec
                           % mg.lateralAccel
                           % mg.cameraViewAngle));

    // Track frame: lyt-track/track-model/fallback mode and how the start frame
    // was chosen (see deriveSwoopTrackFrame).
    std::string trackLabel(mg.player.trackResRef.empty() ? std::string("<none>") : mg.player.trackResRef);
    if (trackFrame.mode == "fallback") {
        _console.printLine(str(boost::format("swoop: track=%s mode=fallback reason=%s%s")
                               % trackLabel
                               % trackFrame.reason
                               % (trackFrame.info.empty() ? std::string() : (" " + trackFrame.info))));
    } else {
        _console.printLine(str(boost::format("swoop: track=%s mode=%s %s startFacing=%.2f")
                               % trackLabel
                               % trackFrame.mode
                               % trackFrame.info
                               % trackFrame.facing));
    }

    // Movement model: track-relative progress + lateral strafe (no turning).
    _console.printLine(str(boost::format("swoop: movement=track-progress strafeOnly=yes progressAxis=trackForward lateralAxis=trackRight anim=deferred start=[%.1f,%.1f,%.1f] facing=%.2f finish=%.1f")
                           % trackFrame.position.x % trackFrame.position.y % trackFrame.position.z
                           % trackFrame.facing
                           % finishProgress));

    // Lateral bounds chosen for the strafe (see SwoopRace::computeLateralBounds).
    _console.printLine(str(boost::format("swoop: bounds lateral=[-%.1f,+%.1f] source=%s tunnelX=[%.1f,%.1f]")
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
        _console.printLine(str(boost::format("swoop: lyt obstacles=%zu areObstacles=%zu matched=%zu")
                               % layout->obstacles.size() % mg.obstacles.size() % areMatched));
        constexpr size_t kMaxObstacleDiag = 6;
        for (size_t i = 0; i < layout->obstacles.size() && i < kMaxObstacleDiag; ++i) {
            const auto &obs = layout->obstacles[i];
            glm::vec3 d = obs.position - trackFrame.position;
            float progress = glm::dot(d, fwd);
            float lateral = glm::dot(d, right);
            _console.printLine(str(boost::format("  swoopobj[%zu] name=%s pos=[%.1f,%.1f,%.1f] progress=%.1f lateral=%.1f type=obstacle")
                                   % i % obs.name
                                   % obs.position.x % obs.position.y % obs.position.z
                                   % progress % lateral));
        }
    }

    // Print the per-model breakdown when nothing loaded or a load failed; it is
    // a one-shot dev diagnostic, so avoid spam on the common success path.
    if (loadedCount == 0 || anyMissing) {
        for (size_t i = 0; i < modelDiag.size(); ++i) {
            _console.printLine(str(boost::format("  model[%zu]=%s") % i % modelDiag[i]));
        }
    }
}

void Game::closeSwoopRace() {
    if (!_swoopRace.isActive()) {
        _console.printLine("swoop: not active");
        return;
    }
    auto &sceneGraph = _services.scene.graphs.get(kSceneMain);
    for (const auto &bikeNode : _swoopRace.bikeNodes()) {
        if (bikeNode) {
            sceneGraph.removeRoot(*bikeNode);
        }
    }
    _swoopRace.stop();
    setPartyVisible(true);
    _cameraType = _savedCameraType;
    setRelativeMouseMode(_cameraType == CameraType::FirstPerson);
    openInGame();
    _console.printLine("swoop: stopped");
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
    SwoopLifecycle session = _swoopLifecycle;
    _swoopLifecycle = SwoopLifecycle();
    std::string raceModule = _module ? _module->name() : "";

    // Stop the race (removes bike models, restores camera/FOV/input, screen).
    closeSwoopRace();

    // Return to the originating module. Prefer the vanilla race-return waypoint
    // (e.g. Taris heartbeat returns to tar_m03af at tar03_wpmechanic) so the
    // leader lands on the authored return spot and naturally occupies the
    // post-race trigger; fall back to the saved pre-race position otherwise.
    loadModule(session.originModule);
    if (auto mod = _module) {
        if (auto area = mod->area()) {
            if (auto leader = _party.getLeader()) {
                glm::vec3 pos = session.originPosition;
                float facing = session.originFacing;
                bool havePlacement = session.haveOrigin;

                std::string returnWaypoint = swoopReturnWaypoint(raceModule);
                if (!returnWaypoint.empty()) {
                    if (auto wp = area->getObjectByTag(returnWaypoint)) {
                        pos = wp->position();
                        facing = wp->getFacing();
                        havePlacement = true;
                        _console.printLine(str(boost::format("swoop: return waypoint=%s pos=[%.1f,%.1f,%.1f]")
                                               % returnWaypoint % pos.x % pos.y % pos.z));
                    }
                }

                if (havePlacement) {
                    leader->setPosition(pos);
                    leader->setFacing(facing);
                    area->determineObjectRoom(*leader);
                    area->onPartyLeaderMoved(/*roomChanged=*/true);
                }
            }
        }
    }

    if (success) {
        applySwoopForcedSuccessResult(raceModule);
    }

    _console.printLine(str(boost::format("swoop: finished forcedSuccess=%s returning=%s")
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
    // TAR_SWOOP_RUN = TRUE (the confirmed value) so postswoop passes its guard,
    // and a best-possible finish time (0) so it computes a win. The win state
    // (Tar_SwoopStatus) and the scene are then produced by the vanilla trigger
    // ->postswoop chain in proper context (see Area::updateLeaderTriggerOccupancy
    // and the return-waypoint placement). No result/winner globals are set here.
    // Other planets are not yet wired.
    if (!boost::iequals(raceModule, "tar_m03mg")) {
        return;
    }
    setGlobalBoolean("TAR_SWOOP_RUN", true);
    setGlobalNumber("TAR_SWOOP_MIN", 0);
    setGlobalNumber("TAR_SWOOP_SEC", 0);
    setGlobalNumber("TAR_SWOOP_MSEC", 0);

    _console.printLine("swoop: result forcedSuccess=yes planet=taris TAR_SWOOP_RUN=1 time=TAR_SWOOP_MIN/SEC/MSEC=0 handoff=trigger:tar03_postrace->k_ptar_postswoop mechanism=leader-trigger-occupancy");
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
    _conversation->resume();
}

void Game::pauseConversation() {
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
    case Screen::SwoopRace:
        return nullptr; // race skeleton has no HUD yet
    default:
        return nullptr;
    }
}

void Game::setBarkBubbleText(std::string text, float duration) {
    _hud->barkBubble().setBarkText(text, duration);
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
    object->addItem(std::string(args[1].value()), stackSize);
}

void Game::consoleGiveXP(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "amount");
    auto creature = getConsoleTargetCreature();
    creature->giveXP(args.get<int>(1).value());
}

void Game::consoleGiveGold(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "amount");
    _party.giveGold(args.get<int>(1).value());
    _console.printLine(str(boost::format("party gold: %d") % _party.gold()));
}

void Game::consoleWarp(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "module");
    loadModule(std::string(args[1].value()));
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
        creature = std::make_shared<Creature>(id.value(), kSceneMain, *this, _services);
        _objectById.insert(std::make_pair(creature->id(), creature));
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
        companion = std::make_shared<Creature>(id.value(), kSceneMain, *this, _services);
        _objectById.insert(std::make_pair(companion->id(), companion));
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

    for (auto [id, object] : _objectById) {
        if (object->tag() == tag) {
            getConsoleArea()->selectObject(object, /*force=*/true);
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
    consoleCheckUsage(args, 0, 0, "");

    auto leader = getConsoleLeader();
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

    std::stringstream ss;
    unsigned index = 0;
    const char *newline = "";
    for (const std::string &name : _saveNames) {
        ss << newline << "[" << index++ << "] " << name;
        newline = "\n";
    }
    _console.printLine(ss.str());
}

void Game::consoleLoadGame(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "save_id");
    size_t id = *args.get<size_t>(1);
    const auto &_saveNames = _services.resource.director.saveNames();
    if (id >= _saveNames.size()) {
        throw std::runtime_error("Invalid savegame id");
    }
    auto name = _saveNames.begin();
    std::advance(name, id);
    loadGame(*name);
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
                           % mg.player.modelResRefs.size()));
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
                               % i % e.trackResRef % e.hitPoints % e.modelResRefs.size()));
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
    SwoopLifecycle session;
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

} // namespace game

} // namespace reone
