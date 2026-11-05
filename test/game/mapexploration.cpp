/*
 * Copyright (c) 2020-2026 The reone project contributors
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

#include "../fixtures/engine.h"

#include "reone/game/game.h"
#include "reone/game/mapexploration.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/resource/gff.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;

void reone::game::TestGameModule::loadAreaMapExploration(
    Area &area,
    const generated::ARE_Map &map,
    const Gff &are) {

    area.loadMapExploration(map, are);
}

void reone::game::TestGameModule::updateMapExploration(Area &area) {
    area.updateMapExploration();
}

namespace {

// Every cell of a grid, as a string of rows, '#' explored and '.' not, so a
// failure shows the whole grid.
std::string picture(const MapExploration &grid) {
    std::string result;
    for (int row = 0; row < grid.rows(); ++row) {
        for (int column = 0; column < grid.columns(); ++column) {
            result += grid.isExplored(column, row) ? '#' : '.';
        }
        result += '\n';
    }
    return result;
}

// A map that puts world (0, 0) at the top left of the canvas and world
// (100, 100) at its bottom right, so a world point divided by 100 is its map
// position.
generated::ARE_Map squareMap(int mapResX) {
    generated::ARE_Map map;
    map.MapResX = mapResX;
    map.NorthAxis = 0;
    map.WorldPt1X = 0.0f;
    map.WorldPt1Y = 0.0f;
    map.WorldPt2X = 100.0f;
    map.WorldPt2Y = 100.0f;
    map.MapPt1X = 0.0f;
    map.MapPt1Y = 0.0f;
    map.MapPt2X = 1.0f;
    map.MapPt2Y = 1.0f;
    return map;
}

// Loads an area's map as Area::load does, from an area record that keeps no
// explored cells unless one is given.
void loadMap(Area &area, const generated::ARE_Map &map, const Gff *are = nullptr) {
    auto empty = Gff::Builder().build();
    TestGameModule::loadAreaMapExploration(area, map, are ? *are : *empty);
}

} // namespace

TEST(MapExploration, grid_takes_its_columns_from_map_res_x_and_square_rows) {
    // KotOR's canvas is 440 by 256: 15 * 256 / 440 = 8.7, 24 * 256 / 440 = 14.0.
    auto endar = MapExploration::forMapResX(15, false);
    EXPECT_EQ(15, endar.columns());
    EXPECT_EQ(9, endar.rows());
    auto taris = MapExploration::forMapResX(24, false);
    EXPECT_EQ(24, taris.columns());
    EXPECT_EQ(14, taris.rows());

    // TSL's canvas is 512 by 256, so half as many rows as columns.
    auto tsl = MapExploration::forMapResX(24, true);
    EXPECT_EQ(24, tsl.columns());
    EXPECT_EQ(12, tsl.rows());
    EXPECT_EQ(8, MapExploration::forMapResX(15, true).rows());

    // A single column still gets a row, and no MapResX gives no grid.
    EXPECT_EQ(1, MapExploration::forMapResX(1, false).rows());
    EXPECT_TRUE(MapExploration::forMapResX(0, false).empty());
    EXPECT_TRUE(MapExploration::forMapResX(-3, true).empty());

    EXPECT_EQ(0, endar.exploredCount());
}

TEST(MapExploration, reveal_around_marks_exactly_the_cells_within_the_radius) {
    MapExploration grid(10, 6);

    EXPECT_TRUE(grid.revealAround(glm::ivec2(4, 3), 2));

    EXPECT_EQ(
        "..........\n"
        "....#.....\n"
        "...###....\n"
        "..#####...\n"
        "...###....\n"
        "....#.....\n",
        picture(grid));
    EXPECT_EQ(13, grid.exploredCount());
}

TEST(MapExploration, reveal_around_clamps_at_edges_and_corners) {
    MapExploration grid(5, 4);

    grid.revealAround(glm::ivec2(0, 0), 2);
    grid.revealAround(glm::ivec2(4, 3), 1);

    EXPECT_EQ(
        "###..\n"
        "##...\n"
        "#...#\n"
        "...##\n",
        picture(grid));
    EXPECT_FALSE(grid.isExplored(-1, 0));
    EXPECT_FALSE(grid.isExplored(5, 3));
    EXPECT_FALSE(grid.isExplored(0, 4));
}

TEST(MapExploration, reveal_around_a_centre_off_the_grid_reaches_only_cells_in_range) {
    MapExploration grid(5, 4);

    EXPECT_FALSE(grid.revealAround(glm::ivec2(-10, -10), 2));
    EXPECT_FALSE(grid.revealAround(glm::ivec2(1000000, 2), 3));
    EXPECT_EQ(0, grid.exploredCount());

    EXPECT_TRUE(grid.revealAround(glm::ivec2(-1, 0), 1));
    EXPECT_EQ(
        "#....\n"
        ".....\n"
        ".....\n"
        ".....\n",
        picture(grid));

    // A huge radius from far off reaches the whole grid without overflowing.
    EXPECT_TRUE(grid.revealAround(glm::ivec2(-1000000, 1000000), std::numeric_limits<int>::max()));
    EXPECT_EQ(20, grid.exploredCount());
}

TEST(MapExploration, radius_zero_reveals_one_cell_and_a_negative_radius_none) {
    MapExploration grid(5, 4);

    EXPECT_FALSE(grid.revealAround(glm::ivec2(2, 2), -1));
    EXPECT_EQ(0, grid.exploredCount());

    EXPECT_TRUE(grid.revealAround(glm::ivec2(2, 2), 0));
    EXPECT_EQ(1, grid.exploredCount());
    EXPECT_TRUE(grid.isExplored(2, 2));
}

TEST(MapExploration, reveal_around_a_map_position_uses_the_cell_it_falls_in) {
    MapExploration grid(10, 6);

    EXPECT_EQ(glm::ivec2(4, 3), grid.cellAt(glm::vec2(0.45f, 0.55f)));
    EXPECT_EQ(glm::ivec2(-1, 6), grid.cellAt(glm::vec2(-0.01f, 1.0f)));

    EXPECT_TRUE(grid.revealAround(glm::vec2(0.45f, 0.55f), 0));
    EXPECT_TRUE(grid.isExplored(4, 3));
    EXPECT_EQ(1, grid.exploredCount());

    // Positions a map with no reference points gives reveal nothing.
    float nan = std::numeric_limits<float>::quiet_NaN();
    float inf = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(grid.revealAround(glm::vec2(nan, 0.5f), 5));
    EXPECT_FALSE(grid.revealAround(glm::vec2(0.5f, inf), 5));
    EXPECT_EQ(1, grid.exploredCount());

    // Far off the map, the cell index stays representable.
    auto far = grid.cellAt(glm::vec2(1e30f, -1e30f));
    EXPECT_GT(far.x, 1000000);
    EXPECT_LT(far.y, -1000000);
}

TEST(MapExploration, reveal_all_explores_every_cell_once) {
    MapExploration grid(7, 4);
    auto before = grid.revision();

    EXPECT_TRUE(grid.revealAll());
    EXPECT_EQ(28, grid.exploredCount());
    EXPECT_NE(before, grid.revision());

    auto revealed = grid.revision();
    EXPECT_FALSE(grid.revealAll());
    EXPECT_FALSE(grid.revealAround(glm::ivec2(3, 2), 2));
    EXPECT_EQ(revealed, grid.revision());

    MapExploration none;
    EXPECT_FALSE(none.revealAll());
    EXPECT_FALSE(none.revealAround(glm::ivec2(0, 0), 3));
    EXPECT_FALSE(none.isExplored(0, 0));
}

TEST(MapExploration, separately_built_grids_never_share_a_revision) {
    MapExploration first(3, 3);
    MapExploration second(3, 3);
    EXPECT_NE(first.revision(), second.revision());

    MapExploration copy(first);
    EXPECT_EQ(first.revision(), copy.revision());
    copy.revealAll();
    EXPECT_NE(first.revision(), copy.revision());
}

TEST(MapExploration, bytes_round_trip) {
    MapExploration grid(15, 9);
    grid.revealAround(glm::ivec2(3, 4), 2);
    grid.revealAround(glm::ivec2(14, 8), 1);

    auto bytes = grid.toBytes();
    EXPECT_EQ(static_cast<size_t>((15 * 9 + 7) / 8), bytes.size());

    auto restored = MapExploration::forMapResX(15, false);
    restored.fromBytes(15, 9, bytes);
    EXPECT_EQ(picture(grid), picture(restored));
}

TEST(MapExploration, gff_round_trip) {
    MapExploration grid(24, 14);
    grid.revealAround(glm::ivec2(10, 7), 3);

    auto are = Gff::Builder()
                   .field(Gff::Field::newStruct(kAreaMapStructLabel, grid.toGff()))
                   .build();
    auto areaMap = are->findStruct(kAreaMapStructLabel);
    ASSERT_TRUE(areaMap);
    EXPECT_EQ(24, areaMap->getInt("AreaMapResX"));
    EXPECT_EQ(14, areaMap->getInt("AreaMapResY"));
    EXPECT_EQ(grid.toBytes(), areaMap->getData("AreaMapData"));

    auto restored = MapExploration::forMapResX(24, false);
    restored.loadGff(*are);
    EXPECT_EQ(picture(grid), picture(restored));
}

TEST(MapExploration, missing_short_or_mismatched_data_is_safe) {
    MapExploration grid(10, 6);

    // Short data: the cells it covers come back, the rest stay unexplored.
    grid.fromBytes(10, 6, ByteBuffer {static_cast<char>(0xff)});
    EXPECT_EQ(8, grid.exploredCount());
    EXPECT_TRUE(grid.isExplored(7, 0));
    EXPECT_FALSE(grid.isExplored(8, 0));

    grid.fromBytes(10, 6, ByteBuffer());
    EXPECT_EQ(0, grid.exploredCount());

    // A grid of another size, as after the area's map changed, is dropped.
    MapExploration other(5, 3);
    other.revealAll();
    grid.revealAll();
    grid.fromBytes(5, 3, other.toBytes());
    EXPECT_EQ(0, grid.exploredCount());

    // An area record without the struct, such as an older save, has nothing explored.
    grid.revealAll();
    grid.loadGff(*Gff::Builder().build());
    EXPECT_EQ(0, grid.exploredCount());

    // Nor does one whose struct lacks the fields.
    grid.revealAll();
    grid.loadGff(*Gff::Builder().field(Gff::Field::newStruct(kAreaMapStructLabel, Gff::Builder().build())).build());
    EXPECT_EQ(0, grid.exploredCount());
}

TEST(MapExploration, world_positions_convert_through_the_area_map_points_on_every_north_axis) {
    // Reference points with offsets and different scales on the two axes, so
    // a swapped scale or a dropped offset shows.
    generated::ARE_Map map;
    map.WorldPt1X = 10.0f;
    map.WorldPt1Y = 20.0f;
    map.WorldPt2X = 110.0f;
    map.WorldPt2Y = 70.0f;
    map.MapPt1X = 0.1f;
    map.MapPt1Y = 0.2f;
    map.MapPt2X = 0.6f;
    map.MapPt2Y = 0.9f;
    const glm::vec2 world(60.0f, 30.0f);

    // Axes 0 and 1: map x follows world x, map y world y.
    //   scale x = (0.1 - 0.6) / (10 - 110) = 0.005
    //   scale y = (0.2 - 0.9) / (20 - 70)  = 0.014
    //   x = (60 - 10) * 0.005 + 0.1 = 0.35
    //   y = (30 - 20) * 0.014 + 0.2 = 0.34
    for (int axis : {0, 1}) {
        map.NorthAxis = axis;
        auto position = worldToMapPosition(map, world);
        EXPECT_NEAR(0.35f, position.x, 1e-5f) << "axis " << axis;
        EXPECT_NEAR(0.34f, position.y, 1e-5f) << "axis " << axis;
    }

    // Axes 2 and 3: map x follows world y, map y world x.
    //   scale of world x = (0.2 - 0.9) / (10 - 110) = 0.007
    //   scale of world y = (0.1 - 0.6) / (20 - 70)  = 0.01
    //   x = (30 - 20) * 0.01  + 0.1 = 0.2
    //   y = (60 - 10) * 0.007 + 0.2 = 0.55
    for (int axis : {2, 3}) {
        map.NorthAxis = axis;
        auto position = worldToMapPosition(map, world);
        EXPECT_NEAR(0.2f, position.x, 1e-5f) << "axis " << axis;
        EXPECT_NEAR(0.55f, position.y, 1e-5f) << "axis " << axis;
    }

    // Any other axis places everything at the top left corner.
    for (int axis : {-1, 4}) {
        map.NorthAxis = axis;
        EXPECT_FALSE(isValidMapNorthAxis(axis));
        auto position = worldToMapPosition(map, world);
        EXPECT_EQ(0.0f, position.x) << "axis " << axis;
        EXPECT_EQ(0.0f, position.y) << "axis " << axis;
    }
    for (int axis : {0, 1, 2, 3}) {
        EXPECT_TRUE(isValidMapNorthAxis(axis));
    }
}

namespace {

class MapExplorationGame : public ::testing::TestWithParam<GameID> {
protected:
    MapExplorationGame() :
        _game(GetParam(), "", testEngine().options(), testEngine().services(), _console),
        _routines(GetParam(), &_game, &testEngine().services()) {

        _routines.init();
    }

    void SetUp() override {
        _area = _game.newArea();
        TestGameModule::setActiveModuleArea(_game, _area);
        loadMap(*_area, squareMap(10));

        _leader = _game.newCreature();
        _game.party().addMember(kNpcPlayer, _leader);
        _game.party().setPlayer(_leader);
        _game.party().setActualPlayer(_leader);
    }

    void callRevealMap(std::vector<Variable> args) {
        Routine &routine = _routines.get(_routines.getIndexByName("RevealMap"));
        ExecutionContext execution;
        execution.routines = &_routines;
        routine.invoke(args, execution);
    }

    const MapExploration &grid() const { return _area->mapExploration(); }

    StubConsole _console;
    Game _game;
    Routines _routines;
    std::shared_ptr<Area> _area;
    std::shared_ptr<Creature> _leader;
};

} // namespace

TEST_P(MapExplorationGame, walking_reveals_the_cells_around_the_party_leader) {
    // KotOR: 10 columns by round(10 * 256 / 440) = 6 rows; TSL: 10 by 5.
    ASSERT_EQ(10, grid().columns());
    ASSERT_EQ(GetParam() == GameID::TSL ? 5 : 6, grid().rows());

    _leader->setPosition(glm::vec3(45.0f, 55.0f, 0.0f));
    TestGameModule::updateMapExploration(*_area);
    auto cell = grid().cellAt(glm::vec2(0.45f, 0.55f));
    EXPECT_TRUE(grid().isExplored(cell.x, cell.y));
    EXPECT_TRUE(grid().isExplored(cell.x + 2, cell.y));
    EXPECT_TRUE(grid().isExplored(cell.x + 1, cell.y + 1));
    EXPECT_FALSE(grid().isExplored(cell.x + 2, cell.y + 1));
    EXPECT_FALSE(grid().isExplored(cell.x + 3, cell.y));
    auto explored = grid().exploredCount();
    auto revision = grid().revision();

    // Moving within the cell changes nothing.
    _leader->setPosition(glm::vec3(46.0f, 56.0f, 0.0f));
    TestGameModule::updateMapExploration(*_area);
    EXPECT_EQ(revision, grid().revision());

    // Walking to the far corner reveals there too, and keeps what was seen.
    _leader->setPosition(glm::vec3(99.0f, 1.0f, 0.0f));
    TestGameModule::updateMapExploration(*_area);
    EXPECT_TRUE(grid().isExplored(9, 0));
    EXPECT_TRUE(grid().isExplored(7, 0));
    EXPECT_FALSE(grid().isExplored(6, 0));
    EXPECT_TRUE(grid().isExplored(cell.x, cell.y));
    EXPECT_EQ(explored + 6, grid().exploredCount());
}

TEST_P(MapExplorationGame, walking_reveals_nothing_without_a_map_grid) {
    loadMap(*_area, squareMap(0));
    _leader->setPosition(glm::vec3(45.0f, 55.0f, 0.0f));
    TestGameModule::updateMapExploration(*_area);
    EXPECT_TRUE(grid().empty());

    // An area file without reference points puts the leader nowhere.
    generated::ARE_Map noPoints;
    noPoints.MapResX = 10;
    loadMap(*_area, noPoints);
    TestGameModule::updateMapExploration(*_area);
    EXPECT_EQ(0, grid().exploredCount());
}

TEST_P(MapExplorationGame, loading_the_map_sizes_the_grid_and_restores_the_saved_cells) {
    // The saved cells, for the grid this game sizes from MapResX 10.
    auto saved = MapExploration::forMapResX(10, GetParam() == GameID::TSL);
    saved.revealAround(glm::ivec2(2, 1), 1);
    saved.revealAround(glm::ivec2(9, 4), 0);
    auto are = Gff::Builder()
                   .field(Gff::Field::newStruct(kAreaMapStructLabel, saved.toGff()))
                   .build();

    loadMap(*_area, squareMap(10), are.get());

    ASSERT_EQ(saved.columns(), grid().columns());
    ASSERT_EQ(saved.rows(), grid().rows());
    EXPECT_EQ(picture(saved), picture(grid()));
    EXPECT_EQ(6, grid().exploredCount());

    // A record without them, such as an older save, starts with nothing explored.
    loadMap(*_area, squareMap(10));
    EXPECT_EQ(0, grid().exploredCount());
}

TEST_P(MapExplorationGame, loading_the_map_forgets_the_last_revealed_cell) {
    _leader->setPosition(glm::vec3(45.0f, 55.0f, 0.0f));
    TestGameModule::updateMapExploration(*_area);
    ASSERT_GT(grid().exploredCount(), 0);

    // Standing in the same cell after the area loads anew still reveals it.
    loadMap(*_area, squareMap(10));
    ASSERT_EQ(0, grid().exploredCount());
    TestGameModule::updateMapExploration(*_area);
    EXPECT_GT(grid().exploredCount(), 0);
}

TEST_P(MapExplorationGame, an_invalid_north_axis_leaves_the_area_without_a_grid) {
    auto map = squareMap(10);
    map.NorthAxis = 4;
    loadMap(*_area, map);
    EXPECT_TRUE(grid().empty());

    _leader->setPosition(glm::vec3(45.0f, 55.0f, 0.0f));
    TestGameModule::updateMapExploration(*_area);
    callRevealMap({});
    EXPECT_EQ(0, grid().exploredCount());
}

TEST_P(MapExplorationGame, the_map_is_not_revealed_while_the_game_is_paused) {
    _leader->setPosition(glm::vec3(45.0f, 55.0f, 0.0f));

    _game.setPaused(true);
    TestGameModule::updateMapExploration(*_area);
    EXPECT_EQ(0, grid().exploredCount());

    _game.setPaused(false);
    TestGameModule::updateMapExploration(*_area);
    EXPECT_GT(grid().exploredCount(), 0);
}

TEST_P(MapExplorationGame, reveal_map_without_arguments_reveals_the_whole_map) {
    callRevealMap({});
    EXPECT_EQ(grid().columns() * grid().rows(), grid().exploredCount());
}

TEST_P(MapExplorationGame, reveal_map_with_a_point_and_radius_reveals_only_around_it) {
    callRevealMap({Variable::ofVector(glm::vec3(45.0f, 55.0f, 0.0f)), Variable::ofInt(1)});

    auto cell = grid().cellAt(glm::vec2(0.45f, 0.55f));
    EXPECT_EQ(5, grid().exploredCount());
    EXPECT_TRUE(grid().isExplored(cell.x, cell.y));
    EXPECT_TRUE(grid().isExplored(cell.x - 1, cell.y));
    EXPECT_TRUE(grid().isExplored(cell.x, cell.y + 1));
    EXPECT_FALSE(grid().isExplored(cell.x + 1, cell.y + 1));
}

TEST_P(MapExplorationGame, reveal_map_with_a_point_and_the_default_radius_reveals_the_whole_map) {
    callRevealMap({Variable::ofVector(glm::vec3(45.0f, 55.0f, 0.0f)), Variable::ofInt(-1)});
    EXPECT_EQ(grid().columns() * grid().rows(), grid().exploredCount());

    // A point with the radius left out is the same.
    _area = _game.newArea();
    TestGameModule::setActiveModuleArea(_game, _area);
    loadMap(*_area, squareMap(10));
    ASSERT_EQ(0, grid().exploredCount());
    callRevealMap({Variable::ofVector(glm::vec3(45.0f, 55.0f, 0.0f))});
    EXPECT_EQ(grid().columns() * grid().rows(), grid().exploredCount());
}

TEST_P(MapExplorationGame, reveal_map_without_an_area_does_nothing) {
    TestGameModule::setActiveModuleArea(_game, nullptr);
    EXPECT_NO_THROW(callRevealMap({}));
    EXPECT_NO_THROW(callRevealMap({Variable::ofVector(glm::vec3(1.0f)), Variable::ofInt(2)}));

    TestGameModule::setActiveModule(_game, false);
    ASSERT_FALSE(_game.module());
    EXPECT_NO_THROW(callRevealMap({}));
}

INSTANTIATE_TEST_SUITE_P(
    BothGames,
    MapExplorationGame,
    ::testing::Values(GameID::KotOR, GameID::TSL),
    [](const ::testing::TestParamInfo<GameID> &info) {
        return info.param == GameID::TSL ? std::string("TSL") : std::string("KotOR");
    });
