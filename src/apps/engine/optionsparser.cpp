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

#include "optionsparser.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <optional>

#include "reone/system/types.h"

using namespace boost::program_options;

using namespace reone::game;
using namespace reone::graphics;

namespace reone {

static constexpr char kConfigFilename[] = "reone.cfg";

static float positiveFiniteScale(const variables_map &vars, const char *name) {
    float value = vars[name].as<float>();
    if (!std::isfinite(value) || value <= 0.0f) {
        throw std::invalid_argument(std::string("--") + name + " must be finite and greater than zero");
    }
    return value;
}

std::unique_ptr<Options> OptionsParser::parse() {
    auto options = std::make_unique<Options>();

    int defaultLogChannels = 0;
    for (auto &channel : options->logging.channels) {
        defaultLogChannels |= static_cast<int>(channel);
    }

    // Initialize options description

    options_description descCommon;
    descCommon.add_options()                                                                                                    //
        ("game", value<std::string>(), "path to game directory")                                                                //
        ("commands-file", value<std::string>()->default_value(""), "execute console commands from a file at startup")           //
        ("dev", value<bool>()->default_value(options->game.developer), "enable developer mode")                                 //
        ("width", value<int>()->default_value(options->graphics.width), "render width")                                         //
        ("height", value<int>()->default_value(options->graphics.height), "render height")                                      //
        ("winscale", value<int>()->default_value(options->graphics.winScale), "window scale")                                   //
        ("fullscreen", value<bool>()->default_value(options->graphics.fullscreen), "enable fullscreen")                         //
        ("headless", value<bool>()->default_value(false), "never show the window; for scripted batch runs")                     //
        ("vsync", value<bool>()->default_value(options->graphics.vsync), "enable v-sync")                                       //
        ("wheelzoom", value<bool>()->default_value(options->game.wheelZoom), "zoom the follow camera with the mouse wheel")     //
        ("grass", value<bool>()->default_value(options->graphics.grass), "enable grass")                                        //
        ("shadows", value<bool>()->default_value(options->graphics.shadows), "enable shadows")                                  //
        ("particles", value<bool>()->default_value(options->graphics.particles), "enable emitter particles")                    //
        ("pbr", value<bool>()->default_value(options->graphics.pbr), "enable physically-based rendering")                       //
        ("ssao", value<bool>()->default_value(options->graphics.ssao), "enable screen-space ambient occlusion")                 //
        ("ssr", value<bool>()->default_value(options->graphics.ssr), "enable screen-space reflections")                         //
        ("fxaa", value<bool>()->default_value(options->graphics.fxaa), "enable anti-aliasing")                                  //
        ("sharpen", value<bool>()->default_value(options->graphics.sharpen), "enable image sharpening")                         //
        ("texquality", value<int>()->default_value(static_cast<int>(options->graphics.textureQuality)), "texture quality")      //
        ("shadowres", value<int>()->default_value(glm::log2(options->graphics.shadowResolution) - 10), "shadow map resolution") //
        ("anisofilter", value<int>()->default_value(options->graphics.anisotropicFiltering), "anisotropic filtering")           //
        ("drawdist", value<int>()->default_value(static_cast<int>(kDefaultObjectDrawDistance)), "draw distance")                //
        ("guiscale", value<float>()->default_value(options->graphics.guiScale), "GUI layout scale")                              //
        ("guitextscale", value<float>()->default_value(options->graphics.guiTextScale), "GUI text scale")                       //
        ("guidialogtextscale", value<float>()->default_value(options->graphics.guiDialogTextScale), "dialog text scale")       //
        ("guiborderscale", value<float>()->default_value(options->graphics.guiBorderScale), "GUI border scale")                 //
        ("guilistscale", value<float>()->default_value(options->graphics.guiListScale), "GUI list row scale")                    //
        ("musicvol", value<int>()->default_value(options->audio.musicVolume), "music volume in percents")                       //
        ("voicevol", value<int>()->default_value(options->audio.voiceVolume), "voice volume in percents")                       //
        ("soundvol", value<int>()->default_value(options->audio.soundVolume), "sound volume in percents")                       //
        ("movievol", value<int>()->default_value(options->audio.movieVolume), "movie volume in percents")                       //
        ("forcesoftware", value<bool>()->default_value(options->audio.forceSoftware), "force software sound")                  //
        ("voices2d", value<int>()->default_value(options->audio.voices2D), "number of 2D voices, half for streams")             //
        ("voices3d", value<int>()->default_value(options->audio.voices3D), "number of 3D voices")                               //
        ("envstreaming", value<bool>()->default_value(options->audio.streamedReverb), "reverb streamed 3D sounds")              //
        ("envnonstreaming", value<bool>()->default_value(options->audio.unstreamedReverb), "reverb unstreamed 3D sounds")       //
        ("logsev", value<int>()->default_value(static_cast<int>(options->logging.severity)), "minimum log severity")            //
        ("logch", value<int>()->default_value(defaultLogChannels), "log channel mask");

    options_description descCmdLine {"Usage"};
    descCmdLine.add(descCommon);

    // Parse command line and configuration file

    variables_map vars;
    store(parse_command_line(_argc, _argv, descCmdLine), vars);
    // A configuration that is there but cannot be read, whether it fails to
    // open or part way through, leaves the options at their defaults; the log
    // is not open yet, so this is said on the error stream.
    std::error_code configError;
    if (std::filesystem::exists(kConfigFilename, configError) || configError) {
        std::ifstream config(kConfigFilename);
        std::optional<parsed_options> parsed;
        if (config) {
            parsed = parse_config_file<char>(config, descCommon, true);
        }
        if (parsed && !config.bad()) {
            store(*parsed, vars);
        } else {
            std::cerr << "Unable to read the configuration at " << kConfigFilename << "; the options are left at their defaults" << std::endl;
        }
    }
    notify(vars);

    // Convert Boost options to game options

    options->game.path = vars.count("game") > 0 ? std::filesystem::path(vars["game"].as<std::string>()) : std::filesystem::current_path();
    options->game.developer = vars["dev"].as<bool>();
    options->game.wheelZoom = vars["wheelzoom"].as<bool>();
    options->game.configurationPath = std::filesystem::absolute(kConfigFilename);
    options->game.menuPresentation = game::MenuPresentation::load(options->game.configurationPath);
    options->graphics.width = vars["width"].as<int>();
    options->graphics.height = vars["height"].as<int>();
    options->graphics.winScale = vars["winscale"].as<int>();
    options->graphics.fullscreen = vars["fullscreen"].as<bool>();
    options->graphics.headless = vars["headless"].as<bool>();
    options->graphics.vsync = vars["vsync"].as<bool>();
    options->graphics.grass = vars["grass"].as<bool>();
    options->graphics.shadows = vars["shadows"].as<bool>();
    options->graphics.particles = vars["particles"].as<bool>();
    options->graphics.pbr = vars["pbr"].as<bool>();
    options->graphics.ssao = vars["ssao"].as<bool>();
    options->graphics.ssr = vars["ssr"].as<bool>();
    options->graphics.fxaa = vars["fxaa"].as<bool>();
    options->graphics.sharpen = vars["sharpen"].as<bool>();
    options->graphics.textureQuality = static_cast<TextureQuality>(vars["texquality"].as<int>());
    options->graphics.shadowResolution = 1 << (10 + vars["shadowres"].as<int>());
    options->graphics.anisotropicFiltering = vars["anisofilter"].as<int>();
    options->graphics.drawDistance = static_cast<float>(vars["drawdist"].as<int>());
    options->graphics.guiScale = positiveFiniteScale(vars, "guiscale");
    options->graphics.guiTextScale = positiveFiniteScale(vars, "guitextscale");
    options->graphics.guiDialogTextScale = positiveFiniteScale(vars, "guidialogtextscale");
    options->graphics.guiBorderScale = positiveFiniteScale(vars, "guiborderscale");
    options->graphics.guiListScale = positiveFiniteScale(vars, "guilistscale");
    options->audio.musicVolume = vars["musicvol"].as<int>();
    options->audio.voiceVolume = vars["voicevol"].as<int>();
    options->audio.soundVolume = vars["soundvol"].as<int>();
    options->audio.movieVolume = vars["movievol"].as<int>();
    options->audio.forceSoftware = vars["forcesoftware"].as<bool>();
    options->audio.voices2D = vars["voices2d"].as<int>();
    options->audio.voices3D = vars["voices3d"].as<int>();
    options->audio.streamedReverb = vars["envstreaming"].as<bool>();
    options->audio.unstreamedReverb = vars["envnonstreaming"].as<bool>();
    options->logging.severity = static_cast<LogSeverity>(vars["logsev"].as<int>());

    std::set<LogChannel> logChannels;
    int logChannelsMask = vars["logch"].as<int>();
    if ((logChannelsMask & static_cast<int>(LogChannel::Global)) != 0) {
        logChannels.insert(LogChannel::Global);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::Resources)) != 0) {
        logChannels.insert(LogChannel::Resources);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::Resources2)) != 0) {
        logChannels.insert(LogChannel::Resources2);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::Graphics)) != 0) {
        logChannels.insert(LogChannel::Graphics);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::Audio)) != 0) {
        logChannels.insert(LogChannel::Audio);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::GUI)) != 0) {
        logChannels.insert(LogChannel::GUI);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::Perception)) != 0) {
        logChannels.insert(LogChannel::Perception);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::Conversation)) != 0) {
        logChannels.insert(LogChannel::Conversation);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::Combat)) != 0) {
        logChannels.insert(LogChannel::Combat);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::Script)) != 0) {
        logChannels.insert(LogChannel::Script);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::Script2)) != 0) {
        logChannels.insert(LogChannel::Script2);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::Script3)) != 0) {
        logChannels.insert(LogChannel::Script3);
    }
    options->logging.channels = std::move(logChannels);

    options->commandsFile = vars["commands-file"].as<std::string>();

    return options;
}

} // namespace reone
