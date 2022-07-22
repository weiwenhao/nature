#include "parser.h"
#include "src/lib/error.h"
#include <stdio.h>
#include "src/debug/debug.h"

parser_cursor p_cursor;

ast_expr_operator token_to_ast_expr_operator[] = {
        [TOKEN_PLUS] = AST_EXPR_OPERATOR_ADD,
        [TOKEN_MINUS] = AST_EXPR_OPERATOR_SUB,
        [TOKEN_STAR] = AST_EXPR_OPERATOR_MUL,
        [TOKEN_SLASH] = AST_EXPR_OPERATOR_DIV,
        [TOKEN_EQUAL_EQUAL] = AST_EXPR_OPERATOR_EQ_EQ,
        [TOKEN_NOT_EQUAL] = AST_EXPR_OPERATOR_NOT_EQ,
        [TOKEN_GREATER_EQUAL] = AST_EXPR_OPERATOR_GTE,
        [TOKEN_RIGHT_ANGLE] = AST_EXPR_OPERATOR_GT,
        [TOKEN_LESS_EQUAL] = AST_EXPR_OPERATOR_LTE,
        [TOKEN_LEFT_ANGLE] = AST_EXPR_OPERATOR_LT,
};

type_category token_to_type_category[] = {
        // literal
        [TOKEN_TRUE] = TYPE_BOOL,
        [TOKEN_FALSE] = TYPE_BOOL,
        [TOKEN_LITERAL_FLOAT] = TYPE_FLOAT,
        [TOKEN_LITERAL_INT] = TYPE_INT,
        [TOKEN_LITERAL_STRING] = TYPE_STRING,

        // type
        [TOKEN_BOOL] = TYPE_BOOL,
        [TOKEN_FLOAT] = TYPE_FLOAT,
        [TOKEN_INT] = TYPE_INT,
        [TOKEN_STRING] = TYPE_STRING,
        [TOKEN_VOID] = TYPE_VOID,
        [TOKEN_NULL] = TYPE_NULL,
        [TOKEN_VAR] = TYPE_VAR,
        [TOKEN_ANY] = TYPE_ANY
};

parser_rule rules[] = {
        // infix: test()、void test() {}, void () {}
        [TOKEN_LEFT_PAREN] = {parser_grouping, parser_call_expr, PRECEDENCE_CALL},
        // map["foo"] list[0]
        [TOKEN_LEFT_SQUARE] = {parser_new_list, parser_access, PRECEDENCE_CALL},
        [TOKEN_LEFT_CURLY] = {parser_new_map, NULL, PRECEDENCE_NULL},
        [TOKEN_DOT] = {NULL, parser_select_property, PRECEDENCE_CALL},
        [TOKEN_MINUS] = {parser_unary, parser_binary, PRECEDENCE_TERM},
        [TOKEN_PLUS] = {NULL, parser_binary, PRECEDENCE_TERM},
        [TOKEN_SLASH] = {NULL, parser_binary, PRECEDENCE_FACTOR},
        [TOKEN_STAR] = {NULL, parser_binary, PRECEDENCE_FACTOR},
        [TOKEN_OR_OR] = {NULL, parser_binary, PRECEDENCE_OR},
        [TOKEN_AND_AND] = {NULL, parser_binary, PRECEDENCE_AND},
        [TOKEN_NOT_EQUAL] = {NULL, parser_binary, PRECEDENCE_EQUALITY},
        [TOKEN_EQUAL_EQUAL] = {NULL, parser_binary, PRECEDENCE_EQUALITY},
        [TOKEN_RIGHT_ANGLE] = {NULL, parser_binary, PRECEDENCE_COMPARE},
        [TOKEN_GREATER_EQUAL] = {NULL, parser_binary, PRECEDENCE_COMPARE},
        [TOKEN_LEFT_ANGLE] = {NULL, parser_binary, PRECEDENCE_COMPARE},
        [TOKEN_LESS_EQUAL] = {NULL, parser_binary, PRECEDENCE_COMPARE},
        [TOKEN_LITERAL_STRING] = {parser_literal, NULL, PRECEDENCE_NULL},
        [TOKEN_LITERAL_INT] = {parser_literal, NULL, PRECEDENCE_NULL},
        [TOKEN_LITERAL_FLOAT] = {parser_literal, NULL, PRECEDENCE_NULL},
        [TOKEN_TRUE] = {parser_literal, NULL, PRECEDENCE_NULL},
        [TOKEN_FALSE] = {parser_literal, NULL, PRECEDENCE_NULL},
        [TOKEN_LITERAL_IDENT] = {parser_ident_expr, NULL, PRECEDENCE_NULL},

        // 表达式的前缀是一个类型
        [TOKEN_VOID] = {parser_direct_type_expr, NULL, PRECEDENCE_NULL},
        [TOKEN_ANY] = {parser_direct_type_expr, NULL, PRECEDENCE_NULL},
        [TOKEN_INT] = {parser_direct_type_expr, NULL, PRECEDENCE_NULL},
        [TOKEN_FLOAT] = {parser_direct_type_expr, NULL, PRECEDENCE_NULL},
        [TOKEN_BOOL] = {parser_direct_type_expr, NULL, PRECEDENCE_NULL},
        [TOKEN_STRING] = {parser_direct_type_expr, NULL, PRECEDENCE_NULL},
        [TOKEN_FUNCTION] = {parser_direct_type_expr, NULL, PRECEDENCE_NULL},
        [TOKEN_MAP] = {parser_direct_type_expr, NULL, PRECEDENCE_NULL},
        [TOKEN_LIST] = {parser_direct_type_expr, NULL, PRECEDENCE_NULL},
        // type decl 不会出现在表达式这里
        [TOKEN_STRUCT] = {parser_struct_type_expr, NULL, PRECEDENCE_NULL},

        [TOKEN_EOF] = {NULL, NULL, PRECEDENCE_NULL},
};

