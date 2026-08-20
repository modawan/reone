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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"

#include "reone/game/game.h"
#include "reone/game/object/module.h"
#include "reone/game/script/routines.h"
#include "reone/resource/gff.h"
#include "reone/resource/parser/gff/ifo.h"
#include "reone/script/executioncontext.h"
#include "reone/script/program.h"
#include "reone/script/virtualmachine.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

void reone::game::TestGameModule::loadModuleInfo(
    Module &module,
    std::string name,
    const resource::Gff &ifo) {

    module._name = std::move(name);
    module.loadInfo(resource::generated::parseIFO(ifo));
}

namespace {

constexpr int kGetModuleName = 561;
constexpr int kGetSubString = 65;

// Talk table references picked per test, since the engine fixture is shared
// across the whole binary.
constexpr int kNihilusShipStrRef = 100735;
constexpr int kNarShaddaaStrRef = 87001;

// Mod_Entry_Area has to name something: loadInfo rejects an empty entry area.
std::shared_ptr<Gff> makeModuleIfo(int strRef, std::string modName) {
    return Gff::Builder()
        .field(Gff::Field::newCExoLocString("Mod_Name", strRef, std::move(modName)))
        .field(Gff::Field::newResRef("Mod_Entry_Area", "entryarea"))
        .build();
}

struct ModuleNameFixture {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game;
    Routines routines;
    script::ExecutionContext ctx;

    explicit ModuleNameFixture(GameID gameId = GameID::TSL) :
        game(gameId, "", engine.options(), engine.services(), console),
        routines(gameId, &game, &engine.services()) {

        routines.init();
    }

    // Bring up a module the way the game does: a resource name plus the module
    // IFO. The resource name is what reone normalizes to lower case, so it is
    // deliberately spelled that way here.
    std::shared_ptr<Module> loadModule(std::string name, int strRef, std::string modName) {
        TestGameModule::setActiveModule(game, true);
        auto module = game.module();
        TestGameModule::loadModuleInfo(*module, std::move(name), *makeModuleIfo(strRef, std::move(modName)));
        return module;
    }

    std::string callGetModuleName() {
        return routines.get(kGetModuleName).invoke({}, ctx).strValue;
    }

    // Run a program and return what it leaves on the stack: the virtual machine
    // yields the topmost int, or -1 if the script halted.
    int run(const std::shared_ptr<script::ScriptProgram> &program) {
        auto execution = std::make_unique<script::ExecutionContext>();
        execution->routines = &routines;
        return script::VirtualMachine(program, std::move(execution)).run();
    }

    void expectStrRef(int strRef, std::string text) {
        EXPECT_CALL(engine.resourceModule().strings(), getText(strRef))
            .Times(AnyNumber())
            .WillRepeatedly(Return(std::move(text)));
    }
};

} // namespace

// 1. The critical discriminator. A KotOR II module names itself through a talk
// table reference, and the resolved text is what scripts see - not the module's
// resource name, which reone holds in lower case.
TEST(GetModuleName, returns_the_localized_module_name_not_the_resource_name) {
    ModuleNameFixture fixture;
    fixture.expectStrRef(kNihilusShipStrRef, "851NIH");

    auto module = fixture.loadModule("851nih", kNihilusShipStrRef, "");

    // The two really do differ, which is the whole point of the routine.
    ASSERT_EQ("851nih", module->name());
    EXPECT_EQ("851NIH", module->localizedName());

    EXPECT_EQ("851NIH", fixture.callGetModuleName());
}

