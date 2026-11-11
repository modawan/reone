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

#include "reone/game/gui/mainmenu.h"

#include <algorithm>

#include <boost/format.hpp>

#include "reone/audio/mixer.h"
#include "reone/audio/source.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/party.h"
#include "reone/game/object/creature.h"
#include "reone/game/types.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/model.h"
#include "reone/gui/guis.h"
#include "reone/gui/sceneinitializer.h"
#include "reone/resource/2da.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/provider/movies.h"
#include "reone/resource/strings.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/camera.h"
#include "reone/scene/types.h"
#include "reone/system/logutil.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace game {

static constexpr float kKotorModelSize = 1.4f;

// KotOR's movie list opens with a row that runs the credits, to their music.
static constexpr char kCreditsRow[] = "credits";
static constexpr char kCreditsMusic[] = "credits";
static constexpr int kCreditsStrRef = 47918;

// A click on Next is not answered again for a second; a stopped song fades
// out over half a second.
static constexpr float kNextSongHold = 1.0f;
static constexpr float kSongFadeTime = 0.5f;

MainMenu::MainMenu(Game &game, ServicesView &services) :
    GameGUI(game, services) {

    if (_game.isTSL()) {
        _resRef = "mainmenu8x6_p";
        _musicResRef = "mus_sion";
    } else {
        _resRef = "mainmenu16x12";
        _musicResRef = "mus_theme_cult";
    }
}

void MainMenu::preload(IGUI &gui) {
    GameGUI::preload(gui);
    gui.setResolution(800, 600);
}

void MainMenu::onGUILoaded() {
    if (!_game.isTSL()) {
        loadBackground(BackgroundType::Menu);
    }

    bindControls();

    _controls.LB_MODULES->setVisible(false);
    _controls.LBL_NEWCONTENT->setVisible(false);
    _controls.LBL_BW->setVisible(false);
    _controls.LBL_LUCAS->setVisible(false);

    if (_controls.BTN_MOREGAMES) {
        _controls.BTN_MOREGAMES->setVisible(false);
    }
    if (_controls.BTN_TSLRCM) {
        _controls.BTN_TSLRCM->setVisible(false);
    }

    // Hide warp button in developer mode
    if (!_game.options().game.developer) {
        _controls.BTN_WARP->setVisible(false);
    }

    _controls.BTN_NEWGAME->setOnClick([this]() {
        _game.startCharacterGeneration();
    });
    _controls.BTN_LOADGAME->setOnClick([this]() {
        _game.openSaveLoad(SaveLoadMode::LoadFromMainMenu);
    });
    _controls.BTN_OPTIONS->setOnClick([this]() {
        _game.openMainMenuOptions();
    });
    _controls.BTN_MOVIES->setOnClick([this]() {
        openMovies();
    });
    loadMoviesPage();
    if (_game.isTSL()) {
        _controls.BTN_MUSIC->setOnClick([this]() {
            openMusic();
        });
        loadMusicPage();
    }
    _controls.BTN_EXIT->setOnClick([this]() {
        _game.quit();
    });
    _controls.BTN_WARP->setOnClick([this]() {
        startModuleSelection();
    });
    _controls.LB_MODULES->setOnItemClick([this](const std::string &item) {
        onModuleSelected(item);
    });

    setup3DView();
    configureButtons();
}

void MainMenu::configureButtons() {
    setButtonColors(*_controls.BTN_EXIT);
    setButtonColors(*_controls.BTN_LOADGAME);
    setButtonColors(*_controls.BTN_MOVIES);
    setButtonColors(*_controls.BTN_NEWGAME);
    setButtonColors(*_controls.BTN_OPTIONS);

    if (_game.isTSL()) {
        setButtonColors(*_controls.BTN_MUSIC);
    }
}

void MainMenu::setButtonColors(Control &control) {
    control.setTextColor(_baseColor);
    control.setHilightColor(_hilightColor);
}

