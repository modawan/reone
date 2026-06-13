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

#include "reone/game/gui/saveload.h"

#include "reone/game/game.h"
#include "reone/graphics/format/tgareader.h"
#include "reone/resource/format/erfreader.h"
#include "reone/resource/format/gffreader.h"
#include "reone/resource/parser/gff/nfo.h"
#include "reone/resource/strings.h"
#include "reone/system/fileutil.h"
#include "reone/system/logutil.h"
#include "reone/system/stream/gameinput.h"
#include "reone/system/stream/input.h"
#include "reone/system/stream/memoryinput.h"

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

static constexpr char kSavesDirectoryName[] = "saves";

static constexpr int kStrRefLoadGame = 1585;
static constexpr int kStrRefSave = 1587;
static constexpr int kStrRefSaveGame = 1588;
static constexpr int kStrRefLoad = 1589;

void SaveLoad::onGUILoaded() {
    if (!_game.isTSL()) {
        loadBackground(BackgroundType::Menu);
    }

    bindControls();

    _controls.LBL_PLANETNAME->setVisible(false);
    _controls.LBL_AREANAME->setVisible(false);

    _controls.LB_GAMES->setSelectionMode(ListBox::SelectionMode::OnClick);
    _controls.LB_GAMES->setPadding(3);
    _controls.LB_GAMES->protoItem().setUseBorderColorOverride(true);
    _controls.LB_GAMES->protoItem().setBorderColorOverride(_baseColor);
    _controls.LB_GAMES->protoItem().setHilightColor(_hilightColor);
    _controls.LB_GAMES->setOnItemClick([this](const std::string &item) {
        std::shared_ptr<Texture> screenshot;
        for (auto &save : _saves) {
            if (save.name == item) {
                screenshot = save.save.screen;
                break;
            }
        }
        _controls.LBL_SCREENSHOT->setBorderFill(std::move(screenshot));
    });

    _controls.BTN_SAVELOAD->setOnClick([this]() {
        int number = getSelectedSaveNumber();
        switch (_mode) {
        case SaveLoadMode::Save:
            if (number == -1) {
                number = getNewSaveNumber();
            }
            saveGame(number);
            refresh();
            break;
        default:
            if (number != -1) {
                loadGame(number);
            }
            break;
        }
    });
    _controls.BTN_DELETE->setOnClick([this]() {
        int number = getSelectedSaveNumber();
        if (number != -1) {
            deleteGame(number);
            refresh();
        }
    });
    _controls.BTN_BACK->setOnClick([this]() {
        _controls.LB_GAMES->clearSelection();
        switch (_mode) {
        case SaveLoadMode::Save:
        case SaveLoadMode::LoadFromInGame:
            _game.openInGame();
            break;
        default:
            _game.openMainMenu();
            break;
        }
    });
}

void SaveLoad::refresh() {
    _controls.BTN_DELETE->setDisabled(_mode != SaveLoadMode::Save);

    std::string panelName(_services.resource.strings.getText(_mode == SaveLoadMode::Save ? kStrRefSaveGame : kStrRefLoadGame));
    _controls.LBL_PANELNAME->setTextMessage(std::move(panelName));

    std::string actionName(_services.resource.strings.getText(_mode == SaveLoadMode::Save ? kStrRefSave : kStrRefLoad));
    _controls.BTN_SAVELOAD->setTextMessage(std::move(actionName));

    refreshSavedGames();
}

static std::optional<ByteBuffer> readErfResource(IInputStream &erf, const ErfReader &reader, const ResourceId &id) {
    const auto &keys = reader.keys();
    const auto &resources = reader.resources();
    if (keys.size() != resources.size()) {
        return std::nullopt;
    }
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].resId != id) {
            continue;
        }
        const auto &entry = resources[i];
        if (entry.size == 0) {
            return ByteBuffer();
        }
        ByteBuffer buf;
        buf.resize(entry.size);
        erf.seek(entry.offset, SeekOrigin::Begin);
        erf.read(&buf[0], buf.size());
        return buf;
    }
    return std::nullopt;
}

static std::shared_ptr<Gff> readLooseGff(const std::filesystem::path &dir, std::string_view resRef) {
    auto path = findFileIgnoreCase(dir, std::string(resRef) + ".res");
    if (!path) {
        return nullptr;
    }
    auto stream = openGameInputStream(*path);
    GffReader reader(*stream);
    reader.load();
    return reader.root();
}

