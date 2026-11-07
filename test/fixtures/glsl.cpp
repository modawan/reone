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

#include "glsl.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace reone {

namespace glsl {

namespace {

using Tokens = std::vector<std::string>;

std::filesystem::path shaderDir() {
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "glsl";
}

std::string stripComments(const std::string &source) {
    std::string out;
    out.reserve(source.size());
    for (size_t i = 0; i < source.size(); ++i) {
        if (source.compare(i, 2, "//") == 0) {
            while (i < source.size() && source[i] != '\n') {
                ++i;
            }
            if (i < source.size()) {
                out += '\n';
            }
        } else if (source.compare(i, 2, "/*") == 0) {
            auto end = source.find("*/", i + 2);
            // Keep line breaks so that line-based preprocessing still works.
            auto stop = end == std::string::npos ? source.size() : end + 2;
            out += ' ';
            out.append(std::count(source.begin() + i, source.begin() + stop, '\n'), '\n');
            i = stop - 1;
        } else {
            out += source[i];
        }
    }
    return out;
}

Tokens tokenize(const std::string &text) {
    Tokens tokens;
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = text[i];
        if (std::isspace(c)) {
            ++i;
        } else if (std::isalpha(c) || c == '_') {
            size_t j = i;
            while (j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_')) {
                ++j;
            }
            tokens.push_back(text.substr(i, j - i));
            i = j;
        } else if (std::isdigit(c) || (c == '.' && i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1])))) {
            size_t j = i;
            while (j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '.')) {
                ++j;
            }
            tokens.push_back(text.substr(i, j - i));
            i = j;
        } else {
            tokens.push_back(std::string(1, text[i]));
            ++i;
        }
    }
    return tokens;
}

bool isIdentifier(const std::string &token) {
    return !token.empty() && (std::isalpha(static_cast<unsigned char>(token[0])) || token[0] == '_');
}

// Integer constant expressions: literals, known constants, + - * / % and
// parentheses.
class Evaluator {
public:
    Evaluator(const Tokens &tokens, const std::map<std::string, int> &constants) :
        _tokens(tokens),
        _constants(constants) {
    }

    std::optional<long> evaluate() {
        auto value = additive();
        if (!value || _pos != _tokens.size()) {
            return std::nullopt;
        }
        return value;
    }

private:
    const Tokens &_tokens;
    const std::map<std::string, int> &_constants;
    size_t _pos {0};

    bool accept(const char *token) {
        if (_pos < _tokens.size() && _tokens[_pos] == token) {
            ++_pos;
            return true;
        }
        return false;
    }

    std::optional<long> additive() {
        auto lhs = multiplicative();
        while (lhs) {
            if (accept("+")) {
                auto rhs = multiplicative();
                lhs = rhs ? std::optional<long>(*lhs + *rhs) : std::nullopt;
            } else if (accept("-")) {
                auto rhs = multiplicative();
                lhs = rhs ? std::optional<long>(*lhs - *rhs) : std::nullopt;
            } else {
                break;
            }
        }
        return lhs;
    }

    std::optional<long> multiplicative() {
        auto lhs = unary();
        while (lhs) {
            char op;
            if (accept("*")) {
                op = '*';
            } else if (accept("/")) {
                op = '/';
            } else if (accept("%")) {
                op = '%';
            } else {
                break;
            }
            auto rhs = unary();
            if (!rhs || (op != '*' && *rhs == 0)) {
                return std::nullopt;
            }
            lhs = op == '*' ? *lhs * *rhs : (op == '/' ? *lhs / *rhs : *lhs % *rhs);
        }
        return lhs;
    }

    std::optional<long> unary() {
        if (accept("-")) {
            auto value = unary();
            return value ? std::optional<long>(-*value) : std::nullopt;
        }
        if (accept("+")) {
            return unary();
        }
        if (accept("(")) {
            auto value = additive();
            if (!value || !accept(")")) {
                return std::nullopt;
            }
            return value;
        }
        if (_pos >= _tokens.size()) {
            return std::nullopt;
        }
        const auto &token = _tokens[_pos++];
        if (std::isdigit(static_cast<unsigned char>(token[0]))) {
            static const std::regex kInteger(R"((0[xX][0-9a-fA-F]+|[0-9]+)[uU]?)");
            if (!std::regex_match(token, kInteger)) {
                return std::nullopt;
            }
            return std::stol(token, nullptr, 0);
        }
        auto it = _constants.find(token);
        if (it == _constants.end()) {
            return std::nullopt;
        }
        return it->second;
    }
};

std::optional<long> evaluate(const Tokens &tokens, const std::map<std::string, int> &constants) {
    return Evaluator(tokens, constants).evaluate();
}

