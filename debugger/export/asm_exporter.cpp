// ---------------------------------------------------------------------------
// asm_exporter.cpp — Stage 6.17
//
// Z88DK ASM Exporter implementation.
// See asm_exporter.h for architecture overview.
// ---------------------------------------------------------------------------

#include "asm_exporter.h"
#include "sha256.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>

// ---------------------------------------------------------------------------
// Helpers — file I/O
// ---------------------------------------------------------------------------

static bool writeFile(const std::string &path, const std::string &content)
{
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << content;
    return f.good();
}

static bool mkdirs(const std::string &path)
{
    // Simple recursive mkdir — creates parent directories as needed.
    std::string cmd = "mkdir -p '" + path + "'";
    return system(cmd.c_str()) == 0;
}

static std::string basename(const std::string &path)
{
    auto pos = path.find_last_of('/');
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

// ---------------------------------------------------------------------------
// Helpers — hex formatting
// ---------------------------------------------------------------------------

static std::string toLower(const std::string &s)
{
    std::string r = s;
    for (auto &c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

static bool isAllHex(const std::string &s)
{
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

// Trim surrounding whitespace.  The disassembler emits operands as "B, 082A"
// (a space after the comma); without trimming, the right-hand field is seen as
// " 082A", which is neither all-hex nor the expected width, so it slips through
// unclassified and the "h" suffix / hex interpretation is lost.
static std::string trim(const std::string &s)
{
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// Format a hex-digit string as a z80asm numeric literal.  A literal MUST begin
// with a digit; "A1FFh"/"FFh" are parsed as (undefined) symbols, and a bare
// "10" is parsed as *decimal* ten, silently corrupting the byte.  So guard a
// leading hex letter with "0" and always terminate with the "h" suffix.
// Case of the digits is preserved (callers pass upper- or lower-case).
static std::string hexLiteral(const std::string &hexDigits)
{
    std::string s = hexDigits;
    if (!s.empty() && std::isalpha(static_cast<unsigned char>(s[0])))
        s = "0" + s;
    return s + "h";
}

// ---------------------------------------------------------------------------
// AsmExporter — construction
// ---------------------------------------------------------------------------

AsmExporter::AsmExporter(const AsmExportConfig &config)
    : config_(config)
{
}

// ---------------------------------------------------------------------------
// run — full export pipeline
// ---------------------------------------------------------------------------

AsmExportReport AsmExporter::run()
{
    AsmExportReport report;

    // Step 1: Load ROM
    if (!loadRom()) {
        report.errors.push_back({"ROM file not found or empty: " + config_.romPath, 0});
        return report;
    }

    report.romFile   = basename(config_.romPath);
    report.romSize   = rom_.size();
    report.romSha256 = computeSha256(rom_);
    report.origin    = config_.origin;

    // Step 2: Load RDB (optional)
    if (!config_.rdbPath.empty()) {
        if (!loadRdb()) {
            report.warnings.push_back({"RDB file not found: " + config_.rdbPath, 0});
        }
    }

    // Step 3: Run code analysis
    runAnalysis();
    report.mappingEntryPoint = analysis_.entryPoint;
    report.codeRanges        = static_cast<int>(analysis_.ranges.size());
    report.conflicts         = static_cast<int>(analysis_.conflicts.size());

    // Step 4: Build name map
    buildNameMap();

    // Step 5: Count RDB stats
    if (hasRdb_) {
        report.objectCount = static_cast<int>(rdb_.objectCount());
        // Count links
        auto objects = rdb_.listObjects();
        for (const auto &obj : objects) {
            report.linkCount += static_cast<int>(obj.links.size());
        }
    }

    // Step 6: Emit files
    report = emitFiles();

    // Restore computed fields (emitFiles overwrites some)
    report.romFile   = basename(config_.romPath);
    report.romSize   = rom_.size();
    report.romSha256 = computeSha256(rom_);
    report.origin    = config_.origin;
    report.mappingEntryPoint = analysis_.entryPoint;
    report.codeRanges        = static_cast<int>(analysis_.ranges.size());
    report.conflicts         = static_cast<int>(analysis_.conflicts.size());

    return report;
}

// ---------------------------------------------------------------------------
// loadRom
// ---------------------------------------------------------------------------

bool AsmExporter::loadRom()
{
    std::ifstream f(config_.romPath, std::ios::binary);
    if (!f.is_open()) return false;

    f.seekg(0, std::ios::end);
    auto size = f.tellg();
    if (size <= 0 || size > 65536) return false;

    f.seekg(0, std::ios::beg);
    rom_.resize(static_cast<size_t>(size));
    f.read(reinterpret_cast<char *>(rom_.data()), size);
    return f.good() || f.eof();
}

// ---------------------------------------------------------------------------
// loadRdb
// ---------------------------------------------------------------------------

bool AsmExporter::loadRdb()
{
    if (!rdb_.load(config_.rdbPath)) return false;
    hasRdb_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// runAnalysis
// ---------------------------------------------------------------------------

void AsmExporter::runAnalysis()
{
    auto readFn = [this](uint16_t addr) -> uint8_t {
        return readByte(addr);
    };

    // Entry points: the ROM mapping vector at 0x0000 plus every function
    // recorded in the RDB.  Seeding all function addresses guarantees coverage
    // for routines that static BFS from 0x0000 cannot reach through a
    // self-modified entry vector or an indirect computed jump (PCHL).
    std::vector<uint16_t> entries;
    entries.push_back(0x0000);
    if (hasRdb_) {
        for (const auto &obj : rdb_.listObjects()) {
            if (obj.type == RdbObjectType::Function ||
                obj.type == RdbObjectType::Label ||
                obj.type == RdbObjectType::Code) {
                entries.push_back(obj.address);
            }
        }
    }
    std::sort(entries.begin(), entries.end());
    entries.erase(std::unique(entries.begin(), entries.end()), entries.end());

    analysis_ = ::analyzeCodeMulti(entries, readFn, config_.maxAnalysisInstructions);

    // Build code address set
    for (const auto &instr : analysis_.instructions) {
        for (uint8_t i = 0; i < instr.size; ++i) {
            codeAddresses_.insert(instr.address + i);
        }
    }
}

// ---------------------------------------------------------------------------
// buildNameMap
// ---------------------------------------------------------------------------

void AsmExporter::buildNameMap()
{
    nameMap_.clear();

    // Priority 1: RDB object names
    if (hasRdb_) {
        auto objects = rdb_.listObjects();
        for (const auto &obj : objects) {
            if (!obj.name.empty()) {
                nameMap_[obj.address] = sanitizeLabel(obj.name);
            }
        }
    }

    // Priority 2: Generated labels for code analysis targets
    // (no SymbolDatabase in standalone exporter — RDB is the primary source)
    for (const auto &ref : analysis_.references) {
        if (nameMap_.find(ref.to) == nameMap_.end()) {
            nameMap_[ref.to] = generateLabel(ref.to);
        }
    }
}

// ---------------------------------------------------------------------------
// emitFiles — write all output files
// ---------------------------------------------------------------------------

AsmExportReport AsmExporter::emitFiles()
{
    AsmExportReport report;

    // Ensure output directories exist
    mkdirs(config_.outputDir);
    mkdirs(config_.outputDir + "/code");
    mkdirs(config_.outputDir + "/data");

    // Emit main.asm
    {
        std::string path = config_.outputDir + "/main.asm";
        if (writeFile(path, emitMainAsm())) {
            report.generatedFiles.push_back("main.asm");
        } else {
            report.errors.push_back({"Failed to write main.asm", 0});
        }
    }

    // Emit code.asm
    {
        std::string path = config_.outputDir + "/code/code.asm";
        if (writeFile(path, emitCodeAsm())) {
            report.generatedFiles.push_back("code/code.asm");
        } else {
            report.errors.push_back({"Failed to write code/code.asm", 0});
        }
    }

    // Emit data.asm
    {
        std::string path = config_.outputDir + "/data/data.asm";
        std::string content = emitDataAsm();
        if (writeFile(path, content)) {
            report.generatedFiles.push_back("data/data.asm");
            // Count data ranges (RDB data objects + unknown regions)
            if (hasRdb_) {
                auto objects = rdb_.listObjects();
                for (const auto &obj : objects) {
                    if (obj.type == RdbObjectType::Data ||
                        obj.type == RdbObjectType::Table ||
                        obj.type == RdbObjectType::String) {
                        report.dataRanges++;
                    }
                }
            }
        } else {
            report.errors.push_back({"Failed to write data/data.asm", 0});
        }
    }

    // Count unresolved references
    for (const auto &ref : analysis_.references) {
        if (nameMap_.find(ref.to) == nameMap_.end()) {
            report.unresolvedReferences++;
        }
    }

    // Add conflict warnings
    for (const auto &conflict : analysis_.conflicts) {
        report.warnings.push_back({conflict.description, conflict.address});
    }

    // Emit export.json
    report.romFile   = basename(config_.romPath);
    report.romSize   = rom_.size();
    report.romSha256 = computeSha256(rom_);
    report.origin    = config_.origin;
    report.mappingEntryPoint = analysis_.entryPoint;
    report.codeRanges = static_cast<int>(analysis_.ranges.size());
    report.conflicts  = static_cast<int>(analysis_.conflicts.size());

    if (hasRdb_) {
        report.objectCount = static_cast<int>(rdb_.objectCount());
        auto objects = rdb_.listObjects();
        report.linkCount = 0;
        for (const auto &obj : objects) {
            report.linkCount += static_cast<int>(obj.links.size());
        }
    }

    {
        std::string path = config_.outputDir + "/export.json";
        if (writeFile(path, emitExportJson(report))) {
            report.generatedFiles.push_back("export.json");
        } else {
            report.errors.push_back({"Failed to write export.json", 0});
        }
    }

    return report;
}

// ---------------------------------------------------------------------------
// emitMainAsm
// ---------------------------------------------------------------------------

std::string AsmExporter::emitMainAsm()
{
    std::ostringstream os;
    os << "; Generated by v06c-asm-export (Stage 6.17)\n";
    os << "; ROM: " << basename(config_.romPath) << "\n";
    os << "; Size: " << rom_.size() << " bytes\n";
    os << "; SHA-256: " << computeSha256(rom_) << "\n";
    os << "\n";

    char buf[16];
    snprintf(buf, sizeof(buf), "%04X", config_.origin);
    os << "\torg\t" << hexLiteral(buf) << "\n";
    os << "\n";

    os << "\tinclude\t\"code/code.asm\"\n";
    os << "\tinclude\t\"data/data.asm\"\n";

    return os.str();
}

// ---------------------------------------------------------------------------
// emitCodeAsm
// ---------------------------------------------------------------------------

std::string AsmExporter::emitCodeAsm()
{
    std::ostringstream os;
    os << "; Code section — generated by v06c-asm-export\n";
    os << "; Instructions discovered by control-flow analysis from 0x0000\n";
    os << "\n";

    // Sort instructions by address
    std::vector<AnalyzedInstruction> sorted = analysis_.instructions;
    std::sort(sorted.begin(), sorted.end(),
              [](const AnalyzedInstruction &a, const AnalyzedInstruction &b) {
                  return a.address < b.address;
              });

    // Build a set of instruction start addresses for quick lookup
    std::set<uint16_t> instrStarts;
    for (const auto &instr : sorted) {
        instrStarts.insert(instr.address);
    }

    // Emit instructions grouped by code range
    size_t instrIdx = 0;
    for (const auto &range : analysis_.ranges) {
        char rangeBuf[32];
        snprintf(rangeBuf, sizeof(rangeBuf), "%04X", range.start);
        os << "\n; --- Code range: " << rangeBuf << " ---\n";

        while (instrIdx < sorted.size() &&
               sorted[instrIdx].address >= range.start &&
               sorted[instrIdx].address <= range.end) {
            const auto &instr = sorted[instrIdx];

            // Check if there's a label at this address
            std::string name = resolveName(instr.address);
            if (!name.empty() && instrStarts.count(instr.address)) {
                // Check if this address is a reference target
                bool isTarget = false;
                for (const auto &ref : analysis_.references) {
                    if (ref.to == instr.address) {
                        isTarget = true;
                        break;
                    }
                }
                // Also check RDB objects
                if (hasRdb_) {
                    auto objects = rdb_.listObjects();
                    for (const auto &obj : objects) {
                        if (obj.address == instr.address) {
                            isTarget = true;
                            break;
                        }
                    }
                }

                if (isTarget || name != generateLabel(instr.address)) {
                    // Emit RDB comment if available
                    if (hasRdb_) {
                        std::string comment = rdb_.getComment(instr.address);
                        if (!comment.empty()) {
                            os << "; " << comment << "\n";
                        }
                    }
                    os << name << ":\n";
                }
            }

            // Emit instruction
            os << emitInstruction(instr);
            ++instrIdx;
        }
    }

    return os.str();
}

// ---------------------------------------------------------------------------
// emitDataAsm
// ---------------------------------------------------------------------------

std::string AsmExporter::emitDataAsm()
{
    std::ostringstream os;
    os << "; Data section — generated by v06c-asm-export\n";
    os << "\n";

    if (!hasRdb_) {
        os << "; No RDB available — data section is empty\n";
        os << "; (RDB not found)\n";
        return os.str();
    }

    // Collect data objects from RDB, sorted by address
    auto objects = rdb_.listObjects();
    std::vector<const RdbObject *> dataObjects;
    for (const auto &obj : objects) {
        if (obj.type == RdbObjectType::Data ||
            obj.type == RdbObjectType::Table ||
            obj.type == RdbObjectType::String) {
            dataObjects.push_back(&obj);
        }
    }
    std::sort(dataObjects.begin(), dataObjects.end(),
              [](const RdbObject *a, const RdbObject *b) {
                  return a->address < b->address;
              });

    // rom_ holds the file bytes starting at the load origin; a CPU address maps
    // to buffer offset (addr - origin).  romEndAbs is the last ROM address.
    const size_t origin   = config_.origin;
    const size_t romEndAbs = static_cast<size_t>(config_.origin) + rom_.size();

    // Emit each data object
    for (const auto *obj : dataObjects) {
        if (!obj->hasSize || obj->size == 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%04X", obj->address);
            os << "; WARNING: data object at " << buf
               << " has no size — skipped\n";
            continue;
        }

        // Check bounds against the absolute ROM window [origin, romEndAbs)
        if (static_cast<size_t>(obj->address) + obj->size > romEndAbs) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%04X", obj->address);
            os << "; WARNING: data object at " << buf
               << " extends beyond ROM — truncated\n";
        }

        size_t emitSize = obj->size;
        if (static_cast<size_t>(obj->address) + emitSize > romEndAbs) {
            emitSize = romEndAbs - obj->address;
        }

        std::string label = resolveName(obj->address);
        if (label.empty()) {
            label = generateLabel(obj->address);
        }

        size_t off = static_cast<size_t>(obj->address) - origin;
        os << emitDataBlock(label, obj->comment,
                            &rom_[off], emitSize);
        os << "\n";
    }

    // Emit unknown regions (ROM bytes not classified as code or data)
    // Find gaps between code ranges and data objects
    std::vector<std::pair<uint16_t, uint16_t>> coveredRanges;
    for (const auto &range : analysis_.ranges) {
        coveredRanges.push_back({range.start, range.end});
    }
    for (const auto *obj : dataObjects) {
        if (obj->hasSize && obj->size > 0) {
            coveredRanges.push_back({obj->address,
                static_cast<uint16_t>(obj->address + obj->size - 1)});
        }
    }
    // Treat every RDB function's [address, address+size) as code so that the
    // interior continuation blocks (branch/PCHL targets that BFS range-merging
    // may split) are not re-emitted as unknown data.
    if (hasRdb_) {
        for (const auto &obj : rdb_.listObjects()) {
            if (obj.type == RdbObjectType::Function && obj.size > 0) {
                coveredRanges.push_back({obj.address,
                    static_cast<uint16_t>(static_cast<uint32_t>(obj.address) + obj.size - 1)});
            }
        }
    }
    std::sort(coveredRanges.begin(), coveredRanges.end());

    // Merge overlapping ranges
    std::vector<std::pair<uint16_t, uint16_t>> merged;
    for (const auto &r : coveredRanges) {
        if (!merged.empty() && r.first <= merged.back().second + 1) {
            if (r.second > merged.back().second) {
                merged.back().second = r.second;
            }
        } else {
            merged.push_back(r);
        }
    }

    // Emit gaps as unknown data (addresses are absolute; rom_ indexed from origin)
    size_t romEndAbsU = static_cast<size_t>(config_.origin) + rom_.size();
    size_t pos = config_.origin;
    for (const auto &r : merged) {
        if (static_cast<size_t>(r.first) > pos) {
            // Gap from pos to r.first - 1
            size_t gapSize = static_cast<size_t>(r.first) - pos;
            char labelBuf[16];
            snprintf(labelBuf, sizeof(labelBuf), "%04X", static_cast<uint16_t>(pos));
            os << "; Unknown region at " << labelBuf << "\n";
            os << emitDataBlock(std::string("unknown_") + labelBuf, "",
                                &rom_[pos - config_.origin], gapSize);
            os << "\n";
        }
        pos = static_cast<size_t>(r.second) + 1;
        if (pos > romEndAbsU) break;
    }

    // Trailing unknown region
    if (pos < romEndAbsU) {
        size_t tailSize = romEndAbsU - pos;
        char labelBuf[16];
        snprintf(labelBuf, sizeof(labelBuf), "%04X", static_cast<uint16_t>(pos));
        os << "; Unknown region at " << labelBuf << "\n";
        os << emitDataBlock(std::string("unknown_") + labelBuf, "",
                            &rom_[pos - config_.origin], tailSize);
    }

    return os.str();
}

// ---------------------------------------------------------------------------
// emitExportJson
// ---------------------------------------------------------------------------

std::string AsmExporter::emitExportJson(const AsmExportReport &report)
{
    std::ostringstream os;
    os << "{\n";
    os << "  \"rom\": \"" << report.romFile << "\",\n";
    os << "  \"rom_size\": " << report.romSize << ",\n";

    char originBuf[8], entryBuf[8];
    snprintf(originBuf, sizeof(originBuf), "0x%04X", report.origin);
    snprintf(entryBuf, sizeof(entryBuf), "0x%04X", report.mappingEntryPoint);
    os << "  \"origin\": \"" << originBuf << "\",\n";
    os << "  \"mapping_entry_point\": \"" << entryBuf << "\",\n";
    os << "  \"rom_sha256\": \"" << report.romSha256 << "\",\n";
    os << "  \"objects\": " << report.objectCount << ",\n";
    os << "  \"links\": " << report.linkCount << ",\n";
    os << "  \"code_ranges\": " << report.codeRanges << ",\n";
    os << "  \"data_ranges\": " << report.dataRanges << ",\n";
    os << "  \"warnings\": " << report.warnings.size() << ",\n";
    os << "  \"errors\": " << report.errors.size() << ",\n";
    os << "  \"conflicts\": " << report.conflicts << ",\n";
    os << "  \"unresolved_references\": " << report.unresolvedReferences << "\n";
    os << "}\n";

    return os.str();
}

// ---------------------------------------------------------------------------
// emitInstruction — convert one analyzed instruction to z80asm text
// ---------------------------------------------------------------------------

std::string AsmExporter::emitInstruction(const AnalyzedInstruction &instr)
{
    std::ostringstream os;
    os << "\t" << toLower(instr.mnemonic);

    if (instr.hasTarget) {
        // JMP, CALL, Jcc, Ccc, RST — replace target with symbol name
        std::string name = resolveName(instr.target);
        if (!name.empty()) {
            os << "\t" << name;
        } else {
            // Fallback: numeric address with h suffix (leading digit guard so
            // targets >= 0xA000, e.g. 0xA1FF -> "0A1FFh", aren't read as symbols)
            char buf[8];
            snprintf(buf, sizeof(buf), "%04X", instr.target);
            os << "\t" << hexLiteral(buf);
        }
    } else if (!instr.operands.empty()) {
        os << "\t" << convertOperands(instr.mnemonic, instr.operands);
    }

    os << "\n";
    return os.str();
}

// ---------------------------------------------------------------------------
// emitDataBlock — format bytes as defb lines
// ---------------------------------------------------------------------------

std::string AsmExporter::emitDataBlock(const std::string &label,
                                        const std::string &comment,
                                        const uint8_t *data, size_t size)
{
    std::ostringstream os;

    if (!comment.empty()) {
        os << "; " << comment << "\n";
    }
    os << label << ":\n";

    // Emit 8 bytes per line for readability
    const size_t bytesPerLine = 8;
    for (size_t i = 0; i < size; i += bytesPerLine) {
        os << "\tdefb\t";
        size_t count = std::min(bytesPerLine, size - i);
        for (size_t j = 0; j < count; ++j) {
            if (j > 0) os << ",";
            os << formatByte(data[i + j]);
        }
        os << "\n";
    }

    return os.str();
}

// ---------------------------------------------------------------------------
// convertOperands — static helper for z80asm operand conversion
// ---------------------------------------------------------------------------

std::string AsmExporter::convertOperands(const std::string &mnemonic,
                                          const std::string &operands)
{
    (void)mnemonic;  // kept for API stability / future mnemonic-specific rules

    // Register mapping: 8080 names -> z80asm lowercase.
    // NOTE: M and PSW map to the 8080 names "m"/"psw", NOT the Z80 "(hl)"/"af".
    // The latter are rejected by `z80asm -m=8080_strict` (and "(hl)" fails even
    // in default mode for the two-register MOV), so emitting them made the export
    // unbuildable.  Verified byte-identical: sub m=96, mov a,m=7e, pop psw=f1.
    static const std::map<std::string, std::string> regMap = {
        {"A", "a"}, {"B", "b"}, {"C", "c"}, {"D", "d"},
        {"E", "e"}, {"H", "h"}, {"L", "l"}, {"M", "m"},
        {"PSW", "psw"}, {"SP", "sp"},
    };

    // Classify one already-trimmed operand token: register, hex literal, or
    // pass-through.  Used for both the single and the two-operand forms so the
    // register-vs-number decision lives in exactly one place.
    auto classify = [&](const std::string &tok) -> std::string {
        auto it = regMap.find(tok);
        if (it != regMap.end()) return it->second;
        if (isAllHex(tok) && (tok.size() == 2 || tok.size() == 4))
            return hexLiteral(toLower(tok));
        return tok;
    };

    auto commaPos = operands.find(',');
    if (commaPos == std::string::npos) {
        return classify(trim(operands));
    }

    std::string left  = trim(operands.substr(0, commaPos));
    std::string right = trim(operands.substr(commaPos + 1));
    return classify(left) + "," + classify(right);
}

// ---------------------------------------------------------------------------
// generateLabel — stable label for an address
// ---------------------------------------------------------------------------

std::string AsmExporter::generateLabel(uint16_t address)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "label_%04X", address);
    return buf;
}

// ---------------------------------------------------------------------------
// sanitizeLabel — make a name safe for z80asm
// ---------------------------------------------------------------------------

std::string AsmExporter::sanitizeLabel(const std::string &name)
{
    std::string result;
    result.reserve(name.size());

    for (size_t i = 0; i < name.size(); ++i) {
        char c = name[i];
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            result += c;
        } else {
            result += '_';
        }
    }

    // Ensure it doesn't start with a digit
    if (!result.empty() && std::isdigit(static_cast<unsigned char>(result[0]))) {
        result = "_" + result;
    }

    return result;
}

// ---------------------------------------------------------------------------
// formatByte — format a byte as z80asm hex
// ---------------------------------------------------------------------------

std::string AsmExporter::formatByte(uint8_t b)
{
    // z80asm hex literal: always starts with a digit and ends with 'h'.
    // "%02Xh" alone yields "FFh"/"A1h" for bytes >= 0xA0, which z80asm reads as
    // undefined symbols.  hexLiteral() guards the leading letter with '0'.
    char buf[8];
    snprintf(buf, sizeof(buf), "%02X", b);
    return hexLiteral(buf);
}

// ---------------------------------------------------------------------------
// resolveName — look up name for address
// ---------------------------------------------------------------------------

std::string AsmExporter::resolveName(uint16_t address) const
{
    auto it = nameMap_.find(address);
    if (it != nameMap_.end()) return it->second;
    return "";
}

// ---------------------------------------------------------------------------
// readByte — safe ROM read with wrap-around
// ---------------------------------------------------------------------------

uint8_t AsmExporter::readByte(uint16_t addr) const
{
    // NOTE: origin-relative mapping fix (found via MCP ROM-analysis agent).
    // The agent reported it as "relative addresses not accounted for", but the
    // real defect was that rom_ was indexed by absolute CPU address while it
    // actually holds file bytes mapped at config_.origin (default 0x0100) —
    // causing an off-by-origin misread for any non-zero origin.  i8080 has no
    // PC-relative jumps; the fix below maps addr -> (addr - origin) instead.
    //
    // rom_ holds the file bytes mapped at config_.origin, so a CPU address maps
    // to buffer offset (addr - origin).  Outside the ROM window -> 0x00 (NOP).
    if (addr < config_.origin) return 0x00;
    size_t off = static_cast<size_t>(addr) - config_.origin;
    if (off < rom_.size()) return rom_[off];
    return 0x00; // NOP for addresses beyond ROM
}

// ---------------------------------------------------------------------------
// computeSha256
// ---------------------------------------------------------------------------

std::string AsmExporter::computeSha256(const std::vector<uint8_t> &data)
{
    return Sha256::bufferDigest(data.data(), data.size());
}
