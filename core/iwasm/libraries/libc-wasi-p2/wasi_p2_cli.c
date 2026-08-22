/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasi_p2_cli.h"
#include "wasi_p2_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <bh_common.h>

#include "wasi_p2_common.h"

wasi_tuple_string_string_t *
wasi_cli_environment_split_str(char **env, uint32_t env_count)
{

    if (!env_count || !env) {
        return NULL;
    }
    wasi_tuple_string_string_t *ret =
        wasm_runtime_calloc(env_count, sizeof(wasi_tuple_string_string_t));
    if (!ret) {
        return NULL;
    }

    for (uint32_t i = 0; i < env_count; i++) {
        char *eq = strchr(env[i], '=');
        if (eq) {
            size_t key_len = eq - env[i];
            ret[i].key = wasm_runtime_malloc(key_len + 1);
            if (!ret[i].key) {
                goto fail;
            }
            memcpy(ret[i].key, env[i], key_len);
            ret[i].key[key_len] = '\0';
            ret[i].value = wa_strdup(eq + 1);
            if (!ret[i].value) {
                goto fail;
            }
        }
        else {
            // Handle case where a variable might not have a value.
            ret[i].key = wa_strdup(env[i]);
            if (!ret[i].key) {
                goto fail;
            }
            ret[i].value = wa_strdup("");
            if (!ret[i].value) {
                goto fail;
            }
        }
    }

    return ret;

fail:
    for (uint32_t i = 0; i < env_count; i++) {
        if (ret[i].key)
            wasm_runtime_free(ret[i].key);
        if (ret[i].value)
            wasm_runtime_free(ret[i].value);
    }
    wasm_runtime_free(ret);
    return NULL;
}

/**
 * @brief Get the environment variables.
 * @details Implements the `get-environment` function from the
 *          `wasi:cli/environment` interface.
 * @param[out] ret_len Pointer to store the number of environment variables.
 * @return A pointer to an array of key-value pairs. The caller is
 *         responsible for freeing this memory.
 */
wasi_tuple_string_string_t *
wasi_cli_get_environment(uint32_t *ret_len)
{
    if (!ret_len) {
        return NULL;
    }

    // `environ` is a POSIX global variable containing environment strings.
    extern char **environ;
    char **env = environ;
    uint32_t count = 0;

    // First, count the number of environment variables.
    while (*env) {
        count++;
        env++;
    }
    env = environ;

    const wasi_tuple_string_string_t *ret =
        wasm_runtime_calloc(count, sizeof(wasi_tuple_string_string_t));
    if (!ret) {
        *ret_len = 0;
        return NULL;
    }

    *ret_len = count;
    return wasi_cli_environment_split_str(env, count);
}

/**
 * @brief Get the standard input stream.
 * @details Implements the `get-stdin` function from the `wasi:cli/stdin`
 *          interface.
 * @return The resource handle for stdin.
 */
wasi_input_stream_t
wasi_cli_get_stdin(void)
{
    return STDIN_FILENO;
}

/**
 * @brief Get the standard output stream.
 * @details Implements the `get-stdout` function from the `wasi:cli/stdout`
 *          interface.
 * @return The resource handle for stdout.
 */
wasi_output_stream_t
wasi_cli_get_stdout(void)
{
    return STDOUT_FILENO;
}

/**
 * @brief Get the standard error stream.
 * @details Implements the `get-stderr` function from the `wasi:cli/stderr`
 *          interface.
 * @return The resource handle for stderr.
 */
wasi_output_stream_t
wasi_cli_get_stderr(void)
{
    return STDERR_FILENO;
}

/**
 * @brief Exit the program.
 * @details Implements the `exit` function from the `wasi:cli/exit`
 *          interface. This function does not return.
 * @param status A result indicating success (0) or error (1).
 */
void
wasi_cli_exit(int32_t status)
{
    exit(status);
}

/**
 * @brief Static helper to check if a file descriptor is a terminal.
 * @details This is the core implementation for the `get-terminal-*` functions.
 * @param fd The file descriptor to check.
 * @param[out] is_some Set to true if `fd` is a terminal, false otherwise.
 * @return The file descriptor if it's a terminal, otherwise -1.
 */
static int
wasi_cli_get_terminal(int fd, bool *is_some)
{
    if (!is_some) {
        return -1;
    }
    if (isatty(fd)) {
        *is_some = true;
        return fd;
    }
    *is_some = false;
    return -1;
}

/**
 * @brief Get the terminal input handle if stdin is a TTY.
 * @details Implements `get-terminal-stdin` from `wasi:cli/terminal-stdin`.
 * @param[out] is_some Set to true if stdin is a terminal, false otherwise.
 * @return A terminal input handle if available.
 */
wasi_terminal_input_t
wasi_cli_get_terminal_stdin(bool *is_some)
{
    return wasi_cli_get_terminal(STDIN_FILENO, is_some);
}

/**
 * @brief Get the terminal output handle if stdout is a TTY.
 * @details Implements `get-terminal-stdout` from `wasi:cli/terminal-stdout`.
 * @param[out] is_some Set to true if stdout is a terminal, false otherwise.
 * @return A terminal output handle if available.
 */
wasi_terminal_output_t
wasi_cli_get_terminal_stdout(bool *is_some)
{
    return wasi_cli_get_terminal(STDOUT_FILENO, is_some);
}

/**
 * @brief Get the terminal output handle if stderr is a TTY.
 * @details Implements `get-terminal-stderr` from `wasi:cli/terminal-stderr`.
 * @param[out] is_some Set to true if stderr is a terminal, false otherwise.
 * @return A terminal output handle if available.
 */
wasi_terminal_output_t
wasi_cli_get_terminal_stderr(bool *is_some)
{
    return wasi_cli_get_terminal(STDERR_FILENO, is_some);
}

/**
 * @brief Get the initial current working directory.
 * @details Implements the `initial-cwd` function from the
 * `wasi:cli/environment` interface.
 * @param[out] is_some Set to true if the CWD was successfully retrieved.
 * @return The current working directory as a string, or NULL on failure. The
 *         caller is responsible for freeing the returned string.
 */
char *
wasi_cli_initial_cwd(bool *is_some)
{
    char *cwd = NULL;
    size_t size = 1024;
    char *buffer = NULL;

    if (!is_some) {
        return NULL;
    }

    while (1) {
        buffer = wasm_runtime_realloc(cwd, size);
        if (!buffer) {
            wasm_runtime_free(cwd);
            *is_some = false;
            return NULL;
        }
        cwd = buffer;

        if (getcwd(cwd, size) != NULL) {
            *is_some = true;
            return cwd;
        }

        if (errno != ERANGE) {
            wasm_runtime_free(cwd);
            *is_some = false;
            return NULL;
        }
        size *= 2;
    }
}