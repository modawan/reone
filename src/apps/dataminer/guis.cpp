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

#include "guis.h"

#include "installation_helpers.h"

#include "reone/extract/installation.h"
#include "reone/gui/types.h"
#include "reone/resource/format/gffreader.h"
#include "reone/system/fileutil.h"
#include "reone/system/stream/fileoutput.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/textwriter.h"

#include "code.h"

using namespace reone::dataminer;
using namespace reone::extract;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

struct ParsedControl {
    ControlType type {ControlType::Invalid};
    std::string tag;
    std::string cppName;

    ParsedControl() = default;

    ParsedControl(ControlType type, std::string tag) :
        type(type), tag(tag), cppName(tag) {
        if (boost::starts_with(cppName, "3D")) {
            cppName.replace(0, 2, "THREE_D");
        }
    }
};

struct ParsedGUI {
    std::map<std::string, ParsedControl> controls;
};

static ParsedGUI parseGui(ByteBuffer &guiBytes) {
    auto guiStream = MemoryInputStream(guiBytes);
    auto guiReader = GffReader(guiStream);
    guiReader.load();
    auto gff = guiReader.root();

    ParsedGUI parsed;
    auto controls = gff->getList("CONTROLS");
    for (auto &control : controls) {
        auto type = control->getEnum("CONTROLTYPE", ControlType::Invalid);
        auto tag = control->getString("TAG");
        parsed.controls[tag] = ParsedControl(type, tag);
    }
    return parsed;
}

static ParsedGUI mergeGuis(const ParsedGUI &k1Gui, const ParsedGUI &k2Gui) {
    ParsedGUI merged;
    for (auto &[tag, control] : k1Gui.controls) {
        merged.controls[tag] = control;
    }
    for (auto &[tag, control] : k2Gui.controls) {
        if (merged.controls.count(tag) > 0) {
            auto &exisiting = merged.controls.at(tag);
            if (control.type != exisiting.type) {
                throw std::runtime_error(str(boost::format("Control type mismatch: %s, %d, %d") % tag % static_cast<int>(control.type) % static_cast<int>(exisiting.type)));
            }
            continue;
        }
        merged.controls[tag] = control;
    }
    return merged;
}

static void writeControlDeclaration(const std::string &typeName,
                                    const std::string &cppName,
                                    TextWriter &writer) {
    writer.write(str(boost::format("std::shared_ptr<%s> %s;\n") % typeName % cppName));
}

static std::string controlTypeToTypeName(ControlType type) {
    std::string typeName;
    switch (type) {
    case ControlType::Panel:
        typeName = "gui::Panel";
        break;
    case ControlType::Label:
        typeName = "gui::Label";
        break;
    case ControlType::ImageButton:
        typeName = "gui::ImageButton";
        break;
    case ControlType::Button:
        typeName = "gui::Button";
        break;
    case ControlType::ToggleButton:
        typeName = "gui::ToggleButton";
        break;
    case ControlType::Slider:
        typeName = "gui::Slider";
        break;
    case ControlType::ScrollBar:
        typeName = "gui::ScrollBar";
        break;
    case ControlType::ProgressBar:
        typeName = "gui::ProgressBar";
        break;
    case ControlType::ListBox:
        typeName = "gui::ListBox";
        break;
    default:
        throw std::runtime_error("Invalid control type: " + std::to_string(static_cast<int>(type)));
    }
    return typeName;
}

static std::string controlTypeToInclude(ControlType type) {
    std::string include;
    switch (type) {
    case ControlType::Panel:
        include = "reone/gui/control/panel.h";
        break;
    case ControlType::Label:
        include = "reone/gui/control/label.h";
        break;
    case ControlType::ImageButton:
        include = "reone/gui/control/imagebutton.h";
        break;
    case ControlType::Button:
        include = "reone/gui/control/button.h";
        break;
    case ControlType::ToggleButton:
        include = "reone/gui/control/togglebutton.h";
        break;
    case ControlType::Slider:
        include = "reone/gui/control/slider.h";
        break;
    case ControlType::ScrollBar:
        include = "reone/gui/control/scrollbar.h";
        break;
    case ControlType::ProgressBar:
        include = "reone/gui/control/progressbar.h";
        break;
    case ControlType::ListBox:
        include = "reone/gui/control/listbox.h";
        break;
    default:
        throw std::runtime_error("Invalid control type: " + std::to_string(static_cast<int>(type)));
    }
    return include;
}