static SavedGame peekSaveSlot(const std::filesystem::path &slotPath) {
    std::shared_ptr<Gff> nfoGff = readLooseGff(slotPath, "savenfo");
    if (!nfoGff) {
        auto savegamePath = findFileIgnoreCase(slotPath, "savegame.sav");
        if (!savegamePath) {
            throw std::runtime_error("No save metadata in " + slotPath.string());
        }
        auto erfStream = openGameInputStream(*savegamePath);
        ErfReader erfReader(*erfStream);
        erfReader.load();
        auto nfoData = readErfResource(*erfStream, erfReader, ResourceId("savenfo", ResType::Res));
        if (!nfoData) {
            throw std::runtime_error("savenfo missing in " + slotPath.string());
        }
        MemoryInputStream nfoStream(*nfoData);
        GffReader nfoReader(nfoStream);
        nfoReader.load();
        nfoGff = nfoReader.root();
    }

    NFO nfo = parseNFO(*nfoGff);

    std::shared_ptr<Texture> screen;
    if (auto screenPath = findFileIgnoreCase(slotPath, "screen.tga")) {
        auto tgaStream = openGameInputStream(*screenPath);
        TgaReader tgaReader(*tgaStream, "screen", TextureUsage::GUI);
        tgaReader.load();
        screen = tgaReader.texture();
    } else if (auto savegamePath = findFileIgnoreCase(slotPath, "savegame.sav")) {
        auto erfStream = openGameInputStream(*savegamePath);
        ErfReader erfReader(*erfStream);
        erfReader.load();
        if (auto screenData = readErfResource(*erfStream, erfReader, ResourceId("screen", ResType::Tga))) {
            MemoryInputStream tga(*screenData);
            TgaReader tgaReader(tga, "screen", TextureUsage::GUI);
            tgaReader.load();
            screen = tgaReader.texture();
        }
    }

    SavedGame result;
    result.screen = std::move(screen);
    result.lastModule = nfo.lastModule;
    return result;
}

void SaveLoad::refreshSavedGames() {
    _saves.clear();

    auto savesPath = findFileIgnoreCase(_game.path(), kSavesDirectoryName);
    if (!savesPath) {
        savesPath = _game.path() / kSavesDirectoryName;
    }
    if (!std::filesystem::exists(*savesPath)) {
        std::filesystem::create_directories(*savesPath);
    }

    for (auto &entry : std::filesystem::directory_iterator(*savesPath)) {
        if (entry.is_directory()) {
            indexSaveSlot(entry.path());
        }
    }

    std::sort(_saves.begin(), _saves.end(), [](auto &a, auto &b) { return a.number < b.number; });

    _controls.LB_GAMES->clearItems();
    for (auto &save : _saves) {
        ListBox::Item item;
        item.tag = save.name;
        item.text = save.name;
        _controls.LB_GAMES->addItem(std::move(item));
    }
}

void SaveLoad::indexSaveSlot(std::filesystem::path slotPath) {
    try {
        if (!findFileIgnoreCase(slotPath, "savegame.sav")) {
            return;
        }

        SavedGameDescriptor descriptor;
        descriptor.name = boost::to_lower_copy(slotPath.filename().string());
        descriptor.number = parseSlotNumber(descriptor.name);
        descriptor.save = peekSaveSlot(slotPath);
        descriptor.path = std::move(slotPath);
        _saves.push_back(std::move(descriptor));
    } catch (const std::exception &e) {
        warn("Error indexing a saved game: " + std::string(e.what()));
    }
}

int SaveLoad::parseSlotNumber(const std::string &name) {
    try {
        return stoi(name);
    } catch (const std::exception &) {
        return 0;
    }
}

void SaveLoad::setMode(SaveLoadMode mode) {
    _mode = mode;
}

int SaveLoad::getSelectedSaveNumber() const {
    int hilightedIdx = _controls.LB_GAMES->selectedItemIndex();
    if (hilightedIdx == -1) {
        return -1;
    }

    std::string tag(_controls.LB_GAMES->getItemAt(hilightedIdx).tag);
    for (auto &save : _saves) {
        if (save.name == tag) {
            return save.number;
        }
    }
    return parseSlotNumber(tag);
}

int SaveLoad::getNewSaveNumber() const {
    int number = 0;
    for (auto &save : _saves) {
        number = std::max(number, save.number);
    }
    return number + 1;
}

void SaveLoad::saveGame(int number) {
    warn("Save game is not implemented yet");
    (void)number;
}

void SaveLoad::loadGame(int number) {
    for (auto &save : _saves) {
        if (save.number == number) {
            _game.loadGame(save.name);
            return;
        }
    }
    warn("SaveLoad: no save slot for number " + std::to_string(number));
}

void SaveLoad::deleteGame(int number) {
    for (auto &save : _saves) {
        if (save.number == number) {
            std::filesystem::remove_all(save.path);
            return;
        }
    }
}

} // namespace game

} // namespace reone
