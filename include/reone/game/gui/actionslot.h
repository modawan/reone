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

#include <optional>
#include <string>

#include "reone/game/contextaction.h"

namespace reone {

namespace audio {

class AudioSource;

}

namespace game {

class Game;
struct ContextAction;
struct ServicesView;

/**
 * The action menus' answer to a selection: a refused power shows why on the
 * menu for five seconds, fading over the second half, with the rejection
 * sound; a form, implant mode or behaviour announces itself for four seconds.
 */
class ActionMenuFeedback {
public:
    /** Sound the rejection and show the message of \p reason; a reason without one keeps the last. */
    void reject(PowerUnavailableReason reason, ServicesView &services);
    /** Sound the acceptance of a selection. */
    void accept(ServicesView &services);
    /** Show \p message, the outcome of a selection, for a while; none shows an empty line. */
    void announce(std::optional<uint32_t> message);
    void update(float dt);

    /** The rejection message is shown. */
    bool active() const { return _remaining > 0.0f; }
    bool announcing() const { return _announcementRemaining > 0.0f; }
    std::string text(const Game &game) const;
    std::string announcementText(const Game &game) const;
    /** The opacity of the menu's description line, which a rejection fades out. */
    float descriptionOpacity() const;

private:
    /** The seconds the rejection message has left. */
    float _remaining {0.0f};
    std::optional<uint32_t> _message;
    std::shared_ptr<audio::AudioSource> _sound;
    float _announcementRemaining {0.0f};
    std::optional<uint32_t> _announcement;
};

struct ActionSlot {
    std::vector<ContextAction> actions;
    size_t indexSelected {0};
};

/** Draw the icon of \p action, dimmed when it cannot be used. */
void renderContextActionIcon(const ContextAction &action, glm::mat4 transform, ServicesView &services);

} // namespace game
} // namespace reone
