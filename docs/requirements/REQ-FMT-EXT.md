# AMOS Reborn — File Format & Extension System Requirements

**Document:** REQ-FMT-EXT
**Generated:** 2026-04-10
**Source:** AMOS Technical Blueprint §7 (File Formats), §8 (Extension System)
**Scope:** All testable specifications for binary file loading and the extension subsystem

---

## File Format Requirements (REQ-FMT)

### .AMOS File Structure

```
REQ-FMT-001: .AMOS header — first 16 bytes = "AMOS Basic V1.3 " or "AMOS Pro V?.?? "
  Acceptance: Loading a file starting with "AMOS Basic" returns AMOS_FILE_13;
              "AMOS Pro" returns AMOS_FILE_PRO; anything else returns AMOS_FILE_UNKNOWN
              and the loader rejects the file
  Level: 2 (subsystem)
  Source: Blueprint §7 ".AMOS File Structure"
  Status: IMPL — detect_file_type() in amos_loader.c checks "AMOS Basic" (10 bytes)
          and "AMOS Pro" (8 bytes)
```

```
REQ-FMT-002: .AMOS tested/untested flag — byte 11 distinguishes 'V' (tested) from 'v' (untested)
  Acceptance: A file with byte 11 = 'V' is marked tested; byte 11 = 'v' is marked untested;
              both load successfully, with the flag accessible to the runtime
  Level: 2 (subsystem)
  Source: Blueprint §7 ".AMOS File Structure"
  Status: TODO — detect_file_type() does not inspect byte 11; no tested/untested flag
          is stored in the runtime state
```

```
REQ-FMT-003: .AMOS source length — bytes 16-19 are a big-endian 32-bit unsigned integer
             giving the length of the tokenized source section in bytes
  Acceptance: BE32(data+16) == exact byte count of tokenized source following byte 20;
              if value exceeds file size, loader truncates with warning (not crash)
  Level: 2 (subsystem)
  Source: Blueprint §7 ".AMOS File Structure"
  Status: IMPL — amos_load_amos_file() reads BE32(data+16) and truncates if > file size
```

```
REQ-FMT-004: .AMOS bank section marker — the 4 bytes immediately after tokenized source = "AmBs"
  Acceptance: After source_offset + source_length, memcmp(..., "AmBs", 4) == 0 triggers
              bank parsing; absence logs "no bank section found" and is non-fatal
  Level: 2 (subsystem)
  Source: Blueprint §7 ".AMOS File Structure"
  Status: IMPL — process_banks() checks for "AmBs" at expected offset
```

```
REQ-FMT-005: .AMOS bank count — 2 bytes (big-endian uint16) immediately after "AmBs" marker
  Acceptance: BE16(data+offset+4) returns the number of banks to parse; value 0 means
              no banks to process
  Level: 2 (subsystem)
  Source: Blueprint §7 ".AMOS File Structure"
  Status: IMPL — process_banks() reads BE16(data+offset+4) as bank_count
```

### Tokenized Line Format

```
REQ-FMT-010: Tokenized line — byte 0 = line length / 2 (in words); total line bytes = byte0 * 2
  Acceptance: For each line, the detokenizer advances by (byte0 * 2) bytes; a value of 0
              terminates the line stream
  Level: 1 (unit)
  Source: Blueprint §7 "Tokenized Line Format"
  Status: IMPL — amos_detokenize() reads line_len_half at data[pos], computes
          line_bytes = line_len_half * 2, breaks on 0
```

```
REQ-FMT-011: Tokenized line — byte 1 = indent level (spaces + 1)
  Acceptance: indent_byte == 1 produces 0 spaces; indent_byte == 5 produces 4 spaces;
              indent_byte == 0 produces 0 spaces
  Level: 1 (unit)
  Source: Blueprint §7 "Tokenized Line Format"
  Status: IMPL — amos_detokenize() computes indent = (indent_byte > 1) ? indent_byte - 1 : 0
```

```
REQ-FMT-012: Tokenized line — token stream is 16-bit big-endian words starting at byte 2
  Acceptance: Each token is read as BE16(data+tpos) and dispatched by value;
              all multi-byte fields within token data are also big-endian
  Level: 1 (unit)
  Source: Blueprint §7 "Tokenized Line Format"
  Status: IMPL — detokenizer reads BE16 at each token position
```

