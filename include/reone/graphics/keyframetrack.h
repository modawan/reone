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

namespace graphics {

template <class Value>
class KeyframeTrack {
public:
    struct Keyframe {
        float time {0.0f};
        Value value {Value()};
    };

    void add(float time, Value value) {
        Keyframe kf;
        kf.time = time;
        kf.value = std::move(value);
        _keyframes.push_back(std::move(kf));
    }

    /**
     * Adds a key of a curved track. Its handles are offsets from its value:
     * the incoming one shapes the curve from the key before, the outgoing one
     * the curve to the key after. A track's keys are all curved or none is.
     */
    void addCurved(float time, Value value, Value incoming, Value outgoing) {
        add(time, std::move(value));
        _handles.push_back({std::move(incoming), std::move(outgoing)});
    }

    void update() {
        auto byTime = [](const auto &lhs, const auto &rhs) {
            return lhs.time < rhs.time;
        };
        if (_handles.empty()) {
            std::sort(_keyframes.begin(), _keyframes.end(), byTime);
            return;
        }
        // The handles move with their keys.
        std::vector<size_t> order(_keyframes.size());
        for (size_t index = 0; index < order.size(); ++index) {
            order[index] = index;
        }
        std::stable_sort(order.begin(), order.end(), [&](size_t lhs, size_t rhs) {
            return byTime(_keyframes[lhs], _keyframes[rhs]);
        });
        std::vector<Keyframe> keyframes;
        std::vector<Handles> handles;
        keyframes.reserve(order.size());
        handles.reserve(order.size());
        for (size_t index : order) {
            keyframes.push_back(std::move(_keyframes[index]));
            handles.push_back(std::move(_handles[index]));
        }
        _keyframes = std::move(keyframes);
        _handles = std::move(handles);
    }

    bool valueAtTime(float time, Value &value) const {
        if (_keyframes.empty()) {
            return false;
        }
        if (_keyframes.size() == 1ll || _keyframes[0].time >= time) {
            value = _keyframes[0].value;
            return true;
        }
        // From its last key on, a track holds its last value.
        if (_keyframes.back().time <= time) {
            value = _keyframes.back().value;
            return true;
        }
        int rightKfIdx;
        for (int i = 1; i < _keyframes.size(); ++i) {
            rightKfIdx = i;
            if (_keyframes[i].time >= time) {
                break;
            }
        }
        int leftKfIdx = rightKfIdx - 1;
        const auto &leftKeyframe = _keyframes[leftKfIdx];
        const auto &rightKeyframe = _keyframes[rightKfIdx];
        if (leftKeyframe.time == rightKeyframe.time) {
            value = leftKeyframe.value;
        } else {
            float factor = (time - leftKeyframe.time) / (rightKeyframe.time - leftKeyframe.time);
            value = _handles.empty() ? interpolateKeyframes(leftKeyframe, rightKeyframe, factor)
                                     : curveBetween(leftKfIdx, rightKfIdx, factor);
        }
        return true;
    }

    const std::vector<Keyframe> &keyframes() const {
        return _keyframes;
    }

private:
    struct Handles {
        Value incoming {Value()};
        Value outgoing {Value()};
    };

    std::vector<Keyframe> _keyframes;
    /** One per key of a curved track; empty for a track of straight segments. */
    std::vector<Handles> _handles;

    // A curved segment is the cubic Bezier curve from the left key's value to
    // the right key's, pulled towards the left key's outgoing handle and the
    // right key's incoming one.
    Value curveBetween(int left, int right, float factor) const {
        const Value &from = _keyframes[left].value;
        const Value &to = _keyframes[right].value;
        const Value fromHandle = from + _handles[left].outgoing;
        const Value toHandle = to + _handles[right].incoming;
        const float rest = 1.0f - factor;
        return (rest * rest * rest) * from +
               (3.0f * rest * rest * factor) * fromHandle +
               (3.0f * rest * factor * factor) * toHandle +
               (factor * factor * factor) * to;
    }

    template <typename T = Value,
              typename std::enable_if_t<!std::is_same<glm::quat, T>::value, int> = 0>
    Value interpolateKeyframes(const Keyframe &lhs, const Keyframe &rhs, float factor) const {
        if (lhs.time == rhs.time) {
            return lhs.value;
        }
        return glm::mix(lhs.value, rhs.value, factor);
    }

    // For quaternions we have to use slerp to always take a shorter path.
    template <typename T = Value,
              typename std::enable_if_t<std::is_same<glm::quat, T>::value, int> = 1>
    Value interpolateKeyframes(const Keyframe &lhs, const Keyframe &rhs, float factor) const {
        if (lhs.time == rhs.time) {
            return lhs.value;
        }
        return glm::slerp(lhs.value, rhs.value, factor);
    }
};

} // namespace graphics

} // namespace reone
