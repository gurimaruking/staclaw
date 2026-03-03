#!/usr/bin/env python3
"""
Download Misaki 8x8 Japanese font data from Tamakichi's Arduino library
and generate a C source file for the staclaw project.

Usage: python tools/gen_misaki_font.py
Output: components/ui/src/ui_font_jp_data.c
"""

import urllib.request
import re
import os
import sys

FONT_URL = "https://raw.githubusercontent.com/Tamakichi/Arduino-misakiUTF16/master/src/misakiUTF16FontData.h"
OUTPUT_DIR = os.path.join(os.path.dirname(__file__), "..", "components", "ui", "src")
OUTPUT_FILE = os.path.join(OUTPUT_DIR, "ui_font_jp_data.c")

def download_font_header():
    """Download the font data header from GitHub."""
    print(f"Downloading from {FONT_URL}...")
    req = urllib.request.Request(FONT_URL, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=30) as resp:
        data = resp.read().decode("utf-8")
    print(f"Downloaded {len(data)} bytes")
    return data

def extract_array(text, array_name):
    """Extract a C array from the source text."""
    # Find the array declaration and its contents between { }
    # Handle both PROGMEM and non-PROGMEM variants
    pattern = rf'{array_name}\s*\[\s*\]\s*(?:PROGMEM\s*)?=\s*\{{(.*?)\}};'
    match = re.search(pattern, text, re.DOTALL)
    if not match:
        # Try alternative pattern without PROGMEM
        pattern = rf'(?:PROGMEM\s+)?{array_name}\s*\[\s*\]\s*=\s*\{{(.*?)\}};'
        match = re.search(pattern, text, re.DOTALL)
    if not match:
        # Try yet another pattern - static const with PROGMEM
        pattern = rf'static\s+const\s+\w+\s+{array_name}\s*\[\s*\]\s*(?:PROGMEM\s*)?=\s*\{{(.*?)\}};'
        match = re.search(pattern, text, re.DOTALL)
    if not match:
        print(f"ERROR: Could not find array '{array_name}' in source")
        print("Searching for similar patterns...")
        for line in text.split('\n'):
            if array_name in line:
                print(f"  Found: {line.strip()[:100]}")
        return None

    content = match.group(1)
    # Remove comments
    content = re.sub(r'//[^\n]*', '', content)
    content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)
    # Extract hex values
    values = re.findall(r'0[xX][0-9a-fA-F]+', content)
    return values

def generate_c_file(fdata_values, ftable_values):
    """Generate the C source file with font data."""
    num_glyphs = len(ftable_values)
    bytes_per_glyph = 7  # Misaki uses 7 bytes per glyph (8th row is always 0)

    print(f"Generating C file: {num_glyphs} glyphs, {len(fdata_values)} font bytes")

    lines = []
    lines.append("/* Auto-generated Misaki Gothic 8x8 Japanese font data */")
    lines.append("/* Source: https://github.com/Tamakichi/Arduino-misakiUTF16 */")
    lines.append("/* License: Free (public domain) */")
    lines.append("/* Generator: tools/gen_misaki_font.py */")
    lines.append("")
    lines.append('#include "ui_font_jp.h"')
    lines.append('#include <stddef.h>')
    lines.append("")
    lines.append(f"#define MISAKI_NUM_GLYPHS  {num_glyphs}")
    lines.append(f"#define MISAKI_BYTES_PER_GLYPH  {bytes_per_glyph}")
    lines.append("")

    # Font bitmap data
    lines.append(f"/* Font bitmap data: {num_glyphs} glyphs x {bytes_per_glyph} bytes each */")
    lines.append(f"static const uint8_t misaki_fdata[{len(fdata_values)}] = {{")

    # Write fdata in rows of 16 values
    for i in range(0, len(fdata_values), 16):
        chunk = fdata_values[i:i+16]
        line = "    " + ",".join(chunk) + ","
        lines.append(line)

    lines.append("};")
    lines.append("")

    # Unicode mapping table
    lines.append(f"/* Unicode codepoint mapping table (sorted, {num_glyphs} entries) */")
    lines.append(f"static const uint16_t misaki_ftable[{num_glyphs}] = {{")

    # Write ftable in rows of 12 values
    for i in range(0, len(ftable_values), 12):
        chunk = ftable_values[i:i+12]
        line = "    " + ",".join(chunk) + ","
        lines.append(line)

    lines.append("};")
    lines.append("")

    # Lookup function
    lines.append("/* Binary search for Unicode codepoint -> glyph index */")
    lines.append("static int misaki_find(uint16_t codepoint)")
    lines.append("{")
    lines.append("    int low = 0;")
    lines.append(f"    int high = MISAKI_NUM_GLYPHS - 1;")
    lines.append("    while (low <= high) {")
    lines.append("        int mid = low + ((high - low) >> 1);")
    lines.append("        uint16_t val = misaki_ftable[mid];")
    lines.append("        if (val == codepoint) return mid;")
    lines.append("        if (val < codepoint) low = mid + 1;")
    lines.append("        else high = mid - 1;")
    lines.append("    }")
    lines.append("    return -1;")
    lines.append("}")
    lines.append("")

    # Public API
    lines.append("const uint8_t *ui_font_jp_glyph(uint32_t codepoint)")
    lines.append("{")
    lines.append("    if (codepoint > 0xFFFF) return NULL;")
    lines.append("    int idx = misaki_find((uint16_t)codepoint);")
    lines.append("    if (idx < 0) {")
    lines.append("        /* Try fallback: white square U+25A1 */")
    lines.append("        idx = misaki_find(0x25A1);")
    lines.append("        if (idx < 0) return NULL;")
    lines.append("    }")
    lines.append(f"    return &misaki_fdata[idx * MISAKI_BYTES_PER_GLYPH];")
    lines.append("}")
    lines.append("")
    lines.append("int ui_font_jp_glyph_count(void)")
    lines.append("{")
    lines.append("    return MISAKI_NUM_GLYPHS;")
    lines.append("}")
    lines.append("")

    return "\n".join(lines) + "\n"

def main():
    try:
        source = download_font_header()
    except Exception as e:
        print(f"Download failed: {e}")
        sys.exit(1)

    fdata_values = extract_array(source, "fdata")
    ftable_values = extract_array(source, "ftable")

    if not fdata_values or not ftable_values:
        print("Failed to extract font data!")
        print("\nSearching for array declarations in source:")
        for line in source.split('\n'):
            if 'static' in line and ('[' in line or 'PROGMEM' in line):
                print(f"  {line.strip()[:120]}")
        sys.exit(1)

    print(f"Extracted: fdata={len(fdata_values)} bytes, ftable={len(ftable_values)} entries")

    c_code = generate_c_file(fdata_values, ftable_values)

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        f.write(c_code)

    print(f"Generated: {OUTPUT_FILE}")
    print(f"File size: {len(c_code)} bytes")

if __name__ == "__main__":
    main()
