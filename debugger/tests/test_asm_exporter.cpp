// ---------------------------------------------------------------------------
// test_asm_exporter.cpp — Stage 6.17
//
// Unit tests for the Z88DK ASM Exporter.
// Tests static helpers (convertOperands, generateLabel, etc.) and
// integration tests with real ROM/RDB fixtures.
// ---------------------------------------------------------------------------

#include "asm_exporter.h"
#include "rdb_controller.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Test framework (minimal)
// ---------------------------------------------------------------------------

static int testsPassed = 0;
static int testsFailed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s (line %d): %s\n", msg, __LINE__, #cond); \
        testsFailed++; \
    } else { \
        testsPassed++; \
    } \
} while(0)

#define TEST_BEGIN(name) \
    static void test_##name() { \
        const char *testName_ = #name; \
        (void)testName_;

#define TEST_END() \
    }

#define RUN_TEST(name) do { \
    test_##name(); \
} while(0)

// ---------------------------------------------------------------------------
// Helper: write bytes to a temp file
// ---------------------------------------------------------------------------

static std::string writeTempFile(const std::string &name,
                                  const uint8_t *data, size_t size)
{
    std::string path = "/tmp/test_asm_export_" + name;
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char *>(data), size);
    f.close();
    return path;
}

static void cleanupTempFiles()
{
    system("rm -rf /tmp/test_asm_export_*");
}

// ---------------------------------------------------------------------------
// Test: generateLabel
// ---------------------------------------------------------------------------

TEST_BEGIN(generate_label)
{
    CHECK(AsmExporter::generateLabel(0x0000) == "label_0000", "label at 0000");
    CHECK(AsmExporter::generateLabel(0x1234) == "label_1234", "label at 1234");
    CHECK(AsmExporter::generateLabel(0xFFFF) == "label_FFFF", "label at FFFF");
    CHECK(AsmExporter::generateLabel(0x00FF) == "label_00FF", "label at 00FF");

    // Stability: same address → same label
    CHECK(AsmExporter::generateLabel(0x2A40) == AsmExporter::generateLabel(0x2A40),
          "labels are stable");
TEST_END()
}

// ---------------------------------------------------------------------------
// Test: sanitizeLabel
// ---------------------------------------------------------------------------

TEST_BEGIN(sanitize_label)
{
    CHECK(AsmExporter::sanitizeLabel("draw_sprite") == "draw_sprite", "underscore OK");
    CHECK(AsmExporter::sanitizeLabel("my-func") == "my_func", "dash → underscore");
    CHECK(AsmExporter::sanitizeLabel("123abc") == "_123abc", "digit prefix → _");
    CHECK(AsmExporter::sanitizeLabel("hello world") == "hello_world", "space → underscore");
    CHECK(AsmExporter::sanitizeLabel("ABC") == "ABC", "uppercase preserved");
TEST_END()
}

// ---------------------------------------------------------------------------
// Test: formatByte
// ---------------------------------------------------------------------------

TEST_BEGIN(format_byte)
{
    CHECK(AsmExporter::formatByte(0x00) == "00h", "00h");
    CHECK(AsmExporter::formatByte(0x0A) == "0Ah", "0Ah");
    CHECK(AsmExporter::formatByte(0xFF) == "FFh", "FFh");
    CHECK(AsmExporter::formatByte(0xD3) == "D3h", "D3h");
TEST_END()
}

// ---------------------------------------------------------------------------
// Test: convertOperands — register operands
// ---------------------------------------------------------------------------

TEST_BEGIN(convert_operands_registers)
{
    // Single register
    CHECK(AsmExporter::convertOperands("MOV", "A") == "a", "MOV A");
    CHECK(AsmExporter::convertOperands("INR", "M") == "(hl)", "INR M");
    CHECK(AsmExporter::convertOperands("PUSH", "PSW") == "af", "PUSH PSW");
    CHECK(AsmExporter::convertOperands("INR", "B") == "b", "INR B");

    // Two registers (MOV A,B)
    CHECK(AsmExporter::convertOperands("MOV", "A,B") == "a,b", "MOV A,B");
    CHECK(AsmExporter::convertOperands("MOV", "M,A") == "(hl),a", "MOV M,A");
TEST_END()
}

