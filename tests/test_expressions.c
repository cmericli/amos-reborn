/*
 * test_expressions.c — Expression evaluator tests
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

int test_expressions_all(void)
{
    int errors = 0;

    /* Test 1: Simple integer expression */
    {
        amos_token_list_t *tokens = amos_tokenize("5+3");
        int pos = 0;
        amos_node_t *node = amos_parse_expression(tokens->tokens, &pos, tokens->count);
        ASSERT(node != NULL, "parse returned NULL");
        ASSERT(node->type == NODE_BINARY_OP, "expected BINARY_OP");
        ASSERT(node->token.type == TOK_PLUS, "expected PLUS");
        amos_node_free(node);
        amos_token_list_free(tokens);
    }

    /* Test 2: Precedence: 2+3*4 should parse as 2+(3*4) */
    {
        amos_token_list_t *tokens = amos_tokenize("2+3*4");
        int pos = 0;
        amos_node_t *node = amos_parse_expression(tokens->tokens, &pos, tokens->count);
        ASSERT(node != NULL, "parse returned NULL");
        ASSERT(node->type == NODE_BINARY_OP, "expected BINARY_OP at root");
        ASSERT(node->token.type == TOK_PLUS, "root should be PLUS");
        ASSERT(node->children[1]->type == NODE_BINARY_OP, "right child should be BINARY_OP");
        ASSERT(node->children[1]->token.type == TOK_MULTIPLY, "right child should be MULTIPLY");
        amos_node_free(node);
        amos_token_list_free(tokens);
    }

    /* Test 3: Parenthesized expression */
    {
        amos_token_list_t *tokens = amos_tokenize("(2+3)*4");
        int pos = 0;
        amos_node_t *node = amos_parse_expression(tokens->tokens, &pos, tokens->count);
        ASSERT(node != NULL, "parse returned NULL");
        ASSERT(node->type == NODE_BINARY_OP, "expected BINARY_OP at root");
        ASSERT(node->token.type == TOK_MULTIPLY, "root should be MULTIPLY");
        ASSERT(node->children[0]->type == NODE_BINARY_OP, "left child should be BINARY_OP");
        ASSERT(node->children[0]->token.type == TOK_PLUS, "left child should be PLUS");
        amos_node_free(node);
        amos_token_list_free(tokens);
    }

    /* Test 4: Unary minus */
    {
        amos_token_list_t *tokens = amos_tokenize("-5");
        int pos = 0;
        amos_node_t *node = amos_parse_expression(tokens->tokens, &pos, tokens->count);
        ASSERT(node != NULL, "parse returned NULL");
        ASSERT(node->type == NODE_UNARY_OP, "expected UNARY_OP");
        amos_node_free(node);
        amos_token_list_free(tokens);
    }

    /* Test 5: Comparison */
    {
        amos_token_list_t *tokens = amos_tokenize("X>10");
        int pos = 0;
        amos_node_t *node = amos_parse_expression(tokens->tokens, &pos, tokens->count);
        ASSERT(node != NULL, "parse returned NULL");
        ASSERT(node->type == NODE_BINARY_OP, "expected BINARY_OP");
        ASSERT(node->token.type == TOK_GREATER, "expected GREATER");
        amos_node_free(node);
        amos_token_list_free(tokens);
    }

    printf("  %d tests, %d errors\n", 5, errors);
    return errors;
}
