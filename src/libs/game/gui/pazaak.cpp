/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "reone/game/gui/pazaak.h"

#include "reone/game/game.h"
#include "reone/game/pazaaksession.h"

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/strings.h"
#include "reone/system/logutil.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "reone/game/gui/sounds.h"

namespace reone {

namespace game {

using namespace gui;
using namespace pazaak;

const char *pazaakAudioResRef(PazaakPresentationEventType event) {
    switch (event) {
    case PazaakPresentationEventType::TurnStarted:
        return "mgs_startturn";
    case PazaakPresentationEventType::MainDeckDrawn:
        return "mgs_drawmain";
    case PazaakPresentationEventType::HandCardPlayed:
        return "mgs_playside";
    case PazaakPresentationEventType::PlayerSetWon:
        return "mgs_winset";
    case PazaakPresentationEventType::PlayerSetLost:
        return "mgs_loseset";
    case PazaakPresentationEventType::PlayerMatchWon:
        return "mgs_winmatch";
    case PazaakPresentationEventType::PlayerMatchLost:
        return "mgs_losematch";
    case PazaakPresentationEventType::SetTied:
        return nullptr;
    default:
        return nullptr;
    }
}

// The box that ends a set or the match.
std::optional<int> pazaakResultStrRef(PazaakPresentationEventType event) {
    switch (event) {
    case PazaakPresentationEventType::PlayerSetWon:
        return 32334;
    case PazaakPresentationEventType::PlayerSetLost:
        return 32335;
    case PazaakPresentationEventType::PlayerMatchWon:
        return 32336;
    case PazaakPresentationEventType::PlayerMatchLost:
        return 32337;
    case PazaakPresentationEventType::SetTied:
        return 32338;
    default:
        return std::nullopt;
    }
}

namespace {

std::string signedText(int value) {
    return std::string(value >= 0 ? "+" : "") + std::to_string(value);
}

std::string cardText(const CardDefinition &card,
                     CardSign selectedSign = CardSign::Positive,
                     int selectedValue = 1) {
    switch (card.behavior()) {
    case CardBehavior::FixedPositive:
        return "+" + std::to_string(card.magnitude());
    case CardBehavior::FixedNegative:
        return "-" + std::to_string(card.magnitude());
    case CardBehavior::SignSelectable:
        return std::string(selectedSign == CardSign::Positive ? "+" : "-") +
               std::to_string(card.magnitude());
    // Every special card shares one artwork face, so its identifying label is
    // drawn in the card's value window using the authored symbols.
    case CardBehavior::Tiebreaker:
        return "\xB1" "1T";
    case CardBehavior::ValueChange:
        return signedText(selectedValue);
    case CardBehavior::Double:
        return "D";
    case CardBehavior::FlipTwoFour:
        return "2&4";
    case CardBehavior::FlipThreeSix:
        return "3&6";
    default:
        return "";
    }
}

// One coherent title policy for card art: KotOR I uses the lbl_card* family;
// KotOR II uses the PC (_p) pcards_* family that accompanies the *_p GUI
// layouts. Colour derives from explicit provenance, never signed value.
std::string kotorTwoCardTexture(const char *family) {
    return std::string(family) + "_p";
}

// Family assignment follows the shipped artwork: pcards_generic is the green
// mandatory main-deck card, pcards_gold is the special card, pcards_pos/neg are
// the blue/red numbered side cards, and pcards_dblpos/dblneg are the split
// blue/red states of a sign-selectable card.
std::string mainDeckTexture(bool tsl) {
    return tsl ? kotorTwoCardTexture("pcards_generic") : "lbl_cardstand";
}
std::string specialCardTexture(bool tsl) {
    return tsl ? kotorTwoCardTexture("pcards_gold") : "lbl_cardrarem";
}
std::string positiveCardTexture(bool tsl) {
    return tsl ? kotorTwoCardTexture("pcards_pos") : "lbl_cardmpos";
}
std::string negativeCardTexture(bool tsl) {
    return tsl ? kotorTwoCardTexture("pcards_neg") : "lbl_cardmneg";
}
std::string cardBackTexture(bool tsl) {
    return tsl ? kotorTwoCardTexture("pcards_back") : "lbl_cardback";
}
std::string cardHiliteTexture(bool tsl) {
    return tsl ? kotorTwoCardTexture("pcards_hilite") : "lbl_cardhilite";
}
std::string pipTexture(bool won, bool tsl) {
    if (tsl) {
        return won ? "pz_playerliteon" : "pz_playerliteoff";
    }
    return won ? "lbl_winmark02" : "lbl_winmark01";
}
std::string turnLightTexture(bool tsl) { return tsl ? "pz_playerliteon" : "lbl_pazaakturn"; }

std::string signedSideTexture(bool positive, bool tsl) {
    if (tsl) {
        return kotorTwoCardTexture(positive ? "pcards_dblpos" : "pcards_dblneg");
    }
    return positive ? "lbl_cardrarem" : "lbl_cardraref";
}

std::string handCardTexture(const CardDefinition &card, CardSign sign, bool tsl) {
    switch (card.behavior()) {
    case CardBehavior::FixedPositive:
        return positiveCardTexture(tsl);
    case CardBehavior::FixedNegative:
        return negativeCardTexture(tsl);
    case CardBehavior::SignSelectable:
        return signedSideTexture(sign == CardSign::Positive, tsl);
    default:
        // Special cards: Double, both flips, Tiebreaker and Value Change.
        return specialCardTexture(tsl);
    }
}

std::string collectionCardText(const CardDefinition &card) {
    // Card text is drawn one glyph per byte, so the plus-minus sign is a single
    // byte of the font's code page rather than a multi-byte sequence.
    if (card.behavior() == CardBehavior::SignSelectable) {
        return "\xB1" + std::to_string(card.magnitude());
    }
    if (card.behavior() == CardBehavior::ValueChange) {
        // Uncommitted, the card advertises the range it can take.
        return "1\xB1" "2";
    }
    return cardText(card);
}

// A board card's appearance comes from where it came from, never from its final
// numeric value: a mandatory main-deck draw keeps the main-deck face, and a side
// card keeps its own family's face once played.
std::string boardCardTexture(const PazaakBoardSlot &slot, bool tsl) {
    if (slot.source == PlayedCardSource::MainDeck) {
        return mainDeckTexture(tsl);
    }
    switch (slot.behavior.value_or(CardBehavior::FixedPositive)) {
    case CardBehavior::FixedNegative:
        return negativeCardTexture(tsl);
    case CardBehavior::SignSelectable:
        return signedSideTexture(slot.value >= 0, tsl);
    case CardBehavior::Double:
    case CardBehavior::FlipTwoFour:
    case CardBehavior::FlipThreeSix:
    case CardBehavior::Tiebreaker:
    case CardBehavior::ValueChange:
        return specialCardTexture(tsl);
    default:
        return positiveCardTexture(tsl);
    }
}

std::string boardCardText(const PazaakBoardSlot &slot) {
    if (slot.source == PlayedCardSource::MainDeck) {
        return std::to_string(slot.value);
    }
    if (slot.behavior == CardBehavior::FlipTwoFour) {
        return "2&4";
    }
    if (slot.behavior == CardBehavior::FlipThreeSix) {
        return "3&6";
    }
    return signedText(slot.value);
}

std::string twoDigitIndex(size_t index) {
    std::ostringstream stream;
    stream << (index / 6) << (index % 6);
    return stream.str();
}

void clearCard(Control &button, Control &label) {
    button.setBorderFill("");
    button.setHilightFill("");
    button.setHilightOverBorder(false);
    button.setSelected(false);
    button.setTextMessage("");
    button.setDisabled(true);
    label.setTextMessage("");
    label.setVisible(false);
}

void setCardFace(
    Control &button,
    Control &label,
    const std::string &texture,
    const std::string &text,
    bool interactive,
    bool tsl) {

    button.setBorderFill(texture);
    button.setTextMessage("");
    button.setDisabled(!interactive);
    button.setHilightOverBorder(true);
    button.setHilightFill(cardHiliteTexture(tsl));
    // A card's hilight fill pulses.
    button.setHilightPulsing(false, true);
    button.setSelected(false);
    label.setTextMessage(text);
    label.setVisible(true);
}

void setBoardSlot(
    Control &button,
    Control &label,
    const PazaakBoardSlot &slot,
    bool tsl) {

    if (!slot.occupied) {
        clearCard(button, label);
        return;
    }
    setCardFace(
        button,
        label,
        boardCardTexture(slot, tsl),
        boardCardText(slot),
        false,
        tsl);
}

} // namespace

PazaakWagerGUI::PazaakWagerGUI(Game &game, ServicesView &services) :
    GameGUI(game, services) {
    _resRef = guiResRef("pazaakwager");
}

// The stakes, a newline and the player's credits.
static constexpr int kMaximumWagerStrRef = 32321;
static constexpr int kCreditsStrRef = 38600;
// A held arrow first repeats after half a second, then fifteen times a second.
static constexpr float kWagerRepeatDelay = 0.5f;
static constexpr float kWagerRepeatInterval = 1.0f / 15.0f;

// The wager is a box drawn over the side deck, whose backdrop shows behind
// it, so it has none of its own. TSL's layout is a 640x480 screen, centred
// on the side deck's 800x600 one.
void PazaakWagerGUI::onGUILoaded() {
    _gui->setHilightFollowsKeyboard(true);
    centerRootInCanvas(_game.isTSL() ? 800 : 640, _game.isTSL() ? 600 : 480);
    auto button = [this](const std::string &tag) {
        auto control = findControl<Button>(tag);
        if (!control) {
            throw std::runtime_error("Missing Pazaak button: " + tag);
        }
        return control;
    };
    auto label = [this](const std::string &tag) {
        auto control = findControl<Label>(tag);
        if (!control) {
            throw std::runtime_error("Missing Pazaak label: " + tag);
        }
        return control;
    };
    _controls.less = button("BTN_LESS");
    _controls.more = button("BTN_MORE");
    _controls.quit = button("BTN_QUIT");
    _controls.wager = button("BTN_WAGER");
    _controls.maximum = label("LBL_MAXIMUM");
    _controls.wagerValue = label("LBL_WAGERVAL");

    // No button takes the keyboard. The arrows step the wager as they are
    // pressed, and again while held.
    for (const auto &control : {_controls.less, _controls.more, _controls.quit, _controls.wager}) {
        control->setTakesKeyboardOnHover(false);
    }
    for (const auto &step : {_controls.less, _controls.more}) {
        step->setOnSelectionChanged([this, step](bool selected) {
            if (selected) {
                _hoveredStep = step;
            } else if (_hoveredStep == step) {
                _hoveredStep.reset();
            }
        });
    }
    _controls.wager->setOnClick([this]() {
        if (auto session = _game.pazaakSession(); session && session->confirmWager()) {
            _game.showPazaakSetup();
        }
    });
    _controls.quit->setOnClick([this]() {
        _game.queryPazaakQuit();
    });
    refresh();
}

void PazaakWagerGUI::stepWager(bool more) {
    auto session = _game.pazaakSession();
    if (!session) {
        return;
    }
    const int before = session->wager();
    if (more) {
        session->increaseWager();
    } else {
        session->decreaseWager();
    }
    if (session->wager() != before) {
        _services.game.guiSounds.play(_services.game.guiSounds.getOnEnter());
        refresh();
    }
}

// The arrow keys step the wager, Up and Right raising it; Return wagers and
// Escape asks whether to quit.
bool PazaakWagerGUI::handle(const input::Event &event) {
    if (event.type == input::EventType::KeyDown) {
        switch (event.key.code) {
        case input::KeyCode::Left:
        case input::KeyCode::Down:
            stepWager(false);
            return true;
        case input::KeyCode::Right:
        case input::KeyCode::Up:
            stepWager(true);
            return true;
        case input::KeyCode::Return:
        case input::KeyCode::KeypadEnter:
            if (!event.key.repeat) {
                _heldStep.reset();
                _services.game.guiSounds.play(_services.game.guiSounds.getOnClick());
                if (auto session = _game.pazaakSession(); session && session->confirmWager()) {
                    _game.showPazaakSetup();
                }
            }
            return true;
        case input::KeyCode::Escape:
            if (!event.key.repeat) {
                _heldStep.reset();
                _services.game.guiSounds.play(_services.game.guiSounds.getOnClick());
                _game.queryPazaakQuit();
            }
            return true;
        default:
            break;
        }
    }
    // The pointer's control is taken afresh at the press: a box that was up
    // took the pointer's moves.
    if (event.type == input::EventType::MouseButtonDown && event.button.button == input::MouseButton::Left) {
        hoverOnOpening({event.button.x, event.button.y});
    }
    if (event.type == input::EventType::MouseButtonDown && event.button.button == input::MouseButton::Left &&
        _hoveredStep) {
        _heldStep = _hoveredStep;
        _stepRepeatIn = kWagerRepeatDelay;
        stepWager(_heldStep == _controls.more);
    } else if (event.type == input::EventType::MouseButtonUp && event.button.button == input::MouseButton::Left) {
        _heldStep.reset();
    }
    return GameGUI::handle(event);
}

void PazaakWagerGUI::update(float dt) {
    GameGUI::update(dt);
    if (!_heldStep) {
        return;
    }
    _stepRepeatIn -= dt;
    while (_heldStep && _stepRepeatIn < 0.0f) {
        _stepRepeatIn += kWagerRepeatInterval;
        stepWager(_heldStep == _controls.more);
    }
}

// The title is the layout's own.
void PazaakWagerGUI::refresh() {
    auto session = _game.pazaakSession();
    if (!session) {
        return;
    }
    auto &strings = _services.resource.strings;
    _controls.wagerValue->setTextMessage(std::to_string(session->wager()));
    _controls.maximum->setTextMessage(
        strings.getText(kMaximumWagerStrRef) + " " + std::to_string(session->wagerLimit()) + "\n" +
        strings.getText(kCreditsStrRef) + " " + std::to_string(session->availableCredits()));
}

PazaakSetupGUI::PazaakSetupGUI(Game &game, ServicesView &services) :
    GameGUI(game, services) {
    _resRef = guiResRef("pazaaksetup");
}

// Escape asks whether to quit.
bool PazaakSetupGUI::handle(const input::Event &event) {
    if (event.type == input::EventType::KeyDown &&
        event.key.code == input::KeyCode::Escape &&
        !event.key.repeat) {
        _services.game.guiSounds.play(_services.game.guiSounds.getOnClick());
        _game.queryPazaakQuit();
        return true;
    }
    return GameGUI::handle(event);
}

static constexpr int kConfirmSideDeckStrRef = 32322;
static constexpr glm::vec3 kTSLPazaakGold {0.8f, 0.65f, 0.2f};

// The help TSL gives a card type the player owns, when the pointer is over
// it among the available cards.
static std::optional<int> cardHelpStrRef(const CardDefinition &card) {
    switch (card.behavior()) {
    case CardBehavior::FixedPositive:
        return 128418;
    case CardBehavior::FixedNegative:
        return 128419;
    case CardBehavior::SignSelectable:
        return 128420;
    case CardBehavior::ValueChange:
        return 128421;
    case CardBehavior::Double:
        return 128422;
    case CardBehavior::Tiebreaker:
        return 128423;
    case CardBehavior::FlipTwoFour:
        return 128424;
    case CardBehavior::FlipThreeSix:
        return 128425;
    default:
        return std::nullopt;
    }
}

// A chosen card, or an available one the player owns none of, clears it.
void PazaakSetupGUI::describeCard(std::optional<size_t> collectionIndex) {
    auto session = _game.pazaakSession();
    if (!_controls.help || !session) {
        return;
    }
    std::optional<int> strRef;
    const auto &collection = session->collection();
    if (collectionIndex && *collectionIndex < collection.size() && collection[*collectionIndex].copies > 0) {
        strRef = cardHelpStrRef(collection[*collectionIndex].definition);
    }
    _controls.help->setTextMessage(strRef ? _services.resource.strings.getText(*strRef) : "");
}

void PazaakSetupGUI::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    _gui->setHilightFollowsKeyboard(true);
    auto button = [this](const std::string &tag) {
        auto control = findControl<Button>(tag);
        if (!control) {
            throw std::runtime_error("Missing Pazaak button: " + tag);
        }
        return control;
    };
    auto label = [this](const std::string &tag) {
        auto control = findControl<Label>(tag);
        if (!control) {
            throw std::runtime_error("Missing Pazaak label: " + tag);
        }
        return control;
    };
    _controls.accept = button("BTN_ATEXT");
    // K1's setup has an "Add card" button (BTN_YTEXT); K2's setup omits it (a
    // card is added by clicking it in the available grid), so it is optional.
    _controls.cancel = findControl<Button>("BTN_YTEXT");
    _controls.clear = findControl<Button>("BTN_CLEARCARDS");
    _controls.help = findControl<Label>("LBL_HELP");
    auto session = _game.pazaakSession();
    size_t requiredAvailableSlots = session ? session->collection().size() : 0;
    for (size_t i = 0; i < _controls.availableButtons.size(); ++i) {
        std::string suffix(twoDigitIndex(i));
        _controls.availableButtons[i] = findControl<Button>("BTN_AVAIL" + suffix);
        _controls.availableLabels[i] = findControl<Label>("LBL_AVAIL" + suffix);
        _controls.availableCounts[i] = findControl<Label>("LBL_AVAILNUM" + suffix);
        if (!_controls.availableButtons[i] ||
            !_controls.availableLabels[i] ||
            !_controls.availableCounts[i]) {
            if (i < requiredAvailableSlots) {
                throw std::runtime_error(
                    "Missing Pazaak collection slot: " + suffix);
            }
            break;
        }
        _controls.availableButtons[i]->setOnSelectionChanged([this, i](bool selected) {
            if (selected) {
                describeCard(i);
            }
        });
        _controls.availableButtons[i]->setOnClick([this, i]() {
            _selectedAvailable = i;
            if (auto session = _game.pazaakSession()) {
                session->selectCard(i);
            }
            refresh();
        });
    }
    for (size_t i = 0; i < _controls.chosenButtons.size(); ++i) {
        std::string suffix(std::to_string(i));
        _controls.chosenButtons[i] = button("BTN_CHOSEN" + suffix);
        _controls.chosenLabels[i] = label("LBL_CHOSEN" + suffix);
        _controls.chosenButtons[i]->setOnSelectionChanged([this](bool selected) {
            if (selected) {
                describeCard(std::nullopt);
            }
        });
        _controls.chosenButtons[i]->setOnClick([this, i]() {
            if (auto session = _game.pazaakSession()) {
                session->removeChosenCard(i);
                refresh();
            }
        });
    }
    if (_controls.clear) {
        _controls.clear->setOnClick([this]() {
            if (auto session = _game.pazaakSession()) {
                session->clearChosenCards();
                refresh();
            }
        });
    }
    // Accept asks whether to play with the side deck chosen.
    _controls.accept->setOnClick([this]() {
        auto session = _game.pazaakSession();
        if (!session || !session->canConfirmSetup()) {
            return;
        }
        auto play = [this]() {
            if (auto session = _game.pazaakSession(); session && session->confirmSetup()) {
                _game.showPazaakBoard();
            }
        };
        if (!_game.showConfirmPopup(_services.resource.strings.getText(kConfirmSideDeckStrRef), play, {},
                                    _game.pazaakPopupColors())) {
            play();
        }
    });
    if (_controls.cancel) {
        _controls.cancel->setOnClick([this]() {
            if (auto session = _game.pazaakSession(); session && _selectedAvailable) {
                session->selectCard(*_selectedAvailable);
                refresh();
            }
        });
    }
    // The titles and buttons keep the layout's text.
    if (_controls.help) {
        _controls.help->setTextMessage("");
    }
    refresh();
}

void PazaakSetupGUI::refresh() {
    auto session = _game.pazaakSession();
    if (!session) {
        return;
    }

    const auto &collection = session->collection();
    for (size_t i = 0; i < _controls.availableButtons.size(); ++i) {
        if (!_controls.availableButtons[i]) {
            continue;
        }
        bool present = i < collection.size();
        _controls.availableButtons[i]->setVisible(present);
        _controls.availableLabels[i]->setVisible(present);
        _controls.availableCounts[i]->setVisible(present);
        if (!present) {
            clearCard(
                *_controls.availableButtons[i],
                *_controls.availableLabels[i]);
            _controls.availableCounts[i]->setVisible(false);
            continue;
        }
        size_t remaining = session->remainingCopies(i);
        std::string text(collectionCardText(collection[i].definition));
        setCardFace(
            *_controls.availableButtons[i],
            *_controls.availableLabels[i],
            handCardTexture(collection[i].definition, CardSign::Positive, _game.isTSL()),
            text,
            remaining > 0 && session->chosenCards().size() < kSideDeckSize,
            _game.isTSL());
        _controls.availableButtons[i]->setSelected(
            _selectedAvailable && *_selectedAvailable == i);
        _controls.availableCounts[i]->setTextMessage(std::to_string(remaining));
        _controls.availableCounts[i]->setVisible(true);
    }

    const auto &chosen = session->chosenCards();
    for (size_t i = 0; i < _controls.chosenButtons.size(); ++i) {
        bool present = i < chosen.size();
        if (!present) {
            clearCard(*_controls.chosenButtons[i], *_controls.chosenLabels[i]);
            continue;
        }
        const CardDefinition &definition = collection[chosen[i]].definition;
        setCardFace(
            *_controls.chosenButtons[i],
            *_controls.chosenLabels[i],
            handCardTexture(definition, CardSign::Positive, _game.isTSL()),
            collectionCardText(definition),
            true,
            _game.isTSL());
    }
    const bool complete = session->canConfirmSetup();
    _controls.accept->setDisabled(!complete);
    // TSL's Accept is pazaak gold once the side deck is full, dark tan until
    // then.
    if (_game.isTSL()) {
        const glm::vec3 &color = complete ? kTSLPazaakGold : kTSLColorDisabledText;
        _controls.accept->setTextColor(color);
        _controls.accept->setBorderColor(color);
    }
    if (_controls.cancel) {
        _controls.cancel->setDisabled(
            !_selectedAvailable ||
            session->remainingCopies(*_selectedAvailable) == 0 ||
            session->chosenCards().size() >= kSideDeckSize);
    }
    if (_controls.clear) {
        _controls.clear->setDisabled(chosen.empty());
    }
}

PazaakBoardGUI::PazaakBoardGUI(Game &game, ServicesView &services) :
    GameGUI(game, services) {
    _resRef = guiResRef("pazaakgame");
}

// A set both sides ended on nine cards is left; otherwise the player is
// asked whether to forfeit, on either participant's turn while a set is
// played.
bool PazaakBoardGUI::leaveOrForfeit() {
    auto session = _game.pazaakSession();
    if (session && session->boardProjection().state == PazaakBoardState::UnresolvedBothNine) {
        _game.abortPazaak();
        return true;
    }
    return _game.queryPazaakForfeit();
}

// Escape leaves or forfeits as the forfeit button does, clicking when it acts.
bool PazaakBoardGUI::handle(const input::Event &event) {
    if (event.type == input::EventType::KeyDown &&
        event.key.code == input::KeyCode::Escape &&
        !event.key.repeat) {
        // Leaving or forfeiting can put the board away, so the sounds are
        // taken first.
        auto &sounds = _services.game.guiSounds;
        if (leaveOrForfeit()) {
            sounds.play(sounds.getOnClick());
        }
        return true;
    }
    return GameGUI::handle(event);
}

void PazaakBoardGUI::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    _gui->setHilightFollowsKeyboard(true);
    auto button = [this](const std::string &tag) {
        auto control = findControl<Button>(tag);
        if (!control) {
            throw std::runtime_error("Missing Pazaak button: " + tag);
        }
        return control;
    };
    auto label = [this](const std::string &tag) {
        auto control = findControl<Label>(tag);
        if (!control) {
            throw std::runtime_error("Missing Pazaak label: " + tag);
        }
        return control;
    };
    _controls.endTurn = button("BTN_XTEXT");
    _controls.stand = button("BTN_YTEXT");
    _controls.opponentName = label("LBL_NPCNAME");
    _controls.opponentTotal = label("LBL_NPCTOTAL");
    _controls.opponentTurn = label("LBL_NPCTURN");
    _controls.playerName = label("LBL_PLRNAME");
    _controls.playerTotal = label("LBL_PLRTOTAL");
    _controls.playerTurn = label("LBL_PLRTURN");

