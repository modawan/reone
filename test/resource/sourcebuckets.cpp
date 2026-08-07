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

/**
 * The shared source list, and the equivalence both IResources backends owe
 * each other once sources carry a bucket.
 *
 * Placing a source in a bucket is capability only at this point: no caller
 * does it, so the characterization suites in lookup.cpp and extractresources.cpp
 * continue to describe what the engine actually does.
 */

#include <gtest/gtest.h>

#include "reone/resource/extractresources.h"
#include "reone/resource/format/erfwriter.h"
#include "reone/resource/replacementresources.h"
#include "reone/resource/replacements.h"
#include "reone/resource/resources.h"
#include "reone/resource/sourcelist.h"
#include "reone/system/exception/validation.h"
#include "reone/system/stream/memoryoutput.h"

#include "../fixtures/archive.h"

using namespace reone;
using namespace reone::resource;
using namespace reone::test;

namespace {

struct TestSource {
    ContainerKind kind;
    std::optional<ResourceSourceBucket> bucket;
    std::string name;
};

std::vector<std::string> names(const ResourceSourceList<TestSource> &list) {
    std::vector<std::string> result;
    for (const auto &entry : list) {
        result.push_back(entry.name);
    }
    return result;
}

void add(ResourceSourceList<TestSource> &list,
         std::string name,
         std::optional<ResourceSourceBucket> bucket = std::nullopt,
         ContainerKind kind = ContainerKind::Global) {
    list.add(TestSource {kind, bucket, std::move(name)});
}

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

ByteBuffer erfBytes(const std::string &resRef, const std::string &data) {
    ErfWriter writer;
    writer.add(ErfWriter::Resource {resRef, ResType::Txt, bytes(data)});
    ByteBuffer buffer;
    MemoryOutputStream stream(buffer);
    writer.save(ErfWriter::FileType::ERF, stream);
    return buffer;
}

ByteBuffer rimBytes(const std::string &resRef, const std::string &data) {
    RimWriter writer;
    writer.add(RimWriter::Resource {resRef, ResType::Txt, bytes(data)});
    ByteBuffer buffer;
    MemoryOutputStream stream(buffer);
    writer.save(stream);
    return buffer;
}

std::string dataOf(const std::optional<Resource> &resource) {
    if (!resource) {
        return "<not found>";
    }
    return std::string(resource->data.begin(), resource->data.end());
}

} // namespace

// The list itself: which source wins, and when a source goes away.

TEST(ResourceSourceRank, should_rank_buckets_in_raw_lookup_order) {
    std::optional<std::size_t> previous;
    for (auto bucket : kRawResourceLookupOrder) {
        if (previous) {
            EXPECT_LT(*previous, bucketRank(bucket)) << "buckets must rank in kRawResourceLookupOrder";
        }
        previous = bucketRank(bucket);
    }

    EXPECT_EQ(0u, bucketRank(kRawResourceLookupOrder.front()));
    EXPECT_EQ(kRawResourceLookupOrder.size() - 1, bucketRank(kRawResourceLookupOrder.back()));
}

TEST(ResourceSourceList, should_search_unbucketed_sources_newest_first) {
    ResourceSourceList<TestSource> list;
    add(list, "first");
    add(list, "second");
    add(list, "third");

    EXPECT_EQ((std::vector<std::string> {"third", "second", "first"}), names(list));
}

TEST(ResourceSourceList, should_order_bucketed_sources_by_bucket_not_by_add_order) {
    ResourceSourceList<TestSource> list;
    add(list, "keybif", ResourceSourceBucket::KeyBif);
    add(list, "loose", ResourceSourceBucket::LooseDirectory);
    add(list, "class2", ResourceSourceBucket::EncapsulatedClass2);
    add(list, "image", ResourceSourceBucket::ResourceImage);
    add(list, "class1", ResourceSourceBucket::EncapsulatedClass1);

    EXPECT_EQ((std::vector<std::string> {"loose", "class1", "image", "class2", "keybif"}), names(list));
}

TEST(ResourceSourceList, should_search_one_bucket_newest_first) {
    ResourceSourceList<TestSource> list;
    add(list, "older", ResourceSourceBucket::EncapsulatedClass2);
    add(list, "newer", ResourceSourceBucket::EncapsulatedClass2);
    add(list, "loose", ResourceSourceBucket::LooseDirectory);

    EXPECT_EQ((std::vector<std::string> {"loose", "newer", "older"}), names(list));
}

