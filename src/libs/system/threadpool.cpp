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

#include "reone/system/threadpool.h"

namespace reone {

void ThreadPool::init() {
    if (_numThreads == -1) {
        _numThreads = static_cast<int>(std::thread::hardware_concurrency());
    }
    _running = true;
    for (auto i = 0; i < _numThreads; ++i) {
        _threads.emplace_back(std::bind(&ThreadPool::workerThreadFunc, this));
    }
}

void ThreadPool::deinit() {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _running = false;
    }
    _condVar.notify_all();

    for (auto &thread : _threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    _threads.clear();
}

} // namespace reone