    for (size_t i = 0; i < kHandSize; ++i) {
        std::string suffix(std::to_string(i));
        _controls.flip[i] = findControl<Button>("BTN_FLIP" + suffix);
        _controls.opponentHandButtons[i] = button("BTN_NPCSIDE" + suffix);
        _controls.opponentHandLabels[i] = label("LBL_NPCSIDE" + suffix);
        _controls.playerHandButtons[i] = button("BTN_PLRSIDE" + suffix);
        _controls.playerHandLabels[i] = label("LBL_PLRSIDE" + suffix);

        if (_controls.flip[i]) {
            _controls.flip[i]->setOnClick([this, i]() {
                auto session = _game.pazaakSession();
                if (!session) {
                    return;
                }
                auto projection = session->boardProjection();
                CardSign next = projection.playerHand[i].selectedSign == CardSign::Positive
                                    ? CardSign::Negative
                                    : CardSign::Positive;
                session->selectPlayerCardSign(i, next);
                refresh();
            });
        }
        _controls.playerHandButtons[i]->setOnClick([this, i]() {
            if (auto session = _game.pazaakSession()) {
                refreshAfterCommand(session->playPlayerHandCard(i));
            }
        });
        _controls.opponentHandButtons[i]->setDisabled(true);
    }
    _controls.forfeit = findControl<Button>("BTN_FORFEITGAME");
    // The KotOR II Value Change switch is per hand slot, like the sign switch.
    // It is optional and absent in KotOR I, so a missing control stays unwired.
    for (size_t i = 0; i < _controls.change.size(); ++i) {
        _controls.change[i] = findControl<Button>("BTN_CHANGE" + std::to_string(i));
        if (!_controls.change[i]) {
            continue;
        }
        _controls.change[i]->setOnClick([this, i]() {
            auto session = _game.pazaakSession();
            if (!session) {
                return;
            }
            // Advance that slot's card through its legal values.
            const auto &states = PazaakSession::valueChangeStates();
            int current = session->boardProjection().playerHand[i].selectedValue;
            size_t next = 0;
            for (size_t s = 0; s < states.size(); ++s) {
                if (states[s] == current) {
                    next = (s + 1) % states.size();
                    break;
                }
            }
            session->selectPlayerCardValue(i, states[next]);
            refresh();
        });
    }
    _controls.flipLegend = findControl<Label>("LBL_FLIPLEGEND");
    _controls.flipIcon = findControl<Label>("LBL_FLIPICON");
    _controls.changeLegend = findControl<Label>("LBL_CHANGELEGEND");
    _controls.changeIcon = findControl<Label>("LBL_CHANGEICON");
    for (size_t i = 0; i < kBoardSize; ++i) {
        std::string suffix(std::to_string(i));
        _controls.opponentBoardButtons[i] = button("BTN_NPC" + suffix);
        _controls.opponentBoardLabels[i] = label("LBL_NPC" + suffix);
        _controls.playerBoardButtons[i] = button("BTN_PLR" + suffix);
        _controls.playerBoardLabels[i] = label("LBL_PLR" + suffix);
    }
    for (size_t i = 0; i < kSetWinsForMatch; ++i) {
        std::string suffix(std::to_string(i));
        _controls.opponentScores[i] = label("LBL_NPCSCORE" + suffix);
        _controls.playerScores[i] = label("LBL_PLRSCORE" + suffix);
    }

