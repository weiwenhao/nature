#ifndef NATURE_SRC_AST_ANALYSIS_H_
#define NATURE_SRC_AST_ANALYSIS_H_

#include "src/ast.h"
#include "src/symbol.h"

#define MAIN_FUNCTION_NAME "main"
#define ENV_IDENT "env"
#define ANONYMOUS_FUNCTION_NAME "fn"

int unique_name_count;
int analysis_line;

typedef struct {
    symbol_type type;
    void *decl; // ast_var_decl,ast_type_decl_stmt,ast_new_fn
    string ident; // 原始名称
    string unique_ident; // 唯一名称
    int scope_depth;
    bool is_capture; // 是否被捕获(是否被下级引用)
} analysis_local_ident;

/**
 * free_var 是在 parent function 作用域中被使用,但是被捕获存放在了 current function free_vars 中,
 * 所以这里的 is_local 指的是在 parent 中的位置
 * 如果 is_local 为 true 则 index 为 parent.locals[index]
 * 如果 is_local 为 false 则 index 为参数 env[index]
 */
typedef struct {
    bool is_local;
    uint8_t index;
    string ident;
} analysis_free_ident;

typedef struct analysis_local_scope {
    struct analysis_local_scope *parent;
    analysis_local_ident *idents[UINT8_MAX];
    uint8_t count;
    uint8_t scope_depth;
} analysis_local_scope;

/**
 * 词法作用域
 */
typedef struct analysis_function {
    struct analysis_function *parent;

    analysis_local_scope *current_scope;

//  analysis_local_ident *locals[UINT8_MAX];
//  uint8_t local_count;

    // wwh: 使用了当前作用域之外的变量
    analysis_free_ident frees[UINT8_MAX];
    uint8_t free_count;

    // 当前函数内的块作用域深度(基于当前函数,所以初始值为 0, 用于块作用域判定)
    uint8_t scope_depth;

    // 便于值改写, 放心 env unique name 会注册到字符表的要用
    string env_unique_name;

    // 函数定义在当前作用域仅加载 function name
    // 函数体的解析则延迟到当前作用域内的所有标识符都定义明确好
    struct {
        // 由于需要延迟处理，所以缓存函数定义时的 scope，在处理时进行还原。
        analysis_local_scope *scope;
        union {
            ast_stmt *stmt;
            ast_expr *expr;
        };
        bool is_stmt;
    } contains_fn_decl[UINT8_MAX];
    uint8_t contains_fn_count;
} analysis_function;

analysis_function *analysis_current;

// 符号表收集，类型检查、变量作用域检查（作用域单赋值），闭包转换
ast_closure_decl analysis(ast_block_stmt stmt_list);

analysis_function *analysis_current_init(analysis_local_scope *scope, string fn_name);

analysis_local_scope *analysis_new_local_scope(uint8_t scope_depth, analysis_local_scope *parent);

// 变量 hash_string 表
void analysis_function_begin();

void analysis_function_end();

void analysis_block(ast_block_stmt *block);

void analysis_var_decl(ast_var_decl *stmt);

void analysis_var_decl_assign(ast_var_decl_assign_stmt *stmt);

ast_closure_decl *analysis_function_decl(ast_new_fn *function_decl, analysis_local_scope *scope);

void analysis_function_decl_ident(ast_new_fn *new_fn);

void analysis_stmt(ast_stmt *stmt);

void analysis_expr(ast_expr *expr);

void analysis_binary(ast_binary_expr *expr);

void analysis_unary(ast_unary_expr *expr);

void analysis_ident(ast_expr *expr);

void analysis_type(ast_type_t *type);

int8_t analysis_resolve_free(analysis_function *current, string*ident);

uint8_t analysis_push_free(analysis_function *f, bool is_local, int8_t index, string ident);

void analysis_call(ast_call *call);

void analysis_assign(ast_assign_stmt *assign);

string analysis_resolve_type(analysis_function *current, string ident);

void analysis_if(ast_if_stmt *stmt);

void analysis_while(ast_while_stmt *stmt);

void analysis_for_in(ast_for_in_stmt *stmt);

void analysis_return(ast_return_stmt *stmt);

void analysis_type_decl(ast_type_decl_stmt *stmt);

void analysis_access(ast_access *expr);

void analysis_select_property(ast_select_property *expr);

void analysis_new_struct(ast_new_struct *expr);

void analysis_new_map(ast_new_map *expr);

void analysis_new_list(ast_new_list *expr);

bool analysis_redeclare_check(string ident);

analysis_local_ident *analysis_new_local(symbol_type type, void *decl, string ident);

string analysis_unique_ident(string name);

void analysis_begin_scope();

void analysis_end_scope();

ast_type_t analysis_function_to_type(ast_new_fn *function_decl);

#endif //NATURE_SRC_AST_ANALYSIS_H_
