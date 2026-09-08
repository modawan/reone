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

#include "../fixtures/itempresentation.h"

namespace {

TEST_F(SharedPresentation, real_presenters_route_distinct_state_and_commands_without_gameplay_objects) {
    for (auto gameId : {GameID::KotOR, GameID::TSL}) {
        ScreenResources left(options, scene, graphics, resources), right(options, scene, graphics, resources);
        auto first = items("First", 101, 3), second = items("Second", 202, 7);
        auto inv1 = std::make_shared<InventoryMenu>(gameId, options, services(left), first, []() {});
        auto inv2 = std::make_shared<InventoryMenu>(gameId, options, services(right), second, []() {});
        auto equip1 = std::make_shared<Equipment>(gameId, options, services(left), resources.services().strings, first, []() {});
        auto equip2 = std::make_shared<Equipment>(gameId, options, services(right), resources.services().strings, second, []() {});
        inv1->init();
        inv2->init();
        equip1->init();
        equip2->init();
        inv1->refreshItems();
        inv2->refreshItems();
        equip1->openItems();
        equip2->openItems();
        std::string suffix = gameId == GameID::TSL ? "_p" : "";
        auto list = [&](ScreenResources &provider, std::string name) {
            return std::static_pointer_cast<ListBox>(provider.screens.at(name + suffix)->findControl("LB_ITEMS"));
        };
        EXPECT_EQ("First", list(left, "inventory")->getItemAt(0).text);
        EXPECT_EQ("7", list(right, "inventory")->getItemAt(0).iconText);
        EXPECT_NE(left.screens.at("inventory" + suffix), right.screens.at("inventory" + suffix));
        list(left, "equip")->setSelectedItemIndex(1);
        left.screens.at("equip" + suffix)->findControl("BTN_EQUIP")->handleClick(0, 0);
        ASSERT_EQ(1u, first->requests.size());
        EXPECT_EQ(101u, first->requests.front().handle);
        EXPECT_TRUE(second->requests.empty());
        // Completion is deliberately withheld: sending a request does not edit the read view.
        EXPECT_EQ("3", list(left, "equip")->getItemAt(1).iconText);
        EXPECT_EQ(3, first->equipment.items[0].stackSize);
        list(right, "equip")->setSelectedItemIndex(1);
        right.screens.at("equip" + suffix)->findControl("BTN_EQUIP")->handleClick(0, 0);
        ASSERT_EQ(1u, second->requests.size());
        EXPECT_EQ(202u, second->requests.front().handle);
        first->result = EquipmentRequestResult {101, EquipmentRequestOutcome::Applied};
        first->equipment.items[0].stackSize = 2;
        equip1->update(0.016f);
        EXPECT_EQ("2", list(left, "equip")->getItemAt(0).iconText);
        EXPECT_EQ("7", list(right, "equip")->getItemAt(1).iconText);

        InGameMenuHost host(gameId, options, services(left), {});
        host.init();
        host.registerScreen(InGameMenuTab::Inventory, inv1);
        host.registerScreen(InGameMenuTab::Equipment, equip1);
        for (int i = 0; i < 3; ++i) {
            host.changeTab(InGameMenuTab::Inventory);
            host.update(0.016f);
            host.render();
            host.changeTab(InGameMenuTab::Equipment);
            host.update(0.016f);
            host.render();
        }
        EXPECT_EQ(3, left.screens.at("inventory" + suffix)->renders);
        EXPECT_EQ(3, left.screens.at("equip" + suffix)->renders);
        std::vector<std::string> trace;
        left.screens.at("equip" + suffix)->trace = &trace;
        left.screens.at("top" + suffix)->trace = &trace;
        host.render();
        EXPECT_EQ((std::vector<std::string> {"equip" + suffix, "top" + suffix}), trace);
        input::Event event {input::EventType::KeyUp};
        event.key.code = input::KeyCode::Unknown;
        left.screens.at("equip" + suffix)->consume = true;
        EXPECT_TRUE(host.handle(event));
        EXPECT_EQ(0, left.screens.at("top" + suffix)->events);
        left.screens.at("equip" + suffix)->consume = false;
        host.handle(event);
        EXPECT_EQ(1, left.screens.at("top" + suffix)->events);
        equip1->setBacking(nullptr);
        inv1->setBacking(nullptr);
        EXPECT_EQ(0, list(left, "inventory")->getItemCount());
        EXPECT_EQ(0, list(left, "equip")->getItemCount());
        equip1->setBacking(second);
        inv1->setBacking(second);
        EXPECT_EQ("Second", list(left, "inventory")->getItemAt(0).text);
    }
}

TEST_F(SharedPresentation, registration_and_availability_work_in_both_directions) {
    ScreenResources guis(options, scene, graphics, resources);
    auto backing = items("Item", 1, 1);
    auto screen = std::make_shared<InventoryMenu>(GameID::KotOR, options, services(guis), backing, []() {});
    screen->init();
    InGameMenuHost host(GameID::KotOR, options, services(guis), {});
    host.init();
    host.changeTab(InGameMenuTab::Inventory);
    EXPECT_EQ(InGameMenuTab::None, host.activeTab());
    host.setEnabled(InGameMenuTab::Inventory, false);
    host.registerScreen(InGameMenuTab::Inventory, screen);
    EXPECT_FALSE(host.available(InGameMenuTab::Inventory));
    host.setEnabled(InGameMenuTab::Inventory, true);
    EXPECT_TRUE(host.available(InGameMenuTab::Inventory));
    EXPECT_FALSE(guis.screens.at("top")->findControl("BTN_INV")->isDisabled());
    host.changeTab(InGameMenuTab::Inventory);
    EXPECT_EQ(InGameMenuTab::Inventory, host.activeTab());
    host.setEnabled(InGameMenuTab::Inventory, false);
    EXPECT_EQ(InGameMenuTab::None, host.activeTab());
    host.setEnabled(InGameMenuTab::Inventory, true);
    host.changeTab(InGameMenuTab::Inventory);
    host.registerScreen(InGameMenuTab::Inventory, nullptr);
    EXPECT_EQ(InGameMenuTab::None, host.activeTab());
    host.registerScreen(InGameMenuTab::Inventory, screen);
    host.changeTab(InGameMenuTab::Inventory);
    EXPECT_EQ(InGameMenuTab::Inventory, host.activeTab());
}

class PresentationProbe : public PresentationGUI {
public:
    PresentationProbe(GameID gameId, const GraphicsOptions &options, PresentationServices services) :
        PresentationGUI(gameId, options, services) { _resRef = "probe"; }
    int preloads {0}, loads {0}, clicks {0};

protected:
    void preload(IGUI &gui) override {
        ++preloads;
        PresentationGUI::preload(gui);
    }
    void onGUILoaded() override {
        ++loads;
        findControl<Button>("BTN_PROBE");
    }
    void onClick(const std::string &control) override {
        ++clicks;
        PresentationGUI::onClick(control);
    }
};

TEST_F(SharedPresentation, common_initialization_preserves_virtual_preload_and_event_dispatch) {
    ScreenResources guis(options, scene, graphics, resources);
    PresentationProbe screen(GameID::KotOR, options, services(guis));
    screen.init();
    EXPECT_EQ(1, screen.preloads);
    EXPECT_EQ(1, screen.loads);
    auto gui = guis.screens.at("probe");
    const auto &extent = gui->findControl("BTN_PROBE")->extent();
    input::Event event {input::EventType::MouseButtonDown};
    event.button.button = input::MouseButton::Left;
    event.button.x = extent.left + gui->controlOffset().x + 1;
    event.button.y = extent.top + gui->controlOffset().y + 1;
    screen.handle(event);
    event.type = input::EventType::MouseButtonUp;
    screen.handle(event);
    EXPECT_EQ(1, screen.clicks);
    screen.render();
    EXPECT_EQ(1, guis.screens.at("probe")->renders);
}

} // namespace
