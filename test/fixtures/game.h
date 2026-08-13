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

#include <gmock/gmock.h>

#include "reone/game/di/services.h"
#include "reone/system/exception/notimplemented.h"

#include "reone/game/animations.h"
#include "reone/game/camerastyles.h"
#include "reone/game/d20/classes.h"
#include "reone/game/d20/feats.h"
#include "reone/game/d20/skills.h"
#include "reone/game/d20/spells.h"
#include "reone/game/footstepsounds.h"
#include "reone/game/gui/sounds.h"
#include "reone/game/options.h"
#include "reone/game/pazaaksession.h"
#include "reone/game/portraits.h"
#include "reone/game/projectiles.h"
#include "reone/game/reputes.h"
#include "reone/game/surfaces.h"
#include "reone/game/types.h"
#include "reone/game/visualeffects.h"

#include "reone/game/console.h"

namespace reone {

namespace resource {

class Gff;

}

namespace game {

class Game;
class Conversation;
class Object;

class MockCameraStyles : public ICameraStyles, boost::noncopyable {
public:
    MOCK_METHOD(std::shared_ptr<CameraStyle>, get, (int index), (const override));
    MOCK_METHOD(std::shared_ptr<CameraStyle>, get, (const std::string &name), (const override));
};

class MockClasses : public IClasses, boost::noncopyable {
public:
    MOCK_METHOD(void, clear, (), (override));
    MOCK_METHOD(std::shared_ptr<CreatureClass>, get, (ClassType key), (override));
};

class MockFeats : public IFeats, boost::noncopyable {
public:
    MOCK_METHOD(void, init, (), (override));
    MOCK_METHOD(std::shared_ptr<Feat>, get, (FeatType type), (const override));
    MOCK_METHOD(int, getLevelUpChoiceCount, (const CreatureAttributes &attributes, const CreatureClass &clazz), (const override));
    MOCK_METHOD(bool, isLevelUpCandidate, (FeatType type, const CreatureAttributes &attributes, const CreatureClass &clazz), (const override));
    MOCK_METHOD(std::vector<FeatType>, getLevelUpCandidates, (const CreatureAttributes &attributes, const CreatureClass &clazz), (const override));
    MOCK_METHOD(std::vector<FeatDisplayEntry>, getLevelUpDisplayEntries, (const CreatureAttributes &attributes, const CreatureClass &clazz), (const override));
};

class MockFootstepSounds : public IFootstepSounds, boost::noncopyable {
public:
    MOCK_METHOD(void, clear, (), (override));
    MOCK_METHOD(std::shared_ptr<FootstepTypeSounds>, get, (uint32_t key), (override));
};

class MockGUISounds : public IGUISounds, boost::noncopyable {
public:
    MOCK_METHOD(std::shared_ptr<audio::AudioClip>, getOnClick, (), (const override));
    MOCK_METHOD(std::shared_ptr<audio::AudioClip>, getOnEnter, (), (const override));
    MOCK_METHOD(std::shared_ptr<audio::AudioClip>, getOnLevelUpNotify, (), (const override));
};

class MockPortraits : public IPortraits, boost::noncopyable {
public:
    MOCK_METHOD(std::shared_ptr<graphics::Texture>, getTextureByIndex, (int index), (const override));
    MOCK_METHOD(std::shared_ptr<graphics::Texture>, getTextureByAppearance, (int appearance), (const override));
    MOCK_METHOD(const std::vector<Portrait> &, portraits, (), (const override));
};

class MockReputes : public IReputes, boost::noncopyable {
public:
    MOCK_METHOD(State, baseState, (), (const override));
    MOCK_METHOD(std::optional<State>, parse, (const resource::Gff &gff), (const override));
    MOCK_METHOD(void, replace, (State state), (override));
    MOCK_METHOD(int, getReputation, (Faction sourceFaction, Faction targetFaction), (const override));
    MOCK_METHOD(void, adjustReputation, (Faction sourceFaction, Faction targetFaction, int adjustment), (override));
    MOCK_METHOD(bool, getIsEnemy, (const Creature &source, const Creature &target), (const override));
    MOCK_METHOD(bool, getIsEnemy, (Faction sourceFaction, Faction targetFaction), (const override));
    MOCK_METHOD(bool, getIsFriend, (const Creature &source, const Creature &target), (const override));
    MOCK_METHOD(bool, getIsNeutral, (const Creature &source, const Creature &target), (const override));
};

class MockSkills : public ISkills, boost::noncopyable {
public:
    MOCK_METHOD(std::shared_ptr<Skill>, get, (SkillType type), (const override));
};

class MockSpells : public ISpells, boost::noncopyable {
public:
    MOCK_METHOD(std::shared_ptr<Spell>, get, (SpellType type), (const override));
    MOCK_METHOD(bool, isLevelUpCandidate, (SpellType type, const CreatureAttributes &attributes, const CreatureClass &clazz, const std::set<SpellType> &chosen), (const override));
    MOCK_METHOD(std::vector<SpellType>, getLevelUpCandidates, (const CreatureAttributes &attributes, const CreatureClass &clazz, const std::set<SpellType> &chosen), (const override));
    MOCK_METHOD(std::vector<SpellDisplayEntry>, getLevelUpDisplayEntries, (const CreatureAttributes &attributes, const CreatureClass &clazz, const std::set<SpellType> &chosen), (const override));
};

class MockSurfaces : public ISurfaces, boost::noncopyable {
public:
    MOCK_METHOD(bool, isWalkable, (int index), (const override));
    MOCK_METHOD(const Surface &, getSurface, (int index), (const override));