ast_block_stmt parser(list *token_list) {
    parser_cursor_init(token_list);

    ast_block_stmt block_stmt = ast_new_block_stmt();

    ast_stmt_expr_type stmt_type = -1;

    while (!parser_is(TOKEN_EOF)) {

#ifdef DEBUG_PARSER
        if (stmt_type != -1) {
            debug_parser_stmt(stmt_type);
        }
#endif

        ast_stmt stmt = parser_stmt();

        stmt_type = stmt.type;

        ast_block_stmt_push(&block_stmt, stmt);
        parser_must_stmt_end();
    }

    return block_stmt;
}

ast_block_stmt parser_block() {
    ast_block_stmt block_stmt = ast_new_block_stmt();

    parser_must(TOKEN_LEFT_CURLY); // 必须是
    while (!parser_is(TOKEN_RIGHT_CURLY)) {
        ast_stmt stmt = parser_stmt();
        parser_must_stmt_end();

        ast_block_stmt_push(&block_stmt, stmt);
    }
    parser_must(TOKEN_RIGHT_CURLY);

    return block_stmt;
}

ast_stmt parser_stmt() {
    if (parser_is_direct_type() || parser_is_custom_type_var()) {
        return parser_var_or_function_decl();
    } else if (parser_is(TOKEN_LITERAL_IDENT)) {
        return parser_ident_stmt();
    } else if (parser_is(TOKEN_IF)) {
        return parser_if_stmt();
    } else if (parser_is(TOKEN_FOR)) {
        return parser_for_stmt();
    } else if (parser_is(TOKEN_WHILE)) {
        return parser_while_stmt();
    } else if (parser_is(TOKEN_RETURN)) {
        return parser_return_stmt();
    } else if (parser_is(TOKEN_TYPE)) {
        return parser_type_decl_stmt();
    }

    error_printf(parser_line(), "cannot parser stmt");
    return parser_new_stmt();
}

ast_stmt parser_type_decl_stmt() {
    ast_stmt result = parser_new_stmt();
    ast_type_decl_stmt *type_decl_stmt = malloc(sizeof(ast_type_decl_stmt));
    parser_must(TOKEN_TYPE);
    type_decl_stmt->ident = parser_advance()->literal;
    parser_must(TOKEN_EQUAL);
    // 类型解析
    type_decl_stmt->type = parser_type();

    result.type = AST_STMT_TYPE_DECL;
    result.stmt = type_decl_stmt;

    return result;
}

/**
 * var foo = expr
 * @return
 */
ast_stmt parser_auto_infer_decl() {
    ast_stmt result = parser_new_stmt();
    ast_var_decl_assign_stmt *stmt = malloc(sizeof(ast_var_decl_assign_stmt));

    parser_must(TOKEN_VAR);
    // 变量名称
    token *t = parser_must(TOKEN_LITERAL_IDENT);
    ast_var_decl *var_decl = malloc(sizeof(ast_var_decl));
    var_decl->type = parser_type();
    var_decl->ident = t->literal;
    stmt->expr = parser_expr();
    stmt->var_decl = var_decl;

    result.type = AST_STMT_VAR_DECL_ASSIGN;
    result.stmt = stmt;

    return result;
}