// ---------------------------------------------------------------------------
// Test: convertOperands — register + hex
// ---------------------------------------------------------------------------

TEST_BEGIN(convert_operands_hex)
{
    // LXI H,0100 → h,0100h
    CHECK(AsmExporter::convertOperands("LXI", "H,0100") == "h,0100h", "LXI H,0100");
    // MVI A,D3 → a,d3h
    CHECK(AsmExporter::convertOperands("MVI", "A,D3") == "a,d3h", "MVI A,D3");
    // ADI D3 → d3h
    CHECK(AsmExporter::convertOperands("ADI", "D3") == "d3h", "ADI D3");
    // CPI 00 → 00h
    CHECK(AsmExporter::convertOperands("CPI", "00") == "00h", "CPI 00");
TEST_END()
}

// ---------------------------------------------------------------------------
// Test: convertOperands — direct addresses
// ---------------------------------------------------------------------------

TEST_BEGIN(convert_operands_address)
{
    // STA 0038 → 0038h
    CHECK(AsmExporter::convertOperands("STA", "0038") == "0038h", "STA 0038");
    // LDA 8000 → 8000h
    CHECK(AsmExporter::convertOperands("LDA", "8000") == "8000h", "LDA 8000");
    // OUT 02 → 02h
    CHECK(AsmExporter::convertOperands("OUT", "02") == "02h", "OUT 02");
TEST_END()
}

// ---------------------------------------------------------------------------
// Test: Full export with synthetic ROM
// ---------------------------------------------------------------------------

TEST_BEGIN(full_export_synthetic)
{
    // Create a small ROM fixture:
    //   0000: NOP
    //   0001: NOP
    //   0002: JMP 0010
    //   ...
    //   0010: CALL 0020
    //   0013: RET
    //   ...
    //   0020: NOP
    //   0021: RET
    std::vector<uint8_t> rom(256, 0x00); // NOP fill

    // 0002: JMP 0010
    rom[0x02] = 0xC3;
    rom[0x03] = 0x10;
    rom[0x04] = 0x00;

    // 0010: CALL 0020
    rom[0x10] = 0xCD;
    rom[0x11] = 0x20;
    rom[0x12] = 0x00;

    // 0013: RET
    rom[0x13] = 0xC9;

    // 0020: NOP (already 0x00)
    // 0021: RET
    rom[0x21] = 0xC9;

    std::string romPath = writeTempFile("synth.rom", rom.data(), rom.size());

    AsmExportConfig config;
    config.romPath = romPath;
    config.outputDir = "/tmp/test_asm_export_output";
    config.origin = 0x0000;

    AsmExporter exporter(config);
    AsmExportReport report = exporter.run();

    CHECK(!report.hasErrors(), "export succeeds");
    CHECK(report.romSize == 256, "ROM size = 256");
    CHECK(!report.romSha256.empty(), "SHA-256 computed");
    CHECK(report.codeRanges >= 1, "at least 1 code range");
    CHECK(report.generatedFiles.size() >= 3, "at least 3 files generated");

    // Verify main.asm was created
    bool hasMain = false;
    for (const auto &f : report.generatedFiles) {
        if (f == "main.asm") hasMain = true;
    }
    CHECK(hasMain, "main.asm generated");

    // Verify export.json was created
    bool hasJson = false;
    for (const auto &f : report.generatedFiles) {
        if (f == "export.json") hasJson = true;
    }
    CHECK(hasJson, "export.json generated");

    // Verify export.json content
    {
        std::ifstream jf("/tmp/test_asm_export_output/export.json");
        CHECK(jf.is_open(), "export.json readable");
        std::string content((std::istreambuf_iterator<char>(jf)),
                             std::istreambuf_iterator<char>());
        CHECK(content.find("\"rom_size\": 256") != std::string::npos,
              "export.json has rom_size");
        CHECK(content.find("\"origin\": \"0x0000\"") != std::string::npos,
              "export.json has origin");
    }

    // Verify main.asm content
    {
        std::ifstream af("/tmp/test_asm_export_output/main.asm");
        CHECK(af.is_open(), "main.asm readable");
        std::string content((std::istreambuf_iterator<char>(af)),
                             std::istreambuf_iterator<char>());
        CHECK(content.find("org\t0000h") != std::string::npos,
              "main.asm has org directive");
        CHECK(content.find("include") != std::string::npos,
              "main.asm has includes");
    }

    // Verify code.asm has instructions
    {
        std::ifstream cf("/tmp/test_asm_export_output/code/code.asm");
        CHECK(cf.is_open(), "code.asm readable");
        std::string content((std::istreambuf_iterator<char>(cf)),
                             std::istreambuf_iterator<char>());
        CHECK(content.find("nop") != std::string::npos,
              "code.asm has NOP instructions");
        CHECK(content.find("jmp") != std::string::npos,
              "code.asm has JMP instruction");
        CHECK(content.find("call") != std::string::npos,
              "code.asm has CALL instruction");
        CHECK(content.find("ret") != std::string::npos,
              "code.asm has RET instruction");
    }

    cleanupTempFiles();
TEST_END()
}

