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

#include <memory>
#include <glm/vec3.hpp>

namespace reone::game {

/** Transient, client presentation state. Independent of scripts and cameras. */
class GlobalFade {
public:
    enum class Direction { In, Out };
    enum class Source { Automatic, Script };

    // Identity only: keeping a ticket alive grants no authority after replacement.
    struct DialogIdentity {};
    struct ArrivalIdentity {};
    using DialogTicket = std::shared_ptr<const DialogIdentity>;
    using ArrivalTicket = std::shared_ptr<const ArrivalIdentity>;

    bool request(Direction direction, float wait = 0.0f, float length = 0.0f,
                 glm::vec3 color = glm::vec3(0.0f), Source source = Source::Automatic);
    bool stop();
    void update(float dt);

    void holdForDialog() { _hold = true; }
    void lockUntilScript() { _locked = true; }
    void setMovieOverride(bool active) { _movieOverride = active; }

    ArrivalTicket beginArrival();
    void finishLoading(const ArrivalTicket &arrival);
    void settleArrival(const ArrivalTicket &arrival);
    void invalidateModule();

    DialogTicket admitDialog(bool replace = false);
    bool isCurrentDialog(const DialogTicket &dialog) const;
    void revealDialog(const DialogTicket &dialog);
    void finishDialog(const DialogTicket &dialog);

    /** New game/disk-load retirement, deliberately stronger than locked stop. */
    void resetSession();

    float opacity() const { return _opacity; }
    const glm::vec3 &color() const { return _color; }
    bool heldForDialog() const { return _hold; }
    bool locked() const { return _locked; }
    bool dialogPending() const { return _hasDialog && !_dialog.expired(); }
    bool arrivalPending() const { return static_cast<bool>(_arrival); }

private:
    Direction _direction {Direction::In};
    glm::vec3 _color {0.0f};
    double _elapsed {0.0};
    float _wait {0.0f};
    float _length {0.0f};
    float _opacity {0.0f};
    bool _active {false};
    bool _hold {false};
    bool _locked {false};
    bool _movieOverride {false};
    bool _hasDialog {false};
    std::weak_ptr<const DialogIdentity> _dialog;
    ArrivalTicket _arrival;
    bool _arrivalConsumed {false};

    void evaluate();
    void consumeHold();
};

} // namespace reone::game
