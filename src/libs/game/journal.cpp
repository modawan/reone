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

#include "reone/game/journal.h"

#include "reone/resource/gff.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/strings.h"
#include "reone/system/logutil.h"

using namespace reone::resource;

namespace reone {

namespace game {

static const char kGlobalJournalResRef[] = "global";

void Journal::reset() {
    _quests.clear();
    _jrl.categories.clear();
    _loaded = false;
}

void Journal::ensureLoaded() {
    if (_loaded) {
        return;
    }
    _loaded = true;

    std::shared_ptr<Gff> jrl(_gffs.get(kGlobalJournalResRef, ResType::Jrl));
    if (!jrl) {
        warn("Journal: global journal resource not found");
        return;
    }
    _jrl = parseJRL(*jrl);
}

Journal::Quest *Journal::findQuest(const std::string &plotId) {
    for (auto &quest : _quests) {
        if (boost::iequals(quest.plotId, plotId)) {
            return &quest;
        }
    }
    return nullptr;
}

const JRL::Category *Journal::findCategory(const std::string &plotId) {
    ensureLoaded();
    for (const auto &category : _jrl.categories) {
        if (boost::iequals(category.tag, plotId)) {
            return &category;
        }
    }
    return nullptr;
}

const JRL::Entry *Journal::findEntry(const std::string &plotId, int state) {
    const JRL::Category *category = findCategory(plotId);
    if (!category) {
        return nullptr;
    }
    for (const auto &entry : category->entries) {
        if (entry.id == static_cast<uint32_t>(state)) {
            return &entry;
        }
    }
    return nullptr;
}

bool Journal::addEntry(const std::string &plotId, int state, bool allowOverrideHigher) {
    if (plotId.empty()) {
        return false;
    }
    if (!findCategory(plotId)) {
        warn("Journal: plot ID not found in global journal: " + plotId);
    } else if (!findEntry(plotId, state)) {
        warn(str(boost::format("Journal: entry %d not found in category '%s'") % state % plotId));
    }

    Quest *quest = findQuest(plotId);
    if (!quest) {
        _quests.push_back({plotId, state, 0, 0});
        return true;
    }
    if (quest->state == state) {
        return false;
    }
    if (state < quest->state && !allowOverrideHigher) {
        return false;
    }
    quest->state = state;
    return true;
}

bool Journal::removeEntry(const std::string &plotId) {
    for (auto it = _quests.begin(); it != _quests.end(); ++it) {
        if (boost::iequals(it->plotId, plotId)) {
            _quests.erase(it);
            return true;
        }
    }
    return false;
}

void Journal::restoreEntry(std::string plotId, int state, uint32_t date, uint32_t time) {
    Quest *quest = findQuest(plotId);
    if (quest) {
        quest->state = state;
        quest->date = date;
        quest->time = time;
        return;
    }
    _quests.push_back({std::move(plotId), state, date, time});
}

int Journal::getEntryState(const std::string &plotId) const {
    for (const auto &quest : _quests) {
        if (boost::iequals(quest.plotId, plotId)) {
            return quest.state;
        }
    }
    return 0;
}

bool Journal::isCompleted(const std::string &plotId) {
    const Quest *quest = findQuest(plotId);
    if (!quest) {
        return false;
    }
    const JRL::Entry *entry = findEntry(plotId, quest->state);
    return entry && entry->end;
}

std::string Journal::resolveLocText(const JRL::LocText &text) {
    if (text.first != -1) {
        return _strings.getText(text.first);
    }
    return text.second;
}

std::string Journal::getQuestName(const std::string &plotId) {
    const JRL::Category *category = findCategory(plotId);
    if (!category) {
        return "";
    }
    return resolveLocText(category->name);
}

std::string Journal::getEntryText(const std::string &plotId, int state) {
    const JRL::Entry *entry = findEntry(plotId, state);
    if (!entry) {
        return "";
    }
    return resolveLocText(entry->text);
}

} // namespace game

} // namespace reone
