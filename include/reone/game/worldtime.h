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

#include <cstdint>
#include <limits>

namespace reone {

namespace game {

/** World time's units. */
constexpr uint32_t kMillisecondsInSecond = 1000;
constexpr uint32_t kSecondsInMinute = 60;
constexpr uint32_t kMillisecondsInMinute = kMillisecondsInSecond * kSecondsInMinute;
constexpr uint32_t kHoursInDay = 24;

/** Whether a world time stamp, a calendar day and time of day, is set: day 0 at time 0 is none. */
constexpr bool isWorldTimeStamped(uint32_t day, uint32_t time) {
    return day != 0 || time != 0;
}

/**
 * The world time \p milliseconds after \p start, at most the last calendar
 * day \p lastDay holds, in days of \p dayMilliseconds: a time saved as a day
 * that counts no further stops there.
 */
inline uint64_t getCalendarTimeAfter(uint64_t start, uint64_t milliseconds, uint32_t dayMilliseconds,
                                     uint32_t lastDay) {
    const uint64_t last = (static_cast<uint64_t>(lastDay) + 1) * dayMilliseconds - 1;
    return start >= last || milliseconds > last - start ? last : start + milliseconds;
}

/**
 * \p stamp, or \p now when the stamp is ahead of it, as a module with shorter
 * hours or the end of a time stop can leave it: a timer measured from it
 * starts over from now rather than waiting for the clock to catch up.
 */
inline uint64_t getStampNotAfter(uint64_t stamp, uint64_t now) {
    return stamp > now ? now : stamp;
}

/**
 * The stamp a timer that runs every \p interval milliseconds, last due at
 * \p last, takes when it runs at \p now: when this run was due, so that its
 * runs keep to the interval however late an update reaches them; \p now for
 * a stamp ahead of it or two intervals or more behind it, as a time skip
 * leaves.
 */
inline uint64_t getNextStamp(uint64_t last, uint64_t interval, uint64_t now) {
    return now >= last && now - last < 2 * interval ? last + interval : now;
}

/**
 * \p seconds as whole milliseconds, the rest dropped: a length below zero is
 * none, and one past what 64 bits hold, or not a number, is the longest.
 */
inline uint64_t getWholeMilliseconds(float seconds) {
    const float milliseconds = seconds * static_cast<float>(kMillisecondsInSecond);
    if (!(milliseconds < 9.0e18f)) {
        return std::numeric_limits<uint64_t>::max();
    }
    return milliseconds > 0.0f ? static_cast<uint64_t>(milliseconds) : 0;
}

} // namespace game

} // namespace reone
