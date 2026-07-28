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

#include "reone/game/script/runner.h"
#include "reone/resource/format/erfwriter.h"
#include "reone/resource/provider/scripts.h"
#include "reone/resource/replacementresources.h"
#include "reone/resource/replacements.h"
#include "reone/resource/resources.h"
#include "reone/script/executioncontext.h"
#include "reone/script/executionstate.h"
#include "reone/script/format/ncswriter.h"
#include "reone/script/program.h"
#include "reone/script/virtualmachine.h"
#include "reone/system/stream/memoryoutput.h"

#include "../../fixtures/script.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;

namespace {

ByteBuffer ncsBytes(int value) {
    ScriptProgram program("replacement_test");
    program.add(Instruction::newCONSTI(value));
    program.add(Instruction(InstructionType::RETN));

    ByteBuffer bytes;
    auto stream = std::make_shared<MemoryOutputStream>(bytes);
    NcsWriter(program).save(stream);
    return bytes;
}

ByteBuffer erfBytes(std::string resRef, ByteBuffer data) {
    ErfWriter writer;
    writer.add(ErfWriter::Resource {std::move(resRef), ResType::Ncs, std::move(data)});
    ByteBuffer bytes;
    MemoryOutputStream stream(bytes);
    writer.save(ErfWriter::FileType::ERF, stream);
    return bytes;
}

class ScriptsReplacementTest : public testing::Test {
protected:
    void SetUp() override {
        _resources.addMemERF(erfBytes("script", ncsBytes(1)), ContainerKind::Global);
    }

    ResourceId id() const {
        return ResourceId("script", ResType::Ncs);
    }

    int run(const std::string &resRef = "script") {
        return ScriptRunner(_routines, _scripts).run(resRef, std::vector<Argument> {});
    }

    int runProgram(std::shared_ptr<ScriptProgram> program,
                   std::shared_ptr<ExecutionState> savedState = nullptr) {
        auto context = std::make_unique<ExecutionContext>();
        context->routines = &_routines;
        context->savedState = std::move(savedState);
        return VirtualMachine(std::move(program), std::move(context)).run();
    }

    ResourceReplacements _replacements;
    ReplacementResources _resources {std::make_unique<Resources>(), _replacements};
    Scripts _scripts {_resources, _replacements};
    MockRoutines _routines;
};

TEST_F(ScriptsReplacementTest, replacement_refreshes_an_already_cached_program) {
    EXPECT_EQ(1, run());
    auto original = _scripts.get("script");

    _replacements.replaceResource(id(), ncsBytes(2));

    auto replacement = _scripts.get("SCRIPT");
    EXPECT_NE(original, replacement);
    EXPECT_EQ(2, run());
    EXPECT_EQ(1, runProgram(original));
}

TEST_F(ScriptsReplacementTest, repeated_replacement_uses_the_newest_program) {
    _replacements.replaceResource(id(), ncsBytes(2));
    EXPECT_EQ(2, run());

    _replacements.replaceResource(id(), ncsBytes(3));

    EXPECT_EQ(3, run());
}

TEST_F(ScriptsReplacementTest, removing_a_replacement_reloads_the_underlying_program) {
    _replacements.replaceResource(id(), ncsBytes(2));
    EXPECT_EQ(2, run());

    _replacements.removeResourceReplacement(id());

    EXPECT_EQ(1, run());
}

TEST_F(ScriptsReplacementTest, clearing_replacements_reloads_the_underlying_program) {
    _replacements.replaceResource(id(), ncsBytes(2));
    EXPECT_EQ(2, run());

    _replacements.clearResourceReplacements();

    EXPECT_EQ(1, run());
}

TEST_F(ScriptsReplacementTest, clear_followed_by_replacement_does_not_alias_an_old_revision) {
    _replacements.replaceResource(id(), ncsBytes(2));
    EXPECT_EQ(2, run());
    _replacements.clearResourceReplacements();

    _replacements.replaceResource(id(), ncsBytes(3));

    EXPECT_EQ(3, run());
}

TEST_F(ScriptsReplacementTest, replacement_makes_a_cached_missing_script_available) {
    EXPECT_FALSE(_scripts.get("missing"));
    ResourceId missing("missing", ResType::Ncs);

    _replacements.replaceResource(missing, ncsBytes(4));

    EXPECT_EQ(4, run("missing"));
}

TEST_F(ScriptsReplacementTest, case_variants_share_one_canonical_cache_entry) {
    auto lower = _scripts.get("script");
    auto upper = _scripts.get("SCRIPT");

    EXPECT_EQ(lower, upper);
}

TEST_F(ScriptsReplacementTest, deferred_execution_state_keeps_its_original_program) {
    auto original = _scripts.get("script");
    auto savedState = std::make_shared<ExecutionState>();
    savedState->program = original;
    savedState->insOffset = 13;

    _replacements.replaceResource(id(), ncsBytes(2));

    EXPECT_EQ(2, run());
    EXPECT_EQ(1, runProgram(savedState->program, savedState));
}

} // namespace
