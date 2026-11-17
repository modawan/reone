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

#include "../decodedcache.h"
#include "../parser/gff/pth.h"
#include "../path.h"

namespace reone {

namespace resource {

class Gffs;
class Gff;

class IPaths {
public:
    virtual ~IPaths() = default;

    virtual void clear() = 0;

    virtual std::shared_ptr<Path> get(const std::string &key) = 0;
};

class Paths : public IPaths {
public:
    Paths(Gffs &gffs) :
        _gffs(gffs) {
    }

    void clear() override {
        _objects.clear();
    }

    std::shared_ptr<Path> get(const std::string &key) override {
        return _objects.get(key, [this, &key](ResourceReads &reads) {
            return doGet(key, reads);
        }).value;
    }

private:
    Gffs &_gffs;

    DecodedCache<std::string, Path> _objects;

    std::shared_ptr<Path> doGet(const std::string &resRef, ResourceReads &reads);

    std::unique_ptr<Path> loadPath(const generated::PTH &pth) const;
};

} // namespace resource

} // namespace reone
