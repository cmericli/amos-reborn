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

    printf("  %d tests, %d errors\n", 8, errors);
    return errors;
}
