/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include "../gui.h"
#include "../statussummary.h"

#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"

#include <array>

namespace reone {

namespace game {

/** Modal runtime wrapper for statussummary.gui/statussummary_p.gui. */
class StatusSummary : public GameGUI {
public:
    StatusSummary(Game &game, ServicesView &services, StatusSummaryAccumulator &accumulator);

    /** Snapshot and show the current pending batch. Called from HUD::update. */
    bool presentPending();
    void reset();

    bool isVisible() const { return _visible; }

    bool handle(const input::Event &event) override;

private:
    friend class TestStatusSummary;

    struct RowControls {
        std::shared_ptr<gui::Label> icon;
        std::shared_ptr<gui::Label> description;
        gui::Control::Extent iconExtent;
        gui::Control::Extent descriptionExtent;
        std::string authoredText;
    };

    StatusSummaryAccumulator &_accumulator;
    std::shared_ptr<gui::Button> _ok;
    std::array<RowControls, kStatusSummaryCategoryCount> _rows;
    std::vector<std::shared_ptr<gui::Control>> _unusedTSLRows;
    gui::Control::Extent _rootExtent;
    gui::Control::Extent _okExtent;
    bool _loaded {false};
    bool _visible {false};

    void onGUILoaded() override;
    void acknowledge();
    void clearPresentation();
    void layoutDisplayedBatch();
};

} // namespace game

} // namespace reone
