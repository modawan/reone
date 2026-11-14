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

#include <gtest/gtest.h>

#include "reone/graphics/format/mdlmdxreader.h"
#include "reone/graphics/model.h"
#include "reone/system/binarywriter.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/stream/memoryoutput.h"
#include "reone/system/stringbuilder.h"

#include "../../fixtures/graphics.h"

using namespace reone;
using namespace reone::graphics;

TEST(MdlReader, should_load_mdl) {
    // given
    auto mdlBytes = StringBuilder()
                        // File Header (0)
                        .append("\x00\x00\x00\x00", 4) // unknown
                        .append("\x2e\x01\x00\x00", 4) // MDL size
                        .append("\x00\x00\x00\x00", 4) // MDX size
                        // Geometry Header (12)
                        .append("\x00\x00\x00\x00", 4)                                                                                    // model function pointer 1
                        .append("\x00\x00\x00\x00", 4)                                                                                    // model function pointer 2
                        .append("some_model\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 32) // model name
                        .append("\xd2\x00\x00\x00", 4)                                                                                    // offset to root node
                        .append("\x01\x00\x00\x00", 4)                                                                                    // number of nodes
                        .append('\x00', 6 * 4)                                                                                            // unknown
                        .append("\x00\x00\x00\x00", 4)                                                                                    // reference counter
                        .append("\x00\x00\x00\x00", 4)                                                                                    // model type
                        // Model Header (92)
                        .append("\x00", 1)                                                                                                                  // classification
                        .append("\x00", 1)                                                                                                                  // subclassification
                        .append("\x00", 1)                                                                                                                  // unknown
                        .append("\x00", 1)                                                                                                                  // affected by fog
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // number of child models
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // offset to animations
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // number of animations
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // number of animations
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // supermodel reference
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // bounding box (X min)
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // bounding box (Y min)
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // bounding box (Z min)
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // bounding box (X max)
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // bounding box (Y max)
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // bounding box (Z max)
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // radius
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // animation scale
                        .append("NULL\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 32) // supermodel name
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // offset to animation root node
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // unknown
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // MDX size
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // offset to MDX
                        .append("\xc4\x00\x00\x00", 4)                                                                                                      // offset to names
                        .append("\x01\x00\x00\x00", 4)                                                                                                      // number of names
                        .append("\x01\x00\x00\x00", 4)                                                                                                      // number of names
                        // Name Offsets (208)
                        .append("\xc8\x00\x00\x00", 4)
                        // Names (212)
                        .append("root_node\x00", 10)
                        // Root Node (222)
                        .append("\x00\x00", 2)         // flags
                        .append("\x00\x00", 2)         // node number
                        .append("\x00\x00", 2)         // name index
                        .append("\x00\x00", 2)         // padding
                        .append("\x00\x00\x00\x00", 4) // offset to root node
                        .append("\x00\x00\x00\x00", 4) // offset to parent node
                        .append("\x00\x00\x00\x00", 4) // position (X)
                        .append("\x00\x00\x00\x00", 4) // position (Y)
                        .append("\x00\x00\x00\x00", 4) // position (Z)
                        .append("\x00\x00\x00\x00", 4) // orientation (W)
                        .append("\x00\x00\x00\x00", 4) // orientation (X)
                        .append("\x00\x00\x00\x00", 4) // orientation (Y)
                        .append("\x00\x00\x00\x00", 4) // orientation (Z)
                        .append("\x00\x00\x00\x00", 4) // offset to children
                        .append("\x00\x00\x00\x00", 4) // number of children
                        .append("\x00\x00\x00\x00", 4) // number of children
                        .append("\x00\x00\x00\x00", 4) // offset to controller keys
                        .append("\x00\x00\x00\x00", 4) // number of controller keys
                        .append("\x00\x00\x00\x00", 4) // number of controller keys
                        .append("\x00\x00\x00\x00", 4) // offset to controller values
                        .append("\x00\x00\x00\x00", 4) // number of controller values
                        .append("\x00\x00\x00\x00", 4) // number of controller values
                        .string();
    auto mdl = MemoryInputStream(mdlBytes);
    auto mdxBytes = StringBuilder().string();
    auto mdx = MemoryInputStream(mdxBytes);
    Statistic statistic;
    auto reader = MdlMdxReader(mdl, mdx, statistic);

    // when
    reader.load();

    // then
    auto model = reader.model();
    EXPECT_TRUE(static_cast<bool>(model));
    EXPECT_EQ(std::string("some_model"), model->name());
    auto rootNode = model->rootNode();
    EXPECT_EQ(std::string("root_node"), rootNode->name());
}

static void writeArrayDefinition(BinaryWriter &writer, uint32_t offset, uint32_t count) {
    writer.writeUint32(offset);
    writer.writeUint32(count);
    writer.writeUint32(count);
}

static void writeNodeHeader(BinaryWriter &writer, uint16_t flags, uint16_t number, uint32_t offChildren, uint32_t numChildren) {
    writer.writeUint16(flags);
    writer.writeUint16(number);
    writer.writeUint16(number);         // name index
    writer.write(2 + 4 + 4 + 3 * 4, 0); // padding, root and parent offsets, position
    writer.writeFloat(1.0f);            // orientation (W)
    writer.write(3 * 4, 0);             // orientation (X, Y, Z)
    writeArrayDefinition(writer, offChildren, numChildren);
    writeArrayDefinition(writer, 0, 0); // controller keys
    writeArrayDefinition(writer, 0, 0); // controller values
}

TEST(MdlReader, should_map_only_skin_bone_map_entries_that_are_bones) {
    // given
    // Offsets in the MDL are relative to the end of the 12-byte file header.
    ByteBuffer mdlBytes;
    MemoryOutputStream mdlStream(mdlBytes);
    BinaryWriter writer(mdlStream);
    // File Header
    writer.write(12, 0);
    // Geometry Header (0)
    writer.write(2 * 4, 0); // model function pointers
    writer.writeString(std::string("skinned_model").append(32 - 13, '\0'));
    writer.writeUint32(214);        // offset to root node
    writer.writeUint32(2);          // number of nodes
    writer.write(6 * 4 + 4 + 4, 0); // unknown, reference counter, model type
    // Model Header (80)
    writer.write(4 + 4, 0);             // classification, fog, number of child models
    writeArrayDefinition(writer, 0, 0); // animations
    writer.write(4 + 6 * 4 + 4 + 4, 0); // supermodel reference, bounding box, radius, animation scale
    writer.writeString(std::string("NULL").append(32 - 4, '\0'));
    writer.write(4 * 4, 0);               // animation root, unknown, MDX size, MDX offset
    writeArrayDefinition(writer, 196, 2); // names
    // Name Offsets (196)
    writer.writeUint32(204);
    writer.writeUint32(209);
    // Names (204)
    writer.writeCString("root");
    writer.writeCString("skin");
    // Root Node (214)
    writeNodeHeader(writer, 0x1, 0, 294, 1);
    // Root Node Children (294)
    writer.writeUint32(298);
    // Skin Node (298)
    writeNodeHeader(writer, 0x61, 1, 0, 0);
    // Mesh Header (378)
    writer.write(2 * 4, 0);                     // mesh function pointers
    writeArrayDefinition(writer, 0, 0);         // faces
    writer.write(6 * 4 + 4 + 3 * 3 * 4 + 4, 0); // bounding box, radius, average, diffuse, ambient, transparency
    writer.write(32 + 32 + 12 + 12, 0);         // textures
    writeArrayDefinition(writer, 0, 0);         // indices count
    writeArrayDefinition(writer, 0, 0);         // indices offset
    writeArrayDefinition(writer, 0, 0);         // inverted counter
    writer.write(3 * 4 + 8 + 5 * 4, 0);         // unknown, UV animation
    writer.writeUint32(12);                     // MDX vertex size
    writer.writeUint32(0);                      // MDX data flags
    writer.writeInt32(0);                       // MDX offset to vertices
    for (int i = 0; i < 7; ++i) {
        writer.writeInt32(-1); // MDX offsets to normals, colors, texture coordinates and tangent space
    }
    writer.write(3 * 4 + 2 + 2 + 6 + 2 + 4 + 4 + 4 + 4, 0); // unknown, vertex and texture counts, flags, area, MDX and vertex offsets
    // Skin Header (710)
    writer.write(3 * 4 + 4 + 4, 0);       // unknown, MDX offsets to bone weights and indices
    writer.writeUint32(810);              // offset to bone map
    writer.writeUint32(2);                // number of bones
    writeArrayDefinition(writer, 818, 2); // bone orientations
    writeArrayDefinition(writer, 850, 2); // bone positions
    writer.write(3 * 4 + 16 * 2 + 4, 0);  // unknown, bone node serials, padding
    // Bone Map (810): the root is not a bone, the skin node is bone 0.
    writer.writeFloat(-1.0f);
    writer.writeFloat(0.0f);
    // Bone Orientations (818)
    for (int i = 0; i < 2; ++i) {
        writer.writeFloat(1.0f);
        writer.write(3 * 4, 0);
    }
    // Bone Positions (850)
    writer.write(2 * 3 * 4, 0);

    auto mdl = MemoryInputStream(mdlBytes);
    auto mdxBytes = ByteBuffer();
    auto mdx = MemoryInputStream(mdxBytes);
    Statistic statistic;
    auto reader = MdlMdxReader(mdl, mdx, statistic);

    // when
    reader.load();

    // then
    auto model = reader.model();
    auto skinNode = model->getNodeByName("skin");
    ASSERT_TRUE(static_cast<bool>(skinNode));
    const auto &skin = *skinNode->mesh()->skin;
    EXPECT_EQ((std::vector<uint32_t> {1}), skin.boneSerial);
    EXPECT_EQ((std::vector<uint16_t> {1}), skin.boneNodeNumber);
}
