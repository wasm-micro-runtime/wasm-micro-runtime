/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <gtest/gtest.h>
#include "helpers.h"
#include <iostream>
#include <cmath>

class ComponentExecutionTest : public testing::Test
{
  public:
    ComponentExecutionTest() {}
    ~ComponentExecutionTest() {}
    RuntimeInitArgs init_args;
    unsigned char *component_raw = NULL;
    bool exception = false;
    std::unique_ptr<ComponentHelper> helper;

    char error_buf[128];
    char global_heap_buf[HEAP_SIZE]; // 100 MB

    bool runtime_init = false;

    virtual void SetUp() {
        helper = std::make_unique<ComponentHelper>();
        helper->do_setup();
    }

    virtual void TearDown() {
        helper->do_teardown();
        helper = nullptr;
    }
};

// Test correct call on add(3, 4) method
TEST_F(ComponentExecutionTest, TestAddWASM)
{
    helper->reset_component();
    bool ret = helper->read_wasm_file((std::string("add.wasm").c_str()));
    ASSERT_TRUE(ret);
    ASSERT_TRUE(helper->component_raw != NULL);

    ret = helper->load_component();
    ASSERT_TRUE(ret);
    ret = helper->instantiate_component();
    ASSERT_TRUE(ret);

    uint32 argc1 = 0;
    uint32 *argv1 = (uint32 *)wasm_runtime_malloc(sizeof(uint32) * 10);
    ASSERT_TRUE(argv1 != NULL);

    ret = wasm_component_application_execute_func_ex(this->helper->component_inst, (char*)"add(3,4)", &argc1, &argv1);

    // Check results
    bool function_succeeded = ret && !wasm_component_runtime_get_exception(this->helper->component_inst);

    uint32 result = 0;
    if (function_succeeded && argv1) {
        result = argv1[0];
    }

    // Clean up
    if (argv1) {
        wasm_runtime_free(argv1);
        argv1 = NULL;
    }


    ASSERT_TRUE(function_succeeded);

    ASSERT_GT(argc1, 0U);  // Should have at least one result cell
    ASSERT_EQ(result, 7U);  // Result should be 7
}

// Call non-existent function
TEST_F(ComponentExecutionTest, TestCallNonExistentFunction)
{
    helper->reset_component();
    bool ret = helper->read_wasm_file("add.wasm");
    ASSERT_TRUE(ret);

    ret = helper->load_component();
    ASSERT_TRUE(ret);
    ret = helper->instantiate_component();
    ASSERT_TRUE(ret);

    uint32 argc1 = 0;
    uint32 *argv1 = (uint32 *)wasm_runtime_malloc(sizeof(uint32) * 10);
    ASSERT_TRUE(argv1 != NULL);

    // Call non-existent function
    ret = wasm_component_application_execute_func_ex(this->helper->component_inst, (char*)"random_func(3,4)", &argc1, &argv1);

    // Should fail
    ASSERT_FALSE(ret);

    // Should have exception message
    const char* exception = wasm_component_runtime_get_exception(this->helper->component_inst);
    ASSERT_TRUE(exception != NULL);
    ASSERT_TRUE(strstr(exception, "Exception: lookup function random_func failed") != NULL);

    // Cleanup
    wasm_runtime_free(argv1);
}

// Call with wrong parameter count
TEST_F(ComponentExecutionTest, TestCallWithWrongParameterCount)
{
    helper->reset_component();
    bool ret = helper->read_wasm_file("add.wasm");
    ASSERT_TRUE(ret);

    ret = helper->load_component();
    ASSERT_TRUE(ret);
    ret = helper->instantiate_component();
    ASSERT_TRUE(ret);


    uint32 argc1 = 0;
    uint32 *argv1 = (uint32 *)wasm_runtime_malloc(sizeof(uint32) * 10);
    ASSERT_TRUE(argv1 != NULL);

    // add function expects 2 parameters, but provide 3
    ret = wasm_component_application_execute_func_ex(
        this->helper->component_inst, (char *)"add(3,4,5)", &argc1, &argv1);

    // Should fail
    ASSERT_FALSE(ret);

    // Should have exception message about argument count
    const char* exception = wasm_component_runtime_get_exception(this->helper->component_inst);
    ASSERT_TRUE(exception != NULL);
    ASSERT_TRUE(strstr(exception, "This method waited 2 arguments, but received 3\n") != NULL);

    // Cleanup
    wasm_runtime_free(argv1);
}

// Call div with zero to trigger trap
TEST_F(ComponentExecutionTest, TestIntDivideByZeroTrap)
{
    helper->reset_component();
    bool ret = helper->read_wasm_file("add.wasm");
    ASSERT_TRUE(ret);

    ret = helper->load_component();
    ASSERT_TRUE(ret);
    ret = helper->instantiate_component();
    ASSERT_TRUE(ret);

    uint32 argc1 = 0;
    uint32 *argv1 = (uint32 *)wasm_runtime_malloc(sizeof(uint32) * 10);
    ASSERT_TRUE(argv1 != NULL);

    // Call div with divisor = 0
    ret = wasm_component_application_execute_func_ex(this->helper->component_inst, (char *)"div(10, 0)", &argc1, &argv1);

    // Should fail due to trap
    ASSERT_FALSE(ret);

    // Should have exception message about the trap
    const char* exception = wasm_component_runtime_get_exception(this->helper->component_inst);

    ASSERT_TRUE(exception != NULL);
    ASSERT_TRUE(strstr(exception, "integer divide by zero") != NULL);

    // Cleanup
    wasm_runtime_free(argv1);
}

// Test float division by zero returns infinity
TEST_F(ComponentExecutionTest, TestFloatDivideByZeroReturnsInfinity)
{
    helper->reset_component();
    bool ret = helper->read_wasm_file("add.wasm");
    ASSERT_TRUE(ret);

    ret = helper->load_component();
    ASSERT_TRUE(ret);
    ret = helper->instantiate_component();
    ASSERT_TRUE(ret);

    uint32 argc1 = 0;
    uint32 *argv1 = (uint32 *)wasm_runtime_malloc(sizeof(uint32) * 10);
    ASSERT_TRUE(argv1 != NULL);

    // Call fdiv with divisor = 0.0
    ret = wasm_component_application_execute_func_ex(this->helper->component_inst, (char *)"fdiv(10.0, 0.0)", &argc1, &argv1);

    // Should succeed (no trap for float division by zero)
    bool function_succeeded = ret && !wasm_component_runtime_get_exception(this->helper->component_inst);

    float result = 0.0f;
    if (function_succeeded && argv1) {
        result = *((float *)argv1);
    }

    // Cleanup
    wasm_runtime_free(argv1);

    // Assert function succeeded
    ASSERT_TRUE(function_succeeded);

    // Assert result is infinity
    ASSERT_TRUE(isinf(result));
    ASSERT_TRUE(result > 0);  // Positive infinity
}
