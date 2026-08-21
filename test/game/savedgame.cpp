/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>

#include "reone/game/savedgame.h"
#include "reone/resource/format/gffwriter.h"
#include "reone/resource/gff.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

class SaveBrowserTest : public Test {
protected:
    std::filesystem::path _root;

    void SetUp() override {
        static std::atomic_uint64_t sequence {0};
        _root = std::filesystem::temp_directory_path() /
                ("reone_savedgame_" + std::to_string(++sequence));
        std::filesystem::create_directories(_root / "saves");
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(_root, ec);
    }

    std::filesystem::path addSlot(
        const std::string &directoryName,
        const std::string &saveName = "Test Save",
        bool screenshot = false,
        std::optional<uint32_t> saveNumber = std::nullopt,
        std::optional<uint64_t> timestamp = std::nullopt) {
        auto directory = _root / "saves" / directoryName;
        std::filesystem::create_directories(directory);
        std::ofstream(directory / "SAVEGAME.sav", std::ios::binary).put('x');
        auto nfo = Gff::Builder()
                       .field(Gff::Field::newCExoString("AREANAME", "Upper City"))
                       .field(Gff::Field::newCExoString("LASTMODULE", "tar_m02aa"))
                       .field(Gff::Field::newCExoString("PCNAME", "Revan"))
                       .field(Gff::Field::newDword("TIMEPLAYED", 3723))
                       .field(Gff::Field::newCExoString("SAVEGAMENAME", saveName))
                       .field(Gff::Field::newCExoString("PORTRAIT0", "po_pmhc01"))
                       .build();
        if (saveNumber) {
            nfo->fields().push_back(Gff::Field::newDword("SAVENUMBER", *saveNumber));
        }
        if (timestamp) {
            nfo->fields().push_back(Gff::Field::newDword64("TIMESTAMP", *timestamp));
        }
        auto bytes = GffWriter(GffFileFormat::v32("NFO "), *nfo).toBytes();
        std::ofstream nfoFile(directory / "savenfo.res", std::ios::binary);
        nfoFile.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        nfoFile.close();
        if (screenshot) {
            std::ofstream screen(directory / "Screen.tga", std::ios::binary);
            screen.write("tga", 3);
        }
        return directory;
    }
};

TEST_F(SaveBrowserTest, discoversRetailAndReoneDirectorySlotsWithMetadata) {
    addSlot("000014 - GAME14", "Retail Name", true);
    addSlot("000003 Reone Name", "Reone Name");

    auto saves = discoverSavedGames(_root);

    ASSERT_EQ(saves.size(), 2);
    EXPECT_EQ(saves[0].slot, 3);
    EXPECT_FALSE(saves[0].screenshot);
    EXPECT_EQ(saves[1].slot, 14);
    EXPECT_EQ(saves[1].metadata.savegameName, "Retail Name");
    EXPECT_EQ(saves[1].metadata.pcName, "Revan");
    EXPECT_EQ(saves[1].metadata.timePlayed, 3723);
    ASSERT_TRUE(saves[1].screenshot);
    EXPECT_EQ(*saves[1].screenshot, ByteBuffer({'t', 'g', 'a'}));
}

TEST_F(SaveBrowserTest, k1RetailGameSuffixIsDisplayOnlyWhileSlotRemainsDurableIdentity) {
    auto directory = addSlot("000164 - game163", "Dan-ruins");

    auto saves = discoverSavedGames(_root);

    ASSERT_EQ(saves.size(), 1);
    EXPECT_EQ(saves[0].slot, 164u);
    EXPECT_EQ(saves[0].displayNumber, 163u);
    EXPECT_EQ(saveGameNumberLabel(saves[0]), "Game 163");
    EXPECT_EQ(saves[0].descriptor.directory, directory);
    EXPECT_EQ(saves[0].descriptor.archive, directory / "SAVEGAME.sav");
}

TEST_F(SaveBrowserTest, k1DisplayNumberCannotRedirectAnExactSlotMutation) {
    auto unrelated = addSlot("000163 - game162", "Keep slot 163");
    auto selectedDirectory = addSlot("000164 - game163", "Overwrite slot 164");
    auto saves = discoverSavedGames(_root);
    auto selected = std::find_if(saves.begin(), saves.end(), [](const SavedGame &save) {
        return save.displayNumber == 163;
    });

    ASSERT_NE(selected, saves.end());
    EXPECT_EQ(selected->slot, 164u);
    EXPECT_EQ(selected->descriptor.directory, selectedDirectory);
    EXPECT_EQ(nextManualSaveSlot(saves), 165u);
    ASSERT_TRUE(deleteSavedGame(_root, selected->descriptor));
    EXPECT_FALSE(std::filesystem::exists(selectedDirectory));
    EXPECT_TRUE(std::filesystem::exists(unrelated));
}

