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

#include "reone/system/logger.h"

#include "audio.h"
#include "game.h"
#include "graphics.h"
#include "gui.h"
#include "movie.h"
#include "resource.h"
#include "scene.h"
#include "script.h"
#include "system.h"

namespace reone {

class TestEngine : boost::noncopyable {
public:
    void init() {
        static bool loggerInitialized = false;
        if (!loggerInitialized) {
            Logger::instance.init(LogSeverity::Error, {}, std::nullopt);
            loggerInitialized = true;
        }

        _gameModule = std::make_unique<game::TestGameModule>();
        _movieModule = std::make_unique<movie::TestMovieModule>();
        _audioModule = std::make_unique<audio::TestAudioModule>();
        _graphicsModule = std::make_unique<graphics::TestGraphicsModule>();
        _sceneModule = std::make_unique<scene::TestSceneModule>();
        _guiModule = std::make_unique<gui::TestGUIModule>();
        _scriptModule = std::make_unique<script::TestScriptModule>();
        _resourceModule = std::make_unique<resource::TestResourceModule>();
        _systemModule = std::make_unique<TestSystemModule>();

        _gameModule->init();
        _movieModule->init();
        _audioModule->init();
        _graphicsModule->init();
        _sceneModule->init();
        _guiModule->init();
        _scriptModule->init();
        _resourceModule->init();
        _systemModule->init();

        _services = std::make_unique<game::ServicesView>(
            _gameModule->services(),
            _movieModule->services(),
            _audioModule->services(),
            _graphicsModule->services(),
            _sceneModule->services(),
            _guiModule->services(),
            _scriptModule->services(),
            _resourceModule->services(),
            _systemModule->services());

        // Tables the game always reads. Empty, every row reads as blank; a
        // test that needs rows mocks the table itself.
        EXPECT_CALL(_resourceModule->twoDas(), get(testing::_)).Times(testing::AnyNumber());
        std::shared_ptr<resource::TwoDA> empty(resource::TwoDA::Builder().build());
        // Every companion row and the general row pay experience in full.
        resource::TwoDA::Builder fullShares;
        fullShares.columns({"percentxp"});
        for (int row = 0; row < 14; ++row) {
            fullShares.row({"100"});
        }
        ON_CALL(_resourceModule->twoDas(), get(std::string("npc"))).WillByDefault(testing::Return(std::shared_ptr<resource::TwoDA>(fullShares.build())));
        for (const char *table : {"ranges", "traps", "skills", "animations", "dialoganimations", "tutorial",
                                  "encdifficulty", "ambientmusic", "ambientsound", "prioritygroups", "influence",
                                  "appearancesndset", "placeableobjsnds", "videoeffects", "stringtokens",
                                  "feedbacktext", "aiscripts"}) {
            ON_CALL(_resourceModule->twoDas(), get(std::string(table))).WillByDefault(testing::Return(empty));
        }
        // The races the game reads at start; every adjustment is blank.
        resource::TwoDA::Builder races;
        races.columns({"stradjust", "dexadjust", "conadjust", "intadjust", "wisadjust", "chaadjust"});
        for (int race = 0; race < kRacialTypeCount; ++race) {
            races.row({"", "", "", "", "", ""});
        }
        std::shared_ptr<resource::TwoDA> racialTypes(races.build());
        ON_CALL(_resourceModule->twoDas(), get(std::string("racialtypes"))).WillByDefault(testing::Return(racialTypes));
        _gameModule->races().init(_resourceModule->twoDas());
        // The combat tables the game loads at start, with no rows but an
        // excitement of no duration.
        testing::NiceMock<resource::MockTwoDAs> combatTables;
        ON_CALL(combatTables, get(testing::_)).WillByDefault(testing::Invoke(&TestEngine::defaultCombatTable));
        _gameModule->combatTables().init(combatTables, resource::GameID::KotOR);
    }

    /**
     * Load the combat tables again from the tables the test gives; a table
     * the test does not give reads as at start.
     */
    void initCombatTables() {
        testing::NiceMock<resource::MockTwoDAs> combatTables;
        ON_CALL(combatTables, get(testing::_)).WillByDefault([this](const std::string &resRef) {
            auto table = _resourceModule->twoDas().get(resRef);
            return table ? table : defaultCombatTable(resRef);
        });
        _gameModule->combatTables() = game::CombatTables();
        _gameModule->combatTables().init(combatTables, resource::GameID::KotOR);
    }

    game::OptionsView &options() {
        return _options;
    }

    game::TestGameModule &gameModule() {
        return *_gameModule;
    }

    movie::TestMovieModule &movieModule() {
        return *_movieModule;
    }

    audio::TestAudioModule &audioModule() {
        return *_audioModule;
    }

    graphics::TestGraphicsModule &graphicsModule() {
        return *_graphicsModule;
    }

    scene::TestSceneModule &sceneModule() {
        return *_sceneModule;
    }

    gui::TestGUIModule &guiModule() {
        return *_guiModule;
    }

    script::TestScriptModule &scriptModule() {
        return *_scriptModule;
    }

    resource::TestResourceModule &resourceModule() {
        return *_resourceModule;
    }

    TestSystemModule &systemModule() {
        return *_systemModule;
    }

    game::ServicesView &services() {
        return *_services;
    }

private:
    /** The rows of racialtypes.2da a creature may name. */
    static constexpr int kRacialTypeCount = 9;

    /** A combat table as the game loads it at start: no rows, but an excitement of no duration. */
    static std::shared_ptr<resource::TwoDA> defaultCombatTable(const std::string &resRef) {
        if (resRef == "excitedduration") {
            return resource::TwoDA::Builder().columns({"duration"}).row({"0"}).row({"0"}).row({"0"}).build();
        }
        return resource::TwoDA::Builder().build();
    }

    game::GameOptions _gameOpt;
    graphics::GraphicsOptions _graphicsOpt;
    audio::AudioOptions _audioOpt;
    game::OptionsView _options {_gameOpt, _graphicsOpt, _audioOpt};

    std::unique_ptr<game::TestGameModule> _gameModule;
    std::unique_ptr<movie::TestMovieModule> _movieModule;
    std::unique_ptr<audio::TestAudioModule> _audioModule;
    std::unique_ptr<graphics::TestGraphicsModule> _graphicsModule;
    std::unique_ptr<scene::TestSceneModule> _sceneModule;
    std::unique_ptr<gui::TestGUIModule> _guiModule;
    std::unique_ptr<script::TestScriptModule> _scriptModule;
    std::unique_ptr<resource::TestResourceModule> _resourceModule;
    std::unique_ptr<TestSystemModule> _systemModule;

    std::unique_ptr<game::ServicesView> _services;
};

// TestEngine initializes the Logger singleton, which only tolerates a single
// init per process.
TestEngine &testEngine();

} // namespace reone