void MainMenu::setup3DView() {
    if (_game.isTSL()) {
        refreshScene();
        return;
    }

    auto &sceneGraph = _services.scene.graphs.get(kSceneMainMenu);
    const Control::Extent &extent = _controls.LBL_3DVIEW->extent();
    float aspect = extent.width / static_cast<float>(extent.height);

    SceneInitializer(sceneGraph)
        .aspect(aspect)
        .depth(kDefaultClipPlaneNear, 10.0f)
        .modelSupplier(bind(&MainMenu::getKotorModel, this, std::placeholders::_1))
        .modelScale(kKotorModelSize)
        .cameraFromModelNode("camerahook")
        .invoke();

    _controls.LBL_3DVIEW->setSceneName(kSceneMainMenu);
}

void MainMenu::refreshScene() {
    if (!_game.isTSL() || !_controls.LBL_3DVIEW) return;

    if (_controls.LB_MODULES && _controls.LB_MODULES->isVisible()) {
        _controls.LB_MODULES->setVisible(false);
        for (auto &button : {_controls.BTN_EXIT, _controls.BTN_LOADGAME,
                 _controls.BTN_MOVIES, _controls.BTN_NEWGAME, _controls.BTN_OPTIONS,
                 _controls.BTN_MUSIC}) {
            button->setVisible(true);
        }
        _controls.BTN_WARP->setVisible(_game.options().game.developer);
        _controls.LBL_GAMELOGO->setVisible(true);
    }
    _controls.LBL_3DVIEW->setVisible(true);

    // clear() drops roots but keeps the node arena. Replace only this scene so
    // repeated returns also release obsolete emitters, cameras and listeners.
    _leader.reset();
    _services.scene.graphs.reset(kSceneMainMenu);
    auto &sceneGraph = _services.scene.graphs.get(kSceneMainMenu);
    const auto &state = _game.options().game.menuPresentation;
    const auto &extent = _controls.LBL_3DVIEW->extent();
    auto model = _services.resource.models.get(state.modelResRef(true));
    if (!model) return;
    auto environment = sceneGraph.newModel(*model, ModelUsage::GUI);
    if (!environment) return;
    SceneInitializer initializer {sceneGraph};
    initializer
        .aspect(extent.width / static_cast<float>(extent.height))
        .depth(0.1f, 10000.0f)
        .perspective(glm::radians(22.7259998f))
        .modelScale(1.0f)
        .prewarmEmitters()
        .modelSupplier([environment](ISceneGraph &) { return environment; });
    if (auto room = _services.resource.models.get("gui3d_room")) {
        initializer.room(sceneGraph.newModel(*room, ModelUsage::Room));
    }
    initializer.invoke();
    // mainmenu03 also has a character-local camerahook. Select the scene's
    // direct child by number, avoiding the duplicate-name lookup entirely.
    if (auto camera = sceneGraph.camera()) {
        for (const auto &child : model->rootNode()->children()) {
            if (child->name() == "camerahook") {
                environment->getNodeByNumber(child->number())->addChild(camera->get());
                break;
            }
        }
    }
    environment->playAnimation("default", nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
    _controls.LBL_3DVIEW->setSceneName(kSceneMainMenu);

    if (MenuPresentation::validateSelector(state.selector) == 4 && state.leader) {
        try {
            _leader = _game.newPresentationCreature(kSceneMainMenu);
            _leader->setPresentation(*state.leader);
            auto placement = environment->getNodeByName("cutscenedummy");
            _leader->setPosition(placement ? glm::vec3(placement->absoluteTransform()[3]) : glm::vec3(0.0f, -1.0f, 0.0f));
            // The authored menu actor faces -Y, toward the camera.
            _leader->setFacing(glm::pi<float>());
            _leader->loadAppearance();
            auto node = std::static_pointer_cast<ModelSceneNode>(_leader->sceneNode());
            if (node) {
                // Resolve through the supermodel and propagate to the head,
                // without any runtime action or AI state.
                auto animation = node->model().getAnimation("evil") ? "evil" : "pause1";
                if (std::string_view(animation) != "evil") {
                    warn("Main menu leader model has no evil animation");
                }
                node->playAnimation(animation, nullptr,
                    AnimationProperties::fromFlags(AnimationFlags::loop | AnimationFlags::propagate));
                sceneGraph.addRoot(node);
            } else {
                _leader.reset();
            }
        } catch (const std::exception &ex) {
            _leader.reset();
            warn(std::string("Could not build main menu leader: ") + ex.what());
        }
    }
}

std::shared_ptr<ModelSceneNode> MainMenu::getKotorModel(ISceneGraph &sceneGraph) {
    auto model = _services.resource.models.get("mainmenu");
    if (!model) {
        return nullptr;
    }
    return sceneGraph.newModel(*model, ModelUsage::GUI);
}

void MainMenu::startModuleSelection() {
    loadModuleNames();

    _controls.LB_MODULES->setVisible(true);
    _controls.BTN_EXIT->setVisible(false);
    _controls.BTN_LOADGAME->setVisible(false);
    _controls.BTN_MOVIES->setVisible(false);
    _controls.BTN_NEWGAME->setVisible(false);
    _controls.BTN_OPTIONS->setVisible(false);
    _controls.BTN_WARP->setVisible(false);
    _controls.LBL_3DVIEW->setVisible(false);
    _controls.LBL_GAMELOGO->setVisible(false);

    if (_game.isTSL()) {
        _controls.BTN_MUSIC->setVisible(false);
    } else {
        _controls.LBL_MENUBG->setVisible(false);
    }
}

void MainMenu::loadModuleNames() {
    for (auto &module : _game.moduleNames()) {
        ListBox::Item item;
        item.tag = module;
        item.text = module;
        _controls.LB_MODULES->addItem(std::move(item));
    }
}

// Picking a module sets its loading-screen music.
void MainMenu::onModuleSelected(const std::string &name) {
    _game.setLoadMusicForModule(name);
    _game.loadModule(name);
}

bool MainMenu::handle(const input::Event &event) {
    if (!_page) {
        return GameGUI::handle(event);
    }
    // An open page takes every input; Escape closes it as Close does.
    if (event.type == input::EventType::KeyDown && event.key.code == input::KeyCode::Escape) {
        closePage();
        return true;
    }
    _page->handle(event);
    return true;
}

void MainMenu::update(float dt) {
    GameGUI::update(dt);
    if (_fadingSong) {
        _fadeTime += dt;
        if (_fadeTime < kSongFadeTime) {
            _fadingSong->setGainScale(1.0f - _fadeTime / kSongFadeTime);
        } else {
            _fadingSong->stop();
            _fadingSong.reset();
        }
    }
    if (!_page) {
        return;
    }
    _page->update(dt);
    if (_page != _musicGUI.get()) {
        return;
    }
    _nextHold = std::max(0.0f, _nextHold - dt);
    // The row the player moves to, by pointer or key, is the chosen song.
    const int row = _musicControls.LB_MUSIC->selectedItemIndex();
    if (row >= 0 && row != _chosenSong) {
        chooseSong(row);
    }
    // A song that ends gives way to the next unless it loops.
    if (_playing && !_looping && (!_song || !_song->isPlaying())) {
        playNextSong();
    }
}

void MainMenu::render() {
    GameGUI::render();
    if (_page) {
        _page->render();
    }
}

std::shared_ptr<IGUI> MainMenu::loadPage(const std::string &name) {
    // The pages keep the layout size of their game, not the title screen's.
    auto gui = _services.gui.guis.get(guiResRef(name), [this](IGUI &gui) { GameGUI::preload(gui); });
    if (!gui) {
        throw ResourceNotFoundException("GUI not found: " + guiResRef(name));
    }
    return gui;
}

// The menu's music fades out as a page opens, as it does for a new game, and
// starts again when the page closes.
void MainMenu::openPage(IGUI &page) {
    _game.fadeOutMenuMusic();
    _page = &page;
}

void MainMenu::closePage() {
    if (_page == _musicGUI.get()) {
        if (_song) {
            _song->stop();
            _song.reset();
        }
        _playing = false;
        // The player's volume was for the player alone.
        _services.audio.mixer.setVolume(AudioType::Music, _optionsMusicVolume);
    }
    _page->clearSelection();
    _page = nullptr;
    _game.playMusic(_musicResRef);
}

void MainMenu::loadMoviesPage() {
    _moviesGUI = loadPage("titlemovie");
    auto find = [this](const std::string &tag) { return _moviesGUI->findControl(tag); };
    auto &controls = _movieControls;
    controls.BTN_BACK = std::static_pointer_cast<Button>(find("BTN_BACK"));
    controls.LB_MOVIES = std::static_pointer_cast<ListBox>(find("LB_MOVIES"));
    if (_game.isTSL()) {
        controls.LBL_BAR1 = std::static_pointer_cast<Label>(find("LBL_BAR1"));
        controls.LBL_BAR2 = std::static_pointer_cast<Label>(find("LBL_BAR2"));
        controls.LBL_UNLOCKED_VALUE = std::static_pointer_cast<Label>(find("LBL_UNLOCKED_VALUE"));
        fillK2SectionStrip(controls.LBL_BAR1, controls.LBL_BAR2);
        enableK2ButtonBodyFill(controls.BTN_BACK);
    }
    useK1ListEntryColors(controls.LB_MOVIES->protoItem());

    controls.LB_MOVIES->setOnItemClick([this](const std::string &name) {
        if (!_game.isTSL() && name == kCreditsRow) {
            _game.startCreditSequence(false, kCreditsMusic);
        } else {
            _game.playVideo(name);
        }
    });
    controls.BTN_BACK->setOnClick([this]() {
        closePage();
    });
}

// The list offers each movie of the game that the movie table names, and that
// the table always shows or the player has seen, in the table's order and
// otherwise by name. A row without a name in the table is captioned with the
// movie's own.
void MainMenu::openMovies() {
    auto &list = *_movieControls.LB_MOVIES;
    list.clearItems();

    auto table = _services.resource.twoDas.get("movies");
    const auto &shown = _game.options().game.moviesShown;
    std::vector<std::pair<int, ListBox::Item>> rows;
    for (const auto &name : _services.resource.movies.names()) {
        const int row = table->indexByLabel(name);
        if (row < 0) {
            continue;
        }
        if (table->getInt(row, "alwaysshow") != 1 && shown.count(name) == 0) {
            continue;
        }
        const int strRef = table->getInt(row, "strrefname");
        ListBox::Item item;
        item.tag = name;
        item.text = strRef != 0 ? _services.resource.strings.getText(strRef) : name;
        rows.emplace_back(table->getInt(row, "order"), std::move(item));
    }
    std::stable_sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
        return a.first < b.first;
    });

    if (!_game.isTSL()) {
        ListBox::Item credits;
        credits.tag = kCreditsRow;
        credits.text = _services.resource.strings.getText(kCreditsStrRef);
        list.addItem(std::move(credits));
    } else {
        _movieControls.LBL_UNLOCKED_VALUE->setTextMessage(
            str(boost::format("%d / %d") % rows.size() % table->getRowCount()));
    }
    for (auto &[order, item] : rows) {
        list.addItem(std::move(item));
    }
    openPage(*_moviesGUI);
}

