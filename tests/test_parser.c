/*
 * test_parser.c — Parser tests
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

int test_parser_all(void)
{
    int errors = 0;

    /* Test 1: Parse Print statement */
    {
        amos_token_list_t *tokens = amos_tokenize("Print \"Hello World\"");
        int pos = 0;
        amos_node_t *node = amos_parse_line(tokens->tokens, &pos, tokens->count);
        ASSERT(node != NULL, "parse returned NULL");
        ASSERT(node->type == NODE_PRINT, "expected PRINT node");
        ASSERT(node->child_count >= 1, "expected at least 1 child");
        ASSERT(node->children[0]->type == NODE_STRING_LITERAL, "expected string literal");
        amos_node_free(node);
        amos_token_list_free(tokens);
    }

    /* Test 2: Parse assignment */
    {
        amos_token_list_t *tokens = amos_tokenize("X=42");
        int pos = 0;
        amos_node_t *node = amos_parse_line(tokens->tokens, &pos, tokens->count);
        ASSERT(node != NULL, "parse returned NULL");
        ASSERT(node->type == NODE_LET, "expected LET node");
        ASSERT(node->child_count >= 1, "expected value child");
        amos_node_free(node);
        amos_token_list_free(tokens);
    }

    /* Test 3: Parse For loop */
    {
        amos_token_list_t *tokens = amos_tokenize("For I=1 To 10");
        int pos = 0;
        amos_node_t *node = amos_parse_line(tokens->tokens, &pos, tokens->count);
        ASSERT(node != NULL, "parse returned NULL");
        ASSERT(node->type == NODE_FOR, "expected FOR node");
        ASSERT(node->child_count >= 2, "expected start and end children");
        amos_node_free(node);
        amos_token_list_free(tokens);
    }

    /* Test 4: Parse If/Then */
    {
        amos_token_list_t *tokens = amos_tokenize("If X=1 Then Print \"yes\"");
        int pos = 0;
        amos_node_t *node = amos_parse_line(tokens->tokens, &pos, tokens->count);
        ASSERT(node != NULL, "parse returned NULL");
        ASSERT(node->type == NODE_IF, "expected IF node");
        ASSERT(node->child_count >= 2, "expected condition and body");
        amos_node_free(node);
        amos_token_list_free(tokens);
    }

    /* Test 5: Parse Screen Open */
    {
        amos_token_list_t *tokens = amos_tokenize("Screen Open 0,320,200,5");
        int pos = 0;
        amos_node_t *node = amos_parse_line(tokens->tokens, &pos, tokens->count);
        ASSERT(node != NULL, "parse returned NULL");
        ASSERT(node->type == NODE_COMMAND, "expected COMMAND node");
        ASSERT(node->token.type == TOK_SCREEN_OPEN, "expected SCREEN_OPEN");
        ASSERT(node->child_count >= 4, "expected 4 arguments");
        amos_node_free(node);
        amos_token_list_free(tokens);
    }

    printf("  %d tests, %d errors\n", 5, errors);
    return errors;
}
