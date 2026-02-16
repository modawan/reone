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

#include "reone/game/gui/actionslot.h"
#include "reone/game/contextaction.h"
#include "reone/game/d20/feat.h"
#include "reone/game/d20/feats.h"
#include "reone/game/d20/skill.h"
#include "reone/game/d20/skills.h"
#include "reone/game/d20/spell.h"
#include "reone/game/di/services.h"
#include "reone/game/object/item.h"
#include "reone/game/types.h"
#include "reone/graphics/context.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/uniforms.h"
#include "reone/resource/provider/textures.h"

namespace reone {
namespace game {

static std::string g_attackIcon("i_attack");

void renderContextActionIcon(const ContextAction &action, glm::mat4 transform, ServicesView &services) {
    std::shared_ptr<graphics::Texture> texture;

    switch (action.type) {
    case ActionType::AttackObject:
        texture = services.resource.textures.get(g_attackIcon, graphics::TextureUsage::GUI);
        break;
    case ActionType::UseFeat: {
        std::shared_ptr<Feat> feat(services.game.feats.get(action.feat));
        if (feat) {
            texture = feat->icon;
        }
        break;
    }
    case ActionType::UseSkill: {
        std::shared_ptr<Skill> skill(services.game.skills.get(action.skill));
        if (skill) {
            texture = skill->icon;
        }
        break;
    }
    case ActionType::CastSpellAtObject: {
        if (const auto &spellIcon = action.spell->icon) {
            texture = spellIcon;
        } else if (action.item) {
            texture = action.item->icon();
        }
        break;
    }
    default:
        break;
    }
    if (!texture)
        return;

    services.graphics.context.bindTexture(*texture);

    services.graphics.uniforms.setLocals([transform](auto &locals) {
        locals.reset();
        locals.model = transform;
    });
    services.graphics.context.useProgram(services.graphics.shaderRegistry.get(graphics::ShaderProgramId::mvpTexture));
    services.graphics.meshRegistry.get(graphics::MeshName::quad).draw(services.graphics.statistic);
}

} // namespace game
} // namespace reone
