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

#include "reone/resource/format/erfwriter.h"
#include "reone/resource/format/rimwriter.h"
#include "reone/system/stream/fileoutput.h"

namespace reone {

namespace test {

struct ArchiveRes {
    std::string resRef;
    resource::ResType type;
    std::string data;
};

inline ByteBuffer toBytes(const std::string &s) {
    return ByteBuffer(s.begin(), s.end());
}

inline void writeErf(const std::filesystem::path &path,
                     resource::ErfWriter::FileType fileType,
                     const std::vector<ArchiveRes> &resources) {
    resource::ErfWriter writer;
    for (const auto &res : resources) {
        writer.add(resource::ErfWriter::Resource {res.resRef, res.type, toBytes(res.data)});
    }
    writer.save(fileType, path);
}

inline void writeRim(const std::filesystem::path &path,
                     const std::vector<ArchiveRes> &resources) {
    resource::RimWriter writer;
    for (const auto &res : resources) {
        writer.add(resource::RimWriter::Resource {res.resRef, res.type, toBytes(res.data)});
    }
    writer.save(path);
}

namespace detail {

inline void appendUint16(std::string &out, uint16_t val) {
    out.push_back(static_cast<char>(val & 0xff));
    out.push_back(static_cast<char>((val >> 8) & 0xff));
}

inline void appendUint32(std::string &out, uint32_t val) {
    out.push_back(static_cast<char>(val & 0xff));
    out.push_back(static_cast<char>((val >> 8) & 0xff));
    out.push_back(static_cast<char>((val >> 16) & 0xff));
    out.push_back(static_cast<char>((val >> 24) & 0xff));
}

inline void writeFile(const std::filesystem::path &path, const std::string &data) {
    FileOutputStream stream(path);
    stream.write(data.data(), data.size());
}

} // namespace detail

/**
 * Write a minimal KEY file at dir/chitin.key plus a single BIF holding the
 * given resources. bifRelPath is stored in the KEY verbatim, e.g.
 * "data\\sample.bif"; the BIF file is created at the corresponding location
 * under dir.
 */
inline void writeKeyBif(const std::filesystem::path &dir,
                        const std::string &bifRelPath,
                        const std::vector<ArchiveRes> &resources) {
    auto count = static_cast<uint32_t>(resources.size());

    // BIF: header, variable resource table, payloads
    std::string bif("BIFFV1  ");
    detail::appendUint32(bif, count);
    detail::appendUint32(bif, 0);
    detail::appendUint32(bif, 20);
    uint32_t dataOffset = 20 + 16 * count;
    for (uint32_t i = 0; i < count; ++i) {
        detail::appendUint32(bif, i);
        detail::appendUint32(bif, dataOffset);
        detail::appendUint32(bif, static_cast<uint32_t>(resources[i].data.size()));
        detail::appendUint32(bif, static_cast<uint32_t>(resources[i].type));
        dataOffset += static_cast<uint32_t>(resources[i].data.size());
    }
    for (const auto &res : resources) {
        bif += res.data;
    }
    auto bifPath = dir / boost::replace_all_copy(bifRelPath, "\\", "/");
    std::filesystem::create_directories(bifPath.parent_path());
    detail::writeFile(bifPath, bif);

    // KEY: header, file table, filenames, key table
    auto filenameLen = static_cast<uint32_t>(bifRelPath.size());
    uint32_t offFiles = 64;
    uint32_t offFilename = offFiles + 12;
    uint32_t offKeys = offFilename + filenameLen + 1;

    std::string key("KEY V1  ");
    detail::appendUint32(key, 1);
    detail::appendUint32(key, count);
    detail::appendUint32(key, offFiles);
    detail::appendUint32(key, offKeys);
    detail::appendUint32(key, 0);
    detail::appendUint32(key, 0);
    key.append(32, '\0');

    detail::appendUint32(key, static_cast<uint32_t>(bif.size()));
    detail::appendUint32(key, offFilename);
    detail::appendUint16(key, static_cast<uint16_t>(filenameLen));
    detail::appendUint16(key, 0);

    key += bifRelPath;
    key.push_back('\0');

    for (uint32_t i = 0; i < count; ++i) {
        std::string resRef(resources[i].resRef);
        resRef.resize(16, '\0');
        key += resRef;
        detail::appendUint16(key, static_cast<uint16_t>(resources[i].type));
        detail::appendUint32(key, i); // bifIdx 0, resIdx i
    }
    detail::writeFile(dir / "chitin.key", key);
}

/// Temporary directory tree, removed on destruction.
struct TmpDir {
    std::filesystem::path path;

    explicit TmpDir(const std::string &name) {
        path = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }

    ~TmpDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

} // namespace test

} // namespace reone
