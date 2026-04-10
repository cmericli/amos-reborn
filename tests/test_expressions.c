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

    /* Test 6: Tab$ function */
    {
        amos_state_t *state = amos_create();
        amos_load_text(state, "A$=Tab$(5)");
        amos_run(state);
        amos_run_step(state);

        amos_var_t *var = amos_var_get(state, "A$");
        ASSERT(var != NULL, "A$ should exist");
        ASSERT(var->type == VAR_STRING, "A$ should be string");
        ASSERT(var->sval.data != NULL, "A$ data should not be NULL");
        ASSERT(strlen(var->sval.data) == 5, "Tab$(5) should be 5 spaces");
        amos_destroy(state);
    }

    /* Test 7: String$ function (Repeat$ alias — "Repeat" is a keyword so
     * Repeat$ is accessed via the existing String$ function) */
    {
        amos_state_t *state = amos_create();
        amos_load_text(state, "A$=String$(\"ab\",3)");
        amos_run(state);
        amos_run_step(state);

        amos_var_t *var = amos_var_get(state, "A$");
        ASSERT(var != NULL && var->type == VAR_STRING, "A$ should be string");
        ASSERT(var->sval.data && strcmp(var->sval.data, "ababab") == 0,
               "String$(\"ab\",3) should be \"ababab\"");
        amos_destroy(state);
    }

    /* Test 8: Insert$ function */
    {
        amos_state_t *state = amos_create();
        amos_load_text(state, "A$=Insert$(\"Hello World\",\"Beautiful \",7)");
        amos_run(state);
        amos_run_step(state);

        amos_var_t *var = amos_var_get(state, "A$");
        ASSERT(var != NULL && var->type == VAR_STRING, "A$ should be string");
        ASSERT(var->sval.data && strcmp(var->sval.data, "Hello Beautiful World") == 0,
               "Insert$ should produce 'Hello Beautiful World'");
        amos_destroy(state);
    }

    /* Test 9: Length(n) for bank */
    {
        amos_state_t *state = amos_create();
        amos_load_text(state, "L=Length(5)");
        amos_run(state);
        amos_run_step(state);

        amos_var_t *var = amos_var_get(state, "L");
        ASSERT(var != NULL && var->ival == 0, "Length of unreserved bank should be 0");
        amos_destroy(state);
    }

    /* Test 10: Screen Width and Screen Height */
    {
        amos_state_t *state = amos_create();
        amos_load_text(state,
            "W=Screen Width\n"
            "H=Screen Height\n"
        );
        amos_run(state);
        for (int i = 0; i < 10 && state->running; i++)
            amos_run_step(state);

        amos_var_t *vw = amos_var_get(state, "W");
        amos_var_t *vh = amos_var_get(state, "H");
        ASSERT(vw != NULL && vw->ival == 320, "Screen Width should be 320 (default)");
        ASSERT(vh != NULL && vh->ival == 256, "Screen Height should be 256 (default)");
        amos_destroy(state);
    }

    printf("  %d tests, %d errors\n", 10, errors);
    return errors;
}