// ---------------------------------------------------------------------------
// Test: Export with RDB
// ---------------------------------------------------------------------------

TEST_BEGIN(export_with_rdb)
{
    // Create ROM
    std::vector<uint8_t> rom(256, 0x00);
    rom[0x10] = 0xCD; // CALL 0030
    rom[0x11] = 0x30;
    rom[0x12] = 0x00;
    rom[0x13] = 0xC9; // RET
    rom[0x30] = 0x00; // NOP
    rom[0x31] = 0xC9; // RET

    // Data at 0x40
    rom[0x40] = 0x01;
    rom[0x41] = 0x02;
    rom[0x42] = 0x03;
    rom[0x43] = 0x04;

    std::string romPath = writeTempFile("rdb_test.rom", rom.data(), rom.size());

    // Create RDB
    RdbController rdb;
    rdb.initialize("vector06c", {});

    RdbObject func;
    func.address = 0x0030;
    func.type = RdbObjectType::Function;
    func.name = "my_func";
    func.comment = "Test function";
    rdb.addObject(func);

    RdbObject data;
    data.address = 0x0040;
    data.type = RdbObjectType::Data;
    data.name = "my_data";
    data.size = 4;
    data.hasSize = true;
    data.comment = "Test data table";
    rdb.addObject(data);

    std::string rdbContent;
    {
        // Serialize RDB to JSON manually — use saveAs
        std::string rdbPath = "/tmp/test_asm_export_rdb_test.rdb";
        rdb.saveAs(rdbPath);

        AsmExportConfig config;
        config.romPath = romPath;
        config.rdbPath = rdbPath;
        config.outputDir = "/tmp/test_asm_export_rdb_output";
        config.origin = 0x0000;

        AsmExporter exporter(config);
        AsmExportReport report = exporter.run();

        CHECK(!report.hasErrors(), "export with RDB succeeds");
        CHECK(report.objectCount == 2, "2 RDB objects");
        CHECK(report.dataRanges >= 1, "at least 1 data range");

        // Verify code.asm has the function label
        {
            std::ifstream cf("/tmp/test_asm_export_rdb_output/code/code.asm");
            std::string content((std::istreambuf_iterator<char>(cf)),
                                 std::istreambuf_iterator<char>());
            CHECK(content.find("my_func:") != std::string::npos,
                  "code.asm has my_func label");
            CHECK(content.find("; Test function") != std::string::npos,
                  "code.asm has RDB comment");
        }

        // Verify data.asm has the data block
        {
            std::ifstream df("/tmp/test_asm_export_rdb_output/data/data.asm");
            std::string content((std::istreambuf_iterator<char>(df)),
                                 std::istreambuf_iterator<char>());
            CHECK(content.find("my_data:") != std::string::npos,
                  "data.asm has my_data label");
            CHECK(content.find("defb") != std::string::npos,
                  "data.asm has defb");
            CHECK(content.find("01h") != std::string::npos,
                  "data.asm has correct byte 01h");
            CHECK(content.find("04h") != std::string::npos,
                  "data.asm has correct byte 04h");
            CHECK(content.find("; Test data table") != std::string::npos,
                  "data.asm has RDB comment");
        }
    }

    cleanupTempFiles();
TEST_END()
}