// 2. The shape shipped scripts use: the result is compared against an uppercase
// literal with the case-sensitive string comparison, so the exact spelling has
// to survive all the way into the virtual machine.
TEST(GetModuleName, satisfies_a_case_sensitive_comparison_in_a_compiled_script) {
    ModuleNameFixture fixture;
    fixture.expectStrRef(kNihilusShipStrRef, "851NIH");
    fixture.loadModule("851nih", kNihilusShipStrRef, "");

    auto program = std::make_shared<script::ScriptProgram>("compare_module_name");
    program->add(script::Instruction::newACTION(kGetModuleName, 0));
    program->add(script::Instruction::newCONSTS("851NIH"));
    program->add(script::Instruction(script::InstructionType::EQUALSS));
    program->add(script::Instruction(script::InstructionType::RETN));

    EXPECT_EQ(1, fixture.run(program));

    // The lower-case resource name must not satisfy the same comparison.
    auto rejecting = std::make_shared<script::ScriptProgram>("compare_module_name_lower");
    rejecting->add(script::Instruction::newACTION(kGetModuleName, 0));
    rejecting->add(script::Instruction::newCONSTS("851nih"));
    rejecting->add(script::Instruction(script::InstructionType::EQUALSS));
    rejecting->add(script::Instruction(script::InstructionType::RETN));

    EXPECT_EQ(0, fixture.run(rejecting));
}

// 3. Shipped scripts also slice the result to recover the three-letter planet
// code. Arguments are pushed so that argument 0 - the string - ends up on top,
// and the offset and length are the ones shipped content passes.
TEST(GetModuleName, feeds_a_planet_code_substring_in_a_compiled_script) {
    ModuleNameFixture fixture;
    fixture.expectStrRef(kNarShaddaaStrRef, "301NAR");
    fixture.loadModule("301nar", kNarShaddaaStrRef, "");

    auto program = std::make_shared<script::ScriptProgram>("module_planet_code");
    program->add(script::Instruction::newCONSTI(3)); // nCount
    program->add(script::Instruction::newCONSTI(3)); // nStart
    program->add(script::Instruction::newACTION(kGetModuleName, 0));
    program->add(script::Instruction::newACTION(kGetSubString, 3));
    program->add(script::Instruction::newCONSTS("NAR"));
    program->add(script::Instruction(script::InstructionType::EQUALSS));
    program->add(script::Instruction(script::InstructionType::RETN));

    EXPECT_EQ(1, fixture.run(program));
}

// 4. KotOR modules usually carry the text inline instead of referencing the talk
// table, and the authored value is not always the module's resource name. It is
// returned as authored, which also pins down that the routine is not just
// upper-casing the resource name.
TEST(GetModuleName, returns_an_inline_localized_name_verbatim) {
    ModuleNameFixture fixture(GameID::KotOR);

    auto module = fixture.loadModule("danm14aa", /*strRef=*/-1, "Dantooine");

    EXPECT_EQ("Dantooine", module->localizedName());
    EXPECT_EQ("Dantooine", fixture.callGetModuleName());
}

// 5. Neither form present: the documented result is an empty string.
TEST(GetModuleName, returns_an_empty_string_when_the_name_resolves_to_nothing) {
    ModuleNameFixture fixture(GameID::KotOR);

    auto module = fixture.loadModule("tat_m17mg", /*strRef=*/-1, "");

    EXPECT_TRUE(module->localizedName().empty());
    EXPECT_EQ("", fixture.callGetModuleName());
}

// 6. The routine is declared in both games' script sets.
TEST(GetModuleName, is_registered_for_both_games) {
    TestEngine &engine = testEngine();
    StubConsole console;

    Game k1Game(GameID::KotOR, "", engine.options(), engine.services(), console);
    Routines k1(GameID::KotOR, &k1Game, &engine.services());
    k1.init();
    EXPECT_EQ("GetModuleName", k1.get(kGetModuleName).name());

    Game k2Game(GameID::TSL, "", engine.options(), engine.services(), console);
    Routines k2(GameID::TSL, &k2Game, &engine.services());
    k2.init();
    EXPECT_EQ("GetModuleName", k2.get(kGetModuleName).name());
}

// 7. Defensive reone behaviour, not established retail semantics: scripts do not
// run without a module, but the routine yields an empty string rather than
// failing if one ever does.
TEST(GetModuleName, yields_an_empty_string_when_no_module_is_active) {
    ModuleNameFixture fixture;
    TestGameModule::setActiveModule(fixture.game, false);
    ASSERT_FALSE(fixture.game.module());

    EXPECT_EQ("", fixture.callGetModuleName());
}
