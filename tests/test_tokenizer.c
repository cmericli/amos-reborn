/*
 * test_tokenizer.c — Tokenizer tests
 */

#include "amos.h"
#include <stdio.h>
#include <string.h>

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        errors++; \
    } \
} while(0)

int test_tokenizer_all(void)
{
    int errors = 0;

    /* Test 1: Simple Print statement */
    {
        amos_token_list_t *tokens = amos_tokenize("Print \"Hello\"");
        ASSERT(tokens != NULL, "tokenize returned NULL");
        ASSERT(tokens->count >= 3, "expected at least 3 tokens");
        ASSERT(tokens->tokens[0].type == TOK_PRINT, "expected PRINT token");
        ASSERT(tokens->tokens[1].type == TOK_STRING, "expected STRING token");
        ASSERT(strcmp(tokens->tokens[1].sval, "Hello") == 0, "string content mismatch");
        amos_token_list_free(tokens);
    }

    /* Test 2: Assignment */
    {
        amos_token_list_t *tokens = amos_tokenize("X=42");
        ASSERT(tokens != NULL, "tokenize returned NULL");
        ASSERT(tokens->tokens[0].type == TOK_IDENTIFIER, "expected IDENTIFIER");
        ASSERT(tokens->tokens[1].type == TOK_EQUAL, "expected EQUAL");
        ASSERT(tokens->tokens[2].type == TOK_INTEGER, "expected INTEGER");
        ASSERT(tokens->tokens[2].ival == 42, "expected 42");
        amos_token_list_free(tokens);
    }

    /* Test 3: Multi-word keyword */
    {
        amos_token_list_t *tokens = amos_tokenize("Screen Open 0,320,200,5");
        ASSERT(tokens != NULL, "tokenize returned NULL");
        ASSERT(tokens->tokens[0].type == TOK_SCREEN_OPEN, "expected SCREEN_OPEN");
        amos_token_list_free(tokens);
    }

    /* Test 4: For loop */
    {
        amos_token_list_t *tokens = amos_tokenize("For I=1 To 10 Step 2");
        ASSERT(tokens != NULL, "tokenize returned NULL");
        ASSERT(tokens->tokens[0].type == TOK_FOR, "expected FOR");
        ASSERT(tokens->tokens[1].type == TOK_IDENTIFIER, "expected identifier I");
        ASSERT(tokens->tokens[2].type == TOK_EQUAL, "expected =");
        ASSERT(tokens->tokens[3].type == TOK_INTEGER, "expected 1");
        ASSERT(tokens->tokens[4].type == TOK_TO, "expected TO");
        ASSERT(tokens->tokens[5].type == TOK_INTEGER, "expected 10");
        ASSERT(tokens->tokens[6].type == TOK_STEP, "expected STEP");
        ASSERT(tokens->tokens[7].type == TOK_INTEGER, "expected 2");
        amos_token_list_free(tokens);
    }

    /* Test 5: Comment */
    {
        amos_token_list_t *tokens = amos_tokenize("Rem This is a comment");
        ASSERT(tokens != NULL, "tokenize returned NULL");
        ASSERT(tokens->tokens[0].type == TOK_REM, "expected REM");
        amos_token_list_free(tokens);
    }

    /* Test 6: Operators */
    {
        amos_token_list_t *tokens = amos_tokenize("A+B*C-D/E");
        ASSERT(tokens != NULL, "tokenize returned NULL");
        ASSERT(tokens->tokens[0].type == TOK_IDENTIFIER, "expected A");
        ASSERT(tokens->tokens[1].type == TOK_PLUS, "expected +");
        ASSERT(tokens->tokens[2].type == TOK_IDENTIFIER, "expected B");
        ASSERT(tokens->tokens[3].type == TOK_MULTIPLY, "expected *");
        amos_token_list_free(tokens);
    }

    /* Test 7: String variable */
    {
        amos_token_list_t *tokens = amos_tokenize("A$=\"Hello\"");
        ASSERT(tokens != NULL, "tokenize returned NULL");
        ASSERT(tokens->tokens[0].type == TOK_IDENTIFIER, "expected identifier A$");
        ASSERT(strcmp(tokens->tokens[0].sval, "A$") == 0, "expected A$ identifier");
        amos_token_list_free(tokens);
    }

    /* Test 8: Comparison operators */
    {
        amos_token_list_t *tokens = amos_tokenize("A<>B");
        ASSERT(tokens != NULL, "tokenize returned NULL");
        ASSERT(tokens->tokens[1].type == TOK_NOT_EQUAL, "expected <>");
        amos_token_list_free(tokens);
    }

    /* Test 9: Colon separator */
    {
        amos_token_list_t *tokens = amos_tokenize("A=1 : B=2");
        ASSERT(tokens != NULL, "tokenize returned NULL");
        /* Should have: IDENT = INT COLON IDENT = INT EOF */
        int found_colon = 0;
        for (int i = 0; i < tokens->count; i++) {
            if (tokens->tokens[i].type == TOK_COLON) found_colon++;
        }
        ASSERT(found_colon == 1, "expected exactly 1 COLON token");
        amos_token_list_free(tokens);
    }

    /* Test 10: Select/Case/End Select tokens */
    {
        amos_token_list_t *tokens = amos_tokenize("Select X");
        ASSERT(tokens != NULL, "tokenize returned NULL");
        ASSERT(tokens->tokens[0].type == TOK_SELECT, "expected SELECT");
        amos_token_list_free(tokens);
    }

    /* Test 11: End Select multi-word */
    {
        amos_token_list_t *tokens = amos_tokenize("End Select");
        ASSERT(tokens != NULL, "tokenize returned NULL");
        ASSERT(tokens->tokens[0].type == TOK_END_SELECT, "expected END_SELECT");
        amos_token_list_free(tokens);
    }

    /* Test 12: Every keyword */
    {
        amos_token_list_t *tokens = amos_tokenize("Every 50 Gosub MyTimer");
        ASSERT(tokens != NULL, "tokenize returned NULL");
        ASSERT(tokens->tokens[0].type == TOK_EVERY, "expected EVERY");
        ASSERT(tokens->tokens[1].type == TOK_INTEGER, "expected 50");
        ASSERT(tokens->tokens[2].type == TOK_GOSUB, "expected GOSUB");
        amos_token_list_free(tokens);
    }

    /* Test 13: Clip keyword */
    {
        amos_token_list_t *tokens = amos_tokenize("Clip 10,20 To 100,200");
        ASSERT(tokens != NULL, "tokenize returned NULL");
        ASSERT(tokens->tokens[0].type == TOK_CLIP, "expected CLIP");
        amos_token_list_free(tokens);
    }

    /* Test 14: Reserve As Work */
    {
        amos_token_list_t *tokens = amos_tokenize("Reserve As Work 5,1024");
        ASSERT(tokens != NULL, "tokenize returned NULL");
        ASSERT(tokens->tokens[0].type == TOK_RESERVE_AS_WORK, "expected RESERVE_AS_WORK");
        amos_token_list_free(tokens);
    }

    /* Test 15: On keyword (for On X Goto) */
    {
        amos_token_list_t *tokens = amos_tokenize("On X Goto 10,20,30");
        ASSERT(tokens != NULL, "tokenize returned NULL");
        ASSERT(tokens->tokens[0].type == TOK_ON, "expected ON");
        amos_token_list_free(tokens);
    }

    /* Test 16: Doke/Deek/Loke/Leek */
    {
        amos_token_list_t *tokens = amos_tokenize("Doke 100,5");
        ASSERT(tokens != NULL && tokens->tokens[0].type == TOK_DOKE, "expected DOKE");
        amos_token_list_free(tokens);

        tokens = amos_tokenize("Loke 100,5");
        ASSERT(tokens != NULL && tokens->tokens[0].type == TOK_LOKE, "expected LOKE");
        amos_token_list_free(tokens);
    }

    /* Test 17: Set Line / Set Pattern / Set Font */
    {
        amos_token_list_t *tokens = amos_tokenize("Set Line $FF00");
        ASSERT(tokens != NULL && tokens->tokens[0].type == TOK_SET_LINE, "expected SET_LINE");
        amos_token_list_free(tokens);

        tokens = amos_tokenize("Set Pattern 3");
        ASSERT(tokens != NULL && tokens->tokens[0].type == TOK_SET_PATTERN, "expected SET_PATTERN");
        amos_token_list_free(tokens);

        tokens = amos_tokenize("Set Font 2");
        ASSERT(tokens != NULL && tokens->tokens[0].type == TOK_SET_FONT, "expected SET_FONT");
        amos_token_list_free(tokens);
    }

    printf("  %d tests, %d errors\n", 17, errors);
    return errors;
}