TEST(ResourceSourceList, should_take_its_mode_from_the_first_source) {
    ResourceSourceList<TestSource> unbucketed;
    EXPECT_EQ(ResourceSourceOrder::Empty, unbucketed.order());
    add(unbucketed, "unplaced");
    EXPECT_EQ(ResourceSourceOrder::Insertion, unbucketed.order());

    ResourceSourceList<TestSource> bucketed;
    add(bucketed, "loose", ResourceSourceBucket::LooseDirectory);
    EXPECT_EQ(ResourceSourceOrder::Bucketed, bucketed.order());
}

TEST(ResourceSourceList, should_reject_a_bucketed_source_in_an_insertion_ordered_list) {
    ResourceSourceList<TestSource> list;
    add(list, "unplaced");

    EXPECT_THROW(add(list, "loose", ResourceSourceBucket::LooseDirectory), ValidationException);
    EXPECT_EQ((std::vector<std::string> {"unplaced"}), names(list)) << "a rejected mount must not be held";
}

TEST(ResourceSourceList, should_reject_an_unbucketed_source_in_a_bucketed_list) {
    ResourceSourceList<TestSource> list;
    add(list, "loose", ResourceSourceBucket::LooseDirectory);

    EXPECT_THROW(add(list, "unplaced"), ValidationException);
    EXPECT_EQ((std::vector<std::string> {"loose"}), names(list)) << "a rejected mount must not be held";
}

TEST(ResourceSourceList, should_release_its_mode_once_empty) {
    ResourceSourceList<TestSource> list;
    add(list, "unplaced");
    list.clear();

    EXPECT_EQ(ResourceSourceOrder::Empty, list.order());
    EXPECT_NO_THROW(add(list, "loose", ResourceSourceBucket::LooseDirectory));
    EXPECT_EQ(ResourceSourceOrder::Bucketed, list.order());

    list.clearKind(ContainerKind::Global);

    EXPECT_EQ(ResourceSourceOrder::Empty, list.order()) << "clearing a scope empty must release the mode too";
    EXPECT_NO_THROW(add(list, "unplaced again"));
}

TEST(ResourceSourceList, should_clear_one_kind_across_every_bucket) {
    ResourceSourceList<TestSource> list;
    add(list, "global_loose", ResourceSourceBucket::LooseDirectory, ContainerKind::Global);
    add(list, "local_loose", ResourceSourceBucket::LooseDirectory, ContainerKind::Local);
    add(list, "local_keybif", ResourceSourceBucket::KeyBif, ContainerKind::Local);
    add(list, "save_image", ResourceSourceBucket::ResourceImage, ContainerKind::Save);

    list.clearKind(ContainerKind::Local);

    EXPECT_EQ((std::vector<std::string> {"global_loose", "save_image"}), names(list))
        << "scope decides when a source goes away, never where it is searched";

    list.clearKind(ContainerKind::Save);
    EXPECT_EQ((std::vector<std::string> {"global_loose"}), names(list));

    list.clear();
    EXPECT_TRUE(list.empty());
    EXPECT_EQ(0u, list.size());
}

// Both backends must resolve an identical mount sequence identically.

class ResourceSourceBucketsTest : public testing::TestWithParam<Backend> {
protected:
    void SetUp() override {
        if (GetParam() == Backend::Legacy) {
            _resources = std::make_unique<Resources>();
        } else {
            _resources = std::make_unique<ExtractResources>();
        }
    }

    void mount(const std::string &data,
               std::optional<ResourceSourceBucket> bucket = std::nullopt,
               ContainerKind kind = ContainerKind::Global) {
        _resources->addMemERF(erfBytes("shared", data), kind, bucket);
    }

    std::string shared() {
        return dataOf(_resources->find(ResourceId("shared", ResType::Txt)));
    }

    std::unique_ptr<IResources> _resources;
};

TEST_P(ResourceSourceBucketsTest, should_resolve_by_insertion_order_when_no_source_is_bucketed) {
    mount("first");
    mount("second");

    EXPECT_EQ("second", shared()) << "an unbucketed list must behave exactly as it always has";
}

TEST_P(ResourceSourceBucketsTest, should_resolve_bucketed_sources_in_raw_lookup_order) {
    mount("loose", ResourceSourceBucket::LooseDirectory);
    mount("class2", ResourceSourceBucket::EncapsulatedClass2);
    mount("keybif", ResourceSourceBucket::KeyBif);

    EXPECT_EQ("loose", shared()) << "bucket order must beat add order";
}