ast_expr parser_precedence_expr(parser_precedence precedence) {
// 读取表达式前缀
    parser_prefix_fn prefix_fn = parser_get_rule(parser_peek()->type)->prefix;
    if (prefix_fn == NULL) {
        error_printf(parser_line(), "cannot parser ident: %s", parser_peek()->literal);
    }

    ast_expr expr = prefix_fn(); // advance

    // 前缀表达式已经处理完成，判断是否有中缀表达式，有则处理
    token_type type = parser_peek()->type;
    parser_rule *infix_rule = parser_get_rule(type);
    while (infix_rule->infix_precedence >= precedence) {
        parser_infix_fn infix_fn = infix_rule->infix;
        expr = infix_fn(expr);

        infix_rule = parser_get_rule(parser_peek()->type);
    }

    return expr;
}

/**
 * 表达式优先级处理方式
 * @return
 */
ast_expr parser_expr() {
    return parser_precedence_expr(PRECEDENCE_ASSIGN);
}

/**
 * int foo = 12;
 * int foo;
 * int foo() {};
 * void foo() {};
 * int () {};
 * @return
 */
ast_stmt parser_var_or_function_decl() {
    ast_stmt result = parser_new_stmt();

    ast_type type = parser_type();

    // 声明时函数名称仅占用一个 token
    if (parser_is(TOKEN_LEFT_PAREN) || parser_next_is(1, TOKEN_LEFT_PAREN)) {
        result.type = AST_NEW_FN;
        result.stmt = parser_function_decl(type);
        return result;
    }

    token *ident_token = parser_advance();

    // int foo = 12;
    if (parser_consume(TOKEN_EQUAL)) {
        ast_var_decl_assign_stmt *stmt = malloc(sizeof(ast_var_decl_assign_stmt));
        ast_var_decl *var_decl = malloc(sizeof(ast_var_decl));
        var_decl->type = type;
        var_decl->ident = ident_token->literal;
        stmt->expr = parser_expr();
        stmt->var_decl = var_decl;
        result.type = AST_STMT_VAR_DECL_ASSIGN;
        result.stmt = stmt;
        return result;
    }

    // int foo;
    ast_var_decl *stmt = malloc(sizeof(ast_var_decl));
    stmt->type = type;
    stmt->ident = ident_token->literal;
    result.type = AST_VAR_DECL;
    result.stmt = stmt;

    return result;
}

ast_expr parser_binary(ast_expr left) {
    ast_expr result = parser_new_expr();

    token *operator_token = parser_advance();

    // right expr
    parser_precedence precedence = parser_get_rule(operator_token->type)->infix_precedence;
    ast_expr right = parser_precedence_expr(precedence + 1);

    ast_binary_expr *binary_expr = malloc(sizeof(ast_binary_expr));

    binary_expr->operator = token_to_ast_expr_operator[operator_token->type];
    binary_expr->left = left;
    binary_expr->right = right;

    result.type = AST_EXPR_BINARY;
    result.expr = binary_expr;

//  printf("type: %s\n", operator_token->literal);

    return result;
}

/**
 * ! 取反
 * - 取负数
 * @return
 */
ast_expr parser_unary() {
    ast_expr result = parser_new_expr();
    token *operator_token = parser_advance();
    ast_expr operand = parser_precedence_expr(PRECEDENCE_UNARY);

    ast_unary_expr *unary_expr = malloc(sizeof(ast_unary_expr));

    if (operator_token->type == TOKEN_NOT) {
        unary_expr->operator = AST_EXPR_OPERATOR_NOT;
    } else if (operator_token->type == TOKEN_MINUS) {
        unary_expr->operator = AST_EXPR_OPERATOR_MINUS;
    } else {
        error_exit("unknown operator type");
    }

    unary_expr->operand = operand;

    result.type = AST_EXPR_UNARY;
    result.expr = unary_expr;

    return result;
}

/**
 * (1 + 1)
 * (int a, int b) {}
 */
ast_expr parser_grouping() {
    parser_must(TOKEN_LEFT_PAREN);
    ast_expr expr = parser_expr();
    parser_must(TOKEN_RIGHT_PAREN);
    return expr;
}

