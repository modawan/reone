/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <cstdint>
#include <memory>

namespace reone {

namespace game {

/**
 * A non-owning reference to one exact published runtime-object incarnation.
 *
 * The weak pointer distinguishes storage identity, while the incarnation
 * prevents a deliberately republished object from reviving an old reference.
 * Runtime ObjectId is intentionally not part of resolution: a later object may
 * legally reuse the same number without becoming the former target.
 */
template <class T>
class RuntimeObjectRef {
public:
    RuntimeObjectRef() = default;
    RuntimeObjectRef(const std::shared_ptr<T> &object) { bind(object); }

    RuntimeObjectRef &operator=(const std::shared_ptr<T> &object) {
        bind(object);
        return *this;
    }

    void bind(const std::shared_ptr<T> &object) {
        _object = object;
        _incarnation = object ? object->runtimeIncarnation() : 0;
    }

    void reset() {
        _object.reset();
        _incarnation = 0;
    }

    std::shared_ptr<T> resolve() const {
        auto object = _object.lock();
        if (!object || !object->isRuntimeLive() ||
            object->runtimeIncarnation() != _incarnation) {
            return nullptr;
        }
        return object;
    }

    bool empty() const { return _incarnation == 0; }
    uint64_t incarnation() const { return _incarnation; }

private:
    std::weak_ptr<T> _object;
    uint64_t _incarnation {0};
};

} // namespace game

} // namespace reone
