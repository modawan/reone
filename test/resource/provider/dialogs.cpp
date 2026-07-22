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

#include <gtest/gtest.h>

#include "reone/resource/container/memory.h"
#include "reone/resource/format/gffwriter.h"
#include "reone/resource/gff.h"
#include "reone/resource/provider/dialogs.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/resources.h"
#include "reone/resource/strings.h"
#include "reone/system/stream/memoryoutput.h"

using namespace reone;
using namespace reone::resource;

TEST(Dialogs, should_preserve_quest_and_plot_xp_fields_from_dlg) {
    // given

    auto entry = std::make_shared<Gff>(
        0,
        std::vector<Gff::Field> {
            Gff::Field::newCExoLocString("Text", -1, "greetings"),
            Gff::Field::newCExoString("Quest", "test_plot"),
            Gff::Field::newDword("QuestEntry", 10),
            Gff::Field::newInt("PlotIndex", 4),
            Gff::Field::newFloat("PlotXPPercentage", 0.25f)});

    auto reply = std::make_shared<Gff>(
        0,
        std::vector<Gff::Field> {
            Gff::Field::newCExoLocString("Text", -1, "farewell"),
            Gff::Field::newCExoString("Quest", "other_plot"),
            Gff::Field::newDword("QuestEntry", 20),
            Gff::Field::newInt("PlotIndex", 8),
            Gff::Field::newFloat("PlotXPPercentage", 0.5f)});

    auto root = Gff(
        0xffffffff,
        std::vector<Gff::Field> {
            Gff::Field::newList("EntryList", {entry}),
            Gff::Field::newList("ReplyList", {reply})});

    auto dlgBytes = ByteBuffer();
    auto dlgStream = MemoryOutputStream(dlgBytes);
    GffWriter(ResType::Dlg, root).save(dlgStream);

    auto resources = Resources();
    auto container = std::make_unique<MemoryResourceContainer>();
    container->add(ResourceId("sample", ResType::Dlg), std::move(dlgBytes));
    resources.add(std::move(container));

    auto gffs = Gffs(resources);
    auto strings = Strings();
    auto dialogs = Dialogs(gffs, strings);

    // when

    auto dialog = dialogs.get("sample");

    // then

    ASSERT_TRUE(static_cast<bool>(dialog));

    ASSERT_EQ(1ll, dialog->entries.size());
    EXPECT_EQ("test_plot", dialog->entries[0].quest);
    EXPECT_EQ(10u, dialog->entries[0].questEntry);
    EXPECT_EQ(4, dialog->entries[0].plotIndex);
    EXPECT_FLOAT_EQ(0.25f, dialog->entries[0].plotXPPercentage);

    ASSERT_EQ(1ll, dialog->replies.size());
    EXPECT_EQ("other_plot", dialog->replies[0].quest);
    EXPECT_EQ(20u, dialog->replies[0].questEntry);
    EXPECT_EQ(8, dialog->replies[0].plotIndex);
    EXPECT_FLOAT_EQ(0.5f, dialog->replies[0].plotXPPercentage);
}
