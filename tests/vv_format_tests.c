/*
 * vv_format_tests.c — V&V tests for File Format & System Limits (REQ-FMT)
 *
 * Tests file format detection, system capacity limits,
 * and structural requirements.
 */

#include "vv_framework.h"

/* ══════════════════════════════════════════════════════════════════════
 *  System Capacity Limits
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-FMT-030: Maximum program lines is 65536") {
    VV_ASSERT(AMOS_MAX_PROGRAM_LINES == 65536,
              "AMOS_MAX_PROGRAM_LINES must be 65536");
}

VV_TEST("REQ-FMT-031: Maximum banks is 16") {
    VV_ASSERT(AMOS_MAX_BANKS == 16,
              "AMOS_MAX_BANKS must be 16");
}

VV_TEST("REQ-FMT-032: Maximum screens is at least 8") {
    VV_ASSERT(AMOS_MAX_SCREENS >= 8,
              "AMOS_MAX_SCREENS must be at least 8");
}

VV_TEST("REQ-FMT-033: Maximum sprites is 64") {
    VV_ASSERT(AMOS_MAX_SPRITES == 64,
              "AMOS_MAX_SPRITES must be 64");
}

VV_TEST("REQ-FMT-034: Maximum bobs is 64") {
    VV_ASSERT(AMOS_MAX_BOBS == 64,
              "AMOS_MAX_BOBS must be 64");
}

VV_TEST("REQ-FMT-035: Maximum AMAL channels is 16") {
    VV_ASSERT(AMOS_MAX_AMAL_CHANNELS == 16,
              "AMOS_MAX_AMAL_CHANNELS must be 16");
}

/* ══════════════════════════════════════════════════════════════════════
 *  IFF/ILBM Format (tested via IFF loader in test_iff.c)
 *  These verify the VV traceability layer
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-FMT-020: IFF loader rejects NULL input") {
    amos_state_t *s = vv_create();
    /* Loading NULL path should fail gracefully */
    int result = amos_load_iff(s, NULL);
    VV_ASSERT(result != 0, "IFF loader should reject NULL path");
    vv_destroy(s);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Bank format (Reserve/Erase commands)
 * ══════════════════════════════════════════════════════════════════════ */

VV_TEST("REQ-FMT-025: Reserve As Work creates bank with type BANK_WORK") {
    amos_state_t *s = vv_create();
    vv_run(s, "Reserve As Work 5,1024");
    VV_ASSERT(s->banks[5].data != NULL, "bank 5 should be allocated");
    VV_ASSERT(s->banks[5].size == 1024, "bank 5 should be 1024 bytes");
    VV_ASSERT(s->banks[5].type == BANK_WORK, "bank 5 should be BANK_WORK type");
    vv_destroy(s);
}

VV_TEST("REQ-FMT-026: Erase frees bank data and resets type") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "Reserve As Work 5,1024\n"
        "Erase 5\n"
    );
    VV_ASSERT(s->banks[5].data == NULL, "erased bank data should be NULL");
    VV_ASSERT(s->banks[5].size == 0, "erased bank size should be 0");
    VV_ASSERT(s->banks[5].type == BANK_EMPTY, "erased bank should be BANK_EMPTY");
    vv_destroy(s);
}

VV_TEST("REQ-FMT-027: Reserve As Data creates bank with type BANK_DATA") {
    amos_state_t *s = vv_create();
    vv_run(s, "Reserve As Data 3,512");
    VV_ASSERT(s->banks[3].data != NULL, "bank 3 should be allocated");
    VV_ASSERT(s->banks[3].type == BANK_DATA, "bank 3 should be BANK_DATA type");
    vv_destroy(s);
}

VV_TEST("REQ-FMT-028: Multiple banks can be allocated simultaneously") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "Reserve As Work 1,100\n"
        "Reserve As Work 2,200\n"
        "Reserve As Work 3,300\n"
    );
    VV_ASSERT(s->banks[1].size == 100, "bank 1 should be 100 bytes");
    VV_ASSERT(s->banks[2].size == 200, "bank 2 should be 200 bytes");
    VV_ASSERT(s->banks[3].size == 300, "bank 3 should be 300 bytes");
    vv_destroy(s);
}

VV_TEST("REQ-FMT-029: Erase All clears all banks") {
    amos_state_t *s = vv_create();
    vv_run(s,
        "Reserve As Work 1,100\n"
        "Reserve As Work 2,200\n"
        "Erase All\n"
    );
    VV_ASSERT(s->banks[1].data == NULL, "bank 1 should be erased");
    VV_ASSERT(s->banks[2].data == NULL, "bank 2 should be erased");
    vv_destroy(s);
}
