/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <chrono>
#include <fstream>
#include <gtest/gtest.h>

#include "reone/game/menupresentation.h"

using namespace reone;
using namespace reone::game;

namespace {

class TemporaryConfiguration {
public:
    TemporaryConfiguration() :
        path(std::filesystem::temp_directory_path() /
             ("reone-options-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".cfg")) {
    }

    ~TemporaryConfiguration() {
        std::error_code error;
        std::filesystem::remove(path, error);
        auto temporary = path;
        temporary += ".tmp";
        std::filesystem::remove(temporary, error);
    }

    void write(const std::string &content) const {
        std::ofstream output(path, std::ios::binary);
        output << content;
    }

    std::string read() const {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), {});
    }

    std::filesystem::path path;
};

} // namespace

TEST(OptionsConfiguration, every_options_page_round_trips_without_losing_another_pages_keys) {
    TemporaryConfiguration configuration;
    configuration.write("# kept exactly\r\nfuture-option = untouched\r\n[Unrelated]\r\nvalue=42");

    audio::AudioOptions sound;
    sound.musicVolume = 11;
    sound.voiceVolume = 22;
    sound.soundVolume = 33;
    sound.movieVolume = 44;
    sound.forceSoftware = true;
    saveSoundOptions(configuration.path, sound);

    graphics::GraphicsOptions graphics;
    graphics.shadows = false;
    graphics.softShadows = false;
    graphics.grass = false;
    graphics.fxaa = false;
    graphics.vsync = false;
    saveGraphicsOptions(configuration.path, graphics);

    StartGraphicsOptions nextStart;
    nextStart.displayMode = {1920, 1080, 75};
    nextStart.textureQuality = graphics::TextureQuality::Low;
    nextStart.anisotropicFiltering = 4;
    nextStart.guiScale = 0.75f;
    nextStart.save(configuration.path);

    MouseOptions mouse;
    mouse.mouseLook = true;
    mouse.sensitivity = 137;
    mouse.save(configuration.path);

    GameplayOptions gameplay;
    gameplay.combatMovement = false;
    gameplay.save(configuration.path);

    const auto loadedStart = StartGraphicsOptions::load(configuration.path, graphics);
    EXPECT_EQ(nextStart.displayMode, loadedStart.displayMode);
    EXPECT_EQ(nextStart.textureQuality, loadedStart.textureQuality);
    EXPECT_EQ(nextStart.anisotropicFiltering, loadedStart.anisotropicFiltering);
    EXPECT_FLOAT_EQ(nextStart.guiScale, loadedStart.guiScale);
    const auto loadedMouse = MouseOptions::load(configuration.path);
    EXPECT_EQ(mouse.mouseLook, loadedMouse.mouseLook);
    EXPECT_EQ(mouse.sensitivity, loadedMouse.sensitivity);
    EXPECT_EQ(gameplay.combatMovement, GameplayOptions::load(configuration.path).combatMovement);

    const auto content = configuration.read();
    EXPECT_NE(std::string::npos, content.find("# kept exactly"));
    EXPECT_NE(std::string::npos, content.find("future-option = untouched"));
    EXPECT_NE(std::string::npos, content.find("[Unrelated]"));
    EXPECT_NE(std::string::npos, content.find("value=42"));
    for (const auto &line : {
             "musicvol=11", "voicevol=22", "soundvol=33", "movievol=44", "forcesoftware=1",
             "shadows=0", "softshadows=0", "grass=0", "fxaa=0", "vsync=0",
             "width=1920", "height=1080", "refresh=75", "texquality=2", "anisofilter=4", "guiscale=0.75"}) {
        EXPECT_NE(std::string::npos, content.find(line)) << line;
    }
}
