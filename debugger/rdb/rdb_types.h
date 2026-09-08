#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>

// ---------------------------------------------------------------------------
// RDB Types — Stage 6.11
//
// Core data structures for ROM Database:
//   - RDB object types (function, variable, data, etc.)
//   - RDB property (key-value with typed values)
//   - RDB object (address, type, name, size, comment, properties, links)
//   - ROM identity (file, size, SHA-256)
//   - RDB metadata (format, platform, version)
//
// No GUI, Board, MCP, or Agent dependency — fully testable in isolation.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Object types
// ---------------------------------------------------------------------------

enum class RdbObjectType
{
    Function,
    Variable,
    Data,
    Table,
    String,
    Code,
    Label,
    Unknown
};

// Convert to/from JSON string representation.
const char *rdbObjectTypeToString(RdbObjectType type);
RdbObjectType rdbObjectTypeFromString(const std::string &str);

// ---------------------------------------------------------------------------
// Property value — supports string, integer, boolean, and double
// ---------------------------------------------------------------------------

struct RdbPropertyValue
{
    enum class Type { String, Integer, Boolean, Double };

    Type type = Type::String;

    std::string stringValue;
    int64_t     intValue   = 0;
    bool        boolValue  = false;
    double      doubleValue = 0.0;

    // Convenience constructors
    static RdbPropertyValue fromString(const std::string &v);
    static RdbPropertyValue fromInt(int64_t v);
    static RdbPropertyValue fromBool(bool v);
    static RdbPropertyValue fromDouble(double v);

    bool operator==(const RdbPropertyValue &other) const;
    bool operator!=(const RdbPropertyValue &other) const { return !(*this == other); }
};

// ---------------------------------------------------------------------------
// ROM identity
// ---------------------------------------------------------------------------

struct RdbRomIdentity
{
    std::string file;
    uint64_t    size    = 0;
    std::string sha256;

    bool operator==(const RdbRomIdentity &other) const;
    bool operator!=(const RdbRomIdentity &other) const { return !(*this == other); }

    // True if all fields are populated.
    bool isComplete() const;
};

// ---------------------------------------------------------------------------
// RDB Object
// ---------------------------------------------------------------------------

struct RdbObject
{
    uint16_t     address = 0;
    RdbObjectType type   = RdbObjectType::Unknown;
    std::string  name;
    uint32_t     size    = 0;     // 0 = unknown
    bool         hasSize = false;
    std::string  comment;

    // Additional properties (key → typed value)
    std::map<std::string, RdbPropertyValue> properties;

    // Links to other RDB objects (target addresses)
    std::vector<uint16_t> links;

    bool operator==(const RdbObject &other) const;
    bool operator!=(const RdbObject &other) const { return !(*this == other); }
};
