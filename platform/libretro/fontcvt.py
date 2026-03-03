import os

def inflate_to_bitluni(input_bin, output_header, font_name="CodePage866_8x8"):
    if not os.path.exists(input_bin):
        return

    with open(input_bin, "rb") as f:
        packed_data = f.read()

    with open(output_header, "w") as f:
        f.write('#include "graphics/Font.h"\n\n')
        f.write(f"const unsigned char {font_name}Data[] = {{\n")
        
        for byte in packed_data:
            # For every 1-bit-per-pixel byte, we write 8 bytes
            for i in range(7, -1, -1): # Extract bits MSB to LSB
                bit = (byte >> i) & 1
                f.write(f"{bit},")
            f.write("\n") # New line after every row for readability
            
        f.write("};\n\n")
        # bitluni struct: width, height, firstChar, charCount, dataPtr
        f.write(f"Font {font_name} = {{ 8, 8, {font_name}Data, 0, 256 }};\n")

inflate_to_bitluni("cp866.08", "CodePage866_8x8.cpp")
