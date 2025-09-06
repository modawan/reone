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

#include "reone/script/virtualmachine.h"

#include "reone/script/executioncontext.h"
#include "reone/script/instrutil.h"
#include "reone/script/program.h"
#include "reone/script/routine.h"
#include "reone/script/routines.h"
#include "reone/script/variable.h"
#include "reone/system/logger.h"
#include "reone/system/logutil.h"

namespace reone {

namespace script {

static constexpr int kStartInstructionOffset = 13;
static constexpr float kFloatTolerance = 1e-5;

VirtualMachine::VirtualMachine(std::shared_ptr<ScriptProgram> program, std::unique_ptr<ExecutionContext> context) :
    _context(std::move(context)),
    _program(std::move(program)) {

    _logEnabled = Logger::instance.isChannelEnabled(LogChannel::Script3);

    static std::unordered_map<InstructionType, std::function<void(VirtualMachine *, const Instruction &)>> g_handlers {
        {InstructionType::CPDOWNSP, &VirtualMachine::executeCPDOWNSP},
        {InstructionType::RSADDI, &VirtualMachine::executeRSADDI},
        {InstructionType::RSADDF, &VirtualMachine::executeRSADDF},
        {InstructionType::RSADDS, &VirtualMachine::executeRSADDS},
        {InstructionType::RSADDO, &VirtualMachine::executeRSADDO},
        {InstructionType::RSADDEFF, &VirtualMachine::executeRSADDEFF},
        {InstructionType::RSADDEVT, &VirtualMachine::executeRSADDEVT},
        {InstructionType::RSADDLOC, &VirtualMachine::executeRSADDLOC},
        {InstructionType::RSADDTAL, &VirtualMachine::executeRSADDTAL},
        {InstructionType::CPTOPSP, &VirtualMachine::executeCPTOPSP},
        {InstructionType::CONSTI, &VirtualMachine::executeCONSTI},
        {InstructionType::CONSTF, &VirtualMachine::executeCONSTF},
        {InstructionType::CONSTS, &VirtualMachine::executeCONSTS},
        {InstructionType::CONSTO, &VirtualMachine::executeCONSTO},
        {InstructionType::ACTION, &VirtualMachine::executeACTION},
        {InstructionType::LOGANDII, &VirtualMachine::executeLOGANDII},
        {InstructionType::LOGORII, &VirtualMachine::executeLOGORII},
        {InstructionType::INCORII, &VirtualMachine::executeINCORII},
        {InstructionType::EXCORII, &VirtualMachine::executeEXCORII},
        {InstructionType::BOOLANDII, &VirtualMachine::executeBOOLANDII},
        {InstructionType::EQUALII, &VirtualMachine::executeEQUALII},
        {InstructionType::EQUALFF, &VirtualMachine::executeEQUALFF},
        {InstructionType::EQUALSS, &VirtualMachine::executeEQUALSS},
        {InstructionType::EQUALOO, &VirtualMachine::executeEQUALOO},
        {InstructionType::EQUALTT, &VirtualMachine::executeEQUALTT},
        {InstructionType::EQUALEFFEFF, &VirtualMachine::executeEQUALEFFEFF},
        {InstructionType::EQUALEVTEVT, &VirtualMachine::executeEQUALEVTEVT},
        {InstructionType::EQUALLOCLOC, &VirtualMachine::executeEQUALLOCLOC},
        {InstructionType::EQUALTALTAL, &VirtualMachine::executeEQUALTALTAL},
        {InstructionType::NEQUALII, &VirtualMachine::executeNEQUALII},
        {InstructionType::NEQUALFF, &VirtualMachine::executeNEQUALFF},
        {InstructionType::NEQUALSS, &VirtualMachine::executeNEQUALSS},
        {InstructionType::NEQUALOO, &VirtualMachine::executeNEQUALOO},
        {InstructionType::NEQUALTT, &VirtualMachine::executeNEQUALTT},
        {InstructionType::NEQUALEFFEFF, &VirtualMachine::executeNEQUALEFFEFF},
        {InstructionType::NEQUALEVTEVT, &VirtualMachine::executeNEQUALEVTEVT},
        {InstructionType::NEQUALLOCLOC, &VirtualMachine::executeNEQUALLOCLOC},
        {InstructionType::NEQUALTALTAL, &VirtualMachine::executeNEQUALTALTAL},
        {InstructionType::GEQII, &VirtualMachine::executeGEQII},
        {InstructionType::GEQFF, &VirtualMachine::executeGEQFF},
        {InstructionType::GTII, &VirtualMachine::executeGTII},
        {InstructionType::GTFF, &VirtualMachine::executeGTFF},
        {InstructionType::LTII, &VirtualMachine::executeLTII},
        {InstructionType::LTFF, &VirtualMachine::executeLTFF},
        {InstructionType::LEQII, &VirtualMachine::executeLEQII},
        {InstructionType::LEQFF, &VirtualMachine::executeLEQFF},
        {InstructionType::SHLEFTII, &VirtualMachine::executeSHLEFTII},
        {InstructionType::SHRIGHTII, &VirtualMachine::executeSHRIGHTII},
        {InstructionType::USHRIGHTII, &VirtualMachine::executeUSHRIGHTII},
        {InstructionType::ADDII, &VirtualMachine::executeADDII},
        {InstructionType::ADDIF, &VirtualMachine::executeADDIF},
        {InstructionType::ADDFI, &VirtualMachine::executeADDFI},
        {InstructionType::ADDFF, &VirtualMachine::executeADDFF},
        {InstructionType::ADDSS, &VirtualMachine::executeADDSS},
        {InstructionType::ADDVV, &VirtualMachine::executeADDVV},
        {InstructionType::SUBII, &VirtualMachine::executeSUBII},
        {InstructionType::SUBIF, &VirtualMachine::executeSUBIF},
        {InstructionType::SUBFI, &VirtualMachine::executeSUBFI},
        {InstructionType::SUBFF, &VirtualMachine::executeSUBFF},
        {InstructionType::SUBVV, &VirtualMachine::executeSUBVV},
        {InstructionType::MULII, &VirtualMachine::executeMULII},
        {InstructionType::MULIF, &VirtualMachine::executeMULIF},
        {InstructionType::MULFI, &VirtualMachine::executeMULFI},
        {InstructionType::MULFF, &VirtualMachine::executeMULFF},
        {InstructionType::MULVF, &VirtualMachine::executeMULVF},
        {InstructionType::MULFV, &VirtualMachine::executeMULFV},
        {InstructionType::DIVII, &VirtualMachine::executeDIVII},
        {InstructionType::DIVIF, &VirtualMachine::executeDIVIF},
        {InstructionType::DIVFI, &VirtualMachine::executeDIVFI},
        {InstructionType::DIVFF, &VirtualMachine::executeDIVFF},
        {InstructionType::DIVVF, &VirtualMachine::executeDIVVF},
        {InstructionType::DIVFV, &VirtualMachine::executeDIVFV},
        {InstructionType::MODII, &VirtualMachine::executeMODII},
        {InstructionType::NEGI, &VirtualMachine::executeNEGI},
        {InstructionType::NEGF, &VirtualMachine::executeNEGF},
        {InstructionType::MOVSP, &VirtualMachine::executeMOVSP},
        {InstructionType::JMP, &VirtualMachine::executeJMP},
        {InstructionType::JSR, &VirtualMachine::executeJSR},
        {InstructionType::JZ, &VirtualMachine::executeJZ},
        {InstructionType::RETN, &VirtualMachine::executeRETN},
        {InstructionType::DESTRUCT, &VirtualMachine::executeDESTRUCT},
        {InstructionType::NOTI, &VirtualMachine::executeNOTI},
        {InstructionType::DECISP, &VirtualMachine::executeDECISP},
        {InstructionType::INCISP, &VirtualMachine::executeINCISP},
        {InstructionType::JNZ, &VirtualMachine::executeJNZ},
        {InstructionType::CPDOWNBP, &VirtualMachine::executeCPDOWNBP},
        {InstructionType::CPTOPBP, &VirtualMachine::executeCPTOPBP},
        {InstructionType::DECIBP, &VirtualMachine::executeDECIBP},
        {InstructionType::INCIBP, &VirtualMachine::executeINCIBP},
        {InstructionType::SAVEBP, &VirtualMachine::executeSAVEBP},
        {InstructionType::RESTOREBP, &VirtualMachine::executeRESTOREBP},
        {InstructionType::STORE_STATE, &VirtualMachine::executeSTORE_STATE}};
    for (auto &pair : g_handlers) {
        registerHandler(pair.first, pair.second);
    }
    _handlers.insert(std::make_pair(InstructionType::NOP, [](auto &) {}));
    _handlers.insert(std::make_pair(InstructionType::NOP2, [](auto &) {}));
}

template <typename Iterator>
static void logValues(std::stringstream &logStream, Iterator begin, Iterator end) {
    bool comma = false;
    unsigned i = 0;
    for (auto it = begin; it != end; ++it) {
        if (comma) {
            logStream << ", ";
        }
        comma = true;
        logStream << it->toString();
    }
}

void VirtualMachine::logOperandsIt(InstRevIterator begin, InstRevIterator end) {
    if (!_logEnabled) {
        return;
    }

    _logStream << "(";
    logValues(_logStream, begin, end);
    _logStream << ")";
}

void VirtualMachine::logResultsIt(InstRevIterator begin, InstRevIterator end) {
    if (!_logEnabled) {
        return;
    }

    _logStream << " -> (";
    logValues(_logStream, begin, end);
    _logStream << ")";
}

void VirtualMachine::logOperands(unsigned n) {
    logOperandsIt(_stack.rbegin(), _stack.rbegin() + n);
}

void VirtualMachine::logResults(unsigned n) {
    logResultsIt(_stack.rbegin(), _stack.rbegin() + n);
}

void VirtualMachine::logJump(JumpType type) {
    if (!_logEnabled) {
        return;
    }

    _logStream << " -> ";
    switch (type) {
    case JumpType::Jump:
        _logStream << "jump";
        break;
    case JumpType::Fallthrough:
        _logStream << "fallthrough";
        break;
    }
}

int VirtualMachine::run() {
    uint32_t insOff = kStartInstructionOffset;

    if (_context->savedState) {
        std::vector<Variable> globals(_context->savedState->globals);
        copy(globals.begin(), globals.end(), back_inserter(_stack));
        _globalCount = static_cast<int>(_stack.size());

        std::vector<Variable> locals(_context->savedState->locals);
        copy(locals.begin(), locals.end(), back_inserter(_stack));

        insOff = _context->savedState->insOffset;
    }

    if (Logger::instance.isChannelEnabled(LogChannel::Script)) {
        std::stringstream ss;
        ss << boost::format("Run '%s': Offset=%04x") %
                  _program->name() %
                  insOff;

        if (!_context->args.empty()) {
            for (const Argument &var : _context->args) {
                ss << ", ";
                ss << var.toString();
            }
        }
        debug(ss.str());
    }

    while (insOff < _program->length()) {
        const Instruction &ins = _program->getInstruction(insOff);
        auto handler = _handlers.find(ins.type);

        if (handler == _handlers.end()) {
            error(str(boost::format("Instruction not implemented: %04x") % static_cast<int>(ins.type)), LogChannel::Script);
            return -1;
        }
        _nextInstruction = ins.nextOffset;

        bool halt = false;
        try {
            handler->second(ins);
        } catch (const std::exception &ex) {
            halt = true;
        }

        if (Logger::instance.isChannelEnabled(LogChannel::Script3)) {
            debug(str(boost::format("Instruction: %s %s") % describeInstruction(ins, *_context->routines) % _logStream.str()), LogChannel::Script3);
            _logStream.str("");
            _logStream.clear();
        }

        if (halt) {
            debug(str(boost::format("Halt '%s'") % _program->name()), LogChannel::Script);
            return -1;
        }

        insOff = _nextInstruction;
    }

    if (!_stack.empty() && _stack.back().type == VariableType::Int) {
        return _stack.back().intValue;
    }

    return -1;
}

void VirtualMachine::executeCPDOWNSP(const Instruction &ins) {
    int count = ins.size / 4;
    int srcIdx = static_cast<int>(_stack.size()) - count;
    int dstIdx = static_cast<int>(_stack.size()) + ins.stackOffset / 4;

    for (int i = 0; i < count; ++i) {
        _stack[dstIdx++] = _stack[srcIdx++];
    }
}

void VirtualMachine::executeRSADDI(const Instruction &ins) {
    logOperands(0);
    _stack.push_back(Variable::ofInt(0));
    logResults(1);
}

void VirtualMachine::executeRSADDF(const Instruction &ins) {
    logOperands(0);
    _stack.push_back(Variable::ofFloat(0.0f));
    logResults(1);
}

void VirtualMachine::executeRSADDS(const Instruction &ins) {
    logOperands(0);
    _stack.push_back(Variable::ofString(""));
    logResults(1);
}

void VirtualMachine::executeRSADDO(const Instruction &ins) {
    logOperands(0);
    _stack.push_back(Variable::ofObject(kObjectInvalid));
    logResults(1);
}

void VirtualMachine::executeRSADDEFF(const Instruction &ins) {
    logOperands(0);
    _stack.push_back(Variable::ofEffect(nullptr));
    logResults(1);
}

void VirtualMachine::executeRSADDEVT(const Instruction &ins) {
    logOperands(0);
    _stack.push_back(Variable::ofEvent(nullptr));
    logResults(1);
}

void VirtualMachine::executeRSADDLOC(const Instruction &ins) {
    logOperands(0);
    _stack.push_back(Variable::ofLocation(nullptr));
    logResults(1);
}

void VirtualMachine::executeRSADDTAL(const Instruction &ins) {
    logOperands(0);
    _stack.push_back(Variable::ofTalent(nullptr));
    logResults(1);
}

void VirtualMachine::executeCPTOPSP(const Instruction &ins) {
    int count = ins.size / 4;
    int srcIdx = static_cast<int>(_stack.size()) + ins.stackOffset / 4;

    for (int i = 0; i < count; ++i) {
        _stack.push_back(_stack[srcIdx++]);
    }
}

void VirtualMachine::executeCONSTI(const Instruction &ins) {
    logOperands(0);
    _stack.push_back(Variable::ofInt(ins.intValue));
    logResults(1);
}

void VirtualMachine::executeCONSTF(const Instruction &ins) {
    logOperands(0);
    _stack.push_back(Variable::ofFloat(ins.floatValue));
    logResults(1);
}

void VirtualMachine::executeCONSTS(const Instruction &ins) {
    logOperands(0);
    _stack.push_back(Variable::ofString(ins.strValue));
    logResults(1);
}

void VirtualMachine::executeCONSTO(const Instruction &ins) {
    logOperands(0);

    uint32_t id = ins.objectId;
    if (id != kObjectSelf) {
        _stack.push_back(Variable::ofObject(id));
        logResults(1);
        return;
    }

    if (const Variable *caller = _context->findArg(ArgKind::Caller)) {
        _stack.push_back(*caller);
        logResults(1);
        return;
    }

    _stack.push_back(Variable::ofObject(kObjectInvalid));
    logResults(1);
}

void VirtualMachine::executeACTION(const Instruction &ins) {
    auto &routine = _context->routines->get(ins.routine);
    if (ins.argCount > routine.getArgumentCount()) {
        throw std::invalid_argument("Too many routine arguments");
    }

    std::vector<Variable> args;
    for (int i = 0; i < ins.argCount; ++i) {
        VariableType type = routine.getArgumentType(i);
        switch (type) {
        case VariableType::Vector:
            logOperands(3);
            args.push_back(Variable::ofVector(getVectorFromStack()));
            break;

        case VariableType::Action: {
            auto ctx = std::make_shared<ExecutionContext>(*_context);
            ctx->savedState = std::make_shared<ExecutionState>(_savedState);
            args.push_back(Variable::ofAction(std::move(ctx)));
            break;
        }
        default:
            logOperands(1);
            Variable var(_stack.back());
            if (var.type != type) {
                throw std::runtime_error("Invalid argument variable type");
            }
            args.push_back(std::move(var));
            _stack.pop_back();
            break;
        }
    }

    Variable retValue = routine.invoke(args, *_context);
    if (Logger::instance.isChannelEnabled(LogChannel::Script2)) {
        std::vector<std::string> argStrings;
        for (auto &arg : args) {
            argStrings.push_back(arg.toString());
        }
        std::string argsString(boost::join(argStrings, ", "));
        debug(str(boost::format("Action: %04x %s(%s) -> %s") % ins.offset % routine.name() % argsString % retValue.toString()), LogChannel::Script2);
    }
    switch (routine.returnType()) {
    case VariableType::Void: {
        logResults(0);
        break;
    }
    case VariableType::Vector: {
        _stack.push_back(Variable::ofFloat(retValue.vecValue.z));
        _stack.push_back(Variable::ofFloat(retValue.vecValue.y));
        _stack.push_back(Variable::ofFloat(retValue.vecValue.x));
        logResults(3);
        break;
    }
    default: {
        _stack.push_back(retValue);
        logResults(1);
        break;
    }
    }
}

void VirtualMachine::executeLOGANDII(const Instruction &ins) {
    logOperands(2);
    withIntsFromStack([this](int left, int right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left && right)));
    });
    logResults(1);
}

