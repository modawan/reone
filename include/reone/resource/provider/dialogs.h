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

#include <optional>

#include "../dialog.h"
#include "../parser/gff/dlg.h"

namespace reone {

namespace resource {

class Gffs;
class Gff;
class Strings;
class TwoDA;
class TwoDAs;

class IDialogs {
public:
    virtual ~IDialogs() = default;

    virtual void clear() = 0;

    virtual std::shared_ptr<Dialog> get(const std::string &key) = 0;
};

class Dialogs : public IDialogs {
public:
    /**
     * @param twoDas the tables TSL's alien speech is looked up in
     * @param game the title the dialogs are read for
     */
    Dialogs(Gffs &gffs, Strings &strings, TwoDAs &twoDas, GameID game) :
        _gffs(gffs),
        _strings(strings),
        _twoDas(twoDas),
        _game(game) {
    }

    void clear() override {
        _objects.clear();
    }

    std::shared_ptr<Dialog> get(const std::string &key) override {
        auto maybeObject = _objects.find(key);
        if (maybeObject != _objects.end()) {
            return maybeObject->second;
        }
        auto object = doGet(key);
        return _objects.insert(make_pair(key, std::move(object))).first->second;
    }

private:
    /** TSL: the tables alien speech is looked up in. */
    struct AlienSpeechTables {
        std::shared_ptr<TwoDA> emotions;
        std::shared_ptr<TwoDA> voices;
    };

    /** TSL: the tables a node's alien speech is looked up in, and the race of the dialog's owner. */
    struct AlienSpeech {
        AlienSpeechTables tables;
        int ownerRace {0};
    };

    Gffs &_gffs;
    Strings &_strings;
    TwoDAs &_twoDas;
    GameID _game;
    /** TSL: the alien speech tables, read with the first dialog. */
    std::optional<AlienSpeechTables> _alienSpeechTables;

    std::unordered_map<std::string, std::shared_ptr<Dialog>> _objects;

    std::shared_ptr<Dialog> doGet(std::string resRef);

    std::unique_ptr<Dialog> loadDialog(const generated::DLG &dlg);
    /** TSL: the alien speech tables, read once. */
    const AlienSpeechTables &alienSpeechTables();

    Dialog::EntryReplyLink getEntryReplyLink(const generated::DLG_EntryReplyList_EntriesRepliesList &dlg) const;
    /**
     * @param alienSpeech TSL: where the node's sound is looked up; KotOR
     *        reads the node's own Sound field instead
     */
    Dialog::EntryReply getEntryReply(const generated::DLG_EntryReplyList &dlg, const std::optional<AlienSpeech> &alienSpeech) const;
    /**
     * TSL: the alien speech of \p dlg, from the alien voice table's row for
     * its race and the column its emotion names; empty when there is none.
     */
    std::string getAlienSpeech(const generated::DLG_EntryReplyList &dlg, const AlienSpeech &alienSpeech) const;
    Dialog::Stunt getStunt(const generated::DLG_StuntList &dlg) const;
    Dialog::ParticipantAnimation getParticipantAnimation(const generated::DLG_EntryReplyList_AnimList &dlg) const;
};

} // namespace resource

} // namespace reone