```
REQ-FMT-013: Tokenized line — 0x0000 word marks end of line
  Acceptance: When token == 0x0000, the detokenizer stops processing the current line
              and emits a newline
  Level: 1 (unit)
  Source: Blueprint §7 "Tokenized Line Format"
  Status: IMPL — token == 0x0000 breaks the inner while loop
```

### Token IDs

```
REQ-FMT-020: Variable token (0x0006) — format: +0 2B token, +2 2B reserved, +4 1B name_length,
             +5 1B type_flags (bit0=float#, bit1=string$), +6 N bytes name, padded to even
  Acceptance: Loading a variable token extracts name of exactly name_length bytes;
              type_flags & 1 appends '#'; type_flags & 2 appends '$';
              total consumed = 6 + ((name_length + 1) & ~1)
  Level: 1 (unit)
  Source: Blueprint §7 "Key Token IDs"
  Status: IMPL — amos_detokenize() handles TK_VAR (0x0006) with correct offsets
```

```
REQ-FMT-021: Label definition token (0x000C) — same format as variable, appends ':' after name
  Acceptance: Detokenized output ends with "labelname:"; consumed bytes same as REQ-FMT-020
  Level: 1 (unit)
  Source: Blueprint §7 "Key Token IDs"
  Status: IMPL — TK_LABEL_DEF handler appends ':'
```

```
REQ-FMT-022: Integer literal token (0x003E) — +2 4B big-endian signed int32
  Acceptance: BE32S(data+tpos+2) produces the correct signed integer;
              output is decimal string (e.g., "-42"); total consumed = 6 bytes
  Level: 1 (unit)
  Source: Blueprint §7 "Key Token IDs"
  Status: IMPL — TK_INT handler reads BE32S and formats with %d
```

```
REQ-FMT-023: Hex literal token (0x0036) — +2 4B big-endian uint32
  Acceptance: Output is "$" followed by uppercase hex digits (e.g., "$FF00");
              total consumed = 6 bytes
  Level: 1 (unit)
  Source: Blueprint §7 "Key Token IDs"
  Status: IMPL — TK_HEX handler reads BE32 and formats with "$%X"
```

```
REQ-FMT-024: Float literal token (0x0046) — +2 4B Motorola FFP (AMOS float format)
  Acceptance: amos_float_to_double(BE32(data+tpos+2)) produces correct IEEE double;
              output formatted with %g; total consumed = 6 bytes
  Level: 1 (unit)
  Source: Blueprint §7 "Key Token IDs"
  Status: IMPL — TK_FLOAT handler calls amos_float_to_double()
```

```
REQ-FMT-025: String literal token (0x0026/0x002E) — +2 2B big-endian length, +4 N bytes ASCII,
             padded to even
  Acceptance: Output is quoted string with exact content; consumed = 4 + ((length+1) & ~1);
              both token variants (0x0026, 0x002E) produce identical output
  Level: 1 (unit)
  Source: Blueprint §7 "Key Token IDs"
  Status: IMPL — TK_STRING and TK_STRING2 both handled identically
```

```
REQ-FMT-026: Extension token (0x004E) — +2 1B extension number, +3 1B unused,
             +4 2B offset into extension token table
  Acceptance: ext_num identifies extension slot (0-25); ext_off is the command table offset;
              total consumed = 6 bytes; currently outputs "[Ext%d:$%04X]" placeholder
  Level: 1 (unit)
  Source: Blueprint §7 "Key Token IDs"
  Status: PARTIAL — token is parsed and fields extracted, but output is a placeholder
          string rather than the actual extension keyword name
```

```
REQ-FMT-027: Rem token (0x064A/0x0646) — +2 1B unused, +3 1B text_length, +4 N bytes text,
             padded to even
  Acceptance: Output is "Rem " followed by text_length bytes of comment text;
              consumed = 4 + ((text_length+1) & ~1)
  Level: 1 (unit)
  Source: Blueprint §7 "Key Token IDs"
  Status: IMPL — both 0x064A and 0x0646 handled with same format
```

