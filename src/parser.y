%skeleton "lalr1.cc"
%require "3.8"
%define api.value.type variant
%define api.token.constructor
%define parse.error detailed
%define parse.lac full
%define lr.type lalr
%locations
%expect 0

%code requires {
#include "core.hpp"
}

%code {
yy::parser::symbol_type yylex(Context& driver);
}

%parse-param { Context& driver }
%lex-param { Context& driver }

%token END 0 "end of file"
%token <std::string> IDENTIFIER "identifier" INTEGER "integer" REAL "real number"
%token <std::string> STRING "string" BOOLEAN "boolean" COUT "cout" CIN "cin" ENDL "endl"
%token HEADER "#include <iostream>"
%token INT "int" DOUBLE "double" BOOL "bool" VOID "void"
%token IF "if" ELSE "else" WHILE "while" FOR "for" DO "do"
%token BREAK "break" CONTINUE "continue" RETURN "return"
%token USING "using" NAMESPACE "namespace" STD "std"
%token OR "||" AND "&&" EQ "==" NE "!=" LE "<=" GE ">="
%token INSERT "<<" INC "++" DEC "--"
%token EXTRACT ">>"
%token ADD_ASSIGN "+=" SUB_ASSIGN "-=" MUL_ASSIGN "*=" DIV_ASSIGN "/=" MOD_ASSIGN "%="

%precedence IF_WITHOUT_ELSE
%precedence ELSE
%right '?' ':'
%left OR
%left AND
%left EQ NE
%left '<' '>' LE GE
%left '+' '-'
%left '*' '/' '%'
%precedence UNARY

%type <std::string> type return_type assignment_operator
%type <Node*> program units unit function parameters parameter_list parameter
%type <Node*> block statements statement declaration declarators declarator initializer
%type <Node*> update expression optional_expression for_init for_step
%type <Node*> arguments argument_list output output_items output_item
%type <Node*> lvalue array_initializer optional_array_initializer input input_items

%start program

%%

program:
    units { driver.root = $1; $$ = $1; }
;

units:
    %empty { $$ = driver.make("Program", "", 1, 1); }
  | units unit { $1->children.push_back($2); $$ = $1; }
;

unit:
    function { $$ = $1; }
  | HEADER { $$ = driver.make("Include", "iostream", @1.begin.line, @1.begin.column); }
  | USING NAMESPACE STD ';' { $$ = driver.make("UsingNamespace", "std", @1.begin.line, @1.begin.column); }
;

type:
    INT { $$ = "int"; }
  | DOUBLE { $$ = "double"; }
  | BOOL { $$ = "bool"; }
;

return_type:
    type { $$ = $1; }
  | VOID { $$ = "void"; }
;

function:
    return_type IDENTIFIER '(' parameters ')' block
    { $$ = driver.function($1, $2, $4, $6, @1.begin.line, @1.begin.column); }
  | return_type IDENTIFIER '(' parameters ')' ';'
    { $$ = driver.function($1, $2, $4, nullptr, @1.begin.line, @1.begin.column); }
;

parameters:
    %empty { $$ = driver.make("Parameters", "", @$.begin.line, @$.begin.column); }
  | VOID { $$ = driver.make("Parameters", "", @1.begin.line, @1.begin.column); }
  | parameter_list { $$ = $1; }
;

parameter_list:
    parameter { $$ = driver.make("Parameters", "", @1.begin.line, @1.begin.column, {$1}); }
  | parameter_list ',' parameter { $1->children.push_back($3); $$ = $1; }
;

parameter:
    type IDENTIFIER {
        $$ = driver.make("Parameter", $2, @2.begin.line, @2.begin.column);
        $$->type = parseType($1);
    }
  | type IDENTIFIER '[' ']' {
        $$ = driver.make("Parameter", $2, @2.begin.line, @2.begin.column);
        $$->type = parseType($1);
        $$->array = true;
    }
;

block:
    '{' statements '}' { $$ = $2; $$->line = @1.begin.line; $$->column = @1.begin.column; }
;

statements:
    %empty { $$ = driver.make("Block", "", @$.begin.line, @$.begin.column); }
  | statements statement { $1->children.push_back($2); $$ = $1; }
;

statement:
    block { $$ = $1; }
  | declaration ';' { $$ = $1; }
  | update ';' { $$ = $1; }
  | expression ';' { $$ = driver.make("ExpressionStatement", "", @1.begin.line, @1.begin.column, {$1}); }
  | ';' { $$ = driver.make("Empty", "", @1.begin.line, @1.begin.column); }
  | IF '(' expression ')' statement %prec IF_WITHOUT_ELSE
    { $$ = driver.make("If", "", @1.begin.line, @1.begin.column, {$3, $5}); }
  | IF '(' expression ')' statement ELSE statement
    { $$ = driver.make("If", "", @1.begin.line, @1.begin.column, {$3, $5, $7}); }
  | WHILE '(' expression ')' statement
    { $$ = driver.make("While", "", @1.begin.line, @1.begin.column, {$3, $5}); }
  | DO statement WHILE '(' expression ')' ';'
    { $$ = driver.make("DoWhile", "", @1.begin.line, @1.begin.column, {$5, $2}); }
  | FOR '(' for_init ';' optional_expression ';' for_step ')' statement
    { $$ = driver.make("For", "", @1.begin.line, @1.begin.column, {$3, $5, $7, $9}); }
  | BREAK ';' { $$ = driver.make("Break", "", @1.begin.line, @1.begin.column); }
  | CONTINUE ';' { $$ = driver.make("Continue", "", @1.begin.line, @1.begin.column); }
  | RETURN optional_expression ';'
    { $$ = driver.make("Return", "", @1.begin.line, @1.begin.column, {$2}); }
  | output ';' { $$ = $1; }
  | input ';' { $$ = $1; }
;

declaration:
    type declarators { $$ = driver.declaration($1, $2); }
;

declarators:
    declarator { $$ = driver.make("Declaration", "", @1.begin.line, @1.begin.column, {$1}); }
  | declarators ',' declarator { $1->children.push_back($3); $$ = $1; }
;

declarator:
    IDENTIFIER initializer
    { $$ = driver.make("VariableDeclaration", $1, @1.begin.line, @1.begin.column, {$2}); }
  | IDENTIFIER '[' INTEGER ']' optional_array_initializer {
        Node* size = driver.make("Integer", $3, @3.begin.line, @3.begin.column);
        $$ = driver.make("ArrayDeclaration", $1, @1.begin.line, @1.begin.column, {size, $5});
        $$->array = true;
    }
  | IDENTIFIER '[' ']' '=' array_initializer {
        Node* size = driver.make("Empty", "", @1.begin.line, @1.begin.column);
        $$ = driver.make("ArrayDeclaration", $1, @1.begin.line, @1.begin.column, {size, $5});
        $$->array = true;
    }
;

optional_array_initializer:
    %empty { $$ = driver.make("Empty", "", @$.begin.line, @$.begin.column); }
  | '=' array_initializer { $$ = $2; }
;

array_initializer:
    '{' arguments '}' { $$ = $2; $$->kind = "Initializers"; }
  | '{' argument_list ',' '}' { $$ = $2; $$->kind = "Initializers"; }
;

initializer:
    %empty { $$ = driver.make("Empty", "", @$.begin.line, @$.begin.column); }
  | '=' expression { $$ = $2; }
;

for_init:
    declaration { $$ = $1; }
  | for_step { $$ = $1; }
;

for_step:
    %empty { $$ = driver.make("Empty", "", @$.begin.line, @$.begin.column); }
  | update { $$ = $1; }
  | expression { $$ = driver.make("ExpressionStatement", "", @1.begin.line, @1.begin.column, {$1}); }
;

optional_expression:
    %empty { $$ = driver.make("Empty", "", @$.begin.line, @$.begin.column); }
  | expression { $$ = $1; }
;

assignment_operator:
    '=' { $$ = "="; }
  | ADD_ASSIGN { $$ = "+="; }
  | SUB_ASSIGN { $$ = "-="; }
  | MUL_ASSIGN { $$ = "*="; }
  | DIV_ASSIGN { $$ = "/="; }
  | MOD_ASSIGN { $$ = "%="; }
;

update:
    lvalue assignment_operator expression {
        $$ = driver.make("Assignment", $2, @1.begin.line, @1.begin.column, {$1, $3});
    }
  | lvalue INC { $$ = driver.make("Update", "++", @1.begin.line, @1.begin.column, {$1}); }
  | lvalue DEC { $$ = driver.make("Update", "--", @1.begin.line, @1.begin.column, {$1}); }
  | INC lvalue { $$ = driver.make("Update", "++", @1.begin.line, @1.begin.column, {$2}); }
  | DEC lvalue { $$ = driver.make("Update", "--", @1.begin.line, @1.begin.column, {$2}); }
;

lvalue:
    IDENTIFIER { $$ = driver.make("Variable", $1, @1.begin.line, @1.begin.column); }
  | IDENTIFIER '[' expression ']' {
        Node* array = driver.make("Variable", $1, @1.begin.line, @1.begin.column);
        $$ = driver.make("Index", $1, @1.begin.line, @1.begin.column, {array, $3});
    }
;

output:
    COUT output_items { $$ = $2; $$->text = $1; $$->line = @1.begin.line; $$->column = @1.begin.column; }
;

output_items:
    INSERT output_item { $$ = driver.make("Output", "", @1.begin.line, @1.begin.column, {$2}); }
  | output_items INSERT output_item { $1->children.push_back($3); $$ = $1; }
;

output_item:
    expression { $$ = $1; }
  | STRING { $$ = driver.make("String", $1, @1.begin.line, @1.begin.column); }
  | ENDL { $$ = driver.make("Endl", $1, @1.begin.line, @1.begin.column); }
;

input:
    CIN input_items { $$ = $2; $$->text = $1; $$->line = @1.begin.line; $$->column = @1.begin.column; }
;

input_items:
    EXTRACT lvalue { $$ = driver.make("Input", "", @1.begin.line, @1.begin.column, {$2}); }
  | input_items EXTRACT lvalue { $1->children.push_back($3); $$ = $1; }
;

arguments:
    %empty { $$ = driver.make("Arguments", "", @$.begin.line, @$.begin.column); }
  | argument_list { $$ = $1; }
;

argument_list:
    expression { $$ = driver.make("Arguments", "", @1.begin.line, @1.begin.column, {$1}); }
  | argument_list ',' expression { $1->children.push_back($3); $$ = $1; }
;

expression:
    INTEGER { $$ = driver.make("Integer", $1, @1.begin.line, @1.begin.column); }
  | REAL { $$ = driver.make("Real", $1, @1.begin.line, @1.begin.column); }
  | BOOLEAN { $$ = driver.make("Boolean", $1, @1.begin.line, @1.begin.column); }
  | lvalue { $$ = $1; }
  | IDENTIFIER '(' arguments ')' { $$ = driver.make("Call", $1, @1.begin.line, @1.begin.column, {$3}); }
  | '(' expression ')' { $$ = $2; $$->grouped = true; }
  | '(' type ')' expression %prec UNARY { $$ = driver.make("Cast", $2, @1.begin.line, @1.begin.column, {$4}); }
  | '-' expression %prec UNARY { $$ = driver.make("Unary", "-", @1.begin.line, @1.begin.column, {$2}); }
  | '+' expression %prec UNARY { $$ = driver.make("Unary", "+", @1.begin.line, @1.begin.column, {$2}); }
  | '!' expression %prec UNARY { $$ = driver.make("Unary", "!", @1.begin.line, @1.begin.column, {$2}); }
  | expression '+' expression { $$ = driver.make("Binary", "+", @2.begin.line, @2.begin.column, {$1, $3}); }
  | expression '-' expression { $$ = driver.make("Binary", "-", @2.begin.line, @2.begin.column, {$1, $3}); }
  | expression '*' expression { $$ = driver.make("Binary", "*", @2.begin.line, @2.begin.column, {$1, $3}); }
  | expression '/' expression { $$ = driver.make("Binary", "/", @2.begin.line, @2.begin.column, {$1, $3}); }
  | expression '%' expression { $$ = driver.make("Binary", "%", @2.begin.line, @2.begin.column, {$1, $3}); }
  | expression '<' expression { $$ = driver.make("Binary", "<", @2.begin.line, @2.begin.column, {$1, $3}); }
  | expression '>' expression { $$ = driver.make("Binary", ">", @2.begin.line, @2.begin.column, {$1, $3}); }
  | expression LE expression { $$ = driver.make("Binary", "<=", @2.begin.line, @2.begin.column, {$1, $3}); }
  | expression GE expression { $$ = driver.make("Binary", ">=", @2.begin.line, @2.begin.column, {$1, $3}); }
  | expression EQ expression { $$ = driver.make("Binary", "==", @2.begin.line, @2.begin.column, {$1, $3}); }
  | expression NE expression { $$ = driver.make("Binary", "!=", @2.begin.line, @2.begin.column, {$1, $3}); }
  | expression AND expression { $$ = driver.make("Binary", "&&", @2.begin.line, @2.begin.column, {$1, $3}); }
  | expression OR expression { $$ = driver.make("Binary", "||", @2.begin.line, @2.begin.column, {$1, $3}); }
  | expression '?' expression ':' expression
    { $$ = driver.make("Conditional", "?:", @2.begin.line, @2.begin.column, {$1, $3, $5}); }
;

%%

void yy::parser::error(const location_type& location, const std::string& message) {
    throw Diagnostic("Syntax", location.begin.line, location.begin.column, message);
}