    _controls.endTurn->setOnClick([this]() {
        if (auto session = _game.pazaakSession()) {
            refreshAfterCommand(session->endPlayerTurn());
        }
    });
    _controls.stand->setOnClick([this]() {
        if (auto session = _game.pazaakSession()) {
            refreshAfterCommand(session->standPlayer());
        }
    });
    if (_controls.forfeit) {
        _controls.forfeit->setOnClick([this]() {
            leaveOrForfeit();
        });
    }
    // The buttons keep the layout's text.
    refresh();
}

void PazaakBoardGUI::refreshAfterCommand(ActionError error) {
    // MatchState::apply is atomic. Refreshing from the model for both accepted
    // and rejected commands guarantees the GUI never maintains shadow rules.
    refresh();
    if (error == ActionError::None) {
        _game.completePazaakIfReady();
    }
}

void PazaakBoardGUI::refresh() {
    auto session = _game.pazaakSession();
    if (!session) {
        return;
    }
    PazaakBoardProjection projection(session->boardProjection());
    bool tsl = _game.isTSL();

    _controls.playerName->setTextMessage(projection.playerName);
    _controls.opponentName->setTextMessage(projection.opponentName);
    _controls.playerTotal->setTextMessage(std::to_string(projection.playerTotal));
    _controls.opponentTotal->setTextMessage(std::to_string(projection.opponentTotal));

    _controls.playerTurn->setBorderFill("");
    _controls.opponentTurn->setBorderFill("");
    _controls.playerTurn->setTextMessage("");
    _controls.opponentTurn->setTextMessage("");
    _controls.playerTurn->setVisible(false);
    _controls.opponentTurn->setVisible(false);

    // The turn light marks the active participant while a set is in progress.
    // Set and match results are shown in a box (playPendingAudio).
    if (projection.playerActive) {
        _controls.playerTurn->setVisible(true);
        _controls.playerTurn->setBorderFill(turnLightTexture(tsl));
    } else if (projection.opponentActive) {
        _controls.opponentTurn->setVisible(true);
        _controls.opponentTurn->setBorderFill(turnLightTexture(tsl));
    }

    for (size_t i = 0; i < kBoardSize; ++i) {
        setBoardSlot(
            *_controls.playerBoardButtons[i],
            *_controls.playerBoardLabels[i],
            projection.playerBoard[i],
            tsl);
        setBoardSlot(
            *_controls.opponentBoardButtons[i],
            *_controls.opponentBoardLabels[i],
            projection.opponentBoard[i],
            tsl);
    }
    // Every switch control is hidden and cleared first, then only the controls
    // belonging to the cards actually holding them are turned back on. Because
    // this runs on every refresh, no control can survive a card being played, a
    // turn ending, a stand, a set or match transition, or a screen reopen, and a
    // control can never leak from one hand slot onto another.
    bool anyFlipShown = false;
    bool anyChangeShown = false;
    for (size_t i = 0; i < kHandSize; ++i) {
        if (_controls.flip[i]) {
            _controls.flip[i]->setVisible(false);
            _controls.flip[i]->setDisabled(true);
            _controls.flip[i]->setSelected(false);
            _controls.flip[i]->setBorderFill("");
            _controls.flip[i]->setTextMessage("");
        }
        if (_controls.change[i]) {
            _controls.change[i]->setVisible(false);
            _controls.change[i]->setDisabled(true);
            _controls.change[i]->setSelected(false);
            _controls.change[i]->setBorderFill("");
            _controls.change[i]->setTextMessage("");
        }
    }

    for (size_t i = 0; i < kHandSize; ++i) {
        const PazaakHandSlot &player = projection.playerHand[i];
        std::string playerText = player.definition
                                     ? cardText(*player.definition, player.selectedSign, player.selectedValue)
                                     : "";
        if (player.occupied && !player.used && player.definition) {
            setCardFace(
                *_controls.playerHandButtons[i],
                *_controls.playerHandLabels[i],
                handCardTexture(*player.definition, player.selectedSign, tsl),
                playerText,
                player.playable,
                tsl);
        } else {
            clearCard(
                *_controls.playerHandButtons[i],
                *_controls.playerHandLabels[i]);
        }

        bool live = player.occupied && !player.used && player.playable && player.definition;
        // Only a sign-selectable card carries a sign switch; fixed cards and the
        // non-switchable specials (Double, both flips, Tiebreaker) carry none.
        if (live &&
            player.definition->behavior() == CardBehavior::SignSelectable &&
            _controls.flip[i]) {
            _controls.flip[i]->setVisible(true);
            _controls.flip[i]->setDisabled(false);
            _controls.flip[i]->setBorderFill(
                player.selectedSign == CardSign::Positive ? "pazflip" : "pazflip2");
            anyFlipShown = true;
        }
        // Only a Value Change card carries a value switch.
        if (live && player.definition->isValueSelectable() && _controls.change[i]) {
            _controls.change[i]->setVisible(true);
            _controls.change[i]->setDisabled(false);
            anyChangeShown = true;
        }

        const PazaakHandSlot &opponent = projection.opponentHand[i];
        if (opponent.occupied && !opponent.used && opponent.hidden) {
            _controls.opponentHandButtons[i]->setBorderFill(cardBackTexture(tsl));
            _controls.opponentHandButtons[i]->setHilightFill("");
            _controls.opponentHandButtons[i]->setSelected(false);
            _controls.opponentHandButtons[i]->setTextMessage("");
            _controls.opponentHandButtons[i]->setDisabled(true);
            _controls.opponentHandLabels[i]->setTextMessage("");
            _controls.opponentHandLabels[i]->setVisible(false);
        } else {
            clearCard(
                *_controls.opponentHandButtons[i],
                *_controls.opponentHandLabels[i]);
        }
    }

    // The switch legends and icons are authored artwork, so they are only shown
    // or hidden alongside their controls and never have text written into them.
    if (_controls.flipLegend) {
        _controls.flipLegend->setVisible(anyFlipShown);
    }
    if (_controls.flipIcon) {
        _controls.flipIcon->setVisible(anyFlipShown);
    }
    if (_controls.changeLegend) {
        _controls.changeLegend->setVisible(anyChangeShown);
    }
    if (_controls.changeIcon) {
        _controls.changeIcon->setVisible(anyChangeShown);
    }

    // All six markers are assigned deterministically on every refresh: the lit
    // texture for a won set, the unlit texture otherwise. This never relies on
    // an authored default, so unearned markers can never go missing and a stale
    // win from a previous match cannot persist into a new one.
    for (size_t i = 0; i < kSetWinsForMatch; ++i) {
        bool playerWon = i < projection.playerSetWins;
        bool opponentWon = i < projection.opponentSetWins;
        _controls.playerScores[i]->setBorderFill(pipTexture(playerWon, tsl));
        _controls.opponentScores[i]->setBorderFill(pipTexture(opponentWon, tsl));
        _controls.playerScores[i]->setTextMessage("");
        _controls.opponentScores[i]->setTextMessage("");
        _controls.playerScores[i]->setVisible(true);
        _controls.opponentScores[i]->setVisible(true);
    }

    _controls.endTurn->setDisabled(!projection.canEndTurn);
    _controls.stand->setDisabled(!projection.canStand);
    playPendingAudio();
}

void PazaakBoardGUI::playPendingAudio() {
    auto session = _game.pazaakSession();
    if (!session) {
        return;
    }
    for (const PazaakPresentationEvent &event : session->takePresentationEvents()) {
        if (const char *resRef = pazaakAudioResRef(event.type)) {
            playAudio(resRef);
        }
        if (auto strRef = pazaakResultStrRef(event.type)) {
            _game.showPazaakResult(*strRef);
        }
    }
}

void PazaakBoardGUI::playAudio(const std::string &resRef) {
    try {
        auto clip = _services.resource.audioClips.get(resRef);
        if (!clip) {
            warn("Pazaak audio cue is unavailable: " + resRef);
            return;
        }
        auto source = playInterfaceSound(_services.audio.mixer, std::move(clip));
        if (!source) {
            warn("Pazaak audio cue failed to start: " + resRef);
            return;
        }
        _audioSource = std::move(source);
    } catch (const std::exception &e) {
        warn(
            "Pazaak audio cue failed to load: " +
            resRef +
            " (" +
            e.what() +
            ")");
    }
}

} // namespace game

} // namespace reone
