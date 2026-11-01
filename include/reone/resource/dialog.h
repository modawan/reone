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

#include "types.h"

namespace reone {

namespace resource {

struct Dialog {
    struct WaitFlags {
        static constexpr int waitAnimFinish = 1;
        /** The node waits for its sound to finish. */
        static constexpr int waitSoundFinish = 2;
        /** The node waits for its participants' dialog animations to finish. */
        static constexpr int waitParticipantAnimFinish = 4;
        /** The node waits for its fade to finish. */
        static constexpr int waitFadeFinish = 8;
    };

    struct Stunt {
        std::string participant;
        std::string stuntModel;
    };

    struct EntryReplyLink {
        struct ConditionParams {
            std::array<int, 5> ints {0, 0, 0, 0, 0};
            std::string str;
        };

        int index {0};
        std::string active;
        std::string active2;
        bool notActive {false};
        bool notActive2 {false};
        int logic {0};
        ConditionParams params;
        ConditionParams params2;
        /** A reply this link leads to is listed even when its condition fails. */
        bool displayInactive {false};
    };

    struct ParticipantAnimation {
        std::string participant;
        int animation;
    };

    struct EntryReply {
        struct ActionParams {
            std::array<int, 5> ints {0, 0, 0, 0, 0};
            std::string str;
        };

        std::string speaker;
        std::string text;
        /** The talk table string of the node's text, or -1 for none. */
        int textStrRef {-1};
        std::string voResRef;
        std::string script;
        std::string script2;
        std::string sound;
        std::string listener;
        std::string quest;
        uint32_t questEntry {0};
        int plotIndex {-1};
        float plotXPPercentage {0.0f};
        ActionParams actionParams;
        ActionParams actionParams2;
        /** Seconds the node stays, or -1 for none of its own. */
        int delay {0};
        int waitFlags {0};
        /** The node's sound is a recorded voice, which times the node by the dialog's entry delay. */
        bool soundExists {false};
        /**
         * TSL: the node's sound, or else its voice-over, takes a variation
         * number added to its name, as alien speech does.
         */
        bool soundTakesVariation {false};
        /**
         * TSL: the node is authored as having its variation number already:
         * a shown entry adds none, though a barked one-liner still does.
         */
        bool soundVariationAdded {false};
        /**
         * The node is spoken by its voice-over alone: with subtitles off, the
         * cinematic panel does not show its text.
         */
        bool voiceOverOnly {false};
        /** TSL: the entry cannot be skipped, even in a skippable conversation. */
        bool nodeUnskippable {false};
        /** How the entry fades the screen: 0 not at all; 1 and 4 towards the colour, others away from it. */
        int fadeType {0};
        glm::vec3 fadeColor {0.0f};
        /** Seconds before the fade starts. */
        float fadeDelay {0.0f};
        /** Seconds the fade takes; none for types 1 and 2, which cut. */
        float fadeLength {0.0f};
        int cameraId {-1};
        int cameraAngle {0};
        int cameraAnimation {0};
        float camFieldOfView {0.0f};
        /** Raises the camera of the shot, in metres. */
        float camHeightOffset {0.0f};
        /** Raises the point the shot looks at, in metres. */
        float tarHeightOffset {0.0f};
        int camVidEffect {-1}; /**< videoeffects row of the shot, -1 for none */
        std::vector<EntryReplyLink> replies;
        std::vector<EntryReplyLink> entries;
        std::vector<ParticipantAnimation> animations;
    };

    std::string resRef;
    bool skippable {false};
    std::string cameraModel;
    std::vector<EntryReplyLink> startEntries;
    std::vector<EntryReply> entries;
    std::vector<EntryReply> replies;
    std::string endScript;
    /** The script an interrupted conversation runs in place of endScript. */
    std::string abortScript;
    int entryIndex {-1};
    bool animatedCutscene {false};
    /** Seconds a voiced node without a delay of its own stays, besides waiting for its voice. */
    uint32_t entryDelay {0};
    /** Shots frame the participants' models where they stand, rather than their camera hooks. */
    bool oldHitCheck {false};
    std::vector<Stunt> stunts;
    ConversationType conversationType {ConversationType::Cinematic};
    ComputerType computerType {ComputerType::Normal};

    bool isSkippable() const { return skippable; }
    bool isAnimatedCutscene() const { return animatedCutscene; }

    const EntryReply &getEntry(int index) const { return entries[index]; }
    const EntryReply &getReply(int index) const { return replies[index]; }
};

} // namespace resource

} // namespace reone