void MainMenu::loadMusicPage() {
    _musicGUI = loadPage("titlemusic");
    auto find = [this](const std::string &tag) { return _musicGUI->findControl(tag); };
    auto &controls = _musicControls;
    controls.BTN_BACK = std::static_pointer_cast<Button>(find("BTN_BACK"));
    controls.BTN_LOOP = std::static_pointer_cast<Button>(find("BTN_LOOP"));
    controls.BTN_NEXT = std::static_pointer_cast<Button>(find("BTN_NEXT"));
    controls.BTN_PLAY = std::static_pointer_cast<Button>(find("BTN_PLAY"));
    controls.BTN_STOP = std::static_pointer_cast<Button>(find("BTN_STOP"));
    controls.LBL_BAR1 = std::static_pointer_cast<Label>(find("LBL_BAR1"));
    controls.LBL_BAR2 = std::static_pointer_cast<Label>(find("LBL_BAR2"));
    controls.LBL_TRACKNAME = std::static_pointer_cast<Label>(find("LBL_TRACKNAME"));
    controls.LBL_TRACKNUM = std::static_pointer_cast<Label>(find("LBL_TRACKNUM"));
    controls.LB_MUSIC = std::static_pointer_cast<ListBox>(find("LB_MUSIC"));
    controls.SLI_VOLUME = std::static_pointer_cast<Slider>(find("SLI_VOLUME"));
    fillK2SectionStrip(controls.LBL_BAR1, controls.LBL_BAR2);
    for (auto &button : {controls.BTN_BACK, controls.BTN_LOOP, controls.BTN_NEXT, controls.BTN_PLAY, controls.BTN_STOP}) {
        enableK2ButtonBodyFill(button);
    }
    // A click chooses a song; Play plays it.
    controls.LB_MUSIC->setSelectionMode(ListBox::SelectionMode::OnClick);

    controls.BTN_PLAY->setOnClick([this]() {
        playChosenSong();
    });
    controls.BTN_LOOP->setOnClick([this]() {
        setLooping(!_looping);
    });
    controls.BTN_STOP->setOnClick([this]() {
        if (_playing) {
            stopSong();
        }
    });
    controls.BTN_NEXT->setOnClick([this]() {
        if (_nextHold > 0.0f) {
            return;
        }
        _nextHold = kNextSongHold;
        playNextSong();
    });
    controls.BTN_BACK->setOnClick([this]() {
        closePage();
    });
    // The volume applies at once, to the songs played here only: closing the
    // page gives the options' music volume back.
    controls.SLI_VOLUME->setOnValueChanged([this](int volume) {
        _services.audio.mixer.setVolume(AudioType::Music, volume);
    });
}

