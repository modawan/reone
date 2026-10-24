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

namespace resource {

class TalkTable : boost::noncopyable {
public:
    struct String {
        std::string text;
        std::string soundResRef;
    };

    class Builder {
    public:
        Builder &string(std::string text, std::string soundResRef = "") {
            _strings.push_back(String {std::move(text), std::move(soundResRef)});
            return *this;
        }

        std::shared_ptr<TalkTable> build() {
            return std::make_shared<TalkTable>(_strings);
        }

    private:
        std::vector<String> _strings;
    };

    /**
     * @param strings the strings, by string reference
     * @param languageId the language the strings are written in
     */
    TalkTable(std::vector<String> strings, uint32_t languageId = 0) :
        _strings(std::move(strings)),
        _languageId(languageId) {
    }

    int getStringCount() const;
    const String &getString(int index) const;
    /** The language the strings are written in. */
    uint32_t languageId() const { return _languageId; }

private:
    std::vector<String> _strings;
    uint32_t _languageId {0};
};

} // namespace resource

} // namespace reone