ast_expr parser_literal() {
    ast_expr result = parser_new_expr();
    token *literal_token = parser_advance();
    ast_literal *literal_expr = malloc(sizeof(ast_literal));
    literal_expr->type = token_to_type_category[literal_token->type];
    literal_expr->value = literal_token->literal; // 具体数值

    result.type = AST_EXPR_LITERAL;
    result.expr = literal_expr;

    return result;
}

/**
 * ident is custom type or variable
 * ident 如果是 type 则需要特殊处理
 * @return
 */
ast_expr parser_ident_expr() {
    ast_expr result = parser_new_expr();
    token *ident_token = parser_advance();

    ast_type type_decl_ident = {
            .is_origin = false,
            .category = TYPE_DECL_IDENT,
            .value = ast_new_ident(ident_token->literal)
    };

    // person {
    //  a = 1
    //  b = 2
    // }
    if (parser_consume(TOKEN_LEFT_CURLY)) {
        return parser_new_struct(type_decl_ident);
    }

    // foo_type a() {}
    if (parser_is(TOKEN_LITERAL_IDENT) && parser_next_is(1, TOKEN_LEFT_PAREN)) {
        return parser_function_decl_expr(type_decl_ident);
    };
    // type() {}
    if (parser_is(TOKEN_LEFT_PAREN) && parser_is_function_decl(parser_next(0))) {
        return parser_function_decl_expr(type_decl_ident);
    }

    // call() or other ident prefix expr
    result.type = AST_EXPR_IDENT;
    result.expr = ast_new_ident(ident_token->literal);

    return result;
}

/**
 * 暂时无法确定 foo 的类型是 list 还是 map
 * foo[expr]
 * @param left
 * @return
 */
ast_expr parser_access(ast_expr left) {
    ast_expr result = parser_new_expr();

    parser_must(TOKEN_LEFT_SQUARE);
    ast_expr key = parser_precedence_expr(PRECEDENCE_CALL);
    parser_must(TOKEN_RIGHT_SQUARE);
    ast_access *access_expr = malloc(sizeof(ast_access));
    access_expr->left = left;
    access_expr->key = key;
    result.type = AST_EXPR_ACCESS;
    result.expr = access_expr;

    return result;
}

/**
 * foo.bar[car]
 * for.bar.car
 * @param left
 * @return
 */
ast_expr parser_select_property(ast_expr left) {
    ast_expr result = parser_new_expr();
    parser_must(TOKEN_DOT);

    token *property_token = parser_must(TOKEN_LITERAL_IDENT);
    ast_select_property *select_property_expr = malloc(sizeof(ast_select_property));
    select_property_expr->left = left;
    select_property_expr->property = property_token->literal;

    result.type = AST_EXPR_SELECT_PROPERTY;
    result.expr = select_property_expr;

    return result;
}

ast_expr parser_call_expr(ast_expr left_expr) {
    ast_expr result = parser_new_expr();

    ast_call *call_stmt = malloc(sizeof(ast_call));
    call_stmt->left = left_expr;

    // param handle
    parser_actual_param(call_stmt);

    result.type = AST_CALL;
    result.expr = call_stmt;
    return result;
}

//ast_stmt parser_call_stmt() {
//  ast_stmt result;
//  // left_expr
//  ast_expr name_expr = parser_expr(PRECEDENCE_NULL);
//
//  ast_call *call_stmt = malloc(sizeof(ast_call));
//  call_stmt->left = name_expr;
//
//  // param handle
//  parser_actual_param(call_stmt);
//
//  result.type = AST_CALL;
//  result.stmt = call_stmt;
//
//  return result;
//}

void parser_actual_param(ast_call *call) {
    parser_must(TOKEN_LEFT_PAREN);

    if (!parser_is(TOKEN_RIGHT_PAREN)) {
        // 参数解析 call(1 + 1, param_a)
        ast_expr first_param = parser_expr();
        call->actual_params[0] = first_param;
        call->actual_param_count = 1;

        while (parser_consume(TOKEN_COMMA)) {
            ast_expr rest_param = parser_expr();
            call->actual_params[call->actual_param_count++] = rest_param;
        }
    }

    parser_must(TOKEN_RIGHT_PAREN);
}

ast_var_decl *parser_var_decl() {
    ast_type var_type = parser_type();
    token *var_ident = parser_advance();
    if (var_ident->type != TOKEN_LITERAL_IDENT) {
        error_printf(parser_line(), "parser variable definitions error, '%s' not a ident", var_ident->literal);
    }
    ast_var_decl *var_decl = malloc(sizeof(ast_var_decl));
    var_decl->type = var_type;
    var_decl->ident = var_ident->literal;
    return var_decl;
}