```
REQ-FMT-028: Procedure token (0x0376/0x0372) — +2 8B metadata (2B distance, 2B seed, 4B flags)
  Acceptance: Token is consumed as 2 + 8 = 10 bytes; "Procedure " is emitted;
              procedure name follows as a subsequent proc-name token
  Level: 1 (unit)
  Source: Blueprint §7 "Key Token IDs"
  Status: IMPL — handler emits "Procedure " and skips 8 metadata bytes
```

```
REQ-FMT-029: Binary literal token (0x001E) — +2 4B big-endian uint32
  Acceptance: Output is "%" followed by binary digits (no leading zeros except for value 0);
              total consumed = 6 bytes
  Level: 1 (unit)
  Source: Blueprint §7 (implied by token table)
  Status: IMPL — TK_BIN handler formats with leading-zero suppression
```

### Memory Bank Format (AmBk)

```
REQ-FMT-030: AmBk magic — bytes 0-3 = "AmBk"
  Acceptance: memcmp(data+bpos, "AmBk", 4) == 0 identifies a standard memory bank
  Level: 2 (subsystem)
  Source: Blueprint §7 "Memory Bank Format (AmBk)"
  Status: IMPL — process_banks() checks for "AmBk" magic
```

```
REQ-FMT-031: AmBk bank number — bytes 4-5 = big-endian uint16, range 1-16
  Acceptance: BE16(data+bpos+4) returns bank number; values outside 1-16 are rejected
  Level: 2 (subsystem)
  Source: Blueprint §7 "Memory Bank Format (AmBk)"
  Status: PARTIAL — bank number is read and logged but no range validation
```

```
REQ-FMT-032: AmBk memory type — bytes 6-7 = big-endian uint16 (0=chip, 1=fast)
  Acceptance: Value 0 maps to chip memory; value 1 maps to fast memory;
              other values produce a warning
  Level: 2 (subsystem)
  Source: Blueprint §7 "Memory Bank Format (AmBk)"
  Status: PARTIAL — read and logged as chip/fast but no validation of other values
```

```
REQ-FMT-033: AmBk length — bytes 8-11 = big-endian uint32; bits 0-27 = data length,
             bit 30 = chip flag
  Acceptance: bank_len = BE32(data+bpos+8) & 0x0FFFFFFF gives raw data size;
              (BE32(data+bpos+8) >> 30) & 1 gives chip flag
  Level: 2 (subsystem)
  Source: Blueprint §7 "Memory Bank Format (AmBk)"
  Status: IMPL — process_banks() masks with 0x0FFFFFFF for length
```

```
REQ-FMT-034: AmBk type name — bytes 12-19 = 8-byte ASCII type name, space-padded
  Acceptance: Type name is exactly 8 bytes; common values: "Sprites ", "Music   ",
              "Icons   "; trailing spaces preserved
  Level: 2 (subsystem)
  Source: Blueprint §7 "Memory Bank Format (AmBk)"
  Status: IMPL — 8 bytes copied and logged as type_name
```

```
REQ-FMT-035: AmBk raw data — bytes 20 to 20+length = bank payload
  Acceptance: Bank data starts at offset 20 (0x14); next bank starts at bpos + 20 + bank_len
  Level: 2 (subsystem)
  Source: Blueprint §7 "Memory Bank Format (AmBk)"
  Status: IMPL — process_banks() advances by 20 + bank_len
```

### Sprite/Icon Bank (AmSp/AmIc)

```
REQ-FMT-040: AmSp/AmIc magic — bytes 0-3 = "AmSp" or "AmIc"
  Acceptance: "AmSp" identifies a sprite bank; "AmIc" identifies an icon bank;
              both use the same internal format
  Level: 2 (subsystem)
  Source: Blueprint §7 "Sprite/Icon Bank (AmSp/AmIc)"
  Status: IMPL — bank_loader.c checks MAGIC_AMSP (0x416D5370) and MAGIC_AMIC (0x416D4963)
```