void VirtualMachine::executeLOGORII(const Instruction &ins) {
    logOperands(2);
    withIntsFromStack([this](int left, int right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left || right)));
    });
    logResults(1);
}

void VirtualMachine::executeINCORII(const Instruction &ins) {
    logOperands(2);
    withIntsFromStack([this](int left, int right) {
        _stack.push_back(Variable::ofInt(left | right));
    });
    logResults(1);
}

void VirtualMachine::executeEXCORII(const Instruction &ins) {
    logOperands(2);
    withIntsFromStack([this](int left, int right) {
        _stack.push_back(Variable::ofInt(left ^ right));
    });
    logResults(1);
}

void VirtualMachine::executeBOOLANDII(const Instruction &ins) {
    logOperands(2);
    withIntsFromStack([this](int left, int right) {
        _stack.push_back(Variable::ofInt(left & right));
    });
    logResults(1);
}

void VirtualMachine::executeEQUALII(const Instruction &ins) {
    logOperands(2);
    withIntsFromStack([this](int left, int right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left == right)));
    });
    logResults(1);
}

void VirtualMachine::executeEQUALFF(const Instruction &ins) {
    logOperands(2);
    withFloatsFromStack([this](float left, float right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(fabs(left - right) < kFloatTolerance)));
    });
    logResults(1);
}

