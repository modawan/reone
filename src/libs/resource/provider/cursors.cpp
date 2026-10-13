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

#include "reone/resource/provider/cursors.h"

#include "reone/graphics/cursor.h"
#include "reone/graphics/texture.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/resources.h"

using namespace reone::graphics;

namespace reone {

namespace resource {

static std::unordered_map<CursorType, std::pair<std::string, std::string>> g_groupNamesByType {
    {CursorType::Default, {"gui_mp_defaultu", "gui_mp_defaultd"}},
    {CursorType::Talk, {"gui_mp_talku", "gui_mp_talkd"}},
    {CursorType::Door, {"gui_mp_dooru", "gui_mp_doord"}},
    {CursorType::Pickup, {"gui_mp_useu", "gui_mp_used"}},
    {CursorType::DisableMine, {"gui_mp_dismineu", "gui_mp_dismined"}},
    {CursorType::RecoverMine, {"gui_mp_recmineu", "gui_mp_recmined"}},
    {CursorType::Attack, {"gui_mp_killu", "gui_mp_killd"}}};

void Cursors::deinit() {
    _cache.clear();
}

std::shared_ptr<Cursor> Cursors::get(CursorType type) {
    auto maybeCursor = _cache.find(type);
    if (maybeCursor != _cache.end()) {
        return maybeCursor->second;
    }

    auto &upDown = g_groupNamesByType[type];
    assert(!upDown.first.empty() && !upDown.second.empty());

    std::shared_ptr<Texture> up = _textures.get(upDown.first, TextureUsage::GUI);
    if (!up) {
        return (type != CursorType::Default) ? get(CursorType::Default) : nullptr;
    }

    std::shared_ptr<Texture> down = _textures.get(upDown.second, TextureUsage::GUI);
    if (!down) {
        return (type != CursorType::Default) ? get(CursorType::Default) : nullptr;
    }

    auto cursor = std::make_shared<Cursor>(
        up,
        down,
        _context,
        _meshRegistry,
        _shaderRegistry,
        _uniforms,
        _statistic);
    _cache.insert(std::make_pair(type, cursor));

    return cursor;
}

} // namespace resource

} // namespace reone
