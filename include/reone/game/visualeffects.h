/*
 * Copyright (c) 2026 The reone project contributors
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

#include "reone/game/types.h"

namespace reone {

namespace graphics {
class Model;
}

namespace audio {
class AudioClip;
}

namespace resource {
class TwoDAs;
class TwoDA;
class AudioClips;
class Models;
} // namespace resource

namespace game {

struct VisualEffectDesc {
    std::string label;
    std::shared_ptr<graphics::Model> impRootMNode;
    std::shared_ptr<audio::AudioClip> soundImpact;
};

class IVisualEffects {
public:
    virtual std::optional<const VisualEffectDesc *> get(uint32_t id) const = 0;
};

class VisualEffects : public IVisualEffects {
public:
    VisualEffects(resource::TwoDAs &twoDas,
                  resource::AudioClips &audioClips,
                  resource::Models &models) :
        _twoDas(twoDas),
        _audioClips(audioClips),
        _models(models) {}

    void init();
    void clear();

    std::optional<const VisualEffectDesc *> get(uint32_t id) const override;

private:
    void workaroundLabels();

private:
    resource::TwoDAs &_twoDas;
    resource::AudioClips &_audioClips;
    resource::Models &_models;
    std::map<uint32_t, VisualEffectDesc> _effects;
};

} // namespace game

} // namespace reone