void VirtualMachine::executeEQUALSS(const Instruction &ins) {
    logOperands(2);
    withStringsFromStack([this](auto &left, auto &right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left == right)));
    });
    logResults(1);
}

void VirtualMachine::executeEQUALOO(const Instruction &ins) {
    logOperands(2);
    withObjectsFromStack([this](uint32_t left, uint32_t right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left == right)));
    });
    logResults(1);
}

void VirtualMachine::executeEQUALTT(const Instruction &ins) {
    int numVariables = ins.size / 4;
    logOperands(numVariables * 2);
    std::vector<Variable> vars1;
    for (int i = 0; i < numVariables; ++i) {
        vars1.push_back(std::move(_stack.back()));
        _stack.pop_back();
    }
    std::vector<Variable> vars2;
    for (int i = 0; i < numVariables; ++i) {
        vars2.push_back(std::move(_stack.back()));
        _stack.pop_back();
    }
    bool equal = std::equal(vars1.begin(), vars1.end(), vars2.begin());
    _stack.push_back(Variable::ofInt(static_cast<int>(equal)));
    logResults(1);
}

void VirtualMachine::executeEQUALEFFEFF(const Instruction &ins) {
    logOperands(2);
    withEffectsFromStack([this](auto &left, auto &right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left == right)));
    });
    logResults(1);
}