// The list offers every song of the music table whose planet is open; the
// first group is always open.
void MainMenu::openMusic() {
    auto &list = *_musicControls.LB_MUSIC;
    list.clearItems();
    _songs.clear();

    auto table = _services.resource.twoDas.get("musictable");
    const int open = _game.options().game.unlockedPlanetSongs | 1;
    for (int row = 0; row < table->getRowCount(); ++row) {
        const int planet = table->getInt(row, "unlockplanet");
        if (planet < 0 || planet > 10 || !(open & (1 << planet))) {
            continue;
        }
        Song song {table->getString(row, "filename"),
                   _services.resource.strings.getText(table->getInt(row, "strrefname"))};
        ListBox::Item item;
        item.tag = std::to_string(_songs.size());
        item.text = song.name;
        list.addItem(std::move(item));
        _songs.push_back(std::move(song));
    }

    _chosenSong = -1;
    _playing = false;
    _nextHold = 0.0f;
    setLooping(false);
    _musicControls.LBL_TRACKNAME->setTextMessage("");
    _musicControls.LBL_TRACKNUM->setTextMessage("");
    _optionsMusicVolume = _game.options().audio.musicVolume;
    _musicControls.SLI_VOLUME->setValue(_optionsMusicVolume);
    if (!_songs.empty()) {
        list.setSelectedItemIndex(0);
        chooseSong(0);
    }
    openPage(*_musicGUI);
}

