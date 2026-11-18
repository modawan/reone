/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "reone/game/game.h"
#include "reone/game/script/routines.h"
#include "reone/resource/types.h"
#include "reone/script/executioncontext.h"
#include "reone/script/types.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;

namespace {

// The harness has its own engine, as expectations other tests leave on the
// shared one's mocks would catch the lookup of a missing script.
class RunScriptVarHarness : boost::noncopyable {
public:
    RunScriptVarHarness() {
        _engine.init();
        _game = std::make_unique<Game>(GameID::KotOR, "", _engine.options(), _engine.services(), _console);
        _routines = std::make_unique<Routines>(GameID::KotOR, _game.get(), &_engine.services());
        _game->initLocalServices();
        _routines->init();
    }

    Variable call(const std::string &name, std::vector<Variable> args = {}) {
        Routine &routine = _routines->get(_routines->getIndexByName(name));
        ExecutionContext execution;
        execution.routines = _routines.get();
        return routine.invoke(args, execution);
    }

    // The script need not exist: the variable is set before it runs.
    void executeScript(std::vector<Variable> args) {
        args.insert(args.begin(), Variable::ofString("no_such_script"));
        call("ExecuteScript", std::move(args));
    }

    int runScriptVar() { return call("GetRunScriptVar").intValue; }

private:
    TestEngine _engine;
    StubConsole _console;
    std::unique_ptr<Game> _game;
    std::unique_ptr<Routines> _routines;
};

} // namespace

TEST(RunScriptVar, is_zero_until_a_script_is_executed_with_one) {
    RunScriptVarHarness harness;
    EXPECT_EQ(0, harness.runScriptVar());

    harness.executeScript({Variable::ofObject(kObjectSelf), Variable::ofInt(5)});

    EXPECT_EQ(5, harness.runScriptVar());
}

TEST(RunScriptVar, every_call_with_a_variable_replaces_it_and_one_without_leaves_it) {
    RunScriptVarHarness harness;
    harness.executeScript({Variable::ofObject(kObjectSelf), Variable::ofInt(5)});

    harness.executeScript({Variable::ofObject(kObjectSelf)});
    EXPECT_EQ(5, harness.runScriptVar());

    // A compiled call without a variable passes its -1 default.
    harness.executeScript({Variable::ofObject(kObjectSelf), Variable::ofInt(-1)});
    EXPECT_EQ(-1, harness.runScriptVar());
}