    MOCK_METHOD(std::set<uint32_t>, getGrassSurfaces, (), (const override));
    MOCK_METHOD(std::set<uint32_t>, getWalkableSurfaces, (), (const override));
    MOCK_METHOD(std::set<uint32_t>, getWalkcheckSurfaces, (), (const override));
    MOCK_METHOD(std::set<uint32_t>, getLineOfSightSurfaces, (), (const override));
};

class MockProjectiles : public IProjectiles, boost::noncopyable {
public:
    MOCK_METHOD(void, clear, (), (override));
    MOCK_METHOD(ProjectileSpec *, get, (ProjectileAttackType attack, CreatureWieldType wield, int appearance), (override));
};

class MockAnimations : public IAnimations, boost::noncopyable {
public:
    MOCK_METHOD(void, clear, (), (override));
    MOCK_METHOD(std::string, getNameById, (uint32_t id), (const override));
    MOCK_METHOD(std::string, getAttackResult, (std::string attackAnim, CreatureWieldType targetWield, AttackResultType result), (const override));
};

class MockVisualEffects : public IVisualEffects, boost::noncopyable {
public:
    MOCK_METHOD(std::optional<const VisualEffectDesc *>, get, (uint32_t id), (const override));
};

class TestGameModule : boost::noncopyable {
public:
    static std::pair<std::string, std::string> scheduledTransition(const Game &game);
    static void configurePazaak(
        Game &game,
        bool guiLoadSucceeds,
        PazaakSession::HandSelector playerSelector,
        PazaakSession::HandSelector opponentSelector,
        PazaakSession::MainDeckFactory mainDeckFactory,
        std::function<void(const std::string &, uint32_t)> continuation);
    static void setCurrentScreen(Game &game, int screen);
    static void initConsole(Game &game);
    static void setActiveModule(Game &game, bool active);
    static void cacheActiveModule(Game &game, std::string name);
    static size_t objectRegistrySize(const Game &game);
    static size_t loadedModuleCount(const Game &game);
    static uint32_t nextObjectId(const Game &game);
    static uint64_t runtimeSessionGeneration(const Game &game);
    static void bindConversation(Game &game, Conversation &conversation);
    static bool hasConversation(const Game &game);
    static void bindHUDSelection(Game &game, std::shared_ptr<Object> object);
    static bool hasHUDSelection(const Game &game);
    static void setPazaakDevelopmentSelectedObject(
        Game &game,
        std::shared_ptr<Object> object);
    static void removeObject(Game &game, uint32_t objectId);
    static void useRuntimePazaakGUIs(Game &game);
    static void useAuthoredPazaakDecks(Game &game);
    static void finishPazaak(Game &game, PazaakCompletedResult result);
    static void serializePazaakPartyTable(const Game &game, resource::Gff &gff);
    static void deserializePartyTable(Game &game, resource::Gff &gff);
    static void publishPartyRuntimeState(
        Game &game,
        resource::Gff &ifoGff,
        const std::shared_ptr<resource::Gff> &ptGff,
        const std::shared_ptr<resource::Gff> &pcGff);
    static void deserializeAvailableNpcs(Game &game);
    static void deserializeInventory(Game &game, resource::Gff &gff);
    static void deserializeCustomTokens(Game &game, const resource::Gff &gff);
    static void deserializeGlobalVariables(Game &game, resource::Gff &gff);
    static void replaceJournal(Game &game, const resource::Gff &gff);
    static void replaceInventory(Game &game, resource::Gff &gff);

