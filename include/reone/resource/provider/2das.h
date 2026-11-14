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

#include "../2da.h"
#include "../decodedcache.h"

namespace reone {

namespace resource {

class ITwoDAs {
public:
    virtual ~ITwoDAs() = default;

    virtual void clear() = 0;

    virtual std::shared_ptr<TwoDA> get(const std::string &key) = 0;
};

class TwoDAs : public ITwoDAs, boost::noncopyable {
public:
    TwoDAs(IResources &resources) :
        _resources(resources) {
    }

    void clear() override {
        _cache.clear();
    }

    std::shared_ptr<TwoDA> get(const std::string &key) override {
        return entry(key).value;
    }

    /// The cached table with the lookups it was read from, for a value built from it.
    const DecodedCache<std::string, TwoDA>::Entry &entry(const std::string &resRef);

private:
    IResources &_resources;

    DecodedCache<std::string, TwoDA> _cache;
};

} // namespace resource

} // namespace reone
