/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASI_P2_CLI_WRAPPER_H
#define WASI_P2_CLI_WRAPPER_H

#include "wasm_export.h"
#include "wasi_p2_cli.h"

#ifdef __cplusplus
extern "C" {
#endif

void
wasi_cli_get_environment_wrapper(wasm_exec_env_t exec_env,
                                 uint32_t offset_addr);

void
wasi_cli_get_arguments_wrapper(wasm_exec_env_t exec_env, uint32_t offset_addr);

void
wasi_cli_initial_cwd_wrapper(wasm_exec_env_t exec_env, uint32_t offset_addr);

void
wasi_cli_exit_wrapper(wasm_exec_env_t exec_env, int32_t status);

int32_t
wasi_cli_get_stdin_wrapper(wasm_exec_env_t exec_env);

int32_t
wasi_cli_get_stdout_wrapper(wasm_exec_env_t exec_env);

int32_t
wasi_cli_get_stderr_wrapper(wasm_exec_env_t exec_env);

void
wasi_cli_get_terminal_stdin_wrapper(wasm_exec_env_t exec_env,
                                    uint32_t offset_addr);

void
wasi_cli_get_terminal_stdout_wrapper(wasm_exec_env_t exec_env,
                                     uint32_t offset_addr);

void
wasi_cli_get_terminal_stderr_wrapper(wasm_exec_env_t exec_env,
                                     uint32_t offset_addr);

#ifdef __cplusplus
}
#endif

#endif /* WASI_P2_CLI_WRAPPER_H */
