/*
 * Copyright (c) 2025 The reone project contributors
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

#include "reone/system/cast.h"

#include <memory>

using namespace reone;

enum class ObjectType {
    Creature,
    SubCreature,
    Item,
};

class Object {
public:
    Object(ObjectType type) :
        _type(type) {}

    static bool classof(Object *from) {
        return true;
    }

    ObjectType type() const { return _type; }

private:
    ObjectType _type;
};

class Creature : public Object {
public:
    Creature() :
        Object(ObjectType::Creature) {}
    Creature(ObjectType type) :
        Object(type) {}

    static bool classof(Object *from) {
        return from->type() == ObjectType::Creature
            || from->type() == ObjectType::SubCreature;
    }

    int creatureVar {42};
};

class SubCreature : public Creature {
public:
    SubCreature() :
        Creature(ObjectType::SubCreature) {}

    static bool classof(Object *from) {
        return from->type() == ObjectType::SubCreature;
    }

    int subcreatureVar {43};
};

class Item : public Object {
public:
    Item() :
        Object(ObjectType::Item) {}

    static bool classof(Object *from) {
        return from->type() == ObjectType::Item;
    }

    int itemVar {44};
};

TEST(Cast, isa) {
    Creature realCreature;
    SubCreature realSubCreature;
    Item realItem;

    Object *creature = &realCreature;
    Object *subcreature = &realSubCreature;
    Object *item = &realItem;

    std::shared_ptr<Object> creaturePtr = std::make_shared<Creature>();
    std::shared_ptr<Object> subcreaturePtr = std::make_shared<SubCreature>();
    std::shared_ptr<Object> itemPtr = std::make_shared<Item>();

    Object &creatureRef = realCreature;
    Object &subcreatureRef = realSubCreature;
    Object &itemRef = realItem;

    // isa<Object> is always true
    EXPECT_TRUE(isa<Object>(creature));
    EXPECT_TRUE(isa<Object>(subcreature));
    EXPECT_TRUE(isa<Object>(item));

    EXPECT_TRUE(isa<Object>(creatureRef));
    EXPECT_TRUE(isa<Object>(subcreatureRef));
    EXPECT_TRUE(isa<Object>(itemRef));

    EXPECT_TRUE(isa<Object>(creaturePtr));
    EXPECT_TRUE(isa<Object>(subcreaturePtr));
    EXPECT_TRUE(isa<Object>(itemPtr));

    // isa<Creature> is only true for Creature and SubCreature
    EXPECT_TRUE(isa<Creature>(creature));
    EXPECT_TRUE(isa<Creature>(subcreature));
    EXPECT_FALSE(isa<Creature>(item));

    EXPECT_TRUE(isa<Creature>(creatureRef));
    EXPECT_TRUE(isa<Creature>(subcreatureRef));
    EXPECT_FALSE(isa<Creature>(itemRef));

    EXPECT_TRUE(isa<Creature>(creaturePtr));
    EXPECT_TRUE(isa<Creature>(subcreaturePtr));
    EXPECT_FALSE(isa<Creature>(itemPtr));

    // isa<SubCreature> is only true for SubCreature
    EXPECT_FALSE(isa<SubCreature>(creature));
    EXPECT_TRUE(isa<SubCreature>(subcreature));
    EXPECT_FALSE(isa<SubCreature>(item));

    EXPECT_FALSE(isa<SubCreature>(creatureRef));
    EXPECT_TRUE(isa<SubCreature>(subcreatureRef));
    EXPECT_FALSE(isa<SubCreature>(itemRef));

    EXPECT_FALSE(isa<SubCreature>(creaturePtr));
    EXPECT_TRUE(isa<SubCreature>(subcreaturePtr));
    EXPECT_FALSE(isa<SubCreature>(itemPtr));

    // isa<Item> is only true for Item
    EXPECT_FALSE(isa<Item>(creature));
    EXPECT_FALSE(isa<Item>(subcreature));
    EXPECT_TRUE(isa<Item>(item));

    EXPECT_FALSE(isa<Item>(creatureRef));
    EXPECT_FALSE(isa<Item>(subcreatureRef));
    EXPECT_TRUE(isa<Item>(itemRef));

    EXPECT_FALSE(isa<Item>(creaturePtr));
    EXPECT_FALSE(isa<Item>(subcreaturePtr));
    EXPECT_TRUE(isa<Item>(itemPtr));
}

TEST(Cast, cast) {
    Creature realCreature;
    SubCreature realSubCreature;
    Item realItem;

    Object *creature = &realCreature;
    Object *subcreature = &realSubCreature;
    Object *item = &realItem;

    std::shared_ptr<Object> creaturePtr = std::make_shared<Creature>();
    std::shared_ptr<Object> subcreaturePtr = std::make_shared<SubCreature>();
    std::shared_ptr<Object> itemPtr = std::make_shared<Item>();

    Object &creatureRef = realCreature;
    Object &subcreatureRef = realSubCreature;
    Object &itemRef = realItem;

    EXPECT_EQ(42, cast<Creature>(creature)->creatureVar);
    EXPECT_EQ(42, cast<Creature>(subcreature)->creatureVar);
    EXPECT_EQ(43, cast<SubCreature>(subcreature)->subcreatureVar);
    EXPECT_EQ(44, cast<Item>(item)->itemVar);

    EXPECT_EQ(42, cast<Creature>(creaturePtr)->creatureVar);
    EXPECT_EQ(42, cast<Creature>(subcreaturePtr)->creatureVar);
    EXPECT_EQ(43, cast<SubCreature>(subcreaturePtr)->subcreatureVar);
    EXPECT_EQ(44, cast<Item>(itemPtr)->itemVar);

    EXPECT_EQ(42, cast<Creature>(creatureRef).creatureVar);
    EXPECT_EQ(42, cast<Creature>(subcreatureRef).creatureVar);
    EXPECT_EQ(43, cast<SubCreature>(subcreatureRef).subcreatureVar);
    EXPECT_EQ(44, cast<Item>(itemRef).itemVar);
}

TEST(Cast, dyn_cast) {
    Creature realCreature;
    SubCreature realSubCreature;
    Item realItem;

    Object *creature = &realCreature;
    Object *subcreature = &realSubCreature;
    Object *item = &realItem;

    std::shared_ptr<Object> creaturePtr = std::make_shared<Creature>();
    std::shared_ptr<Object> subcreaturePtr = std::make_shared<SubCreature>();
    std::shared_ptr<Object> itemPtr = std::make_shared<Item>();

    Object &creatureRef = realCreature;
    Object &subcreatureRef = realSubCreature;
    Object &itemRef = realItem;

    // dyn_cast<Object> is always valid
    EXPECT_NE(dyn_cast<Object>(creature), nullptr);
    EXPECT_NE(dyn_cast<Object>(subcreature), nullptr);
    EXPECT_NE(dyn_cast<Object>(item), nullptr);

    EXPECT_NE(dyn_cast<Object>(creaturePtr), nullptr);
    EXPECT_NE(dyn_cast<Object>(subcreaturePtr), nullptr);
    EXPECT_NE(dyn_cast<Object>(itemPtr), nullptr);

    // dyn_cast<Creature> is only valid for Creature and SubCreature
    EXPECT_NE(dyn_cast<Creature>(creature), nullptr);
    EXPECT_NE(dyn_cast<Creature>(subcreature), nullptr);
    EXPECT_EQ(dyn_cast<Creature>(item), nullptr);

    EXPECT_NE(dyn_cast<Creature>(creaturePtr), nullptr);
    EXPECT_NE(dyn_cast<Creature>(subcreaturePtr), nullptr);
    EXPECT_EQ(dyn_cast<Creature>(itemPtr), nullptr);

    // dyn_cast<SubCreature> is only valid for SubCreature
    EXPECT_EQ(dyn_cast<SubCreature>(creature), nullptr);
    EXPECT_NE(dyn_cast<SubCreature>(subcreature), nullptr);
    EXPECT_EQ(dyn_cast<SubCreature>(item), nullptr);

    EXPECT_EQ(dyn_cast<SubCreature>(creaturePtr), nullptr);
    EXPECT_NE(dyn_cast<SubCreature>(subcreaturePtr), nullptr);
    EXPECT_EQ(dyn_cast<SubCreature>(itemPtr), nullptr);

    // dyn_cast<Item> is only valid for Item
    EXPECT_EQ(dyn_cast<Item>(creature), nullptr);
    EXPECT_EQ(dyn_cast<Item>(subcreature), nullptr);
    EXPECT_NE(dyn_cast<Item>(item), nullptr);

    EXPECT_EQ(dyn_cast<Item>(creaturePtr), nullptr);
    EXPECT_EQ(dyn_cast<Item>(subcreaturePtr), nullptr);
    EXPECT_NE(dyn_cast<Item>(itemPtr), nullptr);

    // Check valid casts
    EXPECT_EQ(42, dyn_cast<Creature>(creature)->creatureVar);
    EXPECT_EQ(42, dyn_cast<Creature>(subcreature)->creatureVar);
    EXPECT_EQ(43, dyn_cast<SubCreature>(subcreature)->subcreatureVar);
    EXPECT_EQ(44, dyn_cast<Item>(item)->itemVar);

    EXPECT_EQ(42, dyn_cast<Creature>(creaturePtr)->creatureVar);
    EXPECT_EQ(42, dyn_cast<Creature>(subcreaturePtr)->creatureVar);
    EXPECT_EQ(43, dyn_cast<SubCreature>(subcreaturePtr)->subcreatureVar);
    EXPECT_EQ(44, dyn_cast<Item>(itemPtr)->itemVar);
}