void VirtualMachine::executeEQUALEVTEVT(const Instruction &ins) {
    logOperands(2);
    withEventsFromStack([this](auto &left, auto &right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left == right)));
    });
    logResults(1);
}

void VirtualMachine::executeEQUALLOCLOC(const Instruction &ins) {
    logOperands(2);
    withLocationsFromStack([this](auto &left, auto &right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left == right)));
    });
    logResults(1);
}

void VirtualMachine::executeEQUALTALTAL(const Instruction &ins) {
    logOperands(2);
    withTalentsFromStack([this](auto &left, auto &right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left == right)));
    });
    logResults(1);
}

void VirtualMachine::executeNEQUALII(const Instruction &ins) {
    logOperands(2);
    withIntsFromStack([this](int left, int right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left != right)));
    });
    logResults(1);
}

void VirtualMachine::executeNEQUALFF(const Instruction &ins) {
    logOperands(2);
    withFloatsFromStack([this](float left, float right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left != right)));
    });
    logResults(1);
}

void VirtualMachine::executeNEQUALSS(const Instruction &ins) {
    logOperands(2);
    withStringsFromStack([this](auto &left, auto &right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left != right)));
    });
    logResults(1);
}

void VirtualMachine::executeNEQUALOO(const Instruction &ins) {
    logOperands(2);
    withObjectsFromStack([this](uint32_t left, uint32_t right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left != right)));
    });
    logResults(1);
}

void VirtualMachine::executeNEQUALTT(const Instruction &ins) {
    int numVariables = ins.size / 4;
    logOperands(numVariables * 2);
    std::vector<Variable> vars1;
    for (int i = 0; i < numVariables; ++i) {
        vars1.push_back(std::move(_stack.back()));
        _stack.pop_back();
    }
    std::vector<Variable> vars2;
    for (int i = 0; i < numVariables; ++i) {
        vars2.push_back(std::move(_stack.back()));
        _stack.pop_back();
    }
    bool notEqual = !std::equal(vars1.begin(), vars1.end(), vars2.begin());
    _stack.push_back(Variable::ofInt(static_cast<int>(notEqual)));
    logResults(1);
}

