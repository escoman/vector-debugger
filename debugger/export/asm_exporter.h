#pragma once

// ---------------------------------------------------------------------------
// asm_exporter.h — Stage 6.17
//
// Z88DK ASM Exporter — converts ROM + RDB + code analysis into z80asm source.
//
// This is a read-only tool: it never modifies RDB.
// It does NOT contain its own 8080 opcode decoder — it uses the existing
// disassembler (disassemble()) and code analyzer (analyzeCode()).
//
// Pipeline:
//   ROM file + RDB file → AsmExporter → .asm / .inc / export.json
// ---------------------------------------------------------------------------

#include "code_analyzer.h"
#include "rdb_controller.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Export configuration
// ---------------------------------------------------------------------------

struct AsmExportConfig
{
    std::string romPath;
    std::string rdbPath;        // empty = no RDB
    std::string outputDir;
    uint16_t    origin = 0x0100; // ROM load origin
    size_t      maxAnalysisInstructions = 10000;
};

// ---------------------------------------------------------------------------
// Export report — machine-readable summary
// ---------------------------------------------------------------------------

struct ExportWarning
{
    std::string message;
    uint16_t    address = 0;
};

struct AsmExportReport
{
    // ROM identity
    std::string romFile;
    uint64_t    romSize    = 0;
    std::string romSha256;

    // Addresses
    uint16_t    origin           = 0;
    uint16_t    mappingEntryPoint = 0;

    // Counts
    int objectCount  = 0;
    int linkCount    = 0;
    int codeRanges   = 0;
    int dataRanges   = 0;

    // Generated files
    std::vector<std::string> generatedFiles;

    // Diagnostics
    std::vector<ExportWarning> warnings;
    std::vector<ExportWarning> errors;
    int conflicts            = 0;
    int unresolvedReferences = 0;

    bool hasErrors() const { return !errors.empty(); }
};

// ---------------------------------------------------------------------------
// AsmExporter
// ---------------------------------------------------------------------------

class AsmExporter
{
public:
    explicit AsmExporter(const AsmExportConfig &config);

    // Run the full export pipeline.  Returns the report.
    AsmExportReport run();

    // --- Static helpers (testable in isolation) ---

    // Convert debugger operand text to z80asm format.
    // e.g. "H,0100" → "h,0100h", "A,B" → "a,b"
    static std::string convertOperands(const std::string &mnemonic,
                                       const std::string &operands);

    // Generate a stable label for an address: "label_XXXX"
    static std::string generateLabel(uint16_t address);

    // Sanitize a name for use as z80asm identifier.
    static std::string sanitizeLabel(const std::string &name);

    // Format a byte as z80asm hex: "0AH"
    static std::string formatByte(uint8_t b);

private:
    AsmExportConfig config_;

    // ROM data
    std::vector<uint8_t> rom_;

    // RDB (may be empty if no RDB file)
    RdbController rdb_;
    bool hasRdb_ = false;

    // Code analysis result
    CodeAnalysisResult analysis_;

    // Name map: address → label name
    std::map<uint16_t, std::string> nameMap_;

    // Set of addresses that are code (from analysis)
    std::set<uint16_t> codeAddresses_;

    // --- Pipeline steps ---

    bool loadRom();
    bool loadRdb();
    void runAnalysis();
    void buildNameMap();
    AsmExportReport emitFiles();

    // File emitters
    std::string emitMainAsm();
    std::string emitCodeAsm();
    std::string emitDataAsm();
    std::string emitExportJson(const AsmExportReport &report);

    // Instruction emitter
    std::string emitInstruction(const AnalyzedInstruction &instr);

    // Data emitter
    std::string emitDataBlock(const std::string &label,
                              const std::string &comment,
                              const uint8_t *data, size_t size);

    // Name resolution
    std::string resolveName(uint16_t address) const;

    // ROM read function for disassembler
    uint8_t readByte(uint16_t addr) const;

    // SHA-256 of ROM
    static std::string computeSha256(const std::vector<uint8_t> &data);
};
