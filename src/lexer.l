%option noyywrap nounput noinput nodefault
%x COMMENT

%{
#include "parser.hpp"

static yy::location position;
static int commentLine = 1;
static int commentColumn = 1;

#define YY_DECL yy::parser::symbol_type yylex(Context&)
#define YY_USER_ACTION position.step(); position.columns(yyleng);
%}

DIGIT [0-9]
EXP [eE][+-]?{DIGIT}+
REAL (({DIGIT}+\.{DIGIT}*|\.{DIGIT}+){EXP}?|{DIGIT}+{EXP})
NAME [a-zA-Z_][a-zA-Z_0-9]*

%%

^[ \t]*#[ \t]*include[ \t]*<iostream>[ \t\r]*("//"[^\n]*)?$ { return yy::parser::make_HEADER(position); }
[ \t\r]+ {}
\n { position.lines(1); }
"//"[^\n]* {}
"/*" { commentLine = position.begin.line; commentColumn = position.begin.column; BEGIN(COMMENT); }
<COMMENT>"*/" { BEGIN(INITIAL); }
<COMMENT>\n { position.lines(1); }
<COMMENT>. {}
<COMMENT><<EOF>> { throw Diagnostic("Lexical", commentLine, commentColumn, "Unterminated comment"); }

"int" { return yy::parser::make_INT(position); }
"double" { return yy::parser::make_DOUBLE(position); }
"bool" { return yy::parser::make_BOOL(position); }
"void" { return yy::parser::make_VOID(position); }
"if" { return yy::parser::make_IF(position); }
"else" { return yy::parser::make_ELSE(position); }
"while" { return yy::parser::make_WHILE(position); }
"for" { return yy::parser::make_FOR(position); }
"do" { return yy::parser::make_DO(position); }
"break" { return yy::parser::make_BREAK(position); }
"continue" { return yy::parser::make_CONTINUE(position); }
"return" { return yy::parser::make_RETURN(position); }
"using" { return yy::parser::make_USING(position); }
"namespace" { return yy::parser::make_NAMESPACE(position); }
"std" { return yy::parser::make_STD(position); }
"std::cout"|"cout" { return yy::parser::make_COUT(yytext, position); }
"std::cin"|"cin" { return yy::parser::make_CIN(yytext, position); }
"std::endl"|"endl" { return yy::parser::make_ENDL(yytext, position); }
"true"|"false" { return yy::parser::make_BOOLEAN(yytext, position); }
"||" { return yy::parser::make_OR(position); }
"&&" { return yy::parser::make_AND(position); }
"==" { return yy::parser::make_EQ(position); }
"!=" { return yy::parser::make_NE(position); }
"<=" { return yy::parser::make_LE(position); }
">=" { return yy::parser::make_GE(position); }
"<<" { return yy::parser::make_INSERT(position); }
">>" { return yy::parser::make_EXTRACT(position); }
"++" { return yy::parser::make_INC(position); }
"--" { return yy::parser::make_DEC(position); }
"+=" { return yy::parser::make_ADD_ASSIGN(position); }
"-=" { return yy::parser::make_SUB_ASSIGN(position); }
"*=" { return yy::parser::make_MUL_ASSIGN(position); }
"/=" { return yy::parser::make_DIV_ASSIGN(position); }
"%=" { return yy::parser::make_MOD_ASSIGN(position); }

{REAL} { return yy::parser::make_REAL(yytext, position); }
0|[1-9]{DIGIT}* { return yy::parser::make_INTEGER(yytext, position); }
{NAME} { return yy::parser::make_IDENTIFIER(yytext, position); }
\"([^\"\\\n]|\\[^\n])*\" {
    return yy::parser::make_STRING(decodeString(yytext, position.begin.line, position.begin.column), position);
}
\"([^\"\\\n]|\\[^\n])* { throw Diagnostic("Lexical", position.begin.line, position.begin.column, "Unterminated string"); }
[(){}\[\];,=+*/%<>!?:\-] { return yy::parser::symbol_type(static_cast<yy::parser::token_kind_type>(yytext[0]), position); }
<<EOF>> { position.step(); return yy::parser::make_END(position); }
. { throw Diagnostic("Lexical", position.begin.line, position.begin.column, "Unexpected character " + std::string(yytext)); }

%%