void VirtualMachine::executeNEQUALEFFEFF(const Instruction &ins) {
    logOperands(2);
    withEffectsFromStack([this](auto &left, auto &right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left != right)));
    });
    logResults(1);
}

void VirtualMachine::executeNEQUALEVTEVT(const Instruction &ins) {
    logOperands(2);
    withEventsFromStack([this](auto &left, auto &right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left != right)));
    });
    logResults(1);
}

void VirtualMachine::executeNEQUALLOCLOC(const Instruction &ins) {
    logOperands(2);
    withLocationsFromStack([this](auto &left, auto &right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left != right)));
    });
    logResults(1);
}

void VirtualMachine::executeNEQUALTALTAL(const Instruction &ins) {
    logOperands(2);
    withTalentsFromStack([this](auto &left, auto &right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left != right)));
    });
    logResults(1);
}

void VirtualMachine::executeGEQII(const Instruction &ins) {
    logOperands(2);
    withIntsFromStack([this](int left, int right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left >= right)));
    });
    logResults(1);
}

void VirtualMachine::executeGEQFF(const Instruction &ins) {
    logOperands(2);
    withFloatsFromStack([this](float left, float right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left >= right)));
    });
    logResults(1);
}

void VirtualMachine::executeGTII(const Instruction &ins) {
    logOperands(2);
    withIntsFromStack([this](int left, int right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left > right)));
    });
    logResults(1);
}

void VirtualMachine::executeGTFF(const Instruction &ins) {
    logOperands(2);
    withFloatsFromStack([this](float left, float right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left > right)));
    });
    logResults(1);
}

void VirtualMachine::executeLTII(const Instruction &ins) {
    logOperands(2);
    withIntsFromStack([this](int left, int right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left < right)));
    });
    logResults(1);
}

void VirtualMachine::executeLTFF(const Instruction &ins) {
    logOperands(2);
    withFloatsFromStack([this](float left, float right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left < right)));
    });
    logResults(1);
}

void VirtualMachine::executeLEQII(const Instruction &ins) {
    logOperands(2);
    withIntsFromStack([this](int left, int right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left <= right)));
    });
    logResults(1);
}

void VirtualMachine::executeLEQFF(const Instruction &ins) {
    logOperands(2);
    withFloatsFromStack([this](float left, float right) {
        _stack.push_back(Variable::ofInt(static_cast<int>(left <= right)));
    });
    logResults(1);
}

void VirtualMachine::executeSHLEFTII(const Instruction &ins) {
    logOperands(2);
    withIntsFromStack([this](int left, int right) {
        _stack.push_back(Variable::ofInt(left << right));
    });
    logResults(1);
}

void VirtualMachine::executeSHRIGHTII(const Instruction &ins) {
    logOperands(2);
    withIntsFromStack([this](int left, int right) {
        int result = left;
        if (result < 0) {
            result = -((-result) >> right);
        } else {
            result >>= right;
        }
        _stack.push_back(Variable::ofInt(result));
    });
    logResults(1);
}

void VirtualMachine::executeUSHRIGHTII(const Instruction &ins) {
    logOperands(2);
    withIntsFromStack([this](int left, int right) {
        _stack.push_back(Variable::ofInt(static_cast<unsigned int>(left) >> right));
    });
    logResults(1);
}

void VirtualMachine::executeADDII(const Instruction &ins) {
    logOperands(2);
    withIntsFromStack([this](int left, int right) {
        _stack.push_back(Variable::ofInt(left + right));
    });
    logResults(1);
}

void VirtualMachine::executeADDIF(const Instruction &ins) {
    logOperands(2);
    withIntFloatFromStack([this](int left, float right) {
        _stack.push_back(Variable::ofFloat(left + right));
    });
    logResults(1);
}

void VirtualMachine::executeADDFI(const Instruction &ins) {
    logOperands(2);
    withFloatIntFromStack([this](float left, int right) {
        _stack.push_back(Variable::ofFloat(left + right));
    });
    logResults(1);
}

void VirtualMachine::executeADDFF(const Instruction &ins) {
    logOperands(2);
    withFloatsFromStack([this](float left, float right) {
        _stack.push_back(Variable::ofFloat(left + right));
    });
    logResults(1);
}

void VirtualMachine::executeADDSS(const Instruction &ins) {
    logOperands(2);
    withStringsFromStack([this](auto &left, auto &right) {
        _stack.push_back(Variable::ofString(left + right));
    });
    logResults(1);
}

void VirtualMachine::executeADDVV(const Instruction &ins) {
    logOperands(2);
    withVectorsFromStack([this](auto &left, auto &right) {
        auto result = left + right;
        _stack.push_back(Variable::ofFloat(result.x));
        _stack.push_back(Variable::ofFloat(result.y));
        _stack.push_back(Variable::ofFloat(result.z));
    });
    logResults(1);
}

void VirtualMachine::executeSUBII(const Instruction &ins) {
    logOperands(2);
    withIntsFromStack([this](int left, int right) {
        _stack.push_back(Variable::ofInt(left - right));
    });
    logResults(1);
}

void VirtualMachine::executeSUBIF(const Instruction &ins) {
    logOperands(2);
    withIntFloatFromStack([this](int left, float right) {
        _stack.push_back(Variable::ofFloat(left - right));
    });
    logResults(1);
}