    void init() {
        _cameraStyles = std::make_unique<MockCameraStyles>();
        _classes = std::make_unique<MockClasses>();
        _feats = std::make_unique<MockFeats>();
        _footstepSounds = std::make_unique<MockFootstepSounds>();
        _guiSounds = std::make_unique<MockGUISounds>();
        _portraits = std::make_unique<MockPortraits>();
        _reputes = std::make_unique<MockReputes>();
        _skills = std::make_unique<MockSkills>();
        _spells = std::make_unique<MockSpells>();
        _surfaces = std::make_unique<MockSurfaces>();
        _projectiles = std::make_unique<MockProjectiles>();
        _animations = std::make_unique<MockAnimations>();
        _visualEffects = std::make_unique<MockVisualEffects>();

        _services = std::make_unique<GameServices>(
            *_cameraStyles,
            *_classes,
            *_feats,
            *_footstepSounds,
            *_guiSounds,
            *_portraits,
            *_reputes,
            *_skills,
            *_spells,
            *_surfaces,
            *_projectiles,
            *_animations,
            *_visualEffects);
    }

    GameServices &services() {
        return *_services;
    }

    MockSpells &spells() { return *_spells; }

private:
    std::unique_ptr<MockCameraStyles> _cameraStyles;
    std::unique_ptr<MockClasses> _classes;
    std::unique_ptr<MockFeats> _feats;
    std::unique_ptr<MockFootstepSounds> _footstepSounds;
    std::unique_ptr<MockGUISounds> _guiSounds;
    std::unique_ptr<MockPortraits> _portraits;
    std::unique_ptr<MockReputes> _reputes;
    std::unique_ptr<MockSkills> _skills;
    std::unique_ptr<MockSpells> _spells;
    std::unique_ptr<MockSurfaces> _surfaces;
    std::unique_ptr<MockProjectiles> _projectiles;
    std::unique_ptr<MockAnimations> _animations;
    std::unique_ptr<MockVisualEffects> _visualEffects;

    std::unique_ptr<GameServices> _services;
};

class StubConsole : public IConsole, boost::noncopyable {
public:
    struct RegisteredCommand {
        std::string description;
        CommandHandler handler;
    };

    void registerCommand(
        std::string name,
        std::string description,
        CommandHandler handler) override {

        commands.emplace(
            std::move(name),
            RegisteredCommand {std::move(description), std::move(handler)});
    }

    void printLine(const std::string &text) override {
        lines.push_back(text);
    }

    bool hasCommand(const std::string &name) const {
        return commands.find(name) != commands.end();
    }

    void execute(
        const std::string &name,
        std::vector<std::string> arguments = {}) {

        auto found = commands.find(name);
        if (found == commands.end()) {
            throw std::runtime_error("Command is not registered: " + name);
        }
        ConsoleArgs::TokenList tokens {name};
        tokens.insert(
            tokens.end(),
            arguments.begin(),
            arguments.end());
        found->second.handler(ConsoleArgs(std::move(tokens)));
    }

    std::map<std::string, RegisteredCommand> commands;
    std::vector<std::string> lines;
};

} // namespace game

} // namespace reone
