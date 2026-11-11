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

#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/gui/control/listbox.h"
#include "reone/gui/control/slider.h"
#include "reone/resource/types.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/model.h"

#include "../gui.h"
#include "../types.h"

namespace reone {

namespace audio {

class AudioSource;

}

namespace game {

class Creature;

/**
 * The title screen. Its movie list and, in TSL, its music player open over
 * it as pages that take every input until they are closed.
 */
class MainMenu : public GameGUI {
public:
    MainMenu(Game &game, ServicesView &services);

    bool handle(const input::Event &event) override;
    void update(float dt) override;
    void render() override;

    void onModuleSelected(const std::string &name);
    // Rebuild K2 from the durable options snapshot on every menu entry.
    // Also callable after a host updates the persisted presentation state.
    void refreshScene();

    const std::string &musicResRef() const { return _musicResRef; }

private:
    friend class MainMenuTestAccess;
    struct Controls {
        std::shared_ptr<gui::Button> BTN_EXIT;
        std::shared_ptr<gui::Button> BTN_LOADGAME;
        std::shared_ptr<gui::Button> BTN_MOREGAMES;
        std::shared_ptr<gui::Button> BTN_MOVIES;
        std::shared_ptr<gui::Button> BTN_MUSIC;
        std::shared_ptr<gui::Button> BTN_NEWGAME;
        std::shared_ptr<gui::Button> BTN_OPTIONS;
        std::shared_ptr<gui::Button> BTN_TSLRCM;
        std::shared_ptr<gui::Button> BTN_WARP;
        std::shared_ptr<gui::Label> LBL_3DVIEW;
        std::shared_ptr<gui::Label> LBL_BW;
        std::shared_ptr<gui::Label> LBL_GAMELOGO;
        std::shared_ptr<gui::Label> LBL_LUCAS;
        std::shared_ptr<gui::Label> LBL_MENUBG;
        std::shared_ptr<gui::Label> LBL_NEWCONTENT;
        std::shared_ptr<gui::ListBox> LB_MODULES;
    };

    struct MovieControls {
        std::shared_ptr<gui::Button> BTN_BACK;
        std::shared_ptr<gui::Label> LBL_BAR1;
        std::shared_ptr<gui::Label> LBL_BAR2;
        std::shared_ptr<gui::Label> LBL_UNLOCKED_VALUE;
        std::shared_ptr<gui::ListBox> LB_MOVIES;
    };

    struct MusicControls {
        std::shared_ptr<gui::Button> BTN_BACK;
        std::shared_ptr<gui::Button> BTN_LOOP;
        std::shared_ptr<gui::Button> BTN_NEXT;
        std::shared_ptr<gui::Button> BTN_PLAY;
        std::shared_ptr<gui::Button> BTN_STOP;
        std::shared_ptr<gui::Label> LBL_BAR1;
        std::shared_ptr<gui::Label> LBL_BAR2;
        std::shared_ptr<gui::Label> LBL_TRACKNAME;
        std::shared_ptr<gui::Label> LBL_TRACKNUM;
        std::shared_ptr<gui::ListBox> LB_MUSIC;
        std::shared_ptr<gui::Slider> SLI_VOLUME;
    };

    /** A song of the music list: its music resource and its name. */
    struct Song {
        std::string resRef;
        std::string name;
    };

    Controls _controls;

    std::string _musicResRef;
    std::shared_ptr<Creature> _leader;

    /** The movie list or the music player while it is open over the menu. */
    gui::IGUI *_page {nullptr};

    std::shared_ptr<gui::IGUI> _moviesGUI;
    MovieControls _movieControls;

    std::shared_ptr<gui::IGUI> _musicGUI;
    MusicControls _musicControls;
    std::vector<Song> _songs;
    /** The row of the song chosen in the list, or none. */
    int _chosenSong {-1};
    std::shared_ptr<audio::AudioSource> _song;
    bool _playing {false};
    bool _looping {false};
    /** Seconds before Next answers another click. */
    float _nextHold {0.0f};
    /** A stopped song while it fades out, and how far it has faded. */
    std::shared_ptr<audio::AudioSource> _fadingSong;
    float _fadeTime {0.0f};
    /** The music volume of the options, which the player's own volume gives way to on closing. */
    int _optionsMusicVolume {0};

protected:
    void preload(gui::IGUI &gui) override;

private:
    void onGUILoaded() override;

    void bindControls() {
        _controls.BTN_EXIT = findControl<gui::Button>("BTN_EXIT");
        _controls.BTN_LOADGAME = findControl<gui::Button>("BTN_LOADGAME");
        _controls.BTN_MOREGAMES = findControl<gui::Button>("BTN_MOREGAMES");
        _controls.BTN_MOVIES = findControl<gui::Button>("BTN_MOVIES");
        _controls.BTN_MUSIC = findControl<gui::Button>("BTN_MUSIC");
        _controls.BTN_NEWGAME = findControl<gui::Button>("BTN_NEWGAME");
        _controls.BTN_OPTIONS = findControl<gui::Button>("BTN_OPTIONS");
        _controls.BTN_TSLRCM = findControl<gui::Button>("BTN_TSLRCM");
        _controls.BTN_WARP = findControl<gui::Button>("BTN_WARP");
        _controls.LBL_3DVIEW = findControl<gui::Label>("LBL_3DVIEW");
        _controls.LBL_BW = findControl<gui::Label>("LBL_BW");
        _controls.LBL_GAMELOGO = findControl<gui::Label>("LBL_GAMELOGO");
        _controls.LBL_LUCAS = findControl<gui::Label>("LBL_LUCAS");
        _controls.LBL_MENUBG = findControl<gui::Label>("LBL_MENUBG");
        _controls.LBL_NEWCONTENT = findControl<gui::Label>("LBL_NEWCONTENT");
        _controls.LB_MODULES = findControl<gui::ListBox>("LB_MODULES");
    }

    std::shared_ptr<gui::IGUI> loadPage(const std::string &name);
    void openPage(gui::IGUI &page);
    void closePage();

    void loadMoviesPage();
    void openMovies();

    void loadMusicPage();
    void openMusic();
    void chooseSong(int row);
    void playChosenSong();
    void playNextSong();
    void stopSong();
    void setLooping(bool looping);

    void configureButtons();
    void setup3DView();
    void setButtonColors(gui::Control &control);
    void startModuleSelection();
    void loadModuleNames();

    std::shared_ptr<scene::ModelSceneNode> getKotorModel(scene::ISceneGraph &sceneGraph);
};

} // namespace game

} // namespace reone