```
REQ-FMT-041: AmSp/AmIc sprite count — bytes 4-5 = big-endian uint16
  Acceptance: BE16(data+4) returns total number of sprites/icons in the bank;
              value 0 is rejected as an error
  Level: 2 (subsystem)
  Source: Blueprint §7 "Sprite/Icon Bank (AmSp/AmIc)"
  Status: IMPL — num_sprites = BE16(data+4); 0 returns error
```

```
REQ-FMT-042: Sprite entry header — 10 bytes per sprite: +0 2B width (16-pixel words),
             +2 2B height (pixels), +4 2B planes, +6 2B X hotspot, +8 2B Y hotspot
  Acceptance: Width in pixels = width_words * 16; all fields big-endian uint16;
              hotspot bits 14-15 are mask flags (AND with 0x3FFF for actual value)
  Level: 2 (subsystem)
  Source: Blueprint §7 "Sprite/Icon Bank (AmSp/AmIc)"
  Status: IMPL — bank_loader.c reads all 5 fields, masks hotspot with 0x3FFF
```

```
REQ-FMT-043: Sprite image data — interleaved planar: for each scanline, depth planes of
             (width_words * 2) bytes each appear consecutively
  Acceptance: Total image bytes = width_words * 2 * height * depth;
              plane p of row y starts at offset y * (row_plane_bytes * depth) + p * row_plane_bytes
  Level: 2 (subsystem)
  Source: Blueprint §7 "Sprite/Icon Bank (AmSp/AmIc)"
  Status: IMPL — planar_sprite_to_rgba() converts interleaved planar to RGBA
```

```
REQ-FMT-044: Empty sprite detection — width_words == 0 OR height == 0 OR depth == 0
             indicates an empty/placeholder sprite
  Acceptance: Empty sprites produce a 1x1 transparent pixel; header is still 10 bytes
              but no image data follows
  Level: 1 (unit)
  Source: Blueprint §7 (implied by format)
  Status: IMPL — is_empty_sprite() checks all three; produces 1x1 transparent
```

```
REQ-FMT-045: Sprite palette footer — 64 bytes after all sprite data: 32 entries of
             2-byte big-endian $0RGB
  Acceptance: Each palette entry: R = (rgb4 >> 8) & 0x0F, G = (rgb4 >> 4) & 0x0F,
              B = rgb4 & 0x0F; expanded to 8-bit by replication (0xA -> 0xAA);
              color 0 is always transparent regardless of palette value
  Level: 2 (subsystem)
  Source: Blueprint §7 "Sprite/Icon Bank (AmSp/AmIc)"
  Status: IMPL — rgb4_to_rgba() implements nibble extraction and replication;
          color 0 rendered as transparent in planar_sprite_to_rgba()
```

```
REQ-FMT-046: Planar-to-chunky conversion — bit p of pixel x in plane p determines
             color index bit p; bit_idx = 7 - (x % 8), byte_idx = x / 8
  Acceptance: For a 4-plane image, pixel at (2,0) with plane 0 bit set and plane 2 bit set
              produces color index 5 (binary 0101)
  Level: 1 (unit)
  Source: Blueprint §7 (planar format specification)
  Status: IMPL — both iff_loader.c and bank_loader.c implement this identically
```

### IFF/ILBM Loading

```
REQ-FMT-050: IFF FORM header — bytes 0-3 = "FORM" (0x464F524D)
  Acceptance: Files not starting with "FORM" are rejected with "Not a FORM file" error
  Level: 2 (subsystem)
  Source: Blueprint §7 "IFF/ILBM Loading"
  Status: IMPL — iff_loader.c checks form_id == ID_FORM
```

```
REQ-FMT-051: IFF ILBM type — bytes 8-11 = "ILBM" (0x494C424D)
  Acceptance: Non-ILBM FORM files are rejected with error showing actual type as 4 ASCII chars
  Level: 2 (subsystem)
  Source: Blueprint §7 "IFF/ILBM Loading"
  Status: IMPL — checks ilbm_id == ID_ILBM, error shows actual 4-char type
```

