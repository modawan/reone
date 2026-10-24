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

#include "reone/resource/2da.h"

#include <cstdlib>
#include <limits>

#include "reone/system/logutil.h"

namespace reone {

namespace resource {

static constexpr char kCellValueDeleted[] = "****";

/**
 * ASCII case folding.
 *
 * Table keys are ASCII, and the game's lookup folds case. A locale-aware
 * conversion is deliberately avoided: it would make which row a table resolves
 * to depend on the environment the game happens to run in.
 */
static char asciiLower(char ch) {
    return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
}

static bool asciiEqualsIgnoreCase(const std::string &lhs, const std::string &rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (size_t i = 0; i < lhs.size(); ++i) {
        if (asciiLower(lhs[i]) != asciiLower(rhs[i])) {
            return false;
        }
    }
    return true;
}

int TwoDA::indexByLabel(const std::string &label) const {
    for (size_t i = 0; i < _rows.size(); ++i) {
        if (asciiEqualsIgnoreCase(_rows[i].label, label))
            return static_cast<int>(i);
    }
    return -1;
}

int TwoDA::indexByCellValue(const std::string &column, const std::string &value) const {
    int columnIdx = getColumnIndex(column);
    if (columnIdx == -1) {
        warn("2DA: column not found: " + column);
        return -1;
    }
    for (size_t i = 0; i < _rows.size(); ++i) {
        if (_rows[i].values[columnIdx] == value)
            return static_cast<int>(i);
    }

    return -1;
}

int TwoDA::getColumnIndex(const std::string &column) const {
    for (size_t i = 0; i < _columns.size(); ++i) {
        if (asciiEqualsIgnoreCase(_columns[i], column))
            return static_cast<int>(i);
    }
    return -1;
}

static std::vector<std::string> getColumnNames(const std::vector<std::pair<std::string, std::string>> &values) {
    std::vector<std::string> names;
    for (auto &val : values) {
        names.push_back(val.first);
    }
    return names;
}

int TwoDA::indexByCellValues(const std::vector<std::pair<std::string, std::string>> &values) const {
    std::vector<std::string> columns(getColumnNames(values));
    std::vector<int> columnIndices(getColumnIndices(columns));

    for (size_t i = 0; i < _rows.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < values.size(); ++j) {
            int columnIdx = columnIndices[j];
            if (_rows[i].values[columnIdx] != values[j].second) {
                match = false;
                break;
            }
        }
        if (match)
            return static_cast<int>(i);
    }

    return -1;
}

std::vector<int> TwoDA::getColumnIndices(const std::vector<std::string> &columns) const {
    std::vector<int> indices;
    for (auto &column : columns) {
        int index = getColumnIndex(column);
        if (index == -1) {
            throw std::logic_error("Column not found: " + column);
        }
        indices.push_back(index);
    }
    return indices;
}

std::string TwoDA::getString(int row, const std::string &column, std::string defValue) const {
    return getStringOpt(row, column).value_or(defValue);
}

std::string TwoDA::getString(int row, int column, std::string defValue) const {
    return getStringOpt(row, column).value_or(defValue);
}

std::optional<std::string> TwoDA::getCellOpt(int row, int column) const {
    if (row < 0 || row >= _rows.size()) {
        warn("2DA: row index out of range: " + std::to_string(row));
        return std::nullopt;
    }
    if (column < 0 || column >= static_cast<int>(_columns.size())) {
        return std::nullopt;
    }
    return _rows[row].values[column];
}

std::optional<std::string> TwoDA::getStringOpt(int row, const std::string &column) const {
    return getStringOpt(row, getColumnIndex(column));
}

std::optional<std::string> TwoDA::getStringOpt(int row, int column) const {
    auto value = getCellOpt(row, column);
    if (value && *value == kCellValueDeleted) {
        warn(str(boost::format("2DA: cell value was deleted: %d %s") % row % _columns[column]));
        return std::nullopt;
    }
    return value;
}

int TwoDA::getInt(int row, const std::string &column, int defValue) const {
    return getIntOpt(row, column).value_or(defValue);
}

int TwoDA::getInt(int row, int column, int defValue) const {
    return getIntOpt(row, column).value_or(defValue);
}

static bool isScanSpace(char ch) {
    return ch == ' ' || (ch >= '\t' && ch <= '\r');
}

static int getDigitValue(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    char lower = asciiLower(ch);
    if (lower >= 'a' && lower <= 'z') {
        return lower - 'a' + 10;
    }
    return std::numeric_limits<int>::max();
}

/**
 * Reads an integer as a scanf "%i" conversion does. Leading white space and
 * a sign are skipped; a "0x" or "0X" prefix makes the digits hexadecimal and
 * a leading 0 makes them octal; reading ends at the first character that is
 * not a digit of the base. Text with no digits reads as 0. The number is
 * read as a 64-bit one, saturating, and kept to its low 32 bits.
 */
static int scanInt(const std::string &text) {
    size_t pos = 0;
    while (pos < text.size() && isScanSpace(text[pos])) {
        ++pos;
    }
    bool negative = false;
    if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
        negative = text[pos] == '-';
        ++pos;
    }
    int base = 10;
    if (pos < text.size() && text[pos] == '0') {
        base = 8;
        if (pos + 1 < text.size() && (text[pos + 1] == 'x' || text[pos + 1] == 'X')) {
            base = 16;
            pos += 2;
        }
    }
    // The largest magnitude a signed 64-bit number of this sign holds.
    const uint64_t limit = negative
                               ? static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1
                               : static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    uint64_t magnitude = 0;
    for (; pos < text.size(); ++pos) {
        int digit = getDigitValue(text[pos]);
        if (digit >= base) {
            break;
        }
        if (magnitude > (limit - digit) / base) {
            magnitude = limit;
        } else {
            magnitude = magnitude * base + digit;
        }
    }
    uint64_t value = negative ? ~magnitude + 1 : magnitude;
    return static_cast<int>(static_cast<uint32_t>(value));
}

