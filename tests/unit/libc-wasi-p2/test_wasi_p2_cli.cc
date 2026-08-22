/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#include <gtest/gtest.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#include "wasm_export.h"
#include "wasi_p2_types.h"


extern "C" {
#include "wasi_p2_cli.h"
}

// The set_wasi_p2_args function is not in a public header,
// so we declare it here.
extern "C" void
set_wasi_p2_args(int argc, char **argv);

class WasiP2CliTest : public testing::Test {
protected:
    void SetUp() override {
        wasm_runtime_init();
        // Set up some dummy environment variables for testing
        setenv("VAR1", "value1", 1);
        setenv("VAR2", "value2", 1);
    }

    void TearDown() override {
        unsetenv("VAR1");
        unsetenv("VAR2");
        wasm_runtime_destroy();
    }
};

// Test: wasi:cli/environment.get-environment
TEST_F(WasiP2CliTest, Environment_GetEnvironment) {
    uint32_t len;

    // Test with null len pointer, which should fail.
    wasi_tuple_string_string_t *env = wasi_cli_get_environment(NULL);
    ASSERT_EQ(env, nullptr);

    // Test with some environment variables.
    setenv("VAR3", "", 1);
    env = wasi_cli_get_environment(&len);
    ASSERT_GT(len, 0);

    // Verify that the number of returned variables matches the environment.
    uint32_t env_count = 0;
    for (char **environ_ptr = environ; *environ_ptr; environ_ptr++) {
        env_count++;
    }
    ASSERT_EQ(len, env_count);

    // Check if the expected environment variables are present.
    bool found_var1 = false;
    bool found_var2 = false;
    bool found_var3 = false;
    for (uint32_t i = 0; i < len; i++) {
        if (strcmp(env[i].key, "VAR1") == 0
            && strcmp(env[i].value, "value1") == 0) {
            found_var1 = true;
        }
        if (strcmp(env[i].key, "VAR2") == 0
            && strcmp(env[i].value, "value2") == 0) {
            found_var2 = true;
        }
        if (strcmp(env[i].key, "VAR3") == 0 && strcmp(env[i].value, "") == 0) {
            found_var3 = true;
        }
        free(env[i].key);
        free(env[i].value);
    }
    free(env);
    ASSERT_TRUE(found_var1);
    ASSERT_TRUE(found_var2);
    ASSERT_TRUE(found_var3);
    unsetenv("VAR3");
}

// Test: wasi:cli/stdin.get-stdin, wasi:cli/stdout.get-stdout, and wasi:cli/stderr.get-stderr
TEST_F(WasiP2CliTest, Stdio_GetStdio) {
    ASSERT_EQ(wasi_cli_get_stdin(), STDIN_FILENO);
    ASSERT_EQ(wasi_cli_get_stdout(), STDOUT_FILENO);
    ASSERT_EQ(wasi_cli_get_stderr(), STDERR_FILENO);
}

// Test: wasi:cli/terminal-*.get-terminal-*
TEST_F(WasiP2CliTest, Terminal_GetTerminals) {
    bool is_some;

    wasi_cli_get_terminal_stdin(&is_some);
    ASSERT_EQ(isatty(STDIN_FILENO), is_some);

    wasi_cli_get_terminal_stdout(&is_some);
    ASSERT_EQ(isatty(STDOUT_FILENO), is_some);

    wasi_cli_get_terminal_stderr(&is_some);
    ASSERT_EQ(isatty(STDERR_FILENO), is_some);
}

// Test: wasi:cli/exit.exit
TEST_F(WasiP2CliTest, Exit_Exit) {
    // Test for successful exit (status 0).
    pid_t pid = fork();
    ASSERT_NE(pid, -1);

    if (pid == 0) {
        // Child process
        wasi_cli_exit(0);
        // This should not be reached.
        exit(127);
    } else {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
        ASSERT_TRUE(WIFEXITED(status));
        ASSERT_EQ(WEXITSTATUS(status), 0);
    }

    // Test for failure exit (status 1).
    pid = fork();
    ASSERT_NE(pid, -1);

    if (pid == 0) {
        // Child process
        wasi_cli_exit(1);
        // This should not be reached.
        exit(127);
    } else {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
        ASSERT_TRUE(WIFEXITED(status));
        ASSERT_EQ(WEXITSTATUS(status), 1);
    }
}

// Test: wasi:cli/environment.initial-cwd
TEST_F(WasiP2CliTest, Environment_InitialCwd)
{
    bool is_some;
    char *cwd = wasi_cli_initial_cwd(&is_some);
    ASSERT_TRUE(is_some);
    ASSERT_TRUE(cwd != NULL);

    // Verify that the returned path matches the current working directory.
    char expected_cwd[1024];
    ASSERT_TRUE(getcwd(expected_cwd, sizeof(expected_cwd)) != NULL);

    ASSERT_STREQ(cwd, expected_cwd);

    free(cwd);
}