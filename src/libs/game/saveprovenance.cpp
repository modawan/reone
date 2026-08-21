/* Copyright (c) 2026 The reone project contributors */

#include "reone/game/saveprovenance.h"

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

void SaveResourceShadows::capture(SaveResourceKey key, const resource::Gff &source) {
    _shadows.insert_or_assign(std::move(key), SaveGffShadow::capture(source));
}

const SaveGffShadow *SaveResourceShadows::find(const SaveResourceKey &key) const {
    auto found = _shadows.find(key);
    return found == _shadows.end() ? nullptr : &found->second;
}

} // namespace game
} // namespace reone
