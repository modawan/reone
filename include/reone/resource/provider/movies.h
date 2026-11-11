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

#include <string>
#include <vector>

#include "reone/graphics/di/services.h"
#include "reone/movie/movie.h"

namespace reone {

namespace audio {

class IAudioMixer;

}

namespace resource {

class IMovies {
public:
    virtual ~IMovies() = default;

    /** A new playback of the movie, from its start; none when the game has no such movie. */
    virtual std::shared_ptr<movie::IMovie> get(const std::string &name) = 0;

    /** The names of the game's movies, in lower case and without their extension, in order of name. */
    virtual std::vector<std::string> names() = 0;
};

class Movies : public IMovies, boost::noncopyable {
public:
    Movies(std::filesystem::path gamePath,
           graphics::GraphicsServices &graphicsSvc,
           audio::IAudioMixer &audioPlayer) :
        _gamePath(gamePath),
        _graphicsSvc(graphicsSvc),
        _audioPlayer(audioPlayer) {
    }

    std::shared_ptr<movie::IMovie> get(const std::string &name) override;
    std::vector<std::string> names() override;

private:
    std::filesystem::path _gamePath;
    graphics::GraphicsServices &_graphicsSvc;
    audio::IAudioMixer &_audioPlayer;
};

} // namespace resource

} // namespace reone