void parser_type_function_formal_param(ast_function_type_decl *type_function) {
    parser_must(TOKEN_LEFT_PAREN);

    if (!parser_is(TOKEN_RIGHT_PAREN)) {

        ast_var_decl *var_decl = malloc(sizeof(ast_var_decl));
        var_decl->ident = "";
        var_decl->type = parser_type();
        // formal parameter handle type + ident
        type_function->formal_params[0] = var_decl;
        type_function->formal_param_count = 1;

        while (parser_consume(TOKEN_COMMA)) {
            uint8_t count = type_function->formal_param_count++;
            var_decl = malloc(sizeof(ast_var_decl));
            var_decl->ident = "";
            var_decl->type = parser_type();
            type_function->formal_params[count] = var_decl;
        }
    }
    parser_must(TOKEN_RIGHT_PAREN);
}

void parser_formal_param(ast_new_fn *function_decl) {
    parser_must(TOKEN_LEFT_PAREN);

    if (!parser_is(TOKEN_RIGHT_PAREN)) {
        // formal parameter handle type + ident
        function_decl->formal_params[0] = parser_var_decl();
        function_decl->formal_param_count = 1;

        while (parser_is(TOKEN_COMMA)) {
            parser_advance();
            uint8_t count = function_decl->formal_param_count++;
            function_decl->formal_params[count] = parser_var_decl();
        }
    }

    parser_must(TOKEN_RIGHT_PAREN);
}

/**
 * 兼容 var
 * @return
 */
ast_type parser_type() {
    ast_type result = {
            .is_origin = false
    };

    // int/float/bool/string/void/var/any
    if (parser_is_simple_type()) {
        token *type_token = parser_advance();
        result.category = token_to_type_category[type_token->type];
        result.value = type_token->literal;
        return result;
    }

    if (parser_consume(TOKEN_LIST)) {
        ast_list_decl *type_list_decl = malloc(sizeof(ast_list_decl));
        parser_must(TOKEN_LEFT_SQUARE);

        type_list_decl->type = parser_type();

        parser_must(TOKEN_RIGHT_SQUARE);

        result.category = TYPE_LIST;
        result.value = type_list_decl;
        return result;
    }

    if (parser_consume(TOKEN_MAP)) {
        ast_map_decl *type_map_decl = malloc(sizeof(ast_map_decl));
        parser_must(TOKEN_LEFT_CURLY);
        type_map_decl->key_type = parser_type();
        parser_must(TOKEN_COLON);
        type_map_decl->value_type = parser_type();
        parser_must(TOKEN_RIGHT_CURLY);

        result.category = TYPE_MAP;
        result.value = type_map_decl;
        return result;
    }

    if (parser_consume(TOKEN_STRUCT)) {
        ast_struct_decl *type_struct_decl = malloc(sizeof(ast_struct_decl));
        type_struct_decl->count = 0;
        parser_must(TOKEN_LEFT_CURLY);
        while (!parser_is(TOKEN_RIGHT_CURLY)) {
            // default value
            ast_struct_property item;
            item.type = parser_type();
            item.key = parser_advance()->literal;

            type_struct_decl->list[type_struct_decl->count++] = item;
            parser_must_stmt_end();
        }

        parser_must(TOKEN_RIGHT_CURLY);

        result.category = TYPE_STRUCT;
        result.value = type_struct_decl;
        return result;
    }

    if (parser_consume(TOKEN_FUNCTION)) {
        parser_must(TOKEN_LEFT_ANGLE);
        ast_function_type_decl *type_function = malloc(sizeof(ast_function_type_decl));
//    type_function->name = "";
        type_function->return_type = parser_type();
        parser_type_function_formal_param(type_function);
        parser_must(TOKEN_RIGHT_ANGLE);

        result.category = TYPE_FN;
        result.value = type_function;
        return result;
    }

    if (!parser_is(TOKEN_LITERAL_IDENT)) {
        error_printf(parser_line(), "parser error,type must base/custom type ident");
    }
    // 神奇的 ident
    token *type_token = parser_advance();
    result.category = TYPE_DECL_IDENT;
    result.value = ast_new_ident(type_token->literal);
    return result;
}

/**
 * var a = int [fn_name]() {
 * }
 *
 * int [fn_name]() {
 * }
 * @return
 */
