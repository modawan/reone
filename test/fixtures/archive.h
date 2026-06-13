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

#include "reone/resource/format/erfwriter.h"
#include "reone/resource/format/rimwriter.h"
#include "reone/system/stream/fileoutput.h"
#include "reone/system/stream/memoryoutput.h"

namespace reone {

namespace test {

inline void writeErf(const std::filesystem::path &path,
                     const std::string &resRef,
                     resource::ResType type,
                     ByteBuffer data) {
    ByteBuffer bytes;
    MemoryOutputStream stream(bytes);
    resource::ErfWriter writer;
    writer.add(resource::ErfWriter::Resource {resRef, type, std::move(data)});
    writer.save(resource::ErfWriter::FileType::ERF, stream);
    FileOutputStream out(path);
    out.write(bytes.data(), bytes.size());
    out.close();
}

inline void writeRim(const std::filesystem::path &path,
                     const std::string &resRef,
                     resource::ResType type,
                     ByteBuffer data) {
    ByteBuffer bytes;
    MemoryOutputStream stream(bytes);
    resource::RimWriter writer;
    writer.add(resource::RimWriter::Resource {resRef, type, std::move(data)});
    writer.save(stream);
    FileOutputStream out(path);
    out.write(bytes.data(), bytes.size());
    out.close();
}

} // namespace test

} // namespace reone
