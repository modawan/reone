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

#pragma once

#include <vector>

#include "gui.h"
#include "tooltip.h"

namespace reone {

namespace resource {

class Gffs;

}

namespace gui {

class IGUIs {
public:
    virtual ~IGUIs() = default;

    virtual void clear() = 0;
    /** Sets how the controls of every GUI loaded afterwards draw their border fills (IGUI::fillRules); set before any GUI loads. */
    virtual void setFillRules(FillRules rules) = 0;
    /**
     * Picks the small or large variants of the fonts for every GUI, loaded or
     * loaded later, and shows every text in them.
     */
    virtual void setSmallFonts(bool useSmall) = 0;
    /** The font a text shows for a base font, in the variant picked (Control::fontName). */
    virtual std::string fontName(const std::string &baseFont) const = 0;

    virtual std::shared_ptr<IGUI> get(const std::string &resRef, std::function<void(IGUI &)> preload = nullptr) = 0;

    /**
     * Load a GUI of its own, which the cache does not keep: for a screen that
     * binds its controls to itself while another screen may load the same GUI.
     */
    virtual std::shared_ptr<IGUI> load(const std::string &resRef, std::function<void(IGUI &)> preload = nullptr) = 0;

    /** Advances the clock every GUI shares (GUIClock), once a frame, by dt seconds. */
    virtual void updateClock(float dt) = 0;
    /** The interface's one tooltip, which every GUI made here drives. */
    virtual Tooltip &tooltip() = 0;
};

class GUIs : public IGUIs, boost::noncopyable {
public:
    GUIs(graphics::GraphicsOptions &graphicsOpt,
         scene::ISceneGraphs &sceneGraphs,
         graphics::GraphicsServices &graphicsSvc,
         resource::ResourceServices &resourceSvc) :
        _graphicsOpt(graphicsOpt),
        _sceneGraphs(sceneGraphs),
        _graphicsSvc(graphicsSvc),
        _resourceSvc(resourceSvc),
        _tooltip(graphicsOpt) {
    }

    void clear() override {
        _guis.clear();
    }

    void setFillRules(FillRules rules) override {
        _fillRules = std::move(rules);
    }

    void setSmallFonts(bool useSmall) override {
        _smallFonts = useSmall;
        forEachGUI([useSmall](IGUI &gui) { gui.setSmallFonts(useSmall); });
    }

    std::string fontName(const std::string &baseFont) const override {
        return Control::fontName(baseFont, _smallFonts);
    }

    Tooltip &tooltip() override {
        return _tooltip;
    }

    std::shared_ptr<IGUI> get(const std::string &resRef, std::function<void(IGUI &)> preload) override {
        auto maybeGUI = _guis.find(resRef);
        if (maybeGUI != _guis.end()) {
            return maybeGUI->second;
        }
        auto gui = doGet(resRef, preload);
        return _guis.insert(make_pair(resRef, std::move(gui))).first->second;
    }

    std::shared_ptr<IGUI> load(const std::string &resRef, std::function<void(IGUI &)> preload) override {
        auto gui = doGet(resRef, preload);
        if (gui) {
            forgetGoneGUIs();
            _loaded.push_back(gui);
        }
        return gui;
    }

    void updateClock(float dt) override {
        _clock.update(dt);
    }

private:
    graphics::GraphicsOptions &_graphicsOpt;
    scene::ISceneGraphs &_sceneGraphs;
    graphics::GraphicsServices &_graphicsSvc;
    resource::ResourceServices &_resourceSvc;

    /** The clock every GUI shares; declared before the GUIs that hold it, so it outlives them. */
    GUIClock _clock;
    /** Made before the GUIs and gone after them, which tell it as they go. */
    Tooltip _tooltip;
    std::unordered_map<std::string, std::shared_ptr<IGUI>> _guis;
    /** How the controls of the GUIs draw their borders and fills. */
    FillRules _fillRules {};
    /** The GUIs loaded apart from the shared ones (load), while they live. */
    std::vector<std::weak_ptr<IGUI>> _loaded;
    /** The GUIs show the small variants of their fonts. */
    bool _smallFonts {false};

    std::shared_ptr<IGUI> doGet(std::string resRef, std::function<void(IGUI &)> preload);
    /** Calls \p fn with every GUI that lives, shared or loaded apart. */
    void forEachGUI(const std::function<void(IGUI &)> &fn);
    /** Lets go of the GUIs loaded apart that have gone. */
    void forgetGoneGUIs();
};

} // namespace gui

} // namespace reone
