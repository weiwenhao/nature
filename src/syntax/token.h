#ifndef NATURE_SRC_SYNTAX_TOKEN_H_
#define NATURE_SRC_SYNTAX_TOKEN_H_

#include <stdlib.h>
#include "src/value.h"

typedef enum {
    // SINGLE-CHARACTER TOKENS.
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN, // ()
    TOKEN_LEFT_SQUARE, TOKEN_RIGHT_SQUARE, // []
    TOKEN_LEFT_CURLY, TOKEN_RIGHT_CURLY, // {}
    TOKEN_LEFT_ANGLE, TOKEN_RIGHT_ANGLE, // <>


    TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
    TOKEN_COLON, TOKEN_SEMICOLON, TOKEN_SLASH, TOKEN_STAR, // * STAR


    // ONE OR TWO CHARACTER TOKENS.
    TOKEN_NOT, TOKEN_NOT_EQUAL,
    TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER_EQUAL,
    TOKEN_LESS_EQUAL,
    TOKEN_AND, TOKEN_AND_AND, TOKEN_OR, TOKEN_OR_OR,

    // LITERALS.
    TOKEN_LITERAL_IDENT, TOKEN_LITERAL_STRING, TOKEN_LITERAL_FLOAT, TOKEN_LITERAL_INT,

    // KEYWORDS.
    TOKEN_TRUE, TOKEN_FALSE, TOKEN_TYPE, TOKEN_NULL, TOKEN_ANY, TOKEN_STRUCT,
    TOKEN_FOR, TOKEN_IN, TOKEN_WHILE, TOKEN_IF, TOKEN_ELSE, TOKEN_ELSE_IF,
    TOKEN_VAR, TOKEN_STRING, TOKEN_BOOL, TOKEN_FLOAT, TOKEN_INT,
    TOKEN_ARRAY, TOKEN_MAP, TOKEN_FUNCTION, TOKEN_VOID,
    TOKEN_IMPORT, TOKEN_AS, TOKEN_RETURN,
    TOKEN_STMT_EOF, TOKEN_EOF, // TOKEN_EOF 一定要在最后一个，否则会索引溢出
} token_type;

string token_type_to_string[TOKEN_EOF + 1];

typedef struct {
    token_type type; // 通配类型，如 var
    string literal;
    int line;
} token;

token *token_new(uint8_t type, char *literal, int line);

#endif //NATURE_SRC_SYNTAX_TOKEN_H_