ast_new_fn *parser_function_decl(ast_type type) {
    ast_new_fn *function_decl = malloc(sizeof(ast_new_fn));

    // 必选 type
    function_decl->return_type = type;
    function_decl->name = ""; // 避免随机值干扰

    // 可选函数名称
    if (parser_is(TOKEN_LITERAL_IDENT)) {
        token *name_token = parser_advance();
        function_decl->name = name_token->literal;
    }

    parser_formal_param(function_decl);
    function_decl->body = parser_block();

    return function_decl;
}

/**
 * else if 逻辑优化
 * if () {
 *
 * } else (if() {
 *
 * } else {
 *
 * })
 *
 * ↓
 * if () {
 *
 * } else {
 *     if () {
 *
 *     }
 * }
 *
 * @return
 */
ast_stmt parser_if_stmt() {
    ast_stmt result = parser_new_stmt();
    ast_if_stmt *if_stmt = malloc(sizeof(ast_if_stmt));
    if_stmt->consequent.count = 0;
    if_stmt->alternate.count = 0;

    parser_must(TOKEN_IF);
    parser_must(TOKEN_LEFT_PAREN);
    if_stmt->condition = parser_expr();
    parser_must(TOKEN_RIGHT_PAREN);
    if_stmt->consequent = parser_block();

    if (parser_consume(TOKEN_ELSE)) {
        if (parser_is(TOKEN_IF)) {
            if_stmt->alternate = parser_else_if();
        } else {
            if_stmt->alternate = parser_block();
        }
    }

    result.type = AST_STMT_IF;
    result.stmt = if_stmt;

    return result;
}

/**
 * if () {
 * } else if {
 * } else if {
 * ...
 * } else {
 * }
 * @return
 */
ast_block_stmt parser_else_if() {
    ast_block_stmt block_stmt = ast_new_block_stmt();
    ast_block_stmt_push(&block_stmt, parser_if_stmt());

    return block_stmt;
}

/**
 * for (key,value in list) {
 *
 * }
 * @return
 */
ast_stmt parser_for_stmt() {
    parser_advance();
    ast_stmt result = parser_new_stmt();

    ast_for_in_stmt *for_in_stmt = malloc(sizeof(ast_for_in_stmt));
    for_in_stmt->gen_key = malloc(sizeof(ast_var_decl));

    parser_must(TOKEN_LEFT_PAREN);
    ast_type type_var = parser_type();
    for_in_stmt->gen_key->type = type_var;
    for_in_stmt->gen_key->ident = parser_advance()->literal;

    // 可选参数 val
    if (parser_consume(TOKEN_COMMA)) {
        for_in_stmt->gen_value = malloc(sizeof(ast_var_decl));
        for_in_stmt->gen_value->type = type_var;
        for_in_stmt->gen_value->ident = parser_advance()->literal;
    }

    parser_must(TOKEN_IN);
    for_in_stmt->iterate = parser_expr();
    parser_must(TOKEN_RIGHT_PAREN);

    for_in_stmt->body = parser_block();

    result.type = AST_STMT_FOR_IN;
    result.stmt = for_in_stmt;

    return result;
}

ast_stmt parser_while_stmt() {
    ast_stmt result = parser_new_stmt();
    ast_while_stmt *while_stmt = malloc(sizeof(ast_while_stmt));
    parser_advance();
    while_stmt->condition = parser_expr();
    while_stmt->body = parser_block();

    result.type = AST_STMT_WHILE;
    result.stmt = while_stmt;

    return result;
}

/**
 * @param type
 * @return
 */
parser_rule *parser_get_rule(token_type type) {
    return &rules[type];
}

ast_stmt parser_assign(ast_expr left) {
    ast_stmt result = parser_new_stmt();
    ast_assign_stmt *assign_stmt = malloc(sizeof(ast_assign_stmt));
    assign_stmt->left = left;
    // invalid: foo;
    parser_must(TOKEN_EQUAL);
    assign_stmt->right = parser_expr();

    result.type = AST_STMT_ASSIGN;
    result.stmt = assign_stmt;

    return result;
}

void parser_cursor_init(list *token_list) {
    list_node *first = token_list->front;
//  p_cursor.current = first;
    p_cursor.current = first;
}

token *parser_advance() {
    if (p_cursor.current->next == NULL) {
        error_exit("next token is null");
    }
    token *t = p_cursor.current->value;
    p_cursor.current = p_cursor.current->next;
#ifdef DEBUG_PARSER
    debug_parser(t->line, t->literal);
#endif
    return t;
}

