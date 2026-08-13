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

#include "resources.h"

namespace reone {

namespace resource {

/**
 * Undoes the mounts of an operation that does not reach its end.
 *
 * An operation that mounts more than one source can fail after some of them
 * are already in place. What is left behind then is neither the old source set
 * nor the new one, and because a half-mounted module still answers lookups it
 * can supplement the next one. Scoping the operation removes exactly the
 * sources it added, in every owner and bucket it touched.
 *
 * This restores the resource manager to a defined state; it does not restore
 * the module that was there before. Whether the previous module is still valid
 * is the caller's question, not this one's: the sources it needs were already
 * retired before the new ones were mounted, and bringing them back would be a
 * new operation rather than an undo.
 *
 * Rolls back unless commit() is called, so an exception on the way out is
 * covered without the caller catching it.
 */
class ResourceMountTransaction : boost::noncopyable {
public:
    explicit ResourceMountTransaction(IResources &resources) :
        _resources(resources),
        _token(resources.mountToken()) {
    }

    ~ResourceMountTransaction() {
        if (!_committed) {
            _resources.rollbackTo(_token);
        }
    }

    void commit() { _committed = true; }

    ResourceMountToken token() const { return _token; }

private:
    IResources &_resources;
    ResourceMountToken _token;
    bool _committed {false};
};

} // namespace resource

} // namespace reone
