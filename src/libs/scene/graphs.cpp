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

#include "reone/scene/graphs.h"
#include "reone/graphics/context.h"
#include "reone/graphics/di/services.h"

namespace reone {

namespace scene {

void SceneGraphs::reserve(std::string name) {
    if (_scenes.count(name) > 0) {
        return;
    }
    reset(std::move(name));
}

void SceneGraphs::reset(std::string name) {
    auto scene = std::make_unique<SceneGraph>(
        name,
        _renderPipelineFactory,
        _graphicsOpt,
        _graphicsSvc,
        _audioSvc,
        _resourceSvc);

    if (_scenes.count(name)) {
        // The graphics context caches references to framebuffer objects. Drop
        // those bindings before destroying a scene's render targets.
        _graphicsSvc.context.resetReadFramebuffer();
        _graphicsSvc.context.resetDrawFramebuffer();
    }
    _scenes[name] = std::move(scene);
}

ISceneGraph &SceneGraphs::get(const std::string &name) {
    auto maybeScene = _scenes.find(name);
    if (maybeScene == _scenes.end()) {
        throw std::logic_error(str(boost::format("Scene not found by name '%s'") % name));
    }
    return *maybeScene->second;
}

} // namespace scene

} // namespace reone
