/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

%code requires {
    #include "wave_adapter.h"
    
    typedef struct {
        wit_value_t *elems;
        uint32_t size;
    } temp_array_t;
}

%code {
    #include <stdio.h>
    #include <stdlib.h>
    #include "wasm_canonical_abi.h" 
    #include "wasm_export.h"        

    int wave_lex(void *yylval_param, void *yyscanner);
    void wave_error(void *scanner, wave_invocation_t *out, const char *msg);

    temp_array_t make_temp_array(wit_value_t first_val) {
        temp_array_t arr;
        arr.size = 1;
        arr.elems = wasm_runtime_malloc(sizeof(wit_value_t));
        if (arr.elems) {
            arr.elems[0] = first_val;
        }
        return arr;
    }

    temp_array_t append_temp_array(temp_array_t arr, wit_value_t new_val) {
        uint32_t new_size = arr.size + 1;
        wit_value_t *new_elems = wasm_runtime_malloc(new_size * sizeof(wit_value_t));
        
        if (new_elems) {
            for (uint32_t i = 0; i < arr.size; i++) {
                new_elems[i] = arr.elems[i];
            }
            new_elems[new_size - 1] = new_val;
            wasm_runtime_free(arr.elems);
            arr.elems = new_elems;
            arr.size = new_size;
        }
        return arr;
    }
}

%define api.prefix {wave_}
%define api.pure full
%lex-param {void *scanner}
%parse-param {void *scanner}
%parse-param {wave_invocation_t *out}

%union {
    long long ival;
    double dval;
    char *sval;
    void *node;   
    void *field;
    
    temp_array_t temp_arr; 
}

%token <ival> TOK_INT TOK_BOOL
%token <dval> TOK_FLOAT
%token <sval> TOK_STRING TOK_IDENT
%token TOK_SOME TOK_NONE

%type <node> value variant record list record_body
%type <field> record_field
%token TOK_OK TOK_ERR
%type <node> tuple result

%type <temp_arr> arg_list list_body tuple_body

%%

start_rule:
    invocation
  ;

invocation:
    TOK_IDENT '(' args ')' {
        if (out->func_name == NULL) {
            out->func_name = $1;
        } else {
            wasm_runtime_free($1);
        }
    }
  ;

args:
    {
        /* empty */  
        out->args = wit_values_list_ctor(NULL, 0); 
        out->arg_count = 0;
    }
  | arg_list { 
        out->args = wit_values_list_ctor($1.elems, $1.size); 
        out->arg_count = $1.size;
    }
  ;

arg_list:
    value { $$ = make_temp_array((wit_value_t)$1); }
  | arg_list ',' value { $$ = append_temp_array($1, (wit_value_t)$3); }
  ;

/* --- STANDARD LISTS --- */
list:
    '[' ']'           { $$ = wit_list_ctor(NULL, 0); }
  | '[' list_body ']' { 
        $$ = wit_list_ctor($2.elems, $2.size); 
    }
  ;

list_body:
    value { $$ = make_temp_array((wit_value_t)$1); }
  | list_body ',' value { $$ = append_temp_array($1, (wit_value_t)$3); }
  ;

/* --- TUPLES --- */
tuple:
    '(' ')'           { $$ = wit_tuple_ctor(NULL, 0); }
  | '(' tuple_body ')' { 
        $$ = wit_tuple_ctor($2.elems, $2.size); 
    }
  ;

tuple_body:
    list_body { $$ = $1; }
  ;

value:
    TOK_INT     { $$ = wit_s64_ctor($1); }
  | TOK_FLOAT   { $$ = wit_f64_ctor($1); }
  | TOK_BOOL    { $$ = wit_bool_ctor($1); }
  | TOK_STRING  { $$ = wit_string_ctor($1, strlen($1), 0, 0); wasm_runtime_free($1); }
  | record      { $$ = $1; }
  | list        { $$ = $1; }
  | variant     { $$ = $1; }
  | tuple       { $$ = $1; }
  | result      { $$ = $1; }
  ;


record:
    '{' '}'             { $$ = wit_record_ctor(NULL, 0); }
  | '{' record_body '}' { $$ = $2; }
  ;

record_body:
    record_field {
        ComponentWITRecordField *fields = wasm_runtime_malloc(sizeof(ComponentWITRecordField));
        if (fields) memcpy(&fields[0], (ComponentWITRecordField*)$1, sizeof(ComponentWITRecordField));
        wasm_runtime_free($1);
        $$ = wit_record_ctor(fields, 1);
    }
  | record_body ',' record_field {
        wit_value_t rec_obj = (wit_value_t)$1;
        uint32_t size = rec_obj->value.record_value.size;
        ComponentWITRecordField *new_fields = wasm_runtime_malloc((size + 1) * sizeof(ComponentWITRecordField));
        if (new_fields) {
            memcpy(new_fields, rec_obj->value.record_value.fields, size * sizeof(ComponentWITRecordField));
            memcpy(&new_fields[size], (ComponentWITRecordField*)$3, sizeof(ComponentWITRecordField));
            wasm_runtime_free($3);
            wasm_runtime_free(rec_obj->value.record_value.fields);
            rec_obj->value.record_value.fields = new_fields;
            rec_obj->value.record_value.size = size + 1;
        }
        $$ = rec_obj;
    }
  ;

record_field:
    TOK_IDENT ':' value {
        ComponentWITRecordField *f = wasm_runtime_malloc(sizeof(ComponentWITRecordField));
        if (f) init_record_field(f, $1, strlen($1), (wit_value_t)$3);
        wasm_runtime_free($1);
        $$ = f;
    }
    ;

variant:
    TOK_IDENT '(' value ')' { 
        $$ = wit_variant_ctor($1, strlen($1), (wit_value_t)$3); 
        wasm_runtime_free($1); 
    }
  | TOK_SOME '(' value ')' {
        $$ = wit_option_ctor((wit_value_t)$3);
    }
  | TOK_NONE {
        $$ = wit_option_ctor(NULL);
    }
    ;

    /* TUPLE: (val1, val2, val3) */

/* RESULT: ok(val) or err(val) */
result:
    TOK_OK '(' value ')'  { $$ = wit_result_ctor(false, (wit_value_t)$3); }
  | TOK_ERR '(' value ')' { $$ = wit_result_ctor(true, (wit_value_t)$3); }
  ;

%%

void wave_error(void *scanner, wave_invocation_t *out, const char *msg) {
    printf("Wave Parser Error: %s\n", msg);
}