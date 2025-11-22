/*
 * Copyright (c) 2025 The reone project contributors
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

#include <vector>

namespace reone {

/**
 * TimeEvents is a container for time points and events that should happen at
 * these points. Events may be pointer or integers. "Zero" event is special and
 * should not be used. Callers must call update() and then process triggered
 * events until next() returns zero.
 */
class TimeEvents {
public:
    typedef intptr_t Event;

    /**
     * Add an event that should occur at \timePoint. Note that TimeEvents does
     * not sort events by time, so call push_back() in the right order (earlier
     * events first).
     */
    void push_back(float timePoint, Event event);

    /** Update the timer. */
    void update(float dt) { _time += dt; }

    /**
     * Query for events that triggered since the last call to next(). Returns *
     * either an event, or zero all events have been processed so far.
     */
    Event next();

private:
    float _time {0.0f};
    size_t _next {0};
    std::vector<float> _points;
    std::vector<Event> _events;
};

} // namespace reone