TEST_F(SaveBrowserTest, k2NfoSaveNumberControlsDisplayButNotDurableIdentity) {
    auto directory = addSlot("000164 - game163", "K2 save", false, 91, 134317923752070828ULL);

    auto saves = discoverSavedGames(_root);

    ASSERT_EQ(saves.size(), 1);
    EXPECT_EQ(saves[0].slot, 164u);
    EXPECT_EQ(saves[0].displayNumber, 91u);
    EXPECT_EQ(saves[0].metadata.saveNumber, 91u);
    EXPECT_EQ(saves[0].metadata.timestamp, 134317923752070828ULL);
    EXPECT_EQ(saves[0].descriptor.directory, directory);
}

TEST_F(SaveBrowserTest, reoneK1ManualSaveUsesRetailManualDisplaySequence) {
    addSlot("000998 - FINAL_K1_SAVE", "FINAL_K1_SAVE");

    auto saves = discoverSavedGames(_root);

    ASSERT_EQ(saves.size(), 1);
    EXPECT_EQ(saves[0].slot, 998u);
    EXPECT_EQ(saves[0].displayNumber, 997u);
    EXPECT_EQ(nextManualSaveSlot(saves), 999u);
}

TEST_F(SaveBrowserTest, k1UserDirectorySuffixCannotOverrideManualDisplaySequence) {
    addSlot("000164 - game999", "User chose a game-like name");

    auto saves = discoverSavedGames(_root);

    ASSERT_EQ(saves.size(), 1);
    EXPECT_EQ(saves[0].slot, 164u);
    EXPECT_EQ(saves[0].displayNumber, 163u);
}

TEST_F(SaveBrowserTest, quickAndAutosaveIdentitiesDoNotUnderflowDisplayNumbers) {
    addSlot("000000 - QUICKSAVE", "QUICKSAVE");
    addSlot("000001 - AUTOSAVE", "AUTOSAVE");

    auto saves = discoverSavedGames(_root);

    ASSERT_EQ(saves.size(), 2);
    EXPECT_EQ(saves[0].slot, 0u);
    EXPECT_EQ(saves[0].displayNumber, 0u);
    EXPECT_EQ(saves[1].slot, 1u);
    EXPECT_EQ(saves[1].displayNumber, 1u);
    EXPECT_EQ(nextManualSaveSlot(saves), 2u);
}

TEST_F(SaveBrowserTest, omitsIncompleteMalformedAndNonSlotEntries) {
    auto incomplete = _root / "saves" / "000004 Incomplete";
    std::filesystem::create_directories(incomplete);
    std::ofstream(incomplete / "SAVEGAME.sav").put('x');
    auto malformed = _root / "saves" / "000005 Malformed";
    std::filesystem::create_directories(malformed);
    std::ofstream(malformed / "SAVEGAME.sav").put('x');
    std::ofstream(malformed / "savenfo.res").write("bad", 3);
    addSlot("not-a-slot", "Ignored");

    EXPECT_TRUE(discoverSavedGames(_root).empty());
}

TEST_F(SaveBrowserTest, numericIdentityDeduplicatesAndNewestCompleteDirectoryWins) {
    auto oldDirectory = addSlot("000012 Old", "Old");
    auto newDirectory = addSlot("000012 New", "New");
    auto oldTime = std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
    auto newTime = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(oldDirectory, oldTime);
    std::filesystem::last_write_time(newDirectory, newTime);

    auto saves = discoverSavedGames(_root);

    ASSERT_EQ(saves.size(), 1);
    EXPECT_EQ(saves[0].slot, 12);
    EXPECT_EQ(saves[0].metadata.savegameName, "New");
    EXPECT_EQ(saves[0].descriptor.directory, newDirectory);
}

TEST_F(SaveBrowserTest, manualAllocationReservesRetailQuickAndAutoSlots) {
    EXPECT_EQ(nextManualSaveSlot({}), 2);
    addSlot("000002 First");
    addSlot("000009 Later");
    EXPECT_EQ(nextManualSaveSlot(discoverSavedGames(_root)), 10);
}

TEST_F(SaveBrowserTest, deletesOnlyTheExactValidatedSlotDirectory) {
    auto directory = addSlot("000021 Delete Me");
    auto keep = addSlot("000022 Keep Me");
    auto saves = discoverSavedGames(_root);
    auto selected = std::find_if(saves.begin(), saves.end(), [](const auto &save) {
        return save.slot == 21;
    });
    ASSERT_NE(selected, saves.end());

    EXPECT_TRUE(deleteSavedGame(_root, selected->descriptor));
    EXPECT_FALSE(std::filesystem::exists(directory));
    EXPECT_TRUE(std::filesystem::exists(keep));

    SaveSlotDescriptor outside {_root, _root / "SAVEGAME.sav"};
    EXPECT_FALSE(deleteSavedGame(_root, outside));
    EXPECT_TRUE(std::filesystem::exists(_root));
}

} // namespace
