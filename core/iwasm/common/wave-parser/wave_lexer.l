/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

%{
#include "wave_parser.h" 
#include "wasm_export.h"

typedef WAVE_STYPE YYSTYPE;

void *yyalloc(size_t bytes, void *yyscanner) {
    return wasm_runtime_malloc((unsigned int)bytes);
}

void *yyrealloc(void *ptr, size_t bytes, void *yyscanner) {
    return NULL; 
}

void yyfree(void *ptr, void *yyscanner) {
    if (ptr) wasm_runtime_free(ptr);
}
%}

%option noyywrap
%option reentrant
%option bison-bridge
%option prefix="wave_"
%option noyyalloc noyyfree noyyrealloc
%option noinput nounput 

%%

[ \t\r\n]+          ; 

"true"              { yylval->ival = 1; return TOK_BOOL; }
"false"             { yylval->ival = 0; return TOK_BOOL; }
"some"              { return TOK_SOME; }
"none"              { return TOK_NONE; }
"ok"                { return TOK_OK; }
"err"               { return TOK_ERR; }

-?[0-9]+\.[0-9]+    { yylval->dval = atof(yytext); return TOK_FLOAT; }
-?[0-9]+            { yylval->ival = atoll(yytext); return TOK_INT; }

\"[^"]*\"           { 
    size_t len = strlen(yytext) - 2;
    yylval->sval = wasm_runtime_malloc(len + 1);
    if (yylval->sval) {
        memcpy(yylval->sval, yytext + 1, len);
        yylval->sval[len] = '\0';
    }
    return TOK_STRING; 
}

[a-zA-Z0-9_%\-]+    { 
    yylval->sval = wasm_runtime_malloc(strlen(yytext) + 1);
    if (yylval->sval) strcpy(yylval->sval, yytext);
    return TOK_IDENT; 
}

.                   { return (unsigned char)*yytext; }
%%