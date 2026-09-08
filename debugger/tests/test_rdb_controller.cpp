#include "rdb_controller.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name) \
    static void test_##name(); \
    struct Register_##name { \
        Register_##name() { \
            std::cout << "  " #name "..."; \
            try { \
                test_##name(); \
                std::cout << " PASS" << std::endl; \
                testsPassed++; \
            } catch (const std::exception &e) { \
                std::cout << " FAIL: " << e.what() << std::endl; \
                testsFailed++; \
            } catch (...) { \
                std::cout << " FAIL (unknown exception)" << std::endl; \
                testsFailed++; \
            } \
        } \
    } register_##name; \
    static void test_##name()

#define ASSERT(cond) \
    do { if (!(cond)) throw std::runtime_error("Assertion failed: " #cond); } while(0)

#define ASSERT_EQ(a, b) \
    do { if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); } while(0)

static const char *TEST_RDB_PATH = "/tmp/test_rdb_controller.rdb";
static const char *TEST_RDB_TMP  = "/tmp/test_rdb_controller_tmp.rdb";

static void cleanupTestFiles()
{
    ::remove(TEST_RDB_PATH);
    ::remove((std::string(TEST_RDB_PATH) + ".tmp").c_str());
    ::remove(TEST_RDB_TMP);
}

static RdbObject makeTestObject(uint16_t addr, const std::string &name,
                                 RdbObjectType type = RdbObjectType::Function)
{
    RdbObject obj;
    obj.address = addr;
    obj.type = type;
    obj.name = name;
    return obj;
}

// ---------------------------------------------------------------------------
// Basic lifecycle tests
// ---------------------------------------------------------------------------

TEST(create_empty)
{
    RdbController ctrl;
    ASSERT(!ctrl.isLoaded());
    ASSERT(!ctrl.isDirty());
    ASSERT(ctrl.getPath().empty());
    ASSERT_EQ(ctrl.objectCount(), (size_t)0);
    ASSERT_EQ(ctrl.getPlatform(), std::string("vector06c"));
    ASSERT_EQ(ctrl.getVersion(), 1);
}

TEST(initialize)
{
    RdbController ctrl;
    RdbRomIdentity rom;
    rom.file = "test.rom";
    rom.size = 32768;
    rom.sha256 = "abc123";
    ctrl.initialize("vector06c", rom);

    ASSERT(ctrl.isLoaded());
    ASSERT(!ctrl.isDirty());
    ASSERT_EQ(ctrl.getPlatform(), std::string("vector06c"));
    ASSERT(ctrl.getRomIdentity() == rom);
    ASSERT(!ctrl.existsOnDisk());
}

TEST(close_resets_state)
{
    RdbController ctrl;
    ctrl.addObject(makeTestObject(0x1000, "test"));
    ASSERT(ctrl.isDirty());

    ctrl.close();
    ASSERT(!ctrl.isLoaded());
    ASSERT(!ctrl.isDirty());
    ASSERT_EQ(ctrl.objectCount(), (size_t)0);
}

// ---------------------------------------------------------------------------
// Object CRUD tests
// ---------------------------------------------------------------------------

TEST(add_object)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    RdbObject obj = makeTestObject(0x1234, "draw_sprite");
    obj.size = 47;
    obj.hasSize = true;
    obj.comment = "Draws a sprite";

    ASSERT(ctrl.addObject(obj));
    ASSERT(ctrl.isDirty());
    ASSERT_EQ(ctrl.objectCount(), (size_t)1);

    const RdbObject *got = ctrl.getObject(0x1234);
    ASSERT(got != nullptr);
    ASSERT_EQ(got->address, (uint16_t)0x1234);
    ASSERT_EQ(got->name, std::string("draw_sprite"));
    ASSERT_EQ(got->type, RdbObjectType::Function);
    ASSERT_EQ(got->size, (uint32_t)47);
    ASSERT(got->hasSize);
    ASSERT_EQ(got->comment, std::string("Draws a sprite"));
}

TEST(add_duplicate_fails)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ASSERT(ctrl.addObject(makeTestObject(0x1000, "foo")));
    ASSERT(!ctrl.addObject(makeTestObject(0x1000, "bar")));
    ASSERT_EQ(ctrl.objectCount(), (size_t)1);
}

TEST(update_object)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ctrl.addObject(makeTestObject(0x1000, "foo"));
    ctrl.save();  // reset dirty by saving (will fail silently, no path)

    // Actually test update changes
    RdbObject updated = makeTestObject(0x1000, "bar");
    ASSERT(ctrl.updateObject(updated));
    ASSERT_EQ(ctrl.getObject(0x1000)->name, std::string("bar"));
}

TEST(update_nonexistent_fails)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ASSERT(!ctrl.updateObject(makeTestObject(0x9999, "nope")));
}

TEST(update_same_data_no_dirty)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    RdbObject obj = makeTestObject(0x1000, "foo");
    ctrl.addObject(obj);
    // Force dirty to false via reload-like mechanism
    // We can't easily do this without save/load, so test via saveAs
    ctrl.saveAs(TEST_RDB_PATH);
    ASSERT(!ctrl.isDirty());

    // Update with identical data
    ASSERT(ctrl.updateObject(obj));
    ASSERT(!ctrl.isDirty());  // no change → no dirty
}

TEST(remove_object)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ctrl.addObject(makeTestObject(0x1000, "foo"));
    ASSERT(ctrl.removeObject(0x1000));
    ASSERT(ctrl.isDirty());
    ASSERT_EQ(ctrl.objectCount(), (size_t)0);
    ASSERT(ctrl.getObject(0x1000) == nullptr);
}

TEST(remove_nonexistent_fails)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ASSERT(!ctrl.removeObject(0x9999));
}

TEST(find_by_name)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ctrl.addObject(makeTestObject(0x1000, "alpha"));
    ctrl.addObject(makeTestObject(0x2000, "beta"));

    const RdbObject *found = ctrl.findObject("beta");
    ASSERT(found != nullptr);
    ASSERT_EQ(found->address, (uint16_t)0x2000);

    ASSERT(ctrl.findObject("gamma") == nullptr);
}

TEST(list_objects_sorted)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ctrl.addObject(makeTestObject(0x3000, "c"));
    ctrl.addObject(makeTestObject(0x1000, "a"));
    ctrl.addObject(makeTestObject(0x2000, "b"));

    auto list = ctrl.listObjects();
    ASSERT_EQ(list.size(), (size_t)3);
    ASSERT_EQ(list[0].address, (uint16_t)0x1000);
    ASSERT_EQ(list[1].address, (uint16_t)0x2000);
    ASSERT_EQ(list[2].address, (uint16_t)0x3000);
}

// ---------------------------------------------------------------------------
// Comment tests
// ---------------------------------------------------------------------------

TEST(set_get_comment)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ctrl.addObject(makeTestObject(0x1000, "foo"));
    ASSERT(ctrl.setComment(0x1000, "Hello world"));
    ASSERT(ctrl.isDirty());
    ASSERT_EQ(ctrl.getComment(0x1000), std::string("Hello world"));
}

TEST(comment_no_change_no_dirty)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ctrl.addObject(makeTestObject(0x1000, "foo"));
    ctrl.setComment(0x1000, "test");
    ctrl.saveAs(TEST_RDB_PATH);
    ASSERT(!ctrl.isDirty());

    // Set same comment
    ASSERT(ctrl.setComment(0x1000, "test"));
    ASSERT(!ctrl.isDirty());
}

TEST(comment_nonexistent_fails)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ASSERT(!ctrl.setComment(0x9999, "nope"));
    ASSERT_EQ(ctrl.getComment(0x9999), std::string(""));
}

// ---------------------------------------------------------------------------
// Property tests
// ---------------------------------------------------------------------------

TEST(set_get_property)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ctrl.addObject(makeTestObject(0x1000, "foo"));
    ASSERT(ctrl.setProperty(0x1000, "color", RdbPropertyValue::fromString("red")));
    ASSERT(ctrl.isDirty());

    const RdbPropertyValue *val = ctrl.getProperty(0x1000, "color");
    ASSERT(val != nullptr);
    ASSERT_EQ(val->stringValue, std::string("red"));
}

TEST(remove_property)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ctrl.addObject(makeTestObject(0x1000, "foo"));
    ctrl.setProperty(0x1000, "key", RdbPropertyValue::fromInt(42));
    ASSERT(ctrl.removeProperty(0x1000, "key"));
    ASSERT(ctrl.getProperty(0x1000, "key") == nullptr);
}

TEST(property_nonexistent_object_fails)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ASSERT(!ctrl.setProperty(0x9999, "key", RdbPropertyValue::fromInt(1)));
    ASSERT(ctrl.getProperty(0x9999, "key") == nullptr);
}

// ---------------------------------------------------------------------------
// Link tests
// ---------------------------------------------------------------------------

TEST(add_link)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ctrl.addObject(makeTestObject(0x1000, "A"));
    ctrl.addObject(makeTestObject(0x2000, "B"));

    ASSERT(ctrl.addLink(0x1000, 0x2000));
    ASSERT(ctrl.isDirty());

    auto links = ctrl.getLinks(0x1000);
    ASSERT_EQ(links.size(), (size_t)1);
    ASSERT_EQ(links[0], (uint16_t)0x2000);
}

TEST(duplicate_link_no_dirty)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ctrl.addObject(makeTestObject(0x1000, "A"));
    ctrl.addLink(0x1000, 0x2000);
    ctrl.saveAs(TEST_RDB_PATH);
    ASSERT(!ctrl.isDirty());

    // Adding same link again
    ASSERT(ctrl.addLink(0x1000, 0x2000));
    ASSERT(!ctrl.isDirty());
}

TEST(remove_link)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ctrl.addObject(makeTestObject(0x1000, "A"));
    ctrl.addLink(0x1000, 0x2000);
    ASSERT(ctrl.removeLink(0x1000, 0x2000));
    ASSERT(ctrl.isDirty());
    ASSERT_EQ(ctrl.getLinks(0x1000).size(), (size_t)0);
}

TEST(remove_nonexistent_link_fails)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ctrl.addObject(makeTestObject(0x1000, "A"));
    ASSERT(!ctrl.removeLink(0x1000, 0x9999));
}

TEST(set_links)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ctrl.addObject(makeTestObject(0x1000, "A"));
    ASSERT(ctrl.setLinks(0x1000, {0x2000, 0x3000, 0x4000}));

    auto links = ctrl.getLinks(0x1000);
    ASSERT_EQ(links.size(), (size_t)3);
}

TEST(set_links_deduplicates)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ctrl.addObject(makeTestObject(0x1000, "A"));
    ASSERT(ctrl.setLinks(0x1000, {0x2000, 0x2000, 0x3000}));

    auto links = ctrl.getLinks(0x1000);
    ASSERT_EQ(links.size(), (size_t)2);
}

TEST(has_link)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ctrl.addObject(makeTestObject(0x1000, "A"));
    ctrl.addLink(0x1000, 0x2000);

    ASSERT(ctrl.hasLink(0x1000, 0x2000));
    ASSERT(!ctrl.hasLink(0x1000, 0x3000));
    ASSERT(!ctrl.hasLink(0x9999, 0x2000));
}

TEST(links_are_directed)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ctrl.addObject(makeTestObject(0x1000, "A"));
    ctrl.addObject(makeTestObject(0x2000, "B"));

    ctrl.addLink(0x1000, 0x2000);

    // A → B does NOT imply B → A
    ASSERT(ctrl.hasLink(0x1000, 0x2000));
    ASSERT(!ctrl.hasLink(0x2000, 0x1000));
}

TEST(unresolved_link_allowed)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ctrl.addObject(makeTestObject(0x1000, "A"));
    // Link to 0x5000 which doesn't exist yet
    ASSERT(ctrl.addLink(0x1000, 0x5000));
    ASSERT(ctrl.hasLink(0x1000, 0x5000));
}

TEST(link_nonexistent_source_fails)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ASSERT(!ctrl.addLink(0x9999, 0x1000));
    ASSERT(ctrl.getLinks(0x9999).empty());
}

// ---------------------------------------------------------------------------
// Dirty state tests
// ---------------------------------------------------------------------------

TEST(dirty_after_add)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ASSERT(!ctrl.isDirty());
    ctrl.addObject(makeTestObject(0x1000, "foo"));
    ASSERT(ctrl.isDirty());
}

TEST(dirty_after_remove)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());

    ctrl.addObject(makeTestObject(0x1000, "foo"));
    ctrl.saveAs(TEST_RDB_PATH);
    ASSERT(!ctrl.isDirty());

    ctrl.removeObject(0x1000);
    ASSERT(ctrl.isDirty());
}

TEST(read_operations_no_dirty)
{
    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());
    ctrl.addObject(makeTestObject(0x1000, "foo"));
    ctrl.saveAs(TEST_RDB_PATH);
    ASSERT(!ctrl.isDirty());

    // All these should NOT set dirty
    ctrl.getObject(0x1000);
    ctrl.findObject("foo");
    ctrl.listObjects();
    ctrl.getComment(0x1000);
    ctrl.getProperty(0x1000, "nonexistent");
    ctrl.getRomIdentity();
    ctrl.getPlatform();
    ctrl.getVersion();
    ctrl.isDirty();
    ctrl.getLinks(0x1000);
    ctrl.hasLink(0x1000, 0x2000);
    ctrl.matchesRom(RdbRomIdentity());
    ctrl.existsOnDisk();
    ctrl.objectCount();

    ASSERT(!ctrl.isDirty());
}

// ---------------------------------------------------------------------------
// Save / Load persistence tests
// ---------------------------------------------------------------------------

TEST(save_and_reload)
{
    cleanupTestFiles();

    RdbRomIdentity rom;
    rom.file = "test.rom";
    rom.size = 16384;
    rom.sha256 = "deadbeef";

    {
        RdbController ctrl;
        ctrl.initialize("vector06c", rom);

        RdbObject obj;
        obj.address = 0x0100;
        obj.type = RdbObjectType::Function;
        obj.name = "start";
        obj.size = 42;
        obj.hasSize = true;
        obj.comment = "Entry point";
        obj.properties["calling_convention"] = RdbPropertyValue::fromString("custom");
        obj.links.push_back(0x0200);
        obj.links.push_back(0x0300);
        ctrl.addObject(obj);

        ASSERT(ctrl.saveAs(TEST_RDB_PATH));
        ASSERT(!ctrl.isDirty());
        ASSERT(ctrl.existsOnDisk());
    }

    // Reload
    {
        RdbController ctrl;
        ASSERT(ctrl.load(TEST_RDB_PATH));
        ASSERT(ctrl.isLoaded());
        ASSERT(!ctrl.isDirty());
        ASSERT(ctrl.existsOnDisk());

        ASSERT_EQ(ctrl.getPlatform(), std::string("vector06c"));
        ASSERT(ctrl.getRomIdentity() == rom);
        ASSERT_EQ(ctrl.objectCount(), (size_t)1);

        const RdbObject *obj = ctrl.getObject(0x0100);
        ASSERT(obj != nullptr);
        ASSERT_EQ(obj->name, std::string("start"));
        ASSERT_EQ(obj->type, RdbObjectType::Function);
        ASSERT_EQ(obj->size, (uint32_t)42);
        ASSERT(obj->hasSize);
        ASSERT_EQ(obj->comment, std::string("Entry point"));

        auto propIt = obj->properties.find("calling_convention");
        ASSERT(propIt != obj->properties.end());
        ASSERT_EQ(propIt->second.stringValue, std::string("custom"));

        ASSERT_EQ(obj->links.size(), (size_t)2);
        ASSERT(ctrl.hasLink(0x0100, 0x0200));
        ASSERT(ctrl.hasLink(0x0100, 0x0300));
    }

    cleanupTestFiles();
}

TEST(save_no_dirty_no_write)
{
    cleanupTestFiles();

    RdbController ctrl;
    ctrl.initialize("vector06c", RdbRomIdentity());
    // No objects, no dirty → save should succeed without creating file
    ASSERT(ctrl.saveAs(TEST_RDB_PATH));  // first save creates file
    // Now it exists. Reset dirty by reloading
    ctrl.reload();
    ASSERT(!ctrl.isDirty());

    // save() should not rewrite
    ASSERT(ctrl.save());
    ASSERT(!ctrl.isDirty());

    cleanupTestFiles();
}

TEST(save_without_dirty_returns_true)
{
    RdbController ctrl;
    // Never loaded, no path, no dirty
    ASSERT(ctrl.save());  // should return true (nothing to do)
}

TEST(load_nonexistent_fails)
{
    RdbController ctrl;
    ASSERT(!ctrl.load("/tmp/nonexistent_rdb_test_file.rdb"));
    ASSERT(!ctrl.isLoaded());
}

TEST(reload_without_load_fails)
{
    RdbController ctrl;
    ASSERT(!ctrl.reload());
}

// ---------------------------------------------------------------------------
// ROM identity tests
// ---------------------------------------------------------------------------

TEST(rom_identity_match)
{
    RdbController ctrl;
    RdbRomIdentity rom;
    rom.file = "game.rom";
    rom.size = 32768;
    rom.sha256 = "abcdef";
    ctrl.initialize("vector06c", rom);
    ctrl.saveAs(TEST_RDB_PATH);

    ctrl.reload();
    ASSERT(ctrl.matchesRom(rom));

    RdbRomIdentity other;
    other.file = "other.rom";
    other.size = 32768;
    other.sha256 = "123456";
    ASSERT(!ctrl.matchesRom(other));

    cleanupTestFiles();
}

// ---------------------------------------------------------------------------
// Object type conversion tests
// ---------------------------------------------------------------------------

TEST(object_type_roundtrip)
{
    ASSERT_EQ(std::string(rdbObjectTypeToString(RdbObjectType::Function)), std::string("function"));
    ASSERT_EQ(std::string(rdbObjectTypeToString(RdbObjectType::Variable)), std::string("variable"));
    ASSERT_EQ(std::string(rdbObjectTypeToString(RdbObjectType::Data)), std::string("data"));
    ASSERT_EQ(std::string(rdbObjectTypeToString(RdbObjectType::Unknown)), std::string("unknown"));

    ASSERT(rdbObjectTypeFromString("function") == RdbObjectType::Function);
    ASSERT(rdbObjectTypeFromString("variable") == RdbObjectType::Variable);
    ASSERT(rdbObjectTypeFromString("garbage") == RdbObjectType::Unknown);
}

// ---------------------------------------------------------------------------
// Property value tests
// ---------------------------------------------------------------------------

TEST(property_value_equality)
{
    ASSERT(RdbPropertyValue::fromString("a") == RdbPropertyValue::fromString("a"));
    ASSERT(RdbPropertyValue::fromString("a") != RdbPropertyValue::fromString("b"));
    ASSERT(RdbPropertyValue::fromInt(42) == RdbPropertyValue::fromInt(42));
    ASSERT(RdbPropertyValue::fromInt(42) != RdbPropertyValue::fromInt(43));
    ASSERT(RdbPropertyValue::fromBool(true) == RdbPropertyValue::fromBool(true));
    ASSERT(RdbPropertyValue::fromBool(true) != RdbPropertyValue::fromBool(false));
    // Different types are not equal
    ASSERT(RdbPropertyValue::fromString("1") != RdbPropertyValue::fromInt(1));
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main()
{
    std::cout << "RDB Controller Tests (Stage 6.11):" << std::endl;

    // Tests are auto-registered by static constructors above

    std::cout << std::endl;
    std::cout << "Results: " << testsPassed << " passed, "
              << testsFailed << " failed" << std::endl;

    cleanupTestFiles();

    return testsFailed > 0 ? 1 : 0;
}