```
REQ-FMT-052: IFF chunk iteration — each chunk: 4B ID + 4B big-endian size + size bytes data;
             chunks padded to even boundary
  Acceptance: Parser reads all chunks sequentially; advances by 8 + chunk_size + (1 if odd);
              chunks beyond file boundary terminate parsing gracefully
  Level: 2 (subsystem)
  Source: Blueprint §7 "IFF/ILBM Loading"
  Status: IMPL — while loop in amos_load_iff_from_memory() with even-padding
```

```
REQ-FMT-053: BMHD chunk — 20 bytes minimum: 2B width, 2B height, 2B x_origin, 2B y_origin,
             1B num_planes, 1B masking, 1B compression, 1B pad, 2B transparent_color,
             1B x_aspect, 1B y_aspect, 2B page_width, 2B page_height
  Acceptance: All 11 fields parsed correctly from big-endian data;
              width=0 or height=0 or num_planes=0 rejected as invalid
  Level: 2 (subsystem)
  Source: Blueprint §7 "IFF/ILBM Loading"
  Status: IMPL — parse_bmhd() reads all fields; validation in main loader
```

```
REQ-FMT-054: CMAP chunk — N*3 bytes of RGB triplets (R, G, B each 8-bit)
  Acceptance: num_colors = chunk_size / 3; Amiga OCS expansion: high nibble replicated
              to low nibble (0xA0 -> 0xAA); max 256 colors
  Level: 2 (subsystem)
  Source: Blueprint §7 "IFF/ILBM Loading"
  Status: IMPL — parse_cmap() implements nibble replication, caps at 256
```

```
REQ-FMT-055: BODY chunk — raw or compressed pixel data
  Acceptance: compression == 0: direct copy of plane data;
              compression == 1: ByteRun1 decompression;
              other values: error "Unknown compression type"
  Level: 2 (subsystem)
  Source: Blueprint §7 "IFF/ILBM Loading"
  Status: IMPL — both uncompressed and ByteRun1 paths implemented
```

```
REQ-FMT-056: ByteRun1 (PackBits) decompression algorithm
  Acceptance: n >= 0: copy next n+1 literal bytes;
              n < 0 and n != -128: repeat next byte (-n+1) times;
              n == -128: no-op (skip);
              returns total bytes written or -1 on truncation
  Level: 1 (unit)
  Source: Blueprint §7 "IFF/ILBM Loading"
  Status: IMPL — byterun1_decompress() implements all three cases correctly
```

```
REQ-FMT-057: IFF planar-to-chunky — row-interleaved bitplane data converted to 1-byte-per-pixel
             indexed format; row_bytes = ((width+15)/16)*2 (word-aligned)
  Acceptance: For masking == 1, an extra mask plane exists per row;
              total row size = row_bytes * (num_planes + has_mask);
              pixel color index assembled from bits across all image planes
  Level: 2 (subsystem)
  Source: Blueprint §7 "IFF/ILBM Loading"
  Status: IMPL — planar_to_chunky() in iff_loader.c handles mask plane
```

```
REQ-FMT-058: CAMG chunk — Amiga display mode flags (HAM, EHB, interlace, etc.)
  Acceptance: CAMG chunk recognized and display mode flags stored;
              HAM mode (bit 11) and EHB mode (bit 7) affect color interpretation
  Level: 2 (subsystem)
  Source: Blueprint §7 "IFF/ILBM Loading"
  Status: TODO — chunk is skipped in the "default" case of the switch statement
```

```
REQ-FMT-059: CCRT chunk — color cycling range specification
  Acceptance: CCRT data parsed to extract cycling direction, rate, and color range;
              cycling can be applied to palette at runtime
  Level: 2 (subsystem)
  Source: Blueprint §7 "IFF/ILBM Loading"
  Status: TODO — chunk is skipped
```

```
REQ-FMT-060: AMSC chunk — AMOS-specific screen configuration
  Acceptance: AMSC data parsed to extract AMOS screen parameters (display position,
              dual playfield flags, etc.)
  Level: 2 (subsystem)
  Source: Blueprint §7 "IFF/ILBM Loading"
  Status: TODO — chunk is skipped
```

```
REQ-FMT-061: IFF missing BMHD — if no BMHD chunk found, return error
  Acceptance: Error message "IFF: Missing BMHD chunk" and return -1
  Level: 2 (subsystem)
  Source: Blueprint §7 (implied)
  Status: IMPL — explicit check after chunk parsing
```

