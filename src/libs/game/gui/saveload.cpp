/* Copyright (c) 2020-2026 The reone project contributors */

#include "reone/game/gui/saveload.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "reone/game/game.h"
#include "reone/graphics/font.h"
#include "reone/graphics/format/tgareader.h"
#include "reone/graphics/texture.h"
#include "reone/gui/guis.h"
#include "reone/resource/strings.h"
#include "reone/resource/provider/textures.h"
#include "reone/system/logutil.h"
#include "reone/system/stream/memoryinput.h"

using namespace reone::graphics;
using namespace reone::gui;

namespace reone {
namespace game {
namespace {

constexpr int kStrRefLoadGame = 1585;
constexpr int kStrRefSave = 1587;
constexpr int kStrRefSaveGame = 1588;
constexpr int kStrRefLoad = 1589;
constexpr int kStrRefConfirmOverwrite = 1591;
constexpr int kStrRefConfirmDelete = 1592;

std::string playedTime(uint32_t seconds) {
    std::ostringstream out;
    out << std::setfill('0') << std::setw(2) << seconds / 3600 << ':'
        << std::setw(2) << (seconds / 60) % 60 << ':'
        << std::setw(2) << seconds % 60;
    return out.str();
}

std::string elideToWidth(
    std::string text,
    const Font &font,
    float maxWidth) {
    if (font.measure(text) <= maxWidth) return text;
    static const std::string suffix = "...";
    while (!text.empty() && font.measure(text + suffix) > maxWidth) {
        text.pop_back();
    }
    return text + suffix;
}

std::string saveListText(
    const SavedGame &save,
    const Font &font,
    float maxWidth) {
    auto hours = save.metadata.timePlayed / 3600;
    auto minutes = (save.metadata.timePlayed / 60) % 60;
    auto name = save.metadata.savegameName.empty()
                    ? save.descriptor.directory.filename().string()
                    : save.metadata.savegameName;
    return saveGameNumberLabel(save) + " - " +
           std::to_string(hours) + "H " + std::to_string(minutes) +
           "M\n" + elideToWidth(std::move(name), font, maxWidth);
}

std::string terminalMessage(const SaveResult &result) {
    if (result.durable) {
        return result.cleanupPending
                   ? "Game saved. Cleanup of the previous slot is pending."
                   : "Game saved successfully.";
    }
    switch (result.status) {
    case SaveStatus::Busy: return "Another save is already in progress.";
    case SaveStatus::NotAllowed: return "The game cannot be saved at this time.";
    case SaveStatus::SnapshotFailure: return "The current game state could not be captured.";
    case SaveStatus::SerializationFailure: return "The save data could not be prepared.";
    case SaveStatus::PublicationFailure: return "The save slot could not be written.";
    case SaveStatus::InternalExecutionFailure: return "The save could not be completed.";
    case SaveStatus::Cancelled: return "The save was cancelled.";
    default: return "The save could not be completed.";
    }
}

} // namespace

namespace detail {

std::vector<SavedGame> prepareSaveBrowserEntries(
    std::vector<SavedGame> saves,
    bool tsl,
    SaveLoadMode mode) {

    if (mode == SaveLoadMode::Save) {
        saves.erase(
            std::remove_if(saves.begin(), saves.end(),
                           [](const auto &save) { return save.slot < 2; }),
            saves.end());
    }
    std::sort(saves.begin(), saves.end(), [tsl](const auto &a, const auto &b) {
        if (!tsl) return a.slot < b.slot;
        auto aTimestamp = a.metadata.timestamp.value_or(0);
        auto bTimestamp = b.metadata.timestamp.value_or(0);
        if (aTimestamp != bTimestamp) return aTimestamp > bTimestamp;
        return a.slot > b.slot;
    });
    return saves;
}

SaveBrowserActivation evaluateSaveBrowserActivation(
    SaveLoadMode mode,
    bool pending,
    bool hasSelectedSlot,
    bool selectedSlotExists) {

    if (pending) return SaveBrowserActivation::None;
    if (mode == SaveLoadMode::Save) {
        if (!hasSelectedSlot) return SaveBrowserActivation::SaveNew;
        return selectedSlotExists ? SaveBrowserActivation::SaveExisting
                                  : SaveBrowserActivation::None;
    }
    return hasSelectedSlot && selectedSlotExists
               ? SaveBrowserActivation::LoadExisting
               : SaveBrowserActivation::None;
}

} // namespace detail

void SaveLoad::onGUILoaded() {
    if (!_game.isTSL()) loadBackground(BackgroundType::Menu);
    bindControls();
    _controls.LBL_PLANETNAME->setVisible(false);
    _controls.LBL_AREANAME->setVisible(false);
    _controls.LB_GAMES->setSelectionMode(ListBox::SelectionMode::OnClick);
    _controls.LB_GAMES->setPadding(3);
    _controls.LB_GAMES->protoItem().setUseBorderColorOverride(true);
    _controls.LB_GAMES->protoItem().setBorderColorOverride(_baseColor);
    _controls.LB_GAMES->protoItem().setHilightColor(_hilightColor);
    _controls.LB_GAMES->setOnItemClick([this](const std::string &tag) {
        _selectedSaveSlot.reset();
        if (tag != "new") {
            try {
                _selectedSaveSlot = static_cast<uint32_t>(std::stoul(tag));
            } catch (const std::exception &) {
            }
        }
        refreshSelection();
    });
    _controls.LB_GAMES->setOnItemDoubleClick([this](const std::string &) {
        activateSelection();
    });

    _controls.BTN_SAVELOAD->setOnClick([this]() {
        activateSelection();
    });
    _controls.BTN_DELETE->setOnClick([this]() {
        int selected = getSelectedSaveNumber();
        if (selected >= 0 && !_pendingRequestId) deleteGame(static_cast<uint32_t>(selected));
    });
    _controls.BTN_BACK->setOnClick([this]() {
        if (_saveNameVisible) {
            hideSaveName();
        } else if (_mode == SaveLoadMode::LoadFromMainMenu) {
            dismissTransientState();
            _game.openMainMenu();
        } else {
            dismissTransientState();
            _game.openInGame();
        }
    });

    _saveNameGUI = _services.gui.guis.get(guiResRef("savename"), [this](IGUI &gui) {
        gui.setResolution(_game.isTSL() ? 800 : 640, _game.isTSL() ? 600 : 480);
        gui.setScaling(GUI::ScalingMode::Center);
    });
    if (_saveNameGUI) {
        _saveNameControls.BTN_CANCEL = std::static_pointer_cast<Button>(_saveNameGUI->findControl("BTN_CANCEL"));
        _saveNameControls.BTN_OK = std::static_pointer_cast<Button>(_saveNameGUI->findControl("BTN_OK"));
        _saveNameControls.EDITBOX = std::static_pointer_cast<Label>(_saveNameGUI->findControl("EDITBOX"));
        _saveNameControls.LBL_TITLE = std::static_pointer_cast<Label>(_saveNameGUI->findControl("LBL_TITLE"));
        _saveNameControls.LBL_TITLE->setTextMessage(_services.resource.strings.getText(kStrRefSaveGame));
        _saveNameControls.BTN_CANCEL->setOnClick([this]() { hideSaveName(); });
        _saveNameControls.BTN_OK->setOnClick([this]() { confirmSaveName(); });
    }
    _confirmation = std::make_unique<ConfirmPopup>(_game, _services);
    _confirmation->init();
}

bool SaveLoad::handle(const input::Event &event) {
    if (_confirmation && _confirmation->isVisible()) {
        return _confirmation->handle(event);
    }
    if (_saveNameVisible && _saveNameGUI) {
        if (event.type == input::EventType::KeyDown && _saveNameInput.handle(event)) {
            _saveNameControls.EDITBOX->setTextMessage(std::string(_saveNameBuffer.str()));
            return true;
        }
        return _saveNameGUI->handle(event);
    }
    return GameGUI::handle(event);
}

void SaveLoad::update(float dt) {
    GameGUI::update(dt);
    if (_confirmation && _confirmation->isVisible()) {
        _confirmation->update(dt);
    }
    if (_saveNameVisible && _saveNameGUI) _saveNameGUI->update(dt);
    const auto &result = _game.lastSaveResult();
    if (!consumeTerminalResult(result)) return;
    _controls.BTN_SAVELOAD->setDisabled(false);
    showStatus(terminalMessage(*result));
    if (result->durable) refreshSavedGames();
}

void SaveLoad::render() {
    GameGUI::render();
    if (_confirmation && _confirmation->isVisible()) {
        _confirmation->render();
    }
    if (_saveNameVisible && _saveNameGUI) _saveNameGUI->render();
}

void SaveLoad::refresh() {
    dismissTransientState();
    _controls.LBL_PANELNAME->setTextMessage(_services.resource.strings.getText(
        _mode == SaveLoadMode::Save ? kStrRefSaveGame : kStrRefLoadGame));
    _controls.BTN_SAVELOAD->setTextMessage(_services.resource.strings.getText(
        _mode == SaveLoadMode::Save ? kStrRefSave : kStrRefLoad));
    _controls.LBL_AREANAME->setVisible(false);
    refreshSavedGames();
}

void SaveLoad::activateSelection() {
    int selected = getSelectedSaveNumber();
    auto save = selected < 0 ? nullptr : findSave(static_cast<uint32_t>(selected));
    auto activation = detail::evaluateSaveBrowserActivation(
        _mode, _pendingRequestId.has_value(), selected >= 0, save != nullptr);
    switch (activation) {
    case detail::SaveBrowserActivation::SaveNew:
        showSaveName(getNewSaveNumber(), "");
        break;
    case detail::SaveBrowserActivation::SaveExisting:
        showSaveName(static_cast<uint32_t>(selected), save->metadata.savegameName);
        break;
    case detail::SaveBrowserActivation::LoadExisting:
        debug("SaveLoad: dispatching selected load slot " + std::to_string(selected));
        loadGame(static_cast<uint32_t>(selected));
        break;
    case detail::SaveBrowserActivation::None:
        break;
    }
}

void SaveLoad::refreshSavedGames() {
    _saves = detail::prepareSaveBrowserEntries(
        _game.savedGames(), _game.isTSL(), _mode);
    _selectedSaveSlot.reset();
    _controls.LBL_AREANAME->setTextMessage("");
    _controls.LBL_AREANAME->setVisible(false);
    _controls.LBL_PLANETNAME->setTextMessage("");
    _controls.LBL_PLANETNAME->setVisible(false);
    _controls.LB_GAMES->clearItems();
    if (_mode == SaveLoadMode::Save) {
        ListBox::Item item;
        item.tag = "new";
        item.text = "New Save Game";
        _controls.LB_GAMES->addItem(std::move(item));
    }
    for (const auto &save : _saves) {
        ListBox::Item item;
        item.tag = std::to_string(save.slot);
        auto &proto = _controls.LB_GAMES->protoItem();
        float textWidth = static_cast<float>(
            proto.extent().width - 2 * proto.border().dimension);
        item.text = saveListText(
            save, *proto.text().font, std::max(0.0f, textWidth));
        _controls.LB_GAMES->addItem(std::move(item));
    }
    if (_controls.LBL_SCREENSHOT) {
        _controls.LBL_SCREENSHOT->setBorderFill(std::shared_ptr<Texture>());
    }
    if (_controls.LBL_PM1) {
        _controls.LBL_PM1->setBorderFill(std::shared_ptr<Texture>());
    }
    if (_controls.LBL_PM2) {
        _controls.LBL_PM2->setBorderFill(std::shared_ptr<Texture>());
    }
    if (_controls.LBL_PM3) {
        _controls.LBL_PM3->setBorderFill(std::shared_ptr<Texture>());
    }
    _controls.BTN_DELETE->setDisabled(true);
    _controls.BTN_SAVELOAD->setDisabled(_mode != SaveLoadMode::Save);
    _controls.BTN_SAVELOAD->setVisible(_mode == SaveLoadMode::Save);
}

void SaveLoad::refreshSelection() {
    int selected = getSelectedSaveNumber();
    const SavedGame *save = selected < 0 ? nullptr : findSave(selected);
    _controls.BTN_DELETE->setDisabled(!save || _pendingRequestId.has_value());
    _controls.BTN_SAVELOAD->setDisabled(
        _pendingRequestId.has_value() || (_mode != SaveLoadMode::Save && !save));
    _controls.BTN_SAVELOAD->setVisible(
        _mode == SaveLoadMode::Save || static_cast<bool>(save));
    if (!save) {
        _controls.LBL_AREANAME->setTextMessage("");
        _controls.LBL_AREANAME->setVisible(false);
        _controls.LBL_PLANETNAME->setTextMessage("");
        _controls.LBL_PLANETNAME->setVisible(false);
        if (_controls.LBL_SCREENSHOT) {
            _controls.LBL_SCREENSHOT->setBorderFill(std::shared_ptr<Texture>());
        }
        if (_controls.LBL_PM1) {
            _controls.LBL_PM1->setBorderFill(std::shared_ptr<Texture>());
        }
        if (_controls.LBL_PM2) {
            _controls.LBL_PM2->setBorderFill(std::shared_ptr<Texture>());
        }
        if (_controls.LBL_PM3) {
            _controls.LBL_PM3->setBorderFill(std::shared_ptr<Texture>());
        }
        return;
    }
    std::shared_ptr<Texture> screenshot;
    if (save->screenshot) {
        try {
            auto screenshotBytes = *save->screenshot;
            MemoryInputStream stream(screenshotBytes);
            TgaReader reader(stream, "save_screen", TextureUsage::GUI);
            reader.load();
            screenshot = reader.texture();
            if (screenshot) screenshot->init();
        } catch (const std::exception &e) {
            warn("Unable to decode save thumbnail: " + std::string(e.what()));
        }
    }
    if (_controls.LBL_SCREENSHOT) {
        _controls.LBL_SCREENSHOT->setBorderFill(std::move(screenshot));
    }
    auto separator = save->metadata.areaName.find(" - ");
    auto planetName = separator == std::string::npos
                          ? std::string()
                          : save->metadata.areaName.substr(0, separator);
    auto areaName = separator == std::string::npos
                        ? save->metadata.areaName
                        : save->metadata.areaName.substr(separator + 3);
    if (_controls.LBL_AREANAME) {
        _controls.LBL_AREANAME->setTextMessage(std::move(areaName));
        _controls.LBL_AREANAME->setVisible(true);
    }
    if (_controls.LBL_PLANETNAME) {
        _controls.LBL_PLANETNAME->setTextMessage(std::move(planetName));
        _controls.LBL_PLANETNAME->setVisible(true);
    }
    if (_controls.LBL_PCNAME) {
        _controls.LBL_PCNAME->setTextMessage(save->metadata.pcName);
    }
    if (_controls.LBL_TIMEPLAYED) {
        _controls.LBL_TIMEPLAYED->setTextMessage(playedTime(save->metadata.timePlayed));
    }
    auto loadPortrait = [this](const std::string &resRef) {
        std::shared_ptr<Texture> portrait;
        if (resRef.empty()) return portrait;
        try {
            portrait = _services.resource.textures.get(
                resRef, TextureUsage::GUI);
        } catch (const std::exception &e) {
            warn("Unable to load save portrait: " + std::string(e.what()));
        }
        return portrait;
    };
    if (_controls.LBL_PM1) {
        _controls.LBL_PM1->setBorderFill(loadPortrait(save->metadata.portrait0));
    }
    if (_controls.LBL_PM2) {
        _controls.LBL_PM2->setBorderFill(loadPortrait(save->metadata.portrait1));
    }
    if (_controls.LBL_PM3) {
        _controls.LBL_PM3->setBorderFill(loadPortrait(save->metadata.portrait2));
    }
}

void SaveLoad::showStatus(std::string message) {
    _controls.LBL_AREANAME->setTextMessage(std::move(message));
    _controls.LBL_AREANAME->setVisible(true);
}

void SaveLoad::showSaveName(uint32_t slot, std::string name) {
    if (!_saveNameGUI) {
        saveGame(slot, std::move(name));
        return;
    }
    if (name.empty()) name = "Game " + std::to_string(slot);
    _saveNameSlot = slot;
    _saveNameInput.setText(name);
    _saveNameControls.EDITBOX->setTextMessage(name);
    _saveNameVisible = true;
}

void SaveLoad::hideSaveName() { _saveNameVisible = false; }

void SaveLoad::confirmSaveName() {
    auto name = std::string(_saveNameBuffer.str());
    hideSaveName();
    if (findSave(_saveNameSlot) && _confirmation) {
        auto slot = _saveNameSlot;
        _confirmation->showConfirm(
            _services.resource.strings.getText(kStrRefConfirmOverwrite),
            [this, slot, name = std::move(name)]() mutable {
                saveGame(slot, std::move(name));
            });
        return;
    }
    saveGame(_saveNameSlot, std::move(name));
}

void SaveLoad::saveGame(uint32_t number, std::string name) {
    auto result = _game.requestManualSave(number, std::move(name));
    if (result.status == SaveStatus::Accepted) {
        _pendingRequestId = result.requestId;
        _controls.BTN_SAVELOAD->setDisabled(true);
        _controls.BTN_DELETE->setDisabled(true);
        showStatus("Saving game...");
    } else {
        showStatus(terminalMessage(result));
    }
}

void SaveLoad::loadGame(uint32_t number) {
    auto save = findSave(number);
    if (!save) return;
    try {
        // Hand over the slot discovered by indexing. Reducing it to a name here
        // would make the loader resolve the directory a second time, and the
        // entity it found need not be the one the player selected.
        _game.loadGame(save->descriptor);
    } catch (const std::exception &e) {
        warn("Unable to load selected save: " + std::string(e.what()));
        showStatus("The selected game could not be loaded.");
    }
}

bool SaveLoad::consumeTerminalResult(
    const std::optional<SaveResult> &result) {
    if (!_pendingRequestId || !result ||
        result->requestId != *_pendingRequestId ||
        result->status == SaveStatus::Accepted) {
        return false;
    }
    _pendingRequestId.reset();
    return true;
}

void SaveLoad::dismissTransientState() {
    // SaveLoad is a persistent controller. Screen changes must discard only
    // view-local interaction state; an in-flight request remains observed.
    _selectedSaveSlot.reset();
    _saveNameVisible = false;
}

void SaveLoad::setMode(SaveLoadMode mode) { _mode = mode; }

int SaveLoad::getSelectedSaveNumber() const {
    return _selectedSaveSlot
               ? static_cast<int>(*_selectedSaveSlot)
               : -1;
}

uint32_t SaveLoad::getNewSaveNumber() const { return nextManualSaveSlot(_saves); }

const SavedGame *SaveLoad::findSave(uint32_t number) const {
    auto found = std::find_if(_saves.begin(), _saves.end(), [number](const auto &save) {
        return save.slot == number;
    });
    return found == _saves.end() ? nullptr : &*found;
}

void SaveLoad::deleteGame(uint32_t number) {
    auto save = findSave(number);
    if (!save) return;
    if (_confirmation) {
        _confirmation->showConfirm(
            _services.resource.strings.getText(kStrRefConfirmDelete),
            [this, number]() {
                auto confirmed = findSave(number);
                if (!confirmed ||
                    !deleteSavedGame(_game.gamePath(), confirmed->descriptor)) {
                    showStatus("The selected save could not be deleted.");
                    return;
                }
                refreshSavedGames();
            });
        return;
    }
    if (!deleteSavedGame(_game.gamePath(), save->descriptor)) {
        showStatus("The selected save could not be deleted.");
        return;
    }
    refreshSavedGames();
}

} // namespace game
} // namespace reone