TEST_P(ResourceSourceBucketsTest, should_prefer_the_last_added_source_within_one_bucket) {
    mount("older", ResourceSourceBucket::ResourceImage);
    mount("newer", ResourceSourceBucket::ResourceImage);

    EXPECT_EQ("newer", shared());
}

TEST_P(ResourceSourceBucketsTest, should_reject_mounting_across_the_mode_it_is_already_in) {
    mount("loose", ResourceSourceBucket::LooseDirectory);

    EXPECT_THROW(mount("unplaced"), ValidationException)
        << "an unbucketed source has no rank in the raw lookup order to be placed at";
    EXPECT_EQ("loose", shared()) << "a rejected mount must leave the backend as it was";
}

TEST_P(ResourceSourceBucketsTest, should_reject_a_bucketed_mount_once_ordering_by_insertion) {
    mount("unplaced");

    EXPECT_THROW(mount("loose", ResourceSourceBucket::LooseDirectory), ValidationException);
    EXPECT_EQ("unplaced", shared());
}

TEST_P(ResourceSourceBucketsTest, should_place_rim_sources_in_the_requested_bucket) {
    // RIM mounts take routes of their own on the way to the source list: the
    // extract backend resolves a RIM path through its ERF mount, and both
    // backends build a memory RIM through a shared archive helper. A bucket
    // dropped on either route would leave the source unplaced, and unplaced
    // beats every bucket, so the last one added would win instead.
    TmpDir dir("reone_test_source_buckets_rim");
    writeRim(dir.path / "disk.rim", {{"shared", ResType::Txt, "disk rim"}});

    mount("loose", ResourceSourceBucket::LooseDirectory);
    _resources->addMemRIM(rimBytes("shared", "memory rim"),
                          ContainerKind::Global,
                          ResourceSourceBucket::KeyBif);
    _resources->addRIM(dir.path / "disk.rim",
                       ContainerKind::Global,
                       ResourceSourceBucket::KeyBif);

    EXPECT_EQ("loose", shared());
}

TEST_P(ResourceSourceBucketsTest, should_leave_bucket_order_intact_when_clearing_a_scope) {
    mount("global keybif", ResourceSourceBucket::KeyBif, ContainerKind::Global);
    mount("save image", ResourceSourceBucket::ResourceImage, ContainerKind::Save);
    mount("local loose", ResourceSourceBucket::LooseDirectory, ContainerKind::Local);

    EXPECT_EQ("local loose", shared());

    _resources->clearLocal();
    EXPECT_EQ("save image", shared());

    _resources->clearSave();
    EXPECT_EQ("global keybif", shared());

    _resources->clear();
    EXPECT_EQ("<not found>", shared());
}

INSTANTIATE_TEST_SUITE_P(Backends,
                         ResourceSourceBucketsTest,
                         testing::Values(Backend::Legacy, Backend::Extract),
                         backendName);

// The replacement overlay wraps whichever backend is in use. It has to carry a
// bucket through to that backend, and it is not itself a bucket.

class ReplacementOverlayBucketsTest : public testing::TestWithParam<Backend> {
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

    ResourceId id() const {
        return ResourceId("shared", ResType::Txt);
    }

    void mount(const std::string &data, std::optional<ResourceSourceBucket> bucket) {
        _resources->addMemERF(erfBytes("shared", data), ContainerKind::Global, bucket);
    }

    std::string shared() {
        return dataOf(_resources->find(id()));
    }

    ResourceReplacements _replacements;
    std::unique_ptr<ReplacementResources> _resources;
};

TEST_P(ReplacementOverlayBucketsTest, should_carry_the_bucket_through_to_the_backend) {
    mount("loose", ResourceSourceBucket::LooseDirectory);
    mount("keybif", ResourceSourceBucket::KeyBif);

    EXPECT_EQ("loose", shared())
        << "a bucket dropped on the way to the backend would leave both sources unplaced, "
           "and the one added last would win instead";
}

TEST_P(ReplacementOverlayBucketsTest, should_keep_the_overlay_above_every_bucket) {
    mount("loose", ResourceSourceBucket::LooseDirectory);
    _replacements.replaceResource(id(), bytes("replacement"));

    EXPECT_EQ("replacement", shared())
        << "the overlay is not a bucket of its own: it outranks even the first one";

    _replacements.removeResourceReplacement(id());

    EXPECT_EQ("loose", shared()) << "dropping the replacement must expose the bucketed source again";
}

INSTANTIATE_TEST_SUITE_P(Backends,
                         ReplacementOverlayBucketsTest,
                         testing::Values(Backend::Legacy, Backend::Extract),
                         backendName);
