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

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "reone/game/types.h"
#include "reone/resource/types.h"

namespace reone {

namespace graphics {
class Model;
}

namespace audio {
class AudioClip;
}

namespace resource {
class TwoDAs;
class TwoDA;
class AudioClips;
class Models;
} // namespace resource

namespace game {

/**
 * The beam model a visual program draws: programs 608-621 in KotOR and
 * 608-623 in TSL each draw one beam.
 *
 * @return the model name, or an empty string when the program draws no beam
 */
inline std::string beamModelForProgram(int program, bool tsl) {
    static constexpr int kFirstBeamProgram = 608;
    static constexpr int kLastBeamProgram = 621;
    static constexpr int kLastBeamProgramTSL = 623;
    static constexpr const char *kBeamModels[] = {
        "v_lightns_dur", "v_lightnx_dur", "v_drddisab_dur", "v_drdkill_dur",
        "v_deathfld_dur", "v_drain_dur", "v_flame_dur", "v_stunray_dur",
        "v_coldray_dur", "v_ionray01_dur", "v_ionray02_dur", "v_fstorm_dur",
        "v_drdstun_dur", "v_fshock_dur", "v_flamep_dur", "v_flamepl_dur"};
    const int lastProgram = tsl ? kLastBeamProgramTSL : kLastBeamProgram;
    return program >= kFirstBeamProgram && program <= lastProgram ? kBeamModels[program - kFirstBeamProgram] : "";
}

/**
 * The shell texture a visual program lays over a model. Program 1426 lays
 * the stealth texture; programs 1401-1412 lay fx_tex_01 to fx_tex_12 and
 * 1413-1425 lay fx_tex_14 to fx_tex_26, there being no texture 13.
 *
 * @return the texture name, or an empty string when the program lays no shell
 */
inline std::string shieldTextureForProgram(int program) {
    static constexpr int kStealthProgram = 1426;
    static constexpr int kFirstShieldProgram = 1401;
    static constexpr int kLastShieldProgram = 1425;
    static constexpr int kMissingShieldTexture = 13;
    if (program == kStealthProgram) {
        return "fx_tex_stealth";
    }
    if (program < kFirstShieldProgram || program > kLastShieldProgram) {
        return "";
    }
    int texture = program - kFirstShieldProgram + 1;
    if (texture >= kMissingShieldTexture) {
        ++texture;
    }
    return "fx_tex_" + std::string(texture < 10 ? "0" : "") + std::to_string(texture);
}

/** A model a visual program attaches, and the node it attaches it to. */
struct SpellVisualProgram {
    std::string model;
    std::string hook;
};

/** The spell visual of programs 1200-1202, or none for any other program. */
inline std::optional<SpellVisualProgram> spellVisualForProgram(int program) {
    static constexpr int kFirstSpellVisualProgram = 1200;
    static constexpr int kLastSpellVisualProgram = 1202;
    static constexpr std::pair<const char *, const char *> kSpellVisuals[] = {
        {"v_fizzle_imp", "headconjure"}, {"v_fizzle_imp", "handconjure"}, {"v_fresist_imp", "impact"}};
    if (program < kFirstSpellVisualProgram || program > kLastSpellVisualProgram) {
        return std::nullopt;
    }
    const auto &[model, hook] = kSpellVisuals[program - kFirstSpellVisualProgram];
    return SpellVisualProgram {model, hook};
}

/**
 * The model attachment of programs 1700-1702, and in TSL also 1703, which
 * hooks the head model first; none for any other program.
 */
inline std::optional<SpellVisualProgram> modelAttachmentForProgram(int program, bool tsl) {
    static constexpr int kFirstAttachmentProgram = 1700;
    static constexpr int kLastAttachmentProgram = 1702;
    static constexpr int kLastAttachmentProgramTSL = 1703;
    static constexpr std::pair<const char *, const char *> kAttachments[] = {
        {"v_medal_dur", "medalhook"}, {"v_revmask1_dur", "revmask1hook"}, {"v_revmask2_dur", "revmask2hook"}, {"v_MthMst", "maskhook"}};
    const int lastProgram = tsl ? kLastAttachmentProgramTSL : kLastAttachmentProgram;
    if (program < kFirstAttachmentProgram || program > lastProgram) {
        return std::nullopt;
    }
    const auto &[model, hook] = kAttachments[program - kFirstAttachmentProgram];
    return SpellVisualProgram {model, hook};
}

/**
 * The node of a creature a beam starts from, by body part; any other part
 * starts it from the root.
 */
inline std::string beamSourceHook(BodyNode part) {
    switch (part) {
    case BodyNode::Hand:
        return "handconjure";
    case BodyNode::Chest:
        return "impact";
    case BodyNode::Head:
        return "headconjure";
    case BodyNode::HandLeft:
        return "lhand";
    case BodyNode::HandRight:
        return "rhand";
    default:
        return "root";
    }
}

/** A row of visualeffects.2da. */
struct VisualEffectDesc {
    std::string label;
    /**
     * Models attached to an object at its root, chosen by the object's size;
     * see rootModel().
     */
    std::shared_ptr<graphics::Model> rootHugeModel;
    std::shared_ptr<graphics::Model> rootLargeModel;
    std::shared_ptr<graphics::Model> rootMediumModel;
    /** The model attached at the object's impact node. */
    std::shared_ptr<graphics::Model> impactModel;
    /** The model attached at the object's head. */
    std::shared_ptr<graphics::Model> headModel;
    /** Attached models follow their node's position but not its rotation. */
    bool orientationOff {false};
    /**
     * The one model a visual shown at a point in the world uses: the first of
     * the head, impact and root models the row names.
     */
    std::shared_ptr<graphics::Model> locationModel;
    std::shared_ptr<audio::AudioClip> soundImpact;
    /** The visual program run on impact, or -1 for none. */
    int progFXImpact {-1};
    /** The visual program run for the effect's duration, or -1 for none. */
    int progFXDuration {-1};
    /** Type_FD "F": the row is only ever presented as an impact. */
    bool fireAndForget {false};
    std::shared_ptr<audio::AudioClip> soundDuration;