```
REQ-FMT-062: IFF missing BODY — if no BODY chunk found, return error
  Acceptance: Error message "IFF: Missing BODY chunk" and return -1
  Level: 2 (subsystem)
  Source: Blueprint §7 (implied)
  Status: IMPL — explicit check after chunk parsing
```

```
REQ-FMT-063: IFF file size limit — files larger than 16 MB are rejected
  Acceptance: file_size > 16*1024*1024 returns error "Invalid file size"
  Level: 2 (subsystem)
  Source: Implementation constraint
  Status: IMPL — checked in amos_load_iff_to_screen()
```

### Variable Token Detail

```
REQ-FMT-070: Variable token layout — +0 2B token (0x0006), +2 2B reserved/linking offset,
             +4 1B name_length, +5 1B type_flags, +6 name_length bytes name
  Acceptance: Reserved bytes at +2/+3 are ignored (used for runtime linking on 68000);
              name is NOT null-terminated in the binary; null bytes within name are skipped
              in output
  Level: 1 (unit)
  Source: Blueprint §7 "Key Token IDs" + §2 "Token Format"
  Status: IMPL — null byte filtering in variable name output loop
```

```
REQ-FMT-071: Variable type flags — bit 0 = float (#), bit 1 = string ($);
             both bits 0 = integer, bits 0+1 both set is invalid
  Acceptance: type_flags == 0: no suffix (integer);
              type_flags == 1: '#' suffix (float);
              type_flags == 2: '$' suffix (string);
              type_flags == 3: both '#' and '$' (should be rejected or warned)
  Level: 1 (unit)
  Source: Blueprint §7 "Key Token IDs"
  Status: PARTIAL — suffixes appended correctly but type_flags == 3 not validated
```

```
REQ-FMT-072: Variable name padding — name is padded to even byte boundary after the name bytes
  Acceptance: Consumed bytes = 6 + ((name_length + 1) & ~1);
              e.g., name "AB" (len=2) consumes 6+2=8; name "ABC" (len=3) consumes 6+4=10
  Level: 1 (unit)
  Source: Blueprint §7 "Key Token IDs"
  Status: IMPL — padded = (name_len + 1) & ~(size_t)1 used consistently
```

---

## Extension System Requirements (REQ-EXT)

### Architecture

```
REQ-EXT-001: Extension slot count — system supports up to 26 extension slots (0-25)
  Acceptance: Extension numbers 0-25 are valid; extension number >= 26 is rejected;
              extension token (0x004E) byte +2 must be in range 0-25
  Level: 2 (subsystem)
  Source: Blueprint §8 "Architecture"
  Status: TODO — no extension slot array or validation exists; tokens parsed but
          ext_num not range-checked
```

```
REQ-EXT-002: Extension registration — each extension occupies 16 bytes at ExtAdr + ExtNb*16:
             +0 data zone base (4B), +4 default/reset handler (4B),
             +8 end/quit handler (4B), +12 bank change notification handler (4B)
  Acceptance: Extension data structure has 4 function pointer/address slots per extension;
              all 26 slots initialized to NULL/0 at startup
  Level: 2 (subsystem)
  Source: Blueprint §8 "Architecture"
  Status: TODO — no extension registration data structure implemented
```

### Extension Header

```
REQ-EXT-010: Extension header format — 4 big-endian 32-bit offsets followed by
             force-include flag (16-bit) and "AP20" magic (4 bytes)
  Acceptance: Offsets: [0] token table, [1] library routines, [2] title string, [3] end;
              each offset is relative to the previous section;
              total header size = 4*4 + 2 + 4 = 22 bytes
  Level: 2 (subsystem)
  Source: Blueprint §8 "Extension Header"
  Status: TODO — no extension header parser implemented
```

```
REQ-EXT-011: Force-include flag — 16-bit word: 0 = optional, -1 (0xFFFF) = force-include
  Acceptance: Extensions with force-include flag load even if not referenced by the program;
              optional extensions only load when referenced by extension tokens
  Level: 2 (subsystem)
  Source: Blueprint §8 "Extension Header"
  Status: TODO — no force-include logic
```