// Splits at top-level commas, outside parentheses and brackets.
std::vector<Tokens> splitTopLevel(Tokens::const_iterator begin, Tokens::const_iterator end) {
    std::vector<Tokens> parts(1);
    int depth = 0;
    for (auto it = begin; it != end; ++it) {
        if (*it == "(" || *it == "[") {
            ++depth;
        } else if (*it == ")" || *it == "]") {
            --depth;
        } else if (*it == "," && depth == 0) {
            parts.emplace_back();
            continue;
        }
        parts.back().push_back(*it);
    }
    return parts;
}

const std::set<std::string> kQualifiers {
    "uniform", "const", "in", "out", "inout", "highp", "mediump", "lowp",
    "flat", "smooth", "noperspective", "centroid", "invariant", "precise",
    "readonly", "writeonly", "coherent", "volatile", "restrict", "buffer",
    "shared", "attribute", "varying", "patch", "sample"};

// Index of the matching closer of the opener at tokens[open].
size_t matching(const Tokens &tokens, size_t open, const char *opener, const char *closer) {
    int depth = 0;
    for (size_t i = open; i < tokens.size(); ++i) {
        if (tokens[i] == opener) {
            ++depth;
        } else if (tokens[i] == closer && --depth == 0) {
            return i;
        }
    }
    return tokens.size();
}

struct Declaration {
    std::vector<Variable> variables;
    std::vector<Tokens> initializers;
};

// Parses "qualifiers type name[N] = init, name2[M], ..." into variables.
// An array size that is not a constant expression becomes -1.
Declaration parseDeclaration(const Tokens &tokens, const std::map<std::string, int> &constants) {
    Declaration decl;
    size_t i = 0;
    while (i < tokens.size()) {
        if (tokens[i] == "layout" && i + 1 < tokens.size() && tokens[i + 1] == "(") {
            i = matching(tokens, i + 1, "(", ")") + 1;
        } else if (kQualifiers.count(tokens[i]) > 0) {
            ++i;
        } else {
            break;
        }
    }
    if (i >= tokens.size() || !isIdentifier(tokens[i])) {
        return decl;
    }
    std::string type = tokens[i++];
    auto parseSize = [&constants](const Tokens &declarator, size_t &pos) {
        int size = 0;
        if (pos < declarator.size() && declarator[pos] == "[") {
            auto close = matching(declarator, pos, "[", "]");
            Tokens expr(declarator.begin() + pos + 1, declarator.begin() + std::min(close, declarator.size()));
            auto value = evaluate(expr, constants);
            size = value && *value > 0 ? static_cast<int>(*value) : -1;
            pos = close + 1;
        }
        return size;
    };
    int typeArraySize = parseSize(tokens, i);
    for (auto &declarator : splitTopLevel(tokens.begin() + i, tokens.end())) {
        if (declarator.empty() || !isIdentifier(declarator[0])) {
            continue;
        }
        size_t pos = 1;
        Variable var;
        var.type = type;
        var.name = declarator[0];
        var.arraySize = typeArraySize != 0 ? typeArraySize : parseSize(declarator, pos);
        Tokens init;
        if (pos < declarator.size() && declarator[pos] == "=") {
            init.assign(declarator.begin() + pos + 1, declarator.end());
        }
        decl.variables.push_back(std::move(var));
        decl.initializers.push_back(std::move(init));
    }
    return decl;
}

bool isSamplerType(const std::string &type) {
    static const std::regex kSampler(R"([iu]?sampler\w*)");
    return std::regex_match(type, kSampler);
}

bool contains(const Tokens &tokens, const char *token) {
    return std::find(tokens.begin(), tokens.end(), token) != tokens.end();
}

std::vector<Variable> parseMembers(const Tokens &body, const std::map<std::string, int> &constants, const std::string &owner) {
    std::vector<Variable> members;
    Tokens member;
    for (const auto &token : body) {
        if (token != ";") {
            member.push_back(token);
            continue;
        }
        for (auto &var : parseDeclaration(member, constants).variables) {
            if (var.arraySize < 0) {
                throw std::runtime_error("Array size of " + owner + "." + var.name + " is not a constant");
            }
            members.push_back(std::move(var));
        }
        member.clear();
    }
    return members;
}

size_t roundUp(size_t value, size_t multiple) {
    return (value + multiple - 1) / multiple * multiple;
}

struct TypeLayout {
    size_t align {0};
    size_t size {0};
};

