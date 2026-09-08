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

#pragma once

#include "audio.h"
#include "graphics.h"
#include "reone/game/gui/ingame/equip.h"
#include "reone/game/gui/ingame/inventory.h"
#include "reone/game/gui/ingamehost.h"
#include "reone/game/gui/sounds.h"
#include "reone/graphics/context.h"
#include "reone/graphics/font.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/statistic.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/uniforms.h"
#include "reone/gui/guis.h"
#include "reone/resource/gff.h"
#include "resource.h"
#include "scene.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace reone;
using namespace reone::game;
using namespace reone::gui;
using namespace reone::graphics;
using namespace reone::resource;
using namespace testing;

namespace {

class SilentSounds : public IGUISounds {
public:
    std::shared_ptr<audio::AudioClip> getOnClick() const override { return nullptr; }
    std::shared_ptr<audio::AudioClip> getOnEnter() const override { return nullptr; }
    std::shared_ptr<audio::AudioClip> getOnLevelUpNotify() const override { return nullptr; }
};

// Real controls, layout, list rows and input, with a recording test renderer.
class RecordingGUI : public GUI {
public:
    using GUI::GUI;
    int renders {0};
    int events {0};
    bool consume {false};
    std::vector<std::string> *trace {nullptr};
    std::string name;

    void render() override {
        ++renders;
        if (trace)
            trace->push_back(name);
    }
    bool handle(const input::Event &event) override {
        ++events;
        return consume || GUI::handle(event);
    }
    std::shared_ptr<Control> findControl(const std::string &tag) const override {
        auto found = GUI::findControl(tag);
        if (found)
            return found;
        auto self = const_cast<RecordingGUI *>(this);
        ControlType type = ControlType::Label;
        if (tag.rfind("BTN_", 0) == 0)
            type = ControlType::Button;
        else if (tag.rfind("LBLH_", 0) == 0)
            type = ControlType::ImageButton;
        else if (tag.rfind("LB_", 0) == 0)
            type = ControlType::ListBox;
        else if (tag.rfind("PB_", 0) == 0)
            type = ControlType::ProgressBar;
        generated::GUI_CONTROLS definition;
        definition.CONTROLTYPE = static_cast<int>(type);
        definition.TAG = tag;
        definition.EXTENT = {60, 0, 0, 300};
        definition.HILIGHT.emplace();
        definition.TEXT.emplace();
        definition.TEXT->FONT = "fixture";
        definition.TEXT->STRREF = -1;
        if (type == ControlType::ListBox) {
            definition.EXTENT.HEIGHT = 300;
            definition.PROTOITEM.emplace();
            definition.PROTOITEM->CONTROLTYPE = static_cast<int>(ControlType::Button);
            definition.PROTOITEM->TAG = tag + "_ROW";
            definition.PROTOITEM->EXTENT = {40, 0, 0, 280};
            definition.PROTOITEM->TEXT = definition.TEXT;
            definition.PROTOITEM->HILIGHT.emplace();
        }
        auto control = std::shared_ptr<Control>(self->newControl(type, tag));
        control->load(definition);
        self->addControlToBack(control, IGUI::ControlCoordinates::Authored);
        return control;
    }
};

class ScreenResources : public IGUIs {
public:
    ScreenResources(GraphicsOptions &options, scene::TestSceneModule &scene,
                    TestGraphicsModule &graphics, TestResourceModule &resources) :
        options(options), scene(scene), graphics(graphics), resources(resources) {}
    void clear() override { screens.clear(); }
    std::shared_ptr<IGUI> get(const std::string &name, std::function<void(IGUI &)> preload) override {
        auto gui = std::make_shared<RecordingGUI>(options, scene.graphs(), graphics.services(), resources.services());
        gui->name = name;
        if (preload)
            preload(*gui);
        auto extent = Gff::Builder().field(Gff::Field::newInt("WIDTH", 640)).field(Gff::Field::newInt("HEIGHT", 480)).build();
        auto root = Gff::Builder().field(Gff::Field::newInt("CONTROLTYPE", static_cast<int>(ControlType::Panel))).field(Gff::Field::newCExoString("TAG", "root")).field(Gff::Field::newStruct("EXTENT", extent)).field(Gff::Field::newStruct("BORDER", Gff::Builder().build())).build();
        gui->load(*root);
        screens[name] = gui;
        return gui;
    }
    std::unordered_map<std::string, std::shared_ptr<RecordingGUI>> screens;

private:
    GraphicsOptions &options;
    scene::TestSceneModule &scene;
    TestGraphicsModule &graphics;
    TestResourceModule &resources;
};

class SuppliedItems : public IInventoryMenuBacking, public IEquipmentMenuBacking {
public:
    InventoryView inventory;
    EquipmentView equipment;
    std::optional<EquipmentRequestResult> result;
    struct Request {
        uint64_t revision, handle;
        int slot;
    };
    std::vector<Request> requests;
    InventoryView readInventory(InventoryFilter) override { return inventory; }
    EquipmentView readEquipment(int) override { return equipment; }
    void equip(uint64_t revision, uint64_t handle, int slot) override {
        requests.push_back({revision, handle, slot});
    }
    std::optional<EquipmentRequestResult> equipmentResult() const override { return result; }
};

class SharedPresentation : public Test {
protected:
    GraphicsOptions options;
    TestGraphicsModule graphics;
    TestResourceModule resources;
    scene::TestSceneModule scene;
    audio::TestAudioModule audio;
    SilentSounds sounds;
    Context fontContext {options};
    Statistic statistic;
    MeshRegistry meshes {statistic};
    ShaderRegistry shaders;
    Uniforms uniforms {fontContext};
    std::shared_ptr<Font> font;

    void SetUp() override {
        graphics.init();
        resources.init();
        scene.init();
        audio.init();
        options.width = 1024;
        options.height = 768;
        auto texture = std::make_shared<Texture>("font", TextureType::TwoDim, Texture::Properties {});
        texture->setPixels(1, 1, PixelFormat::RGBA8, Texture::Layer {std::make_shared<ByteBuffer>(4, 0)});
        Texture::Features features;
        features.fontHeight = 0.12f;
        features.numChars = 256;
        features.upperLeftCoords.assign(256, glm::vec3(0, 1, 0));
        features.lowerRightCoords.assign(256, glm::vec3(0.5f, 0, 0));
        texture->setFeatures(std::move(features));
        font = std::make_shared<Font>(fontContext, meshes, shaders, statistic, uniforms);
        font->load(texture);
        EXPECT_CALL(resources.textures(), get(_, _)).Times(AnyNumber()).WillRepeatedly(Return(texture));
        EXPECT_CALL(static_cast<MockFonts &>(resources.services().fonts), get(_))
            .Times(AnyNumber())
            .WillRepeatedly(Return(font));
    }
    PresentationServices services(ScreenResources &guis) {
        return {guis, resources.services().textures, audio.services().mixer, sounds};
    }
    std::shared_ptr<SuppliedItems> items(std::string name, uint64_t handle, int count) {
        auto backing = std::make_shared<SuppliedItems>();
        MenuItemView item;
        item.handle = handle;
        item.name = name;
        item.description = name + " description";
        item.stackSize = count;
        backing->inventory.items = {item};
        backing->equipment.items = {item};
        backing->equipment.slotAvailable = true;
        backing->equipment.revision = handle;
        return backing;
    }
};

} // namespace
