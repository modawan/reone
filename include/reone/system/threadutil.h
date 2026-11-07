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

namespace reone {

void markMainThread();
void checkMainThread();

bool isMainThread();

void setThreadName(std::string name);
const std::string &threadName();

/**
 * Marks the calling thread as the main thread while it lives, then puts back
 * the main thread and the calling thread's name as they were. For tests.
 */
class ScopedMainThread : boost::noncopyable {
public:
    ScopedMainThread();
    ~ScopedMainThread();

private:
    std::thread::id _previousMainThread;
    std::optional<std::string> _previousName;
};

} // namespace reone