void VirtualMachine::executeSUBFI(const Instruction &ins) {
    logOperands(2);
    withFloatIntFromStack([this](float left, int right) {
        _stack.push_back(Variable::ofFloat(left - right));
    });
    logResults(1);
}

void VirtualMachine::executeSUBFF(const Instruction &ins) {
    logOperands(2);
    withFloatsFromStack([this](float left, float right) {
        _stack.push_back(Variable::ofFloat(left - right));
    });
    logResults(1);
}

void VirtualMachine::executeSUBVV(const Instruction &ins) {
    logOperands(2);
    withVectorsFromStack([this](auto &left, auto &right) {
        auto result = left - right;
        _stack.push_back(Variable::ofFloat(result.x));
        _stack.push_back(Variable::ofFloat(result.y));
        _stack.push_back(Variable::ofFloat(result.z));
    });
    logResults(1);
}

void VirtualMachine::executeMULII(const Instruction &ins) {
    logOperands(2);
    withIntsFromStack([this](int left, int right) {
        _stack.push_back(Variable::ofInt(left * right));
    });
    logResults(1);
}

void VirtualMachine::executeMULIF(const Instruction &ins) {
    logOperands(2);
    withIntFloatFromStack([this](int left, float right) {
        _stack.push_back(Variable::ofFloat(left * right));
    });
    logResults(1);
}

void VirtualMachine::executeMULFI(const Instruction &ins) {
    logOperands(2);
    withFloatIntFromStack([this](float left, int right) {
        _stack.push_back(Variable::ofFloat(left * right));
    });
    logResults(1);
}

void VirtualMachine::executeMULFF(const Instruction &ins) {
    logOperands(2);
    withFloatsFromStack([this](float left, float right) {
        _stack.push_back(Variable::ofFloat(left * right));
    });
    logResults(1);
}

void VirtualMachine::executeMULVF(const Instruction &ins) {
    logOperands(2);
    withVectorFloatFromStack([this](auto &left, float right) {
        auto result = left * right;
        _stack.push_back(Variable::ofFloat(result.x));
        _stack.push_back(Variable::ofFloat(result.y));
        _stack.push_back(Variable::ofFloat(result.z));
    });
    logResults(1);
}

void VirtualMachine::executeMULFV(const Instruction &ins) {
    logOperands(2);
    withFloatVectorFromStack([this](float left, auto &right) {
        auto result = left * right;
        _stack.push_back(Variable::ofFloat(result.x));
        _stack.push_back(Variable::ofFloat(result.y));
        _stack.push_back(Variable::ofFloat(result.z));
    });
    logResults(1);
}

void VirtualMachine::executeDIVII(const Instruction &ins) {
    logOperands(2);
    withIntsFromStack([this](int left, int right) {
        _stack.push_back(Variable::ofInt(left / right));
    });
    logResults(1);
}

void VirtualMachine::executeDIVIF(const Instruction &ins) {
    logOperands(2);
    withIntFloatFromStack([this](int left, float right) {
        _stack.push_back(Variable::ofFloat(left / std::max(kFloatTolerance, right)));
    });
    logResults(1);
}

void VirtualMachine::executeDIVFI(const Instruction &ins) {
    logOperands(2);
    withFloatIntFromStack([this](float left, int right) {
        _stack.push_back(Variable::ofFloat(left / right));
    });
    logResults(1);
}

void VirtualMachine::executeDIVFF(const Instruction &ins) {
    logOperands(2);
    withFloatsFromStack([this](float left, float right) {
        _stack.push_back(Variable::ofFloat(left / std::max(kFloatTolerance, right)));
    });
    logResults(1);
}

void VirtualMachine::executeDIVVF(const Instruction &ins) {
    logOperands(2);
    withVectorFloatFromStack([this](auto &left, float right) {
        auto result = left / right;
        _stack.push_back(Variable::ofFloat(result.x));
        _stack.push_back(Variable::ofFloat(result.y));
        _stack.push_back(Variable::ofFloat(result.z));
    });
    logResults(1);
}

void VirtualMachine::executeDIVFV(const Instruction &ins) {
    logOperands(2);
    withFloatVectorFromStack([this](float left, auto &right) {
        auto result = left / right;
        _stack.push_back(Variable::ofFloat(result.x));
        _stack.push_back(Variable::ofFloat(result.y));
        _stack.push_back(Variable::ofFloat(result.z));
    });
    logResults(1);
}

void VirtualMachine::executeMODII(const Instruction &ins) {
    logOperands(2);
    withIntsFromStack([this](int left, int right) {
        _stack.push_back(Variable::ofInt(left % right));
    });
    logResults(1);
}

void VirtualMachine::executeNEGI(const Instruction &ins) {
    logOperands(1);
    Variable neg = Variable::ofInt(-_stack.back().intValue);
    _stack.back() = neg;
}

void VirtualMachine::executeNEGF(const Instruction &ins) {
    logOperands(1);
    Variable neg = Variable::ofFloat(-_stack.back().floatValue);
    _stack.back() = neg;
}

void VirtualMachine::executeMOVSP(const Instruction &ins) {
    int count = -ins.stackOffset / 4;
    for (int i = 0; i < count; ++i) {
        _stack.pop_back();
    }
}

void VirtualMachine::executeJMP(const Instruction &ins) {
    _nextInstruction = ins.offset + ins.jumpOffset;
}

void VirtualMachine::executeJSR(const Instruction &ins) {
    _returnOffsets.push_back(ins.nextOffset);
    _nextInstruction = ins.offset + ins.jumpOffset;
}

