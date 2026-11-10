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

#include "reone/system/stream/memoryinput.h"

#include "../decodedcache.h"
#include "../exception/notfound.h"
#include "../format/ltrreader.h"
#include "../ltr.h"
#include "../resref.h"

namespace reone {

namespace resource {

class ILtrs {
public:
    virtual ~ILtrs() = default;

    virtual std::shared_ptr<Ltr> get(const ResRef &resRef) = 0;
};

class Ltrs : public ILtrs, boost::noncopyable {
public:
    Ltrs(IResources &resources) :
        _resources(resources) {
    }

    std::shared_ptr<Ltr> get(const ResRef &resRef) override {
        return _cache.get(resRef, [this, &resRef](ResourceReads &reads) {
            ResourceId id(resRef, ResType::Ltr);
            auto res = reads.find(_resources, id);
            if (!res) {
                throw ResourceNotFoundException(id.string());
            }
            auto stream = MemoryInputStream(res->data);
            auto reader = LtrReader(stream);
            return reader.load();
        }).value;
    }

private:
    IResources &_resources;

    DecodedCache<ResRef, Ltr> _cache;
};

} // namespace resource

} // namespace reone
