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

#include <charconv>
#include <optional>
#include <string>
#include <type_traits>

namespace reone {

namespace game {

class ConsoleArgs {
public:
    using TokenList = std::vector<std::string>;

    ConsoleArgs(TokenList tokens) :
        _tokens(std::move(tokens)) {}

    std::optional<std::string_view> operator[](size_t i) const {
        return i < _tokens.size() ? _tokens[i] : std::optional<std::string_view>();
    }

    /**
     * Returns the argument at index \p i converted to type T (integer or
     * floating-point).
     */
    template <typename T>
    std::optional<T> get(size_t i) const {
        auto arg = (*this)[i];
        if (!arg) {
            return std::optional<T>();
        }

        std::string_view str = arg.value();
        const char *begin = str.data();
        const char *end = str.data() + str.size();

        T value;
        std::from_chars_result result = std::from_chars(begin, end, value);

        if (result.ec != std::errc {} || result.ptr != end) {
            return std::optional<T>();
        }
        return value;
    }

    /**
     * Returns the argument at index \p i converted to an enum type. Note that
     * this functions does not check that the resulting value is actually a
     * valid enum case.
     */
    template <typename EnumTy>
    std::optional<EnumTy> getEnum(size_t i) const {
        if (auto val = get<std::underlying_type_t<EnumTy>>(i)) {
            return static_cast<EnumTy>(val.value());
        }
        return std::optional<EnumTy>();
    }

    size_t size() const { return _tokens.size(); }

protected:
    TokenList _tokens;
};

class IConsole {
public:
    using CommandHandler = std::function<void(const ConsoleArgs &)>;

    virtual ~IConsole() = default;

    virtual void registerCommand(std::string name, std::string description, CommandHandler handler) = 0;
    virtual void printLine(const std::string &text) = 0;
};

} // namespace game

} // namespace reone
