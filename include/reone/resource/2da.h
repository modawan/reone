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

#include "reone/system/exception/validation.h"

namespace reone {

namespace resource {

class TwoDAReader;

class TwoDA : boost::noncopyable {
public:
    /**
     * A data row together with the label that names it.
     *
     * The label is the first token of a row and is a key in its own right:
     * some tables are addressed by it rather than by a cell value, and it is
     * not always the row ordinal. It is therefore retained rather than
     * discarded, and is not part of the column data.
     */
    struct Row {
        std::string label;
        std::vector<std::string> values;
    };

    class Builder {
    public:
        Builder &columns(std::vector<std::string> columns) {
            _columns = std::move(columns);
            return *this;
        }

        /// Append a row labelled by its ordinal, as an unlabelled table is
        /// written out.
        Builder &row(std::vector<std::string> values) {
            return row(std::to_string(_rows.size()), std::move(values));
        }

        Builder &row(std::string label, std::vector<std::string> values) {
            _rows.push_back({std::move(label), std::move(values)});
            return *this;
        }

        std::unique_ptr<TwoDA> build() {
            return std::make_unique<TwoDA>(_columns, _rows);
        }

    private:
        std::vector<std::string> _columns;
        std::vector<Row> _rows;
    };

    TwoDA(std::vector<std::string> columns, std::vector<Row> rows) :
        _columns(std::move(columns)),
        _rows(std::move(rows)) {
        size_t numColumns = _columns.size();
        for (size_t row = 0; row < _rows.size(); ++row) {
            size_t numValues = _rows[row].values.size();
            if (numValues != numColumns) {
                throw ValidationException(str(boost::format("Expected %d columns in 2DA row %d, was %d") % numColumns % row % numValues));
            }
        }
    }

    /**
     * Find a row by its label, folding ASCII case.
     * Distinct from a cell lookup: a table may be keyed by label alone, and a
     * label is not a column.
     *
     * @return row index or -1 when not found
     */
    int indexByLabel(const std::string &label) const;

    /**
     * @return row index or -1 when not found
     */
    int indexByCellValue(const std::string &column, const std::string &value) const;

    /**
     * @return row index or -1 when not found
     */
    int indexByCellValues(const std::vector<std::pair<std::string, std::string>> &values) const;

    int getColumnCount() const { return static_cast<int>(_columns.size()); }
    int getRowCount() const { return static_cast<int>(_rows.size()); }

    /**
     * Find a column by its name, folding ASCII case.
     *
     * @return index of the first column so named, or -1 when not found
     */
    int getColumnIndex(const std::string &column) const;

    std::string getString(int row, const std::string &column, std::string defValue = "") const;

    /**
     * An integer cell, read as a scanf "%i" conversion reads it: decimal,
     * 0x-prefixed hexadecimal or 0-prefixed octal, ending at the first
     * character that is not a digit. A cell with no digits reads as 0.
     *
     * @return the cell's value, or \p defValue when the row, the column or
     *         the cell's text is missing
     */
    int getInt(int row, const std::string &column, int defValue = 0) const;
    /**
     * An unsigned 32-bit cell: the integer getInt reads, taken as unsigned.
     *
     * @return the cell's value, or \p defValue when the row, the column or
     *         the cell's text is missing
     */
    uint32_t getUint(int row, const std::string &column, uint32_t defValue = 0) const;
    uint32_t getHexInt(int row, const std::string &column, uint32_t defValue = 0) const;
    /**
     * A real cell, read as a scanf "%f" conversion reads it: decimal or
     * 0x-prefixed hexadecimal, with an optional exponent, ending at the first
     * character that cannot continue the number. A cell with no number reads
     * as 0.
     *
     * @return the cell's value, or \p defValue when the row, the column or
     *         the cell's text is missing
     */
    float getFloat(int row, const std::string &column, float defValue = 0.0f) const;
    /**
     * A flag cell: an integer, read as getInt reads it, that is not zero.
     *
     * @return the flag, or \p defValue when the row, the column or the
     *         cell's text is missing
     */
    bool getBool(int row, const std::string &column, bool defValue = false) const;

    /** A cell read by its column's index rather than its name, as getString reads it. */
    std::string getString(int row, int column, std::string defValue = "") const;
    /** A cell read by its column's index rather than its name, as getInt reads it. */
    int getInt(int row, int column, int defValue = 0) const;
    /** A cell read by its column's index rather than its name, as getFloat reads it. */
    float getFloat(int row, int column, float defValue = 0.0f) const;

    std::optional<std::string> getStringOpt(int row, const std::string &column) const;
    /** A cell read by its column's index rather than its name, as getStringOpt reads it. */
    std::optional<std::string> getStringOpt(int row, int column) const;

    /**
     * An integer cell, read as getInt reads it.
     *
     * @return the cell's value, or none when the row, the column or the
     *         cell's text is missing
     */
    std::optional<int> getIntOpt(int row, const std::string &column) const;
    /** A cell read by its column's index rather than its name, as getIntOpt reads it. */
    std::optional<int> getIntOpt(int row, int column) const;
    /** An unsigned 32-bit cell, read as getUint reads it; none when the row, the column or the cell's text is missing. */
    std::optional<uint32_t> getUintOpt(int row, const std::string &column) const;
    std::optional<uint32_t> getHexIntOpt(int row, const std::string &column) const;
    /** A real cell, read as getFloat reads it; none when the row, the column or the cell's text is missing. */
    std::optional<float> getFloatOpt(int row, const std::string &column) const;
    /** A cell read by its column's index rather than its name, as getFloatOpt reads it. */
    std::optional<float> getFloatOpt(int row, int column) const;
    /** A flag cell, read as getBool reads it; none when the row, the column or the cell's text is missing. */
    std::optional<bool> getBoolOpt(int row, const std::string &column) const;

    const std::vector<std::string> &columns() const { return _columns; }
    const std::vector<Row> &rows() const { return _rows; }

    static Row newRow(std::string label, std::vector<std::string> values) {
        auto row = Row();
        row.label = std::move(label);
        row.values = std::move(values);
        return row;
    }

private:
    std::vector<std::string> _columns;
    std::vector<Row> _rows;

    std::vector<int> getColumnIndices(const std::vector<std::string> &columns) const;

    /**
     * A cell's text as the table holds it.
     *
     * @param column the column's index
     * @return the cell's text, or none when the row or the column is missing
     */
    std::optional<std::string> getCellOpt(int row, int column) const;
};

} // namespace resource

} // namespace reone