token *parser_peek() {
    return p_cursor.current->value;
}

bool parser_is(token_type expect) {
    token *t = p_cursor.current->value;
    return t->type == expect;
}

bool parser_consume(token_type expect) {
    token *t = p_cursor.current->value;
    if (t->type == expect) {
        parser_advance();
        return true;
    }
    return false;
}

/**
 * custom_type baz = 1;
 * custom_type baz;
 *
 * call();
 * type() {
 * }
 *
 * foo = 1
 * foo.bar = 1
 * call().name = 2
 * @return
 */
ast_stmt parser_ident_stmt() {

    // 消费左边的 ident
    ast_expr left = parser_expr();
    if (left.type == AST_CALL) {
        if (parser_is(TOKEN_EQUAL)) {
            error_printf(parser_line(), "function call cannot assign value");
            exit(0);
        }
        ast_stmt stmt = parser_new_stmt();
        stmt.type = AST_CALL;
        stmt.stmt = left.expr;
        return stmt;
    }

    if (left.type == AST_NEW_FN) {
        if (parser_is(TOKEN_EQUAL)) {
            error_printf(parser_line(), "function decl cannot assign value");
            exit(0);
        }

        ast_stmt stmt = parser_new_stmt();
        stmt.type = AST_NEW_FN;
        stmt.stmt = left.expr;
        return stmt;
    }

    // foo = 1 、foo.bar = 1 、foo[1] = 1、foo().name = 1;
    // foo = test();
    return parser_assign(left);
}

ast_expr parser_function_decl_expr(ast_type type) {
    ast_expr result = parser_new_expr();
    result.type = AST_NEW_FN;
    result.expr = parser_function_decl(type);

    return result;
}

/**
 * not contains var
 * @return
 */
bool parser_is_direct_type() {
    if (parser_is_simple_type()) {
        return true;
    }

    if (parser_is(TOKEN_FUNCTION)
        || parser_is(TOKEN_STRUCT)
        || parser_is(TOKEN_MAP)
        || parser_is(TOKEN_LIST)) {
        return true;
    }

    if (parser_is(TOKEN_VAR)) {
        return true;
    }

    return false;
}

ast_stmt parser_return_stmt() {
    ast_stmt result = parser_new_stmt();
    parser_advance();
    ast_return_stmt *stmt = malloc(sizeof(ast_return_stmt));
    stmt->expr = parser_expr();
    result.type = AST_STMT_RETURN;
    result.stmt = stmt;

    return result;
}

bool parser_is_simple_type() {
    if (parser_is(TOKEN_VOID)
        || parser_is(TOKEN_NULL)
        || parser_is(TOKEN_VAR)
        || parser_is(TOKEN_ANY)
        || parser_is(TOKEN_INT)
        || parser_is(TOKEN_FLOAT)
        || parser_is(TOKEN_BOOL)
        || parser_is(TOKEN_STRING)) {
        return true;
    }
    return false;
}

token *parser_must(token_type expect) {
    token *t = p_cursor.current->value;
    if (t->type != expect) {
        error_printf(parser_line(), "parser error: expect '%s' token actual '%s' token",
                     token_type_to_string[expect],
                     token_type_to_string[t->type]);
    }

    parser_advance();
    return t;
}

bool parser_next_is(int step, token_type expect) {
    list_node *current = p_cursor.current;

    while (step > 0) {
        if (current->next == NULL) {
            return false;
        }
        current = current->next;
        step--;
    }

    token *t = current->value;
    return t->type == expect;
}

list_node *parser_next(int step) {
    list_node *current = p_cursor.current;

    while (step > 0) {
        if (current->next == NULL) {
            return NULL;
        }

        current = current->next;
        step--;
    }

    return current;
}

/**
 * [a, 1, call(), foo[1]]
 * @return
 */
ast_expr parser_new_list() {
    ast_expr result = parser_new_expr();
    ast_new_list *expr = malloc(sizeof(ast_new_list));
    expr->count = 0;
    expr->capacity = 0;
    parser_must(TOKEN_LEFT_SQUARE);

    if (!parser_is(TOKEN_RIGHT_SQUARE)) {
        expr->values[expr->count++] = parser_expr();
        while (parser_consume(TOKEN_COMMA)) {
            expr->values[expr->count++] = parser_expr();
        }
    }
    parser_must(TOKEN_RIGHT_SQUARE);
    expr->capacity = expr->count;

    result.type = AST_EXPR_NEW_LIST;
    result.expr = expr;

    return result;
}

