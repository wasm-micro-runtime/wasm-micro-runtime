/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASI_CLI_H
#define WASI_CLI_H

#include "wasi_p2_types.h"
#include "bh_common.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A handle to a terminal input stream.
 */
typedef int32_t wasi_terminal_input_t;

/**
 * A handle to a terminal output stream.
 */
typedef int32_t wasi_terminal_output_t;

/**
 * A tuple of two strings.
 */
typedef struct wasi_tuple_string_string_t {
    char *key;
    char *value;
} wasi_tuple_string_string_t;

/**
 * @brief Get the environment variables.
 *
 * @param ret_len The number of environment variables.
 * @return A pointer to an array of key-value pairs. The caller is responsible
 * for freeing the returned pointer, as well as the key and value strings in
 * each element of the array.
 */
wasi_tuple_string_string_t *
wasi_cli_get_environment(uint32_t *ret_len);

/**
 * @brief Get the initial working directory.
 *
 * @param is_some Whether the initial working directory is available.
 * @return A pointer to the initial working directory string. The caller is
 * responsible for freeing the returned pointer.
 */
char *
wasi_cli_initial_cwd(bool *is_some);

/**
 * @brief Exit the program.
 *
 * @param status The exit status.
 */
void
wasi_cli_exit(int32_t status);

/**
 * @brief Get the standard input stream.
 *
 * @return The standard input stream.
 */
wasi_input_stream_t
wasi_cli_get_stdin(void);

/**
 * @brief Get the standard output stream.
 *
 * @return The standard output stream.
 */
wasi_output_stream_t
wasi_cli_get_stdout(void);

/**
 * @brief Get the standard error stream.
 *
 * @return The standard error stream.
 */
wasi_output_stream_t
wasi_cli_get_stderr(void);

/**
 * @brief Get the terminal input stream for stdin.
 *
 * @param is_some Whether the terminal input stream is available.
 * @return The terminal input stream handle.
 */
wasi_terminal_input_t
wasi_cli_get_terminal_stdin(bool *is_some);

/**
 * @brief Get the terminal output stream for stdout.
 *
 * @param is_some Whether the terminal output stream is available.
 * @return The terminal output stream handle.
 */
wasi_terminal_output_t
wasi_cli_get_terminal_stdout(bool *is_some);

/**
 * @brief Get the terminal output stream for stderr.
 *
 * @param is_some Whether the terminal output stream is available.
 * @return The terminal output stream handle.
 */
wasi_terminal_output_t
wasi_cli_get_terminal_stderr(bool *is_some);

/// @brief split enviromnet variable strings into key:value tuples
/// @param env environment valiables list
/// @param env_count environment valiables count
/// @return
wasi_tuple_string_string_t *
wasi_cli_environment_split_str(char **env, uint32_t env_count);

#ifdef __cplusplus
}
#endif

#endif /* WASI_CLI_H */