```
REQ-EXT-012: AP20 magic — 4 bytes "AP20" at offset 18 in extension header
  Acceptance: Extensions without "AP20" magic are rejected as incompatible format;
              magic indicates AMOS Pro V2.0 extension format
  Level: 2 (subsystem)
  Source: Blueprint §8 "Extension Header"
  Status: TODO — no magic validation
```

### Command Table Entry Format

```
REQ-EXT-020: Command table entry — 2 words (handlers) + name + param spec + terminator
  Acceptance: Word 0 = instruction handler offset; Word 1 = function handler offset;
              offset 0x0000 means "not available as instruction/function";
              both non-zero means command works as both
  Level: 2 (subsystem)
  Source: Blueprint §8 "Command Table Entry Format"
  Status: TODO — no command table parser
```

```
REQ-EXT-021: Command name encoding — ASCII name with last character OR'd with $80
  Acceptance: Name bytes are ASCII (0x20-0x7E); final byte has bit 7 set;
              actual character = last_byte & 0x7F; name scan stops at byte with bit 7 set
  Level: 1 (unit)
  Source: Blueprint §8 "Command Table Entry Format"
  Status: TODO — no name decoder
```

```
REQ-EXT-022: Parameter specification — ASCII string after name: I=instruction, 0=int,
             1=float, 2=string, t=TO separator
  Acceptance: "I0,0" = instruction taking two ints separated by comma;
              "I0,0t0,0" = instruction with TO separator (e.g., SCREEN COPY);
              empty spec = no parameters
  Level: 2 (subsystem)
  Source: Blueprint §8 "Command Table Entry Format"
  Status: TODO — no param spec parser
```

```
REQ-EXT-023: Entry terminator — byte -1 (0xFF) ends the entry; -2 (0xFE) indicates a
             variant follows (same keyword, different parameter signature)
  Acceptance: Parser reads entries until 0xFF; on 0xFE, the next entry is an alternative
              signature for the same command
  Level: 2 (subsystem)
  Source: Blueprint §8 "Command Table Entry Format"
  Status: TODO — no terminator handling
```

### Coded Branch System

```
REQ-EXT-030: Rbsr — intra-extension call, 4 bytes encoded
  Acceptance: Rbsr encodes a PC-relative branch within the same extension;
              target must be within the extension's code segment;
              used instead of 68000 BSR for relocatability
  Level: 2 (subsystem)
  Source: Blueprint §8 "Coded Branch System"
  Status: TODO — no Rbsr implementation (native code concept; may be N/A for reborn)
```

```
REQ-EXT-031: Rjsr — cross-extension call to AMOSPro.Lib
  Acceptance: Rjsr calls a routine in the main AMOS library from an extension;
              return address preserved; target resolved at load time
  Level: 2 (subsystem)
  Source: Blueprint §8 "Coded Branch System"
  Status: TODO — no Rjsr implementation (native code concept; may be N/A for reborn)
```

```
REQ-EXT-032: SyCall — system vector table call
  Acceptance: SyCall dispatches through the system call vector table;
              provides access to AMOS runtime services (memory allocation,
              error handling, etc.)
  Level: 2 (subsystem)
  Source: Blueprint §8 "Coded Branch System"
  Status: TODO — no SyCall implementation (native code concept; may be N/A for reborn)
```

```
REQ-EXT-033: EcCall — screen vector table call
  Acceptance: EcCall dispatches through the screen/display vector table;
              provides access to screen operations from extensions
  Level: 2 (subsystem)
  Source: Blueprint §8 "Coded Branch System"
  Status: TODO — no EcCall implementation (native code concept; may be N/A for reborn)
```

### Standard Extensions

```
REQ-EXT-040: Music extension — slot 1, ~50 commands including Track, Sam, Wave,
             Bell/Boom/Shoot, Say
  Acceptance: Extension slot 1 reserved for Music; loading a program with extension
              token ext_num=1 triggers music extension; detokenizer resolves
              music command names from the extension's command table
  Level: 3 (golden)
  Source: Blueprint §8 "Standard Extensions"
  Status: TODO — no extension loading; music commands not implemented via extension system
```

