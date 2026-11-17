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

#include "reone/system/unicodeutil.h"

namespace reone {

namespace resource {

/**
 * The code page a talk table in \p language keeps its text in: Windows-1250
 * for Polish, Windows-1252 for English, French, German, Italian and Spanish.
 * The Korean, Chinese and Japanese releases' double-byte code pages are not
 * supported: their text is taken as Windows-1252.
 */
CodePage codePageOfLanguage(Language language);

class IStrings {
public:
    virtual ~IStrings() = default;

    /** The talk-table text as written, its tokens and {...} notes included. */
    virtual std::string getText(int strRef) = 0;
    virtual std::string getSound(int strRef) = 0;
    /** The length of the string's sound in seconds; 0 for a string not found. */
    virtual float getSoundDuration(int strRef) = 0;
    /** The language of the main talk table; English when none is loaded. */
    virtual Language language() const = 0;
    /**
     * One of the game's custom strings, kept in override/custom.txt: the
     * file's lines hold kCustomStringsPerLanguage strings for each language,
     * English first, then French, Italian, German and Spanish; the main talk
     * table's language picks them. Empty for a string the file lacks, and for
     * every string when the file holds other than the five languages' worth.
     */
    virtual std::string getCustomText(int id) = 0;
};

class Strings : public IStrings {
public:
    static constexpr std::size_t kTalkTableSlotCount = 7;
    /** The custom strings each language has in override/custom.txt. */
    static constexpr int kCustomStringsPerLanguage = 11;
    /** The languages override/custom.txt holds. */
    static constexpr int kCustomStringLanguages = 5;

    Strings() = default;

    void init(const std::filesystem::path &gameDir);

    void setTalkTable(std::size_t slot, std::shared_ptr<TalkTable> table);
    bool loadTalkTable(std::size_t slot, const std::filesystem::path &path);

    std::string getText(int strRef) override;
    std::string getSound(int strRef) override;
    float getSoundDuration(int strRef) override;
    Language language() const override;
    std::string getCustomText(int id) override;

private:
    std::array<std::shared_ptr<TalkTable>, kTalkTableSlotCount> _tables;
    /** The lines of override/custom.txt as written, read once at start; none without the file. */
    std::vector<std::string> _customStrings;

    const TalkTable::String *findString(int strRef) const;
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

    const int32_t &id() const {
        return _id;
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