// ---------------------------------------------------------------------------
// Test: Deterministic export
// ---------------------------------------------------------------------------

TEST_BEGIN(deterministic_export)
{
    std::vector<uint8_t> rom(128, 0x00);
    rom[0x05] = 0xC3; // JMP 0020
    rom[0x06] = 0x20;
    rom[0x07] = 0x00;
    rom[0x20] = 0xC9; // RET

    std::string romPath = writeTempFile("det.rom", rom.data(), rom.size());

    // Export twice
    AsmExportConfig config;
    config.romPath = romPath;
    config.outputDir = "/tmp/test_asm_export_det1";
    config.origin = 0x0000;

    AsmExporter(config).run();

    config.outputDir = "/tmp/test_asm_export_det2";
    AsmExporter(config).run();

    // Compare generated files
    auto readFile = [](const std::string &path) -> std::string {
        std::ifstream f(path);
        return std::string((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
    };

    std::string code1 = readFile("/tmp/test_asm_export_det1/code/code.asm");
    std::string code2 = readFile("/tmp/test_asm_export_det2/code/code.asm");
    CHECK(code1 == code2, "code.asm is deterministic");

    std::string main1 = readFile("/tmp/test_asm_export_det1/main.asm");
    std::string main2 = readFile("/tmp/test_asm_export_det2/main.asm");
    CHECK(main1 == main2, "main.asm is deterministic");

    cleanupTempFiles();
TEST_END()
}

// ---------------------------------------------------------------------------
// Test: Export without RDB
// ---------------------------------------------------------------------------

TEST_BEGIN(export_without_rdb)
{
    std::vector<uint8_t> rom(64, 0x00);
    rom[0x00] = 0xC9; // RET

    std::string romPath = writeTempFile("nordb.rom", rom.data(), rom.size());

    AsmExportConfig config;
    config.romPath = romPath;
    config.outputDir = "/tmp/test_asm_export_nordb";
    config.origin = 0x0000;
    // No RDB path

    AsmExporter exporter(config);
    AsmExportReport report = exporter.run();

    CHECK(!report.hasErrors(), "export without RDB succeeds");
    CHECK(report.objectCount == 0, "0 RDB objects");

    // data.asm should mention "No RDB available"
    {
        std::ifstream df("/tmp/test_asm_export_nordb/data/data.asm");
        std::string content((std::istreambuf_iterator<char>(df)),
                             std::istreambuf_iterator<char>());
        CHECK(content.find("No RDB") != std::string::npos,
              "data.asm mentions no RDB");
    }

    cleanupTempFiles();
TEST_END()
}

// ---------------------------------------------------------------------------
// Test: ROM not found
// ---------------------------------------------------------------------------

TEST_BEGIN(rom_not_found)
{
    AsmExportConfig config;
    config.romPath = "/tmp/nonexistent_rom_file.rom";
    config.outputDir = "/tmp/test_asm_export_err";

    AsmExporter exporter(config);
    AsmExportReport report = exporter.run();

    CHECK(report.hasErrors(), "error when ROM not found");
    CHECK(!report.errors.empty(), "at least 1 error");
TEST_END()
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main()
{
    printf("ASM Exporter Tests (Stage 6.17):\n\n");

    RUN_TEST(generate_label);
    RUN_TEST(sanitize_label);
    RUN_TEST(format_byte);
    RUN_TEST(convert_operands_registers);
    RUN_TEST(convert_operands_hex);
    RUN_TEST(convert_operands_address);
    RUN_TEST(full_export_synthetic);
    RUN_TEST(export_with_rdb);
    RUN_TEST(deterministic_export);
    RUN_TEST(export_without_rdb);
    RUN_TEST(rom_not_found);

    printf("\nResults: %d passed, %d failed\n", testsPassed, testsFailed);
    return testsFailed > 0 ? 1 : 0;
}