void VirtualMachine::executeJZ(const Instruction &ins) {
    logOperands(1);
    bool zero = getIntFromStack() == 0;
    if (zero) {
        _nextInstruction = ins.offset + ins.jumpOffset;
        logJump(JumpType::Jump);
        return;
    }
    logJump(JumpType::Fallthrough);
}

void VirtualMachine::executeRETN(const Instruction &ins) {
    if (_returnOffsets.empty()) {
        _nextInstruction = _program->length();
    } else {
        _nextInstruction = _returnOffsets.back();
        _returnOffsets.pop_back();
    }
}

void VirtualMachine::executeDESTRUCT(const Instruction &ins) {
    int startIdx = static_cast<int>(_stack.size()) - ins.size / 4;
    int startIdxNoDestroy = startIdx + ins.stackOffset / 4;
    int countNoDestroy = ins.sizeNoDestroy / 4;

    for (int i = 0; i < countNoDestroy; ++i) {
        _stack[startIdx + i] = _stack[startIdxNoDestroy + i];
    }
    _stack.resize(startIdx + countNoDestroy);
}

void VirtualMachine::executeDECISP(const Instruction &ins) {
    int dstIdx = static_cast<int>(_stack.size()) + ins.stackOffset / 4;
    auto it = std::reverse_iterator(_stack.begin() + dstIdx);
    logOperandsIt(it, it + 1);
    _stack[dstIdx] = Variable::ofInt(_stack[dstIdx].intValue - 1);
    logResultsIt(it, it + 1);
}

void VirtualMachine::executeINCISP(const Instruction &ins) {
    int dstIdx = static_cast<int>(_stack.size()) + ins.stackOffset / 4;
    auto it = std::reverse_iterator(_stack.begin() + dstIdx);
    logOperandsIt(it, it + 1);
    _stack[dstIdx] = Variable::ofInt(_stack[dstIdx].intValue + 1);
    logResultsIt(it, it + 1);
}

void VirtualMachine::executeNOTI(const Instruction &ins) {
    logOperands(1);
    int value = getIntFromStack();
    _stack.push_back(Variable::ofInt(static_cast<int>(!value)));
    logResults(1);
}

void VirtualMachine::executeJNZ(const Instruction &ins) {
    logOperands(1);
    bool notZero = getIntFromStack() != 0;
    if (notZero) {
        _nextInstruction = ins.offset + ins.jumpOffset;
        logJump(JumpType::Jump);
        return;
    }
    logJump(JumpType::Fallthrough);
}

void VirtualMachine::executeCPDOWNBP(const Instruction &ins) {
    int count = ins.size / 4;
    int srcIdx = static_cast<int>(_stack.size()) - count;
    int dstIdx = _globalCount + ins.stackOffset / 4;

    for (int i = 0; i < count; ++i) {
        _stack[dstIdx++] = _stack[srcIdx++];
    }
}

void VirtualMachine::executeCPTOPBP(const Instruction &ins) {
    int count = ins.size / 4;
    int srcIdx = _globalCount + ins.stackOffset / 4;

    for (int i = 0; i < count; ++i) {
        _stack.push_back(_stack[srcIdx++]);
    }
}

void VirtualMachine::executeDECIBP(const Instruction &ins) {
    int dstIdx = _globalCount + ins.stackOffset / 4;

    auto it = std::reverse_iterator(_stack.begin() + dstIdx);
    logOperandsIt(it, it + 1);
    _stack[dstIdx] = Variable::ofInt(_stack[dstIdx].intValue - 1);
    logResultsIt(it, it + 1);
}

void VirtualMachine::executeINCIBP(const Instruction &ins) {
    int dstIdx = _globalCount + ins.stackOffset / 4;

    auto it = std::reverse_iterator(_stack.begin() + dstIdx);
    logOperandsIt(it, it + 1);
    _stack[dstIdx] = Variable::ofInt(_stack[dstIdx].intValue + 1);
    logResultsIt(it, it + 1);
}

void VirtualMachine::executeSAVEBP(const Instruction &ins) {
    _globalCount = static_cast<int>(_stack.size());
    logOperands(0);
    _stack.push_back(Variable::ofInt(_globalCount));
    logResults(1);
}

void VirtualMachine::executeRESTOREBP(const Instruction &ins) {
    _globalCount = getIntFromStack();
}

void VirtualMachine::executeSTORE_STATE(const Instruction &ins) {
    int count = ins.size / 4;
    int srcIdx = _globalCount - count;

    _savedState.globals.clear();
    for (int i = 0; i < count; ++i) {
        _savedState.globals.push_back(_stack[srcIdx++]);
    }

    count = ins.sizeLocals / 4;
    srcIdx = static_cast<int>(_stack.size()) - count;

    _savedState.locals.clear();
    for (int i = 0; i < count; ++i) {
        _savedState.locals.push_back(_stack[srcIdx++]);
    }

    _savedState.program = _program;
    _savedState.insOffset = ins.offset + 0x10;
}

int VirtualMachine::getIntFromStack() {
    Variable var(std::move(_stack.back()));
    _stack.pop_back();

    throwIfInvalidType(VariableType::Int, var.type);

    return var.intValue;
}

float VirtualMachine::getFloatFromStack() {
    Variable var(std::move(_stack.back()));
    _stack.pop_back();

    throwIfInvalidType(VariableType::Float, var.type);

    return var.floatValue;
}