static void writeHeaderFile(const std::string &resRef,
                            const ParsedGUI &gui,
                            const std::filesystem::path &destDir) {
    auto path = destDir;
    path.append(resRef + ".h");
    auto stream = FileOutputStream(path);
    auto writer = TextWriter(stream);
    writer.write(kCopyrightNotice + "\n\n");
    writer.write("#pragma once\n\n");
    std::set<std::string> includes;
    includes.insert("reone/gui/gui.h");
    for (auto &[_, control] : gui.controls) {
        includes.insert(controlTypeToInclude(control.type));
    }
    for (auto &include : includes) {
        writer.write(str(boost::format(kIncludeFormat + "\n") % include));
    }
    writer.write("\n");
    writer.write("namespace reone {\n\n");
    writer.write("namespace game {\n\n");
    writer.write(str(boost::format("class GUI_%s : gui::IGUI, boost::noncopyable {\n") % resRef));
    writer.write("public:\n");
    writer.write(kIndent + "void bindControls() {\n");
    for (auto &[tag, control] : gui.controls) {
        auto typeName = controlTypeToTypeName(control.type);
        writer.write(str(boost::format("%1%%1%_controls.%2% = findControl<%3%>(\"%4%\");\n") % kIndent % control.cppName % typeName % tag));
    }
    writer.write(kIndent + "}\n");
    writer.write("\n");
    writer.write("private:\n");
    writer.write(kIndent + "struct Controls {\n");
    for (auto &[_, control] : gui.controls) {
        writer.write(kIndent);
        writer.write(kIndent);
        auto typeName = controlTypeToTypeName(control.type);
        writeControlDeclaration(typeName, control.cppName, writer);
    }
    writer.write(kIndent + "};\n");
    writer.write("\n");
    writer.write(kIndent + "Controls _controls;\n");
    writer.write("};\n\n");
    writer.write("} // namespace game\n\n");
    writer.write("} // namespace reone\n");
}

void generateGuis(const std::filesystem::path &k1dir,
                  const std::filesystem::path &k2dir,
                  const std::filesystem::path &destDir) {
    std::set<std::string> guiResRefs;

    Installation k1Installation(GameID::KotOR, k1dir);
    k1Installation.loadChitin();
    forEachResource(k1Installation.chitinResources(), ResType::Gui, [&](const FileResource &resource) {
        guiResRefs.insert(resource.id().resRef.value());
    });

    Installation k2Installation(GameID::TSL, k2dir);
    k2Installation.loadChitin();
    k2Installation.loadOverride();
    forEachResource(k2Installation.chitinResources(), ResType::Gui, [&](const FileResource &resource) {
        if (!boost::ends_with(resource.id().resRef.value(), "_x") && !boost::ends_with(resource.id().resRef.value(), "_p")) {
            throw std::runtime_error("Invalid TSL GUI ResRef");
        }
        auto strippedResRef = resource.id().resRef.value().substr(0, resource.id().resRef.value().length() - 2);
        guiResRefs.insert(std::move(strippedResRef));
    });
    forEachResource(k2Installation.overrideResources(), ResType::Gui, [&](const FileResource &resource) {
        if (!boost::ends_with(resource.id().resRef.value(), "_x") && !boost::ends_with(resource.id().resRef.value(), "_p")) {
            throw std::runtime_error("Invalid TSL GUI ResRef");
        }
        auto strippedResRef = resource.id().resRef.value().substr(0, resource.id().resRef.value().length() - 2);
        guiResRefs.insert(std::move(strippedResRef));
    });

    for (auto &resRef : guiResRefs) {
        std::unique_ptr<ParsedGUI> k1Gui;
        if (auto bytes = readResource(k1Installation, ResourceId(resRef, ResType::Gui), chitinOnlyOrder())) {
            k1Gui = std::make_unique<ParsedGUI>(parseGui(*bytes));
        }

        std::unique_ptr<ParsedGUI> k2Gui;
        if (auto bytes = readResource(k2Installation, ResourceId(resRef + "_p", ResType::Gui), overrideThenChitinOrder())) {
            k2Gui = std::make_unique<ParsedGUI>(parseGui(*bytes));
        }

        ParsedGUI mergedGui;
        if (k1Gui && k2Gui) {
            mergedGui = mergeGuis(*k1Gui, *k2Gui);
        } else if (k1Gui) {
            mergedGui = *k1Gui;
        } else if (k2Gui) {
            mergedGui = *k2Gui;
        }

        writeHeaderFile(resRef, mergedGui, destDir);
    }
}

} // namespace reone
