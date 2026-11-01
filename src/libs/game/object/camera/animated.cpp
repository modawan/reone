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

#include "reone/game/object/camera/animated.h"

#include <boost/algorithm/string/predicate.hpp>

#include "reone/game/di/services.h"
#include "reone/graphics/types.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/camera.h"
#include "reone/scene/node/model.h"

using namespace reone::graphics;
using namespace reone::scene;

namespace reone {

namespace game {

void AnimatedCamera::load() {
    auto &scene = _services.scene.graphs.get(_sceneName);
    _sceneNode = scene.newCamera();
    rebuildProjection();
}

float AnimatedCamera::projectionFovy() const {
    return glm::radians(_fovy);
}

void AnimatedCamera::update(float dt) {
    Camera::update(dt);

    if (_model) {
        _model->update(dt);
    }
}

static constexpr int kCameraClipBandSize = 128;
static constexpr int kFirstLoopingCameraOrdinal = 1400;
static constexpr int kLastLoopingCameraOrdinal = 1727;

// The 29th clip of every band is named as clip 39.
static constexpr int kMisnumberedCameraClipOffset = 28;
static constexpr int kMisnumberedCameraClipNumber = 39;

namespace {

// Each band of ordinals names clips with its own suffix.
struct CameraClipBand {
    int base;
    const char *suffix;
};

} // namespace

static constexpr CameraClipBand kCameraClipBands[] {
    {1000, ""},
    {1200, "w"},
    {1400, "l"},
    {1600, "wl"}};

std::string AnimatedCamera::getShotClipName(int ordinal) {
    for (const auto &band : kCameraClipBands) {
        int offset = ordinal - band.base;
        if (offset >= 0 && offset < kCameraClipBandSize) {
            int number = offset == kMisnumberedCameraClipOffset ? kMisnumberedCameraClipNumber : offset + 1;
            return str(boost::format("cut%03d%s") % number % band.suffix);
        }
    }
    return "";
}

void AnimatedCamera::playAnimation(int ordinal) {
    _requestedClip = getShotClipName(ordinal);
    if (!_model || _requestedClip.empty()) {
        return;
    }
    AnimationProperties properties;
    if (ordinal >= kFirstLoopingCameraOrdinal && ordinal <= kLastLoopingCameraOrdinal) {
        properties.flags |= AnimationFlags::loop;
    }
    _model->playAnimation(_requestedClip, nullptr, std::move(properties));
}

// Only the clip the last request named counts: a clip the model lacks leaves
// the earlier one running, and that one is not waited for.
bool AnimatedCamera::isRequestedClipPlaying() const {
    if (!_model || _requestedClip.empty() || _model->isAnimationFinished()) {
        return false;
    }
    return boost::iequals(_model->activeAnimationName(), _requestedClip);
}

void AnimatedCamera::setModel(std::shared_ptr<Model> model) {
    if ((_model && &_model->model() == model.get()) ||
        (!_model && !model))
        return;

    if (model) {
        auto &scene = _services.scene.graphs.get(_sceneName);
        _model = scene.newModel(*model, ModelUsage::Camera);
        _model->attach("camerahook", *_sceneNode);
    } else {
        _model.reset();
    }
}

void AnimatedCamera::setFieldOfView(float fovy) {
    if (_fovy == fovy) {
        return;
    }
    _fovy = fovy;
    rebuildProjection();
}

} // namespace game

} // namespace reone