static ast_map_item parser_map_item() {
    ast_map_item map_item = {
            .key = parser_expr()
    };
    parser_must(TOKEN_COLON);
    map_item.value = parser_expr();
    return map_item;
}

/**
 * {
 *     foo : bar,
 *     foo[1]: 12,
 * }
 * @return
 */
ast_expr parser_new_map() {
    ast_expr result = parser_new_expr();
    ast_new_map *expr = malloc(sizeof(ast_new_map));
    expr->count = 0;
    expr->capacity = 0;

    parser_must(TOKEN_LEFT_CURLY);
    if (!parser_is(TOKEN_RIGHT_CURLY)) {
        expr->values[expr->count++] = parser_map_item();
        while (parser_consume(TOKEN_COMMA)) {
            expr->values[expr->count++] = parser_map_item();
        }
    }

    parser_must(TOKEN_RIGHT_CURLY);
    expr->capacity = expr->count;

    result.type = AST_EXPR_NEW_MAP;
    result.expr = expr;

    return result;
}

bool parser_must_stmt_end() {
    if (parser_is(TOKEN_EOF) || parser_is(TOKEN_RIGHT_CURLY)) {
        return true;
    }
    if (parser_is(TOKEN_STMT_EOF)) {
        parser_advance();
        return true;
    }

    error_printf(parser_line(), "except ; or } in stmt end");
    return false;
}

/**
 * 左边 left 需要已经被消费到才行
 * (int a, custom b, map[a], list[c], fn<d>){
 * @return
 */
bool parser_is_function_decl(list_node *current) {
    token *t = current->value;
    if (t->type != TOKEN_LEFT_PAREN) {
        error_exit("parser_is_function_decl param must be TOKEN_LEFT_PAREN");
        return false;
    }

    // param is left paren, so close + 1 = 1,
    int close = 1;

    while (t->type != TOKEN_EOF) {
        current = current->next;
        t = current->value;

        if (t->type == TOKEN_LEFT_PAREN) {
            close++;
        }

        if (t->type == TOKEN_RIGHT_PAREN) {
            close--;
            if (close == 0) {
                break;
            }
        }
    }

    if (close > 0) {
        return false;
    }

    // next is '{' ?
    t = current->next->value;
    if (t->type != TOKEN_LEFT_CURLY) {
        return false;
    }

    return true;
}

ast_stmt parser_new_stmt() {
    ast_stmt result = {
            .line =parser_line()
    };
    return result;
}

ast_expr parser_new_expr() {
    ast_expr result = {
            .line = parser_line()
    };
    return result;
}

int parser_line() {
    return parser_peek()->line;
}

ast_expr parser_new_struct(ast_type type) {
    ast_expr result;
    ast_new_struct *new_struct = malloc(sizeof(ast_new_struct));
    new_struct->count = 0;
    new_struct->type = type;

    while (!parser_is(TOKEN_RIGHT_CURLY)) {
        // ident 类型
        ast_struct_property item;
        item.key = parser_must(TOKEN_LITERAL_IDENT)->literal;
        parser_must(TOKEN_EQUAL);
        item.value = parser_expr();
        new_struct->list[new_struct->count++] = item;
        parser_must_stmt_end();
    }

    parser_must(TOKEN_RIGHT_CURLY);

    result.type = AST_EXPR_NEW_STRUCT;
    result.expr = new_struct;
    return result;
}

/**
 * 直接已类型开头，一定是函数声明!
 * @return
 */
ast_expr parser_direct_type_expr() {
    ast_type type = parser_type();
    return parser_function_decl_expr(type);
}

ast_expr parser_struct_type_expr() {
    ast_type struct_type = parser_type();
    if (parser_consume(TOKEN_LEFT_CURLY)) {
        return parser_new_struct(struct_type);
    }
    return parser_function_decl_expr(struct_type);
}

bool parser_is_custom_type_var() {
    if (!parser_is(TOKEN_LITERAL_IDENT)) {
        return false;
    }

    if (!parser_next_is(1, TOKEN_LITERAL_IDENT)) {
        return false;
    }

    if (parser_next_is(2, TOKEN_EQUAL) ||
        parser_next_is(2, TOKEN_STMT_EOF) ||
        parser_next_is(2, TOKEN_EOF)) {
        return true;
    }

    return false;
}