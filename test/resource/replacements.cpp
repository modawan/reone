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

#include <gtest/gtest.h>

#include "reone/resource/extractresources.h"
#include "reone/resource/format/erfwriter.h"
#include "reone/resource/replacementresources.h"
#include "reone/resource/replacements.h"
#include "reone/resource/resources.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/stream/memoryoutput.h"

using namespace reone;
using namespace reone::resource;

namespace {

enum class Backend {
    Legacy,
    Extract,
};

std::string backendName(const testing::TestParamInfo<Backend> &info) {
    return info.param == Backend::Legacy ? "Legacy" : "Extract";
}

ByteBuffer bytes(std::string_view value) {
    return ByteBuffer(value.begin(), value.end());
}

ByteBuffer erfBytes(const std::vector<ErfWriter::Resource> &resources) {
    ErfWriter writer;
    for (auto resource : resources) {
        writer.add(std::move(resource));
    }
    ByteBuffer buffer;
    MemoryOutputStream stream(buffer);
    writer.save(ErfWriter::FileType::ERF, stream);
    return buffer;
}

std::string dataOf(const std::optional<Resource> &resource) {
    if (!resource) {
        return "<not found>";
    }
    return std::string(resource->data.begin(), resource->data.end());
}

class ReplacementResourcesTest : public testing::TestWithParam<Backend> {
protected:
    void SetUp() override {
        std::unique_ptr<IResources> backend;
        if (GetParam() == Backend::Legacy) {
            backend = std::make_unique<Resources>();
        } else {
            backend = std::make_unique<ExtractResources>();
        }
        _resources = std::make_unique<ReplacementResources>(std::move(backend), _replacements);
    }

    ResourceId id(std::string resRef) const {
        return ResourceId(std::move(resRef), ResType::Txt);
    }

    void addMounted(std::string resRef, std::string data, ContainerKind kind = ContainerKind::Global) {
        _resources->addMemERF(erfBytes({ErfWriter::Resource {std::move(resRef), ResType::Txt, bytes(data)}}), kind);
    }

    ResourceReplacements _replacements;
    std::unique_ptr<ReplacementResources> _resources;
};

TEST_P(ReplacementResourcesTest, replacement_beats_a_mounted_resource) {
    addMounted("shared", "mounted");

    _replacements.replaceResource(id("shared"), bytes("replacement"));

    EXPECT_EQ("replacement", dataOf(_resources->find(id("shared"))));
}

TEST_P(ReplacementResourcesTest, replacement_supplies_a_missing_resource) {
    _replacements.replaceResource(id("missing"), bytes("replacement"));

    EXPECT_EQ("replacement", dataOf(_resources->find(id("missing"))));
}

TEST_P(ReplacementResourcesTest, repeated_replacement_uses_the_newest_bytes) {
    _replacements.replaceResource(id("shared"), bytes("first"));
    _replacements.replaceResource(id("shared"), bytes("second"));

    EXPECT_EQ("second", dataOf(_resources->find(id("shared"))));
}

TEST_P(ReplacementResourcesTest, removal_restores_the_mounted_resource) {
    addMounted("shared", "mounted");
    _replacements.replaceResource(id("shared"), bytes("replacement"));

    _replacements.removeResourceReplacement(id("shared"));

    EXPECT_EQ("mounted", dataOf(_resources->find(id("shared"))));
}

TEST_P(ReplacementResourcesTest, clear_restores_all_mounted_resources) {
    addMounted("first", "first mounted");
    addMounted("second", "second mounted");
    _replacements.replaceResource(id("first"), bytes("first replacement"));
    _replacements.replaceResource(id("second"), bytes("second replacement"));

    _replacements.clearResourceReplacements();

    EXPECT_EQ("first mounted", dataOf(_resources->find(id("first"))));
    EXPECT_EQ("second mounted", dataOf(_resources->find(id("second"))));
}

TEST_P(ReplacementResourcesTest, empty_replacement_is_present) {
    _replacements.replaceResource(id("empty"), {});

    auto resource = _resources->find(id("empty"));

    ASSERT_TRUE(resource);
    EXPECT_TRUE(resource->data.empty());
}

TEST_P(ReplacementResourcesTest, resource_ids_are_case_normalized) {
    _replacements.replaceResource(id("MiXeD"), bytes("replacement"));

    EXPECT_EQ("replacement", dataOf(_resources->find(id("mixed"))));
    EXPECT_EQ("replacement", dataOf(_resources->find(id("MIXED"))));
}

TEST_P(ReplacementResourcesTest, preserves_fallback_behavior_without_replacements) {
    addMounted("mounted", "mounted");

    EXPECT_EQ("mounted", dataOf(_resources->find(id("mounted"))));
    EXPECT_FALSE(_resources->find(id("missing")));
}

TEST_P(ReplacementResourcesTest, returned_data_remains_valid_after_replacement_changes) {
    _replacements.replaceResource(id("shared"), bytes("first"));
    auto first = _resources->get(id("shared"));
    MemoryInputStream stream(first.data);

    _replacements.replaceResource(id("shared"), bytes("second"));
    _replacements.removeResourceReplacement(id("shared"));

    ByteBuffer read(first.data.size());
    stream.read(read.data(), read.size());
    EXPECT_EQ("first", std::string(read.begin(), read.end()));
}

TEST_P(ReplacementResourcesTest, replacements_survive_local_and_save_scope_changes) {
    addMounted("shared", "global", ContainerKind::Global);
    addMounted("shared", "save", ContainerKind::Save);
    addMounted("shared", "local", ContainerKind::Local);
    _replacements.replaceResource(id("shared"), bytes("replacement"));

    _resources->clearLocal();
    _resources->clearSave();

    EXPECT_EQ("replacement", dataOf(_resources->find(id("shared"))));
}

TEST(ResourceReplacementsRevision, advances_on_every_meaningful_state_transition) {
    ResourceReplacements replacements;
    ResourceId id("script", ResType::Ncs);

    EXPECT_EQ(0u, replacements.revision(id));

    replacements.replaceResource(id, bytes("first"));
    auto first = replacements.revision(id);
    replacements.removeResourceReplacement(id);
    auto removed = replacements.revision(id);
    replacements.replaceResource(id, bytes("second"));
    auto second = replacements.revision(id);
    replacements.clearResourceReplacements();
    auto cleared = replacements.revision(id);
    replacements.replaceResource(id, bytes("third"));
    auto third = replacements.revision(id);

    EXPECT_LT(first, removed);
    EXPECT_LT(removed, second);
    EXPECT_LT(second, cleared);
    EXPECT_LT(cleared, third);
}

INSTANTIATE_TEST_SUITE_P(Backends,
                         ReplacementResourcesTest,
                         testing::Values(Backend::Legacy, Backend::Extract),
                         backendName);

} // namespace