std::optional<int> TwoDA::getIntOpt(int row, const std::string &column) const {
    return getIntOpt(row, getColumnIndex(column));
}

std::optional<int> TwoDA::getIntOpt(int row, int column) const {
    auto value = getCellOpt(row, column);
    if (!value || value->empty()) {
        return std::nullopt;
    }
    return scanInt(*value);
}

uint32_t TwoDA::getUint(int row, const std::string &column, uint32_t defValue) const {
    return getUintOpt(row, column).value_or(defValue);
}

std::optional<uint32_t> TwoDA::getUintOpt(int row, const std::string &column) const {
    auto value = getIntOpt(row, column);
    if (!value) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(*value);
}

uint32_t TwoDA::getHexInt(int row, const std::string &column, uint32_t defValue) const {
    return getHexIntOpt(row, column).value_or(defValue);
}

std::optional<uint32_t> TwoDA::getHexIntOpt(int row, const std::string &column) const {
    const std::string &value = getString(row, column);
    if (value.empty()) {
        return std::nullopt;
    }
    return stoi(value, nullptr, 16);
}

float TwoDA::getFloat(int row, const std::string &column, float defValue) const {
    return getFloatOpt(row, column).value_or(defValue);
}

float TwoDA::getFloat(int row, int column, float defValue) const {
    return getFloatOpt(row, column).value_or(defValue);
}

std::optional<float> TwoDA::getFloatOpt(int row, const std::string &column) const {
    return getFloatOpt(row, getColumnIndex(column));
}

/**
 * Reads a real number as a scanf "%f" conversion does. Leading white space
 * and a sign are skipped; decimal digits with an optional point and exponent,
 * 0x-prefixed hexadecimal digits with an optional binary exponent, or an
 * infinity or NaN are read up to the first character that cannot continue the
 * number. Text with no number reads as 0, and a magnitude too large for a
 * float reads as infinity.
 */
static float scanFloat(const std::string &text) {
    return std::strtof(text.c_str(), nullptr);
}

std::optional<float> TwoDA::getFloatOpt(int row, int column) const {
    auto value = getCellOpt(row, column);
    if (!value || value->empty()) {
        return std::nullopt;
    }
    return scanFloat(*value);
}

bool TwoDA::getBool(int row, const std::string &column, bool defValue) const {
    return getBoolOpt(row, column).value_or(defValue);
}

std::optional<bool> TwoDA::getBoolOpt(int row, const std::string &column) const {
    auto value = getIntOpt(row, column);
    if (!value) {
        return std::nullopt;
    }
    return *value != 0;
}

} // namespace resource

} // namespace reone