void MainMenu::chooseSong(int row) {
    _chosenSong = row;
    _musicControls.LBL_TRACKNUM->setTextMessage(str(boost::format("%d/%d") % (row + 1) % _songs.size()));
}

void MainMenu::playChosenSong() {
    if (_chosenSong < 0) {
        return;
    }
    if (_song) {
        stopSong();
    }
    const auto &song = _songs[_chosenSong];
    _song = _services.audio.mixer.play(
        _services.resource.audioClips.get(song.resRef), AudioType::Music, kMusicSoundGroup, 1.0f, _looping);
    _musicControls.LBL_TRACKNAME->setTextMessage(song.name);
    _playing = true;
}

// Next moves to the following song, from the last to the first, and plays it.
void MainMenu::playNextSong() {
    if (_songs.empty()) {
        return;
    }
    const int next = _chosenSong >= 0 && _chosenSong + 1 < static_cast<int>(_songs.size()) ? _chosenSong + 1 : 0;
    _musicControls.LB_MUSIC->setSelectedItemIndex(next);
    _musicControls.LB_MUSIC->scrollToItem(next);
    chooseSong(next);
    playChosenSong();
}

void MainMenu::stopSong() {
    _playing = false;
    if (_fadingSong) {
        _fadingSong->stop();
    }
    _fadingSong = std::move(_song);
    _fadeTime = 0.0f;
    _musicControls.LBL_TRACKNAME->setTextMessage("");
}

// Loop pulses while it is on.
void MainMenu::setLooping(bool looping) {
    _looping = looping;
    if (_song) {
        _song->setLooping(looping);
    }
    auto &loop = *_musicControls.BTN_LOOP;
    loop.border().pulseFrame = looping;
    loop.border().pulseFill = looping;
    loop.setHilightPulsing(looping, looping);
}

} // namespace game

} // namespace reone
