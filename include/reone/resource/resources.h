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

#include "reone/system/types.h"

#include "id.h"
#include "resource.h"

#include "reone/extract/finder.h"
namespace reone {

namespace extract {
class Installation;
}

namespace resource {

class IResources {
public:
    virtual ~IResources() = default;

    virtual void clear() = 0;

    virtual Resource get(const ResourceId &id) = 0;
    virtual std::optional<Resource> find(const ResourceId &id) = 0;
    virtual std::optional<Resource> find(const ResourceId &id, const extract::SearchScope &order) = 0;

    virtual extract::Installation *installation() { return nullptr; }
    virtual const extract::Installation *installation() const { return nullptr; }
};

class Resources : public IResources, boost::noncopyable {
public:
    void clear() override {}

    Resource get(const ResourceId &id) override;
    std::optional<Resource> find(const ResourceId &id) override;
    std::optional<Resource> find(const ResourceId &id, const extract::SearchScope &order) override;

    void useInstallation(extract::Installation *installation);
    void setSearchOrder(extract::SearchScope order);
    extract::Installation *installation() override { return _installation; }
    const extract::Installation *installation() const override { return _installation; }

private:
    extract::Installation *_installation {nullptr};
    extract::SearchScope _searchOrder = extract::canonicalSearchOrder();
};

} // namespace resource

} // namespace reone
