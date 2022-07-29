#include "ast.h"

string ast_expr_operator_to_string[100] = {
        [AST_EXPR_OPERATOR_ADD] = "+",
        [AST_EXPR_OPERATOR_SUB] = "-",
        [AST_EXPR_OPERATOR_MUL] = "*",
        [AST_EXPR_OPERATOR_DIV] = "/",

        [AST_EXPR_OPERATOR_LT] = "<",
        [AST_EXPR_OPERATOR_LTE] = "<=",
        [AST_EXPR_OPERATOR_GT] = ">", // >
        [AST_EXPR_OPERATOR_GTE] = ">=",  // >=
        [AST_EXPR_OPERATOR_EQ_EQ] = "==", // ==
        [AST_EXPR_OPERATOR_NOT_EQ] = "!=", // !=

        [AST_EXPR_OPERATOR_NOT] = "!", // unary !expr
        [AST_EXPR_OPERATOR_MINUS] = "-", // unary -expr
};

ast_block_stmt ast_new_block_stmt() {
    ast_block_stmt result;
    result.count = 0;
    result.capacity = 0;
    result.list = NULL;
    return result;
}

void ast_block_stmt_push(ast_block_stmt *block, ast_stmt stmt) {
    if (block->capacity <= block->count) {
        block->capacity = GROW_CAPACITY(block->capacity);
        block->list = (ast_stmt *) realloc(block->list, sizeof(stmt) * block->capacity);
    }

    block->list[block->count++] = stmt;
}

ast_type_t ast_new_simple_type(type_system type) {
    ast_type_t result = {
            .is_origin = true,
            .value = NULL,
            .type = type
    };
    return result;
}

ast_ident *ast_new_ident(char *literal) {
    ast_ident *ident = malloc(sizeof(ast_ident));
    ident->literal = literal;
    return ident;
}

ast_type_t ast_new_point_type(ast_type_t ast_type, uint8_t point) {
    ast_type_t result;
    result.is_origin = ast_type.is_origin;
    result.type = ast_type.type;
    result.value = ast_type.value;
    result.point = point;
    return result;
}

