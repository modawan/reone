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

#include "reone/extract/finder.h"

namespace reone {

namespace extract {

namespace {

SearchScope makeOrder(std::initializer_list<SearchLocation> items) {
    return SearchScope(items);
}

} // namespace

const SearchScope &canonicalSearchOrder() {
    static const SearchScope kOrder = makeOrder({
        SearchLocation::CustomFolders,
        SearchLocation::Override,
        SearchLocation::Root,
        SearchLocation::CustomModules,
        SearchLocation::Modules,
        SearchLocation::Chitin,
        SearchLocation::Executable,
    });
    return kOrder;
}

const SearchScope &textureSearchOrder() {
    static const SearchScope kOrder = makeOrder({
        SearchLocation::CustomFolders,
        SearchLocation::Override,
        SearchLocation::CustomModules,
        SearchLocation::TexturesTpa,
        SearchLocation::TexturesTpb,
        SearchLocation::TexturesTpc,
        SearchLocation::TexturesGui,
        SearchLocation::Chitin,
        SearchLocation::Executable,
    });
    return kOrder;
}

const SearchScope &soundSearchOrder() {
    static const SearchScope kOrder = makeOrder({
        SearchLocation::CustomFolders,
        SearchLocation::Override,
        SearchLocation::CustomModules,
        SearchLocation::Sound,
        SearchLocation::Chitin,
        SearchLocation::Executable,
    });
    return kOrder;
}

const SearchScope &talkTableSearchOrder() {
    static const SearchScope kOrder = makeOrder({
        SearchLocation::Root,
    });
    return kOrder;
}

const SearchScope &movieSearchOrder() {
    static const SearchScope kOrder = makeOrder({
        SearchLocation::CustomFolders,
        SearchLocation::Override,
        SearchLocation::Movies,
        SearchLocation::Chitin,
    });
    return kOrder;
}

const char *searchLocationName(SearchLocation location) {
    switch (location) {
    case SearchLocation::Override:
        return "Override";
    case SearchLocation::Modules:
        return "Modules";
    case SearchLocation::Chitin:
        return "Chitin";
    case SearchLocation::TexturesTpa:
        return "Texture pack TPA";
    case SearchLocation::TexturesTpb:
        return "Texture pack TPB";
    case SearchLocation::TexturesTpc:
        return "Texture pack TPC";
    case SearchLocation::TexturesGui:
        return "Texture pack GUI";
    case SearchLocation::Music:
        return "StreamMusic";
    case SearchLocation::Sound:
        return "StreamSounds";
    case SearchLocation::Voice:
        return "StreamWaves/StreamVoice";
    case SearchLocation::Lips:
        return "Lips";
    case SearchLocation::Rims:
        return "RIM files";
    case SearchLocation::CustomModules:
        return "Custom modules";
    case SearchLocation::CustomFolders:
        return "Custom folders";
    case SearchLocation::Root:
        return "Game root";
    case SearchLocation::Movies:
        return "Movies";
    case SearchLocation::Executable:
        return "Executable";
    default:
        return "Unknown";
    }
}

} // namespace extract

} // namespace reone