```
REQ-EXT-041: Compact extension — slot 2, compression commands
  Acceptance: Extension slot 2 reserved for Compact; provides data compression/decompression
  Level: 3 (golden)
  Source: Blueprint §8 "Standard Extensions"
  Status: TODO
```

```
REQ-EXT-042: Request extension — slot 3, REQUEST ON/OFF/WB (3 commands)
  Acceptance: Extension slot 3 provides exactly 3 commands: REQUEST ON, REQUEST OFF,
              REQUEST WB; controls file requester behavior
  Level: 3 (golden)
  Source: Blueprint §8 "Standard Extensions"
  Status: TODO
```

```
REQ-EXT-043: 3D extension — slot 4, AMOS 3D graphics commands
  Acceptance: Extension slot 4 reserved for 3D graphics operations
  Level: 3 (golden)
  Source: Blueprint §8 "Standard Extensions"
  Status: TODO
```

```
REQ-EXT-044: Compiler extension — slot 5, COMPILE/SQUASH/UNSQUASH commands
  Acceptance: Extension slot 5 provides COMPILE, SQUASH, and UNSQUASH commands
  Level: 3 (golden)
  Source: Blueprint §8 "Standard Extensions"
  Status: TODO
```

```
REQ-EXT-045: IOPorts extension — slot 6, serial/parallel I/O commands
  Acceptance: Extension slot 6 provides serial and parallel port I/O commands
  Level: 3 (golden)
  Source: Blueprint §8 "Standard Extensions"
  Status: TODO
```

### Extension Constraints

```
REQ-EXT-050: Extension code must be fully PC-relative — no absolute addresses
  Acceptance: Extension binary contains no relocation table; all code references
              are relative; single hunk format
  Level: 2 (subsystem)
  Source: Blueprint §8 "Creating Custom Extensions"
  Status: TODO — N/A for C reimplementation; extensions will use native C API
```

```
REQ-EXT-051: Extension routine size limit — each routine < 32KB
  Acceptance: No single extension routine exceeds 32,768 bytes of code;
              this is a 68000 branch displacement limit
  Level: 2 (subsystem)
  Source: Blueprint §8 "Creating Custom Extensions"
  Status: TODO — N/A for C reimplementation
```

```
REQ-EXT-052: Extension return convention — D3 = return value, D2 = type
             (0=int, 1=float, 2=string); D6/D7 must be preserved
  Acceptance: Extension functions return values through the standard register convention;
              in the C reimplementation, this maps to a return struct or output parameters
  Level: 2 (subsystem)
  Source: Blueprint §8 "Creating Custom Extensions"
  Status: TODO — no extension calling convention defined
```

```
REQ-EXT-053: Extension token table address — stored at AdTokens(a5) + ExtNb*4;
             27 slots (26 extensions + base language)
  Acceptance: Token table lookup: AdTokens[ext_num] gives base address of that
              extension's token/command table; slot 0 = base language
  Level: 2 (subsystem)
  Source: Blueprint §8 "Architecture" + §1 "Key A5-Relative Offsets"
  Status: TODO — no AdTokens equivalent implemented
```

---

## Summary

| Category | Total | IMPL | PARTIAL | TODO |
|----------|-------|------|---------|------|
| REQ-FMT (File Formats) | 36 | 28 | 3 | 5 |
| REQ-EXT (Extensions) | 17 | 0 | 0 | 17 |
| **Total** | **53** | **28** | **3** | **22** |

### PARTIAL items requiring attention:
- **REQ-FMT-026**: Extension token outputs placeholder instead of resolved keyword name
- **REQ-FMT-031**: AmBk bank number not range-validated (1-16)
- **REQ-FMT-071**: Variable type_flags == 3 (both float+string) not rejected

### High-priority TODO items:
- **REQ-FMT-002**: Tested/untested flag not extracted from header byte 11
- **REQ-FMT-058**: CAMG chunk (HAM/EHB display modes) not parsed
- **REQ-EXT-001 through REQ-EXT-053**: Entire extension system is unimplemented
