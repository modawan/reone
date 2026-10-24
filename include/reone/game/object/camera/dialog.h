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

#include <array>
#include <optional>

#include "../camera.h"

namespace reone {

namespace game {

/**
 * The camera of a conversation. Each shot frames a speaker and a listener;
 * the camera keeps to one side of the line between a pair for the whole
 * conversation, and follows the pair as they move.
 */
class DialogCamera : public Camera {
public:
    /** The framing of the speaker's face. */
    static constexpr int kFaceAngle = 1;
    /** The framing over the listener's shoulder. */
    static constexpr int kShoulderAngle = 2;
    /** The framing of the pair from the side. */
    static constexpr int kWideAngle = 3;

    /** A shot of a speaker and a listener, as an entry or a reply frames it. */
    struct Shot {
        /** The framing: a face, shoulder or wide shot; any other angle looks from the last placed camera. */
        int angle {0};
        /** Raises the camera, in metres. */
        float cameraHeight {0.0f};
        /** Raises the point looked at, in metres. */
        float targetHeight {0.0f};
        /** Pulls a face shot back from the speaker, in metres. */
        float pullBack {0.0f};
    };

    DialogCamera(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Camera(
            id,
            std::move(sceneName),
            game,
            services) {
    }

    void load();

    /**
     * A conversation starts: the side each pair is seen from is chosen
     * afresh. With \p modelPoints, shots frame the participants' models
     * where they stand, lifted by their head heights inside the framing,
     * and pull the camera in front of what hides the pair; otherwise they
     * frame each participant's camera hook.
     */
    void startConversation(bool modelPoints);
    /**
     * Frames \p shot of \p speaker and \p listener, each a creature,
     * placeable or door; any other object leaves the view as it is. A wide
     * shot right after another keeps the first. A shot that something hides
     * from its side frames the speaker's face instead.
     */
    void setShot(const std::shared_ptr<Object> &speaker, const std::shared_ptr<Object> &listener, const Shot &shot);
    /**
     * A placed camera at \p position takes the view; the dialog camera takes
     * its pose, unturned. A later shot that is no face, shoulder or wide shot
     * looks from there.
     */
    void takePlacedShot(const glm::vec3 &position);
    /** Frames the pair of the current shot where they stand now. */
    void followShot();

private:
    /** The side a pair is seen from: the first object, the second, and side 1 or 2 (0 for a free slot). */
    struct LineOfAction {
        uint32_t first {0};
        uint32_t second {0};
        int side {0};
    };

    /** Where a shot's camera stands, the point it looks toward, and the heading it faces. */
    struct View {
        glm::vec3 eye {0.0f};
        glm::vec3 target {0.0f};
        glm::vec3 heading {0.0f};
    };

    /** The pairs whose side is chosen; a fifth pair takes the first slot. */
    std::array<LineOfAction, 4> _linesOfAction;
    /** Shots frame the participants' models where they stand, rather than their camera hooks. */
    bool _modelPoints {false};
    /** The speaker and listener of the current shot. */
    RuntimeObjectRef<Object> _speaker;
    RuntimeObjectRef<Object> _listener;
    /** The speaker's and listener's head heights, as the shot found them. */
    float _speakerHeight {0.0f};
    float _listenerHeight {0.0f};
    /** The current shot, with its pull-back as kept. */
    Shot _shot;
    /** The current shot is hidden from its side, or frames one object with itself. */
    bool _obstructed {false};
    /** The angle of the shot shown; 6 for a placed camera. */
    int _angle {0};
    /** Where the last placed camera stood. */
    glm::vec3 _placedPosition {0.0f};

    float projectionFovy() const override;

    /**
     * The side \p speaker and \p listener are seen from, choosing it for a new
     * pair. With model points a known pair keeps its side as first chosen.
     */
    int addLineOfAction(const Object &speaker, const Object &listener);
    /** The side a known pair is seen from, swapped when the roles are; 0 for an unknown pair. */
    int lineOfActionSide(const Object &speaker, const Object &listener) const;
    /** The side whose wide and shoulder shots of \p speaker and \p listener the scene blocks less often; 2 on a tie. */
    int unobstructedSide(const Object &speaker, const Object &listener);
    /** The view of a shot at \p angle of points \p a and \p b from \p side, standing at \p from when the angle frames nothing. */
    View frame(int angle, int side, glm::vec3 a, glm::vec3 b, const glm::vec3 &from);
    /** The point a shot frames on \p object: its camera hook, or its head height above where it stands. */
    glm::vec3 framedPoint(const Object &object, float height) const;
    /** The point between the pair that a wide shot must see. */
    glm::vec3 pairMidpoint(const Object &speaker, const Object &listener) const;
    /** Counts 1 when the scene, apart from the pair, blocks the line from \p from to \p to. */
    int blocked(const glm::vec3 &from, const glm::vec3 &to) const;
    /** Like blocked(), giving where the line is blocked. */
    std::optional<glm::vec3> blockedAt(const glm::vec3 &from, const glm::vec3 &to) const;
    /** Puts the camera at the view's eye, facing its heading tilted toward its target. */
    void applyView(const View &view);
};

} // namespace game

} // namespace reone