    /**
     * The root model for an object of this size: a huge object takes the
     * huge model, then the large, then the medium one; a large object the
     * large, then the medium one; any other object the medium one.
     */
    const std::shared_ptr<graphics::Model> &rootModel(CreatureSize size) const {
        if (size == CreatureSize::Huge && rootHugeModel) {
            return rootHugeModel;
        }
        if ((size == CreatureSize::Huge || size == CreatureSize::Large) && rootLargeModel) {
            return rootLargeModel;
        }
        return rootMediumModel;
    }
};

class IVisualEffects {
public:
    virtual std::optional<std::reference_wrapper<const VisualEffectDesc>> get(uint32_t id) const = 0;

    /**
     * The visual effect a hit dealing damage of the given slot shows
     * (damagehitvisual.2da).
     *
     * @param ranged whether the hit is ranged, which reads the ranged column
     * @return the visual effect, or 0 when the slot names none
     */
    virtual int hitVisual(int damageSlot, bool ranged) const = 0;
};

class VisualEffects : public IVisualEffects {
public:
    VisualEffects(resource::TwoDAs &twoDas,
                  resource::AudioClips &audioClips,
                  resource::Models &models) :
        _twoDas(twoDas),
        _audioClips(audioClips),
        _models(models) {}

    void init();
    void clear();

    std::optional<std::reference_wrapper<const VisualEffectDesc>> get(uint32_t id) const override;
    int hitVisual(int damageSlot, bool ranged) const override;

private:
    static constexpr int kDamageSlotCount = 14;

    /** A damage slot's hit visuals; 0 names none. */
    struct HitVisuals {
        int melee {0};
        int ranged {0};
    };

    resource::TwoDAs &_twoDas;
    resource::AudioClips &_audioClips;
    resource::Models &_models;
    std::map<uint32_t, VisualEffectDesc> _effects;
    std::array<HitVisuals, kDamageSlotCount> _hitVisuals;
};

} // namespace game

} // namespace reone
