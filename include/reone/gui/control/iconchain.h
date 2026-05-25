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

#include "../control.h"

namespace reone {

namespace gui {

class IconChain : public Control {
public:
    enum class State {
        Owned,
        Selectable,
        Locked
    };

    struct Item {
        std::string tag;
        int row {0};
        int column {0};
        std::shared_ptr<graphics::Texture> iconTexture;
        State state {State::Owned};
        bool selected {false};
    };

    struct Link {
        std::string sourceTag;
        std::string targetTag;
    };

    struct CellStyle {
        struct BorderColors {
            glm::vec3 locked {1.0f};
            glm::vec3 selectable {1.0f};
            glm::vec3 owned {1.0f};
            glm::vec3 selected {1.0f};
        };

        struct FocusedBorderColors {
            glm::vec3 locked {1.0f};
            glm::vec3 selectable {1.0f};
            glm::vec3 owned {1.0f};
            glm::vec3 selected {1.0f};
        };

        std::shared_ptr<graphics::Texture> backgroundTexture;
        std::shared_ptr<graphics::Texture> linkTexture;
        std::shared_ptr<Border> itemBorder;
        std::shared_ptr<BorderColors> borderColors;
        std::shared_ptr<FocusedBorderColors> focusedBorderColors;
        glm::ivec2 linkSize {0};
        int iconSize {0};
        bool dimLockedBackground {false};
        bool drawItemBorderFill {true};
        bool onlyDrawItemBorderWhenBright {false};
    };

    IconChain(
        IGUI &gui,
        scene::ISceneGraphs &sceneGraphs,
        graphics::GraphicsServices &graphicsSvc,
        resource::ResourceServices &resourceSvc) :
        Control(
            gui,
            ControlType::IconChain,
            sceneGraphs,
            graphicsSvc,
            resourceSvc) {

        _selectable = true;
    }

    void clearItems();
    void addItem(Item item);
    void clearLinks();
    void addLink(Link link);
    void setItemSelected(const std::string &tag, bool selected);
    void setItemState(const std::string &tag, State state);
    void focusItem(const std::string &tag);
    const Item *focusedItem() const;

    bool handleMouseMotion(int x, int y) override;
    bool handleMouseWheel(int x, int y) override;
    bool handleClick(int x, int y, int clicks = 1) override;
    void update(float dt) override;
    void render(const glm::ivec2 &screenSize, const glm::ivec2 &offset, scene::IRenderPass &pass) override;
    void setSelected(bool selected) override;

    void setColumnCount(int count);
    void setCellSize(int size);
    void setCellSpacing(int x, int y);
    void setCellOrigin(int x, int y);
    void setCellStep(int x, int y);
    void setRowOffsets(std::vector<int> offsets);
    void setCellStyle(CellStyle style);

    // Event listeners

    void setOnItemFocus(std::function<void(const std::string &)> fn) { _onItemFocus = std::move(fn); }
    void setOnItemFocusCleared(std::function<void()> fn) { _onItemFocusCleared = std::move(fn); }
    void setOnItemDoubleClick(std::function<void(const std::string &)> fn) { _onItemDoubleClick = std::move(fn); }

    // END Event listeners

private:
    std::vector<Item> _items;
    std::vector<Link> _links;
    int _columnCount {0};
    int _cellSize {0};
    glm::ivec2 _cellSpacing {0};
    glm::ivec2 _cellOrigin {-1};
    glm::ivec2 _cellStep {0};
    std::vector<int> _rowOffsets;
    CellStyle _cellStyle;
    int _rowOffset {0};
    int _focusedItemIndex {-1};
    float _focusedBorderPulsePhase {0.0f};

    // Event listeners

    std::function<void(const std::string &)> _onItemFocus;
    std::function<void()> _onItemFocusCleared;
    std::function<void(const std::string &)> _onItemDoubleClick;

    // END Event listeners

    bool hasValidPosition(const Item &item) const;
    bool isItemVisible(const Extent &extent) const;
    int getCellOriginX() const;
    int getCellOriginY() const;
    int getCellStepX() const;
    int getCellStepY() const;
    int getVisibleRowCount() const;
    int getMaxRow() const;
    int getItemIndex(int x, int y) const;
    const Item *findItem(const std::string &tag) const;
    void clearFocusedItem();
    Extent getItemExtent(const Item &item) const;
    Extent getItemIconExtent(const Extent &itemExtent) const;
    bool isItemBright(const Item &item) const;
    glm::vec4 getCellBackgroundColor(const Item &item) const;
    glm::vec3 getItemBorderColor(const Item &item, bool focused) const;
    glm::vec4 getItemIconColor(const Item &item) const;
    std::optional<glm::vec3> getFocusedBorderColor(const Item &item, bool focused) const;
    float getFocusedBorderPulseFactor() const;
    void renderLink(
        const Link &link,
        const glm::ivec2 &offset,
        scene::IRenderPass &pass) const;
    void renderItemBorder(
        const Item &item,
        bool focused,
        const Extent &extent,
        const glm::ivec2 &offset,
        scene::IRenderPass &pass);
    void renderFocusedBorder(
        const Item &item,
        bool focused,
        const Extent &extent,
        const glm::ivec2 &offset,
        scene::IRenderPass &pass);
};

} // namespace gui

} // namespace reone
