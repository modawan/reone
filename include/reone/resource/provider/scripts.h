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

#include "reone/script/program.h"

#include "../decodedcache.h"

namespace reone {

namespace resource {

class IScripts {
public:
    virtual ~IScripts() = default;

    virtual void clear() = 0;

    virtual std::shared_ptr<script::ScriptProgram> get(const std::string &key) = 0;
};

class Scripts : public IScripts {
public:
    Scripts(IResources &resources) :
        _resources(resources) {
    }

    void clear() override {
        _objects.clear();
    }

    std::shared_ptr<script::ScriptProgram> get(const std::string &key) override {
        ResRef resRef(key);
        return _objects.get(resRef, [this, &resRef](ResourceReads &reads) {
            return doGet(resRef.value(), reads);
        }).value;
    }

private:
    IResources &_resources;

    DecodedCache<ResRef, script::ScriptProgram> _objects;

    std::shared_ptr<script::ScriptProgram> doGet(const std::string &resRef, ResourceReads &reads);
};

} // namespace resource

} // namespace reone
