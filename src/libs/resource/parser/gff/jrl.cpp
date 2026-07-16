/*
 * Copyright (c) 2026 The reone project contributors
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

#include "reone/resource/parser/gff/jrl.h"

#include "reone/resource/gff.h"

namespace reone {

namespace resource {

static JRL::Entry parseEntry(const Gff &gff) {
    JRL::Entry entry;
    entry.id = gff.getUint("ID");
    entry.end = gff.getBool("End");
    entry.text = std::make_pair(gff.getInt("Text", -1), gff.getString("Text"));
    entry.xpPercentage = gff.getFloat("XP_Percentage");
    return entry;
}

static JRL::Category parseCategory(const Gff &gff) {
    JRL::Category category;
    category.tag = gff.getString("Tag");
    category.name = std::make_pair(gff.getInt("Name", -1), gff.getString("Name"));
    category.priority = gff.getUint("Priority");
    category.planetId = gff.getInt("PlanetID", -1);
    category.plotIndex = gff.getInt("PlotIndex", -1);
    category.comment = gff.getString("Comment");
    for (const auto &entry : gff.getList("EntryList")) {
        category.entries.push_back(parseEntry(*entry));
    }
    return category;
}

JRL parseJRL(const Gff &gff) {
    JRL jrl;
    for (const auto &category : gff.getList("Categories")) {
        jrl.categories.push_back(parseCategory(*category));
    }
    return jrl;
}

} // namespace resource

} // namespace reone
