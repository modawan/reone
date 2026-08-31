/* Copyright (c) 2026 The reone project contributors */

#include "reone/game/saveprovenance.h"

#include <array>

namespace reone {
namespace game {

SaveGffShadow SaveGffShadow::capture(const resource::Gff &source) {
    return SaveGffShadow(source.deepCopy());
}

std::shared_ptr<resource::Gff> SaveGffShadow::cloneForMerge() const {
    return _record ? _record->deepCopy() : nullptr;
}

void replaceSaveField(resource::Gff &record, resource::Gff::Field field) {
    auto &fields = record.fields();
    auto first = std::find_if(fields.begin(), fields.end(), [&](const auto &existing) {
        return existing.label == field.label;
    });
    if (first == fields.end()) {
        fields.push_back(std::move(field));
        return;
    }
    *first = std::move(field);
    const std::string label(first->label);
    fields.erase(
        std::remove_if(std::next(first), fields.end(), [&](const auto &existing) {
            return existing.label == label;
        }),
        fields.end());
}

void removeSaveField(resource::Gff &record, std::string_view label) {
    auto &fields = record.fields();
    fields.erase(
        std::remove_if(fields.begin(), fields.end(), [&](const auto &field) {
            return field.label == label;
        }),
        fields.end());
}

std::vector<SerializedObjectIdClaim> collectSerializedObjectIdClaims(
    const resource::Gff &root,
    const SerializedIdentityContext &context,
    SerializedGraphRoot graphRoot) {
    if (!context.hasAuthoritativeObjectIds()) {
        return {};
    }

    std::vector<SerializedObjectIdClaim> result;
    auto add = [&](const resource::Gff &record, std::string path) {
        uint32_t id = 0;
        if (record.readDword(id, "ObjectId")) {
            result.push_back({id, std::move(path)});
        }
    };
    auto addItems = [&](const resource::Gff &owner,
                        const std::string &ownerPath,
                        bool includeInventory) {
        const auto equipped = owner.getList("Equip_ItemList");
        for (size_t index = 0; index < equipped.size(); ++index) {
            add(*equipped[index], ownerPath + "/Equip_ItemList[" +
                                      std::to_string(index) + "]");
        }
        if (!includeInventory) {
            return;
        }
        const auto inventory = owner.getList("ItemList");
        for (size_t index = 0; index < inventory.size(); ++index) {
            add(*inventory[index], ownerPath + "/ItemList[" +
                                      std::to_string(index) + "]");
        }
    };
    auto addCreatureList = [&](const char *label, const char *rootName) {
        const auto creatures = root.getList(label);
        for (size_t index = 0; index < creatures.size(); ++index) {
            std::string path = std::string(rootName) + "/" + label + "[" +
                               std::to_string(index) + "]";
            add(*creatures[index], path);
            addItems(*creatures[index], path, true);
        }
    };

    switch (graphRoot) {
    case SerializedGraphRoot::ModuleIfo: {
        const auto areas = root.getList("Mod_Area_list");
        for (size_t index = 0; index < areas.size(); ++index) {
            add(*areas[index], "ifo/Mod_Area_list[" + std::to_string(index) + "]");
        }
        const auto players = root.getList("Mod_PlayerList");
        for (size_t index = 0; index < players.size(); ++index) {
            std::string path = "ifo/Mod_PlayerList[" + std::to_string(index) + "]";
            add(*players[index], path);
            addItems(*players[index], path, false);
        }
        addCreatureList("Creature List", "ifo");
        break;
    }
    case SerializedGraphRoot::AreaGit: {
        addCreatureList("Creature List", "git");
        static constexpr std::array<const char *, 9> objectLists {
            "Door List", "Placeable List", "TriggerList", "Trigger List",
            "Encounter List", "StoreList", "WaypointList", "SoundList", "List"};
        for (const char *label : objectLists) {
            const auto objects = root.getList(label);
            for (size_t index = 0; index < objects.size(); ++index) {
                std::string path = "git/" + std::string(label) + "[" +
                                   std::to_string(index) + "]";
                add(*objects[index], path);
                if (std::string_view(label) == "Placeable List" ||
                    std::string_view(label) == "StoreList") {
                    addItems(*objects[index], path, true);
                }
            }
        }
        break;
    }
    }
    return result;
}

void SaveResourceShadows::capture(SaveResourceKey key, const resource::Gff &source) {
    _shadows.insert_or_assign(std::move(key), SaveGffShadow::capture(source));
}

const SaveGffShadow *SaveResourceShadows::find(const SaveResourceKey &key) const {
    auto found = _shadows.find(key);
    return found == _shadows.end() ? nullptr : &found->second;
}

} // namespace game
} // namespace reone