glm::vec3 VirtualMachine::getVectorFromStack() {
    float z = getFloatFromStack();
    float y = getFloatFromStack();
    float x = getFloatFromStack();

    return glm::vec3(x, y, z);
}

void VirtualMachine::withStackVariables(const std::function<void(const Variable &, const Variable &)> &fn) {
    Variable second(std::move(_stack.back()));
    _stack.pop_back();

    Variable first(std::move(_stack.back()));
    _stack.pop_back();

    fn(first, second);
}

void VirtualMachine::withIntsFromStack(const std::function<void(int, int)> &fn) {
    withStackVariables([this, &fn](auto &left, auto &right) {
        throwIfInvalidType(VariableType::Int, left.type);
        throwIfInvalidType(VariableType::Int, right.type);
        fn(left.intValue, right.intValue);
    });
}

void VirtualMachine::withIntFloatFromStack(const std::function<void(int, float)> &fn) {
    withStackVariables([this, &fn](auto &left, auto &right) {
        throwIfInvalidType(VariableType::Int, left.type);
        throwIfInvalidType(VariableType::Float, right.type);
        fn(left.intValue, right.floatValue);
    });
}

void VirtualMachine::withFloatIntFromStack(const std::function<void(float, int)> &fn) {
    withStackVariables([this, &fn](auto &left, auto &right) {
        throwIfInvalidType(VariableType::Float, left.type);
        throwIfInvalidType(VariableType::Int, right.type);
        fn(left.floatValue, right.intValue);
    });
}

void VirtualMachine::withFloatsFromStack(const std::function<void(float, float)> &fn) {
    withStackVariables([this, &fn](auto &left, auto &right) {
        throwIfInvalidType(VariableType::Float, left.type);
        throwIfInvalidType(VariableType::Float, right.type);
        fn(left.floatValue, right.floatValue);
    });
}

void VirtualMachine::withStringsFromStack(const std::function<void(const std::string &, const std::string &)> &fn) {
    withStackVariables([this, &fn](auto &left, auto &right) {
        throwIfInvalidType(VariableType::String, left.type);
        throwIfInvalidType(VariableType::String, right.type);
        fn(left.strValue, right.strValue);
    });
}

void VirtualMachine::withObjectsFromStack(const std::function<void(uint32_t, uint32_t)> &fn) {
    withStackVariables([this, &fn](auto &left, auto &right) {
        throwIfInvalidType(VariableType::Object, left.type);
        throwIfInvalidType(VariableType::Object, right.type);
        fn(left.objectId, right.objectId);
    });
}

void VirtualMachine::withEffectsFromStack(const std::function<void(const std::shared_ptr<EngineType> &, const std::shared_ptr<EngineType> &)> &fn) {
    withStackVariables([this, &fn](auto &left, auto &right) {
        throwIfInvalidType(VariableType::Effect, left.type);
        throwIfInvalidType(VariableType::Effect, right.type);
        fn(left.engineType, right.engineType);
    });
}

void VirtualMachine::withEventsFromStack(const std::function<void(const std::shared_ptr<EngineType> &, const std::shared_ptr<EngineType> &)> &fn) {
    withStackVariables([this, &fn](auto &left, auto &right) {
        throwIfInvalidType(VariableType::Event, left.type);
        throwIfInvalidType(VariableType::Event, right.type);
        fn(left.engineType, right.engineType);
    });
}

void VirtualMachine::withLocationsFromStack(const std::function<void(const std::shared_ptr<EngineType> &, const std::shared_ptr<EngineType> &)> &fn) {
    withStackVariables([this, &fn](auto &left, auto &right) {
        throwIfInvalidType(VariableType::Location, left.type);
        throwIfInvalidType(VariableType::Location, right.type);
        fn(left.engineType, right.engineType);
    });
}

void VirtualMachine::withTalentsFromStack(const std::function<void(const std::shared_ptr<EngineType> &, const std::shared_ptr<EngineType> &)> &fn) {
    withStackVariables([this, &fn](auto &left, auto &right) {
        throwIfInvalidType(VariableType::Talent, left.type);
        throwIfInvalidType(VariableType::Talent, right.type);
        fn(left.engineType, right.engineType);
    });
}

void VirtualMachine::withFloatVectorFromStack(const std::function<void(float, const glm::vec3 &)> &fn) {
    auto right = getVectorFromStack();
    auto left = getFloatFromStack();

    fn(left, right);
}

void VirtualMachine::withVectorFloatFromStack(const std::function<void(const glm::vec3 &, float)> &fn) {
    auto right = getFloatFromStack();
    auto left = getVectorFromStack();

    fn(left, right);
}

void VirtualMachine::withVectorsFromStack(const std::function<void(const glm::vec3 &, const glm::vec3 &)> &fn) {
    auto right = getVectorFromStack();
    auto left = getVectorFromStack();

    fn(left, right);
}

void VirtualMachine::throwIfInvalidType(VariableType expected, VariableType actual) {
    if (actual != expected) {
        throw std::runtime_error(str(boost::format("Invalid variable type: expected=%d, actual=%d") %
                                     static_cast<int>(expected) %
                                     static_cast<int>(actual)));
    }
}

int VirtualMachine::getStackSize() const {
    return static_cast<int>(_stack.size());
}

const Variable &VirtualMachine::getStackVariable(int index) const {
    return _stack[index];
}

void VirtualMachine::dump() const {
    std::cerr << "PROGRAM " << _program->name() << "\n";
    for (const Instruction &inst : _program->instructions()) {
        std::cerr << describeInstruction(inst, *_context->routines) << "\n";
    }
}

} // namespace script

} // namespace reone
