#include <string>
#include <cstdint>

std::string utf8_to_cp866(const std::string& input) {
    std::string output;
    output.reserve(input.size()); // Max size will be same as input

    for (size_t i = 0; i < input.size(); ++i) {
        uint8_t c = static_cast<uint8_t>(input[i]);

        // Check for UTF-8 2-byte prefix for Cyrillic
        if (c == 0xD0 && (i + 1) < input.size()) {
            uint8_t next = static_cast<uint8_t>(input[++i]);
            if (next == 0x81) output += (char)0xF0;          // Ё
            else if (next >= 0x90 && next <= 0xBF) {
                output += (char)(next - 0x90 + 0x80);        // А-П and Р-Я (partially)
            }
        } 
        else if (c == 0xD1 && (i + 1) < input.size()) {
            uint8_t next = static_cast<uint8_t>(input[++i]);
            if (next == 0x91) output += (char)0xF1;          // ё
            else if (next >= 0x80 && next <= 0x8F) {
                output += (char)(next - 0x80 + 0xE0);        // р-я
            }
        } 
        else if (c == 0xC2 && (i + 1) < input.size()) {
            uint8_t next = static_cast<uint8_t>(input[++i]);
            if (next == 0xa4) output += (char)0xfd; // klop 
            else output += '?';
        }
        else if (c == 0xe2 && (i + 2) < input.size()) {   // arrows
            uint8_t n1 = static_cast<uint8_t>(input[++i]);
            if (n1 == 0x86) {
                uint8_t n2 = static_cast<uint8_t>(input[++i]);
                switch (n2) {
                    case 0x96: output += (char)0x1c; break;// diagonal arrow NW ~ nonexistent in 866, hack needed
                    case 0x91: output += (char)0x18; break;// arrow up
                    case 0x90: output += (char)0x1b; break;// arrow left
                    case 0x93: output += (char)0x19; break;// arrow down
                    case 0x92: output += (char)0x1a; break;
                    default:   output += '?';
                }
            }
        }
        else {
            // Standard ASCII or unhandled characters pass through
            output += (char)c;
        }
    }
    return output;
}

