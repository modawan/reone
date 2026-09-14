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

#include "reone/game/globalfade.h"

#include <algorithm>
#include <cmath>

namespace reone::game {

static float nonnegativeFinite(float value) {
    return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
}

bool GlobalFade::request(Direction direction, float wait, float length, glm::vec3 color, Source source) {
    // Unlock even if movie override prevents the visual request. No replay.
    if (source == Source::Script && direction == Direction::In) {
        _locked = false;
    }
    if (_locked || _movieOverride) {
        return false;
    }
    _direction = direction;
    _wait = nonnegativeFinite(wait);
    _length = nonnegativeFinite(length);
    for (int i = 0; i < 3; ++i) {
        _color[i] = std::min(1.0f, nonnegativeFinite(color[i]));
    }
    _elapsed = 0.1f;
    _active = true;
    evaluate();
    return true;
}

void GlobalFade::evaluate() {
    double progress = 0.0;
    if (_elapsed > _wait) {
        progress = _length == 0.0f ? 1.0 : std::min(1.0, (_elapsed - _wait) / _length);
    }
    _opacity = static_cast<float>(_direction == Direction::In ? 1.0 - progress : progress);
}

void GlobalFade::update(float dt) {
    if (_active && !_movieOverride && std::isfinite(dt) && dt > 0.0f && dt < 5.0f) {
        _elapsed += dt;
        evaluate();
    }
    // A queued action can disappear with its actor without executing/cancelling.
    // Only the still-current admission's expiration can consume the hold.
    if (_hasDialog && _dialog.expired()) {
        consumeHold();
        _hasDialog = false;
    }
}

bool GlobalFade::stop() {
    if (_locked || _movieOverride) {
        return false;
    }
    _active = false;
    _opacity = 0.0f;
    return true;
}

GlobalFade::ArrivalTicket GlobalFade::beginArrival() {
    invalidateModule();
    _arrival = std::make_shared<ArrivalIdentity>();
    request(Direction::Out);
    return _arrival;
}

void GlobalFade::finishLoading(const ArrivalTicket &arrival) {
    if (arrival && arrival == _arrival && !_arrivalConsumed) {
        // Removing loading UI supplies cover, not a reveal. Early authored
        // requests can subsequently be replaced by ordinary readiness.
        request(Direction::Out);
    }
}

void GlobalFade::settleArrival(const ArrivalTicket &arrival) {
    if (!arrival || arrival != _arrival) {
        return;
    }
    if (!_arrivalConsumed) {
        if (dialogPending() || _hold) {
            _hold = true;
        } else {
            request(Direction::In, 0.5f, 1.0f);
        }
    }
    _arrival.reset();
}

void GlobalFade::invalidateModule() {
    _arrival.reset();
    _arrivalConsumed = false;
    _dialog.reset();
    _hasDialog = false;
}

GlobalFade::DialogTicket GlobalFade::admitDialog(bool replace) {
    if (dialogPending() && !replace) {
        return {};
    }
    auto dialog = std::make_shared<DialogIdentity>();
    _dialog = dialog;
    _hasDialog = true;
    if (_arrival && !_arrivalConsumed) {
        _hold = true;
    }
    return dialog;
}

bool GlobalFade::isCurrentDialog(const DialogTicket &dialog) const {
    return dialog && _hasDialog && _dialog.lock() == dialog;
}

void GlobalFade::consumeHold() {
    if (!_hold) {
        return;
    }
    _hold = false;
    if (_arrival) {
        _arrivalConsumed = true;
    }
    request(Direction::In, 0.0f, 1.0f);
}

void GlobalFade::revealDialog(const DialogTicket &dialog) {
    if (isCurrentDialog(dialog)) {
        consumeHold();
    }
}

void GlobalFade::finishDialog(const DialogTicket &dialog) {
    if (isCurrentDialog(dialog)) {
        consumeHold();
        _dialog.reset();
        _hasDialog = false;
    }
}

void GlobalFade::resetSession() {
    // Transient presentation is not serialized with caller-owned actions.
    *this = GlobalFade();
}

} // namespace reone::game