TypeLayout typeLayout(const std::string &type, const std::map<std::string, std::vector<Variable>> &structs) {
    static const std::regex kVector(R"([biu]?vec([234]))");
    static const std::regex kMatrix(R"(mat([234])(?:x([234]))?)");
    std::smatch match;
    if (type == "float" || type == "int" || type == "uint" || type == "bool") {
        return {4, 4};
    }
    if (std::regex_match(type, match, kVector)) {
        int n = std::stoi(match[1].str());
        return {n == 2 ? 8u : 16u, static_cast<size_t>(4 * n)};
    }
    if (std::regex_match(type, match, kMatrix)) {
        // Column-major: an array of one column vector per column, each
        // rounded up to 16 bytes.
        int columns = std::stoi(match[1].str());
        return {16, static_cast<size_t>(16 * columns)};
    }
    auto it = structs.find(type);
    if (it != structs.end()) {
        auto layout = std140Layout(it->second, structs);
        size_t align = 16;
        return {align, roundUp(layout.size, align)};
    }
    throw std::runtime_error("No std140 layout for type " + type);
}

} // namespace

std::string readShaderSource(const std::string &name) {
    std::ifstream stream(shaderDir() / name);
    return std::string(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
}

std::vector<std::string> listShaderSources() {
    std::vector<std::string> names;
    for (const auto &entry : std::filesystem::directory_iterator(shaderDir())) {
        if (entry.path().extension() == ".glsl") {
            names.push_back(entry.path().filename().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

int Uniforms::samplerCount() const {
    int count = 0;
    for (const auto &sampler : samplers) {
        count += sampler.arraySize > 0 ? sampler.arraySize : 1;
    }
    return count;
}

Uniforms parseUniforms(const std::string &source) {
    Uniforms result;

    // Preprocessor lines are not statements: take object-like #defines as
    // constants and drop the rest.
    static const std::regex kDefine(R"(^\s*#\s*define\s+(\w+)(?:\s+(.*))?$)");
    std::istringstream lines(stripComments(source));
    std::string code;
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        auto first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line[first] == '#') {
            std::smatch match;
            if (std::regex_match(line, match, kDefine) && match[2].matched) {
                if (auto value = evaluate(tokenize(match[2].str()), result.constants)) {
                    result.constants[match[1].str()] = static_cast<int>(*value);
                }
            }
            code += '\n';
            continue;
        }
        code += line;
        code += '\n';
    }

    auto tokens = tokenize(code);
    Tokens statement;
    size_t i = 0;
    while (i < tokens.size()) {
        const auto &token = tokens[i];
        if (token == "{") {
            auto close = matching(tokens, i, "{", "}");
            Tokens body(tokens.begin() + i + 1, tokens.begin() + std::min(close, tokens.size()));
            bool isBlock = contains(statement, "uniform") && !statement.empty() && isIdentifier(statement.back());
            bool isStruct = statement.size() >= 2 && statement[0] == "struct";
            i = close + 1;
            if (isBlock || isStruct) {
                // Skip the optional instance name up to the closing ";".
                while (i < tokens.size() && tokens[i] != ";") {
                    ++i;
                }
                ++i;
            }
            if (isBlock) {
                UniformBlock block;
                block.name = statement.back();
                block.std140 = contains(statement, "std140");
                block.members = parseMembers(body, result.constants, block.name);
                result.blocks.push_back(std::move(block));
            } else if (isStruct) {
                result.structs[statement[1]] = parseMembers(body, result.constants, statement[1]);
            }
            statement.clear();
            continue;
        }
        if (token != ";") {
            statement.push_back(token);
            ++i;
            continue;
        }
        ++i;
        if (contains(statement, "uniform")) {
            for (auto &var : parseDeclaration(statement, result.constants).variables) {
                if (isSamplerType(var.type)) {
                    if (var.arraySize < 0) {
                        throw std::runtime_error("Array size of sampler " + var.name + " is not a constant");
                    }
                    result.samplers.push_back(std::move(var));
                } else {
                    result.loose.push_back(std::move(var));
                }
            }
        } else if (statement.size() >= 2 && statement[0] == "const" && (statement[1] == "int" || statement[1] == "uint")) {
            auto decl = parseDeclaration(statement, result.constants);
            for (size_t k = 0; k < decl.variables.size(); ++k) {
                if (decl.variables[k].arraySize != 0) {
                    continue;
                }
                if (auto value = evaluate(decl.initializers[k], result.constants)) {
                    result.constants[decl.variables[k].name] = static_cast<int>(*value);
                }
            }
        }
        statement.clear();
    }
    return result;
}

Std140Layout std140Layout(const std::vector<Variable> &members,
                          const std::map<std::string, std::vector<Variable>> &structs) {
    Std140Layout layout;
    size_t offset = 0;
    for (const auto &member : members) {
        auto base = typeLayout(member.type, structs);
        size_t align = base.align;
        size_t size = base.size;
        if (member.arraySize > 0) {
            // Array elements are padded to a multiple of 16 bytes.
            align = roundUp(base.align, 16);
            size = roundUp(base.size, 16) * member.arraySize;
        }
        offset = roundUp(offset, align);
        layout.members.push_back({member.name, offset, size});
        offset += size;
    }
    layout.size = offset;
    return layout;
}

} // namespace glsl

} // namespace reone
