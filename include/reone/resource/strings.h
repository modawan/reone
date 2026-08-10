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

#include "talktable.h"

#include "types.h"

#include <array>

namespace reone {

namespace resource {

class IStrings {
public:
    virtual ~IStrings() = default;

    virtual std::string getText(int strRef) = 0;
    virtual std::string getSound(int strRef) = 0;
};

class Strings : public IStrings {
public:
    static constexpr std::size_t kTalkTableSlotCount = 7;

    Strings() = default;

    void init(const std::filesystem::path &gameDir);

    void setTalkTable(std::size_t slot, std::shared_ptr<TalkTable> table);
    bool loadTalkTable(std::size_t slot, const std::filesystem::path &path);

    std::string getText(int strRef) override;
    std::string getSound(int strRef) override;

private:
    std::array<std::shared_ptr<TalkTable>, kTalkTableSlotCount> _tables;

    const TalkTable::String *findString(int strRef) const;

    void process(std::string &str);
    void stripDeveloperNotes(std::string &str);
};

class LocString {
public:
    LocString() :
        _id(-1) {}

    LocString(int32_t id, std::string ref, IStrings &strings) :
        _id(id), _ref(ref), _loc((_id >= 0) ? strings.getText(id) : ref) {}

    operator std::string_view() const {
        return str();
    }

    const std::string &str() const {
        return _loc;
    }

private:
    int32_t _id;
    std::string _ref;
    std::string _loc;
};

class StrRef {
public:
    StrRef() :
        _id(-1) {}
    StrRef(int32_t id, IStrings &strings) :
        _id(id) {
        if (_id >= 0) {
            _str = strings.getText(id);
        }
    }

    operator std::string_view() const {
        return str();
    }

    const std::string &str() const {
        return _str;
    }

private:
    int32_t _id;
    std::string _str;
};

} // namespace resource

} // namespace reone
