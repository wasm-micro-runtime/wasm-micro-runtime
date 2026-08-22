/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#include "gtest/gtest.h"
#include "wasm_native.h"
#include "libc_wasi_p2_wrapper.h"
#include "test_helper.h"

class WasiP2WrapperTest : public testing::Test {
protected:
    void SetUp() override
    {
        runtime_ = std::make_unique<WAMRRuntimeRAII<>>();
    }

    void TearDown() override
    {
        runtime_.reset();
    }

    std::unique_ptr<WAMRRuntimeRAII<>> runtime_;
};

// Test to verify that all exported WASI P2 symbols can be resolved.
TEST_F(WasiP2WrapperTest, LookupAllWasiP2Symbols)
{
    wasi_p2_module_t *modules = NULL;
    uint32_t module_count = get_libc_wasi_p2_export_apis(&modules);

    EXPECT_GT(module_count, 0);

    for (uint32_t i = 0; i < module_count; i++) {
        wasi_p2_module_t module = modules[i];
        for (uint32_t j = 0; j < module.symbol_count; j++) {
            const NativeSymbol symbol = module.symbols[j];

            EXPECT_TRUE(wasm_native_register_wasi_p2_module_func(module.module_name, symbol.symbol));

            void *func_ptr = wasm_native_resolve_symbol(
                module.module_name, symbol.symbol, nullptr, nullptr, nullptr,
                nullptr);
            EXPECT_NE(func_ptr, nullptr)
                << "Failed to resolve symbol: " << symbol.symbol
                << " in module " << module.module_name;
        }

        // Test that a non-existent symbol in a valid module is not resolved.
        void *func_ptr = wasm_native_resolve_symbol(
            module.module_name, "non-existent", nullptr, nullptr, nullptr,
            nullptr);
        EXPECT_EQ(func_ptr, nullptr);
    }

    // Test that a symbol in a non-existent module is not resolved.
    void *func_ptr = wasm_native_resolve_symbol(
        "wasi:non-existent/interface@0.2.3", "non-existent", nullptr, nullptr,
        nullptr, nullptr);
    EXPECT_EQ(func_ptr, nullptr);
}

// Test to ensure that WASI P2 symbols are not resolved in the WASI P1 namespace.
TEST_F(WasiP2WrapperTest, LookupWasiP2SymbolsInWasiP1NamespaceShouldFail)
{
    wasi_p2_module_t *modules = NULL;
    uint32_t module_count = get_libc_wasi_p2_export_apis(&modules);

    EXPECT_GT(module_count, 0);

    for (uint32_t i = 0; i < module_count; i++) {
        wasi_p2_module_t module = modules[i];
        for (uint32_t j = 0; j < module.symbol_count; j++) {
            const NativeSymbol symbol = module.symbols[j];

            // Register it first to ensure it's available in the native symbols list
            EXPECT_TRUE(wasm_native_register_wasi_p2_module_func(module.module_name, symbol.symbol));

            void *func_ptr = wasm_native_resolve_symbol(
                "wasi_snapshot_preview1", symbol.symbol, nullptr, nullptr,
                nullptr, nullptr);
            EXPECT_EQ(func_ptr, nullptr);
        }
    }
}

// Test the registration and unregistration of all WASI P2 modules.
TEST_F(WasiP2WrapperTest, RegisterUnregisterAllModules)
{
    const char *module_name = "wasi:clocks/wall-clock@0.2.3";
    const char *symbol_name = "now";

    // Unregister all modules.
    wasm_native_unregister_wasi_p2_modules();

    // Now, the symbol should not be resolved.
    void *func_ptr = wasm_native_resolve_symbol(module_name, symbol_name, nullptr,
                                          nullptr, nullptr, nullptr);
    EXPECT_EQ(func_ptr, nullptr);

    // Register all modules again.
    EXPECT_TRUE(wasm_native_register_wasi_p2_modules());

    // The symbol should be resolved again.
    func_ptr = wasm_native_resolve_symbol(module_name, symbol_name, nullptr,
                                          nullptr, nullptr, nullptr);
    EXPECT_NE(func_ptr, nullptr);
}

// Test the registration and unregistration of a single WASI P2 module.
TEST_F(WasiP2WrapperTest, RegisterUnregisterSingleModule)
{
    const char *module_to_test = "wasi:clocks/monotonic-clock@0.2.3";
    const char *symbol_to_test = "now";
    const char *another_module = "wasi:clocks/wall-clock@0.2.3";
    const char *another_symbol = "now";

    // Ensure they are registered first
    EXPECT_TRUE(wasm_native_register_wasi_p2_module(another_module));

    // Unregister the specific module.
    wasm_native_unregister_wasi_p2_module(module_to_test);

    // The symbol from the unregistered module should not be resolved.
    void *func_ptr1 = wasm_native_resolve_symbol(module_to_test, symbol_to_test,
                                           nullptr, nullptr, nullptr, nullptr);
    EXPECT_EQ(func_ptr1, nullptr);

    // The symbol from the other module should still be resolved.
    void *func_ptr2 = wasm_native_resolve_symbol(another_module, another_symbol,
                                           nullptr, nullptr, nullptr, nullptr);
    EXPECT_NE(func_ptr2, nullptr);

    // Register the module again.
    EXPECT_TRUE(wasm_native_register_wasi_p2_module(module_to_test));

    // The symbol should be resolved again.
    func_ptr1 = wasm_native_resolve_symbol(module_to_test, symbol_to_test,
                                           nullptr, nullptr, nullptr, nullptr);
    EXPECT_NE(func_ptr1, nullptr);
}

// Test the registration and unregistration of a single function within a WASI P2 module.
TEST_F(WasiP2WrapperTest, RegisterUnregisterSingleFunction)
{
    const char *module_name = "wasi:random/random@0.2.3";
    const char *func_to_test = "get-random-bytes";
    const char *another_func = "get-random-u64";

    // Unregister the whole module to ensure a clean state.
    wasm_native_unregister_wasi_p2_module(module_name);

    // Ensure both functions are not resolved.
    void *func_ptr1 = wasm_native_resolve_symbol(
        module_name, func_to_test, nullptr, nullptr, nullptr, nullptr);
    EXPECT_EQ(func_ptr1, nullptr);
    void *func_ptr2 = wasm_native_resolve_symbol(
        module_name, another_func, nullptr, nullptr, nullptr, nullptr);
    EXPECT_EQ(func_ptr2, nullptr);

    // Register just one function.
    EXPECT_TRUE(
        wasm_native_register_wasi_p2_module_func(module_name, func_to_test));

    // Check that only the registered function is resolved.
    func_ptr1 = wasm_native_resolve_symbol(module_name, func_to_test, nullptr,
                                           nullptr, nullptr, nullptr);
    EXPECT_NE(func_ptr1, nullptr);
    func_ptr2 = wasm_native_resolve_symbol(module_name, another_func, nullptr,
                                           nullptr, nullptr, nullptr);
    EXPECT_EQ(func_ptr2, nullptr);

    // Unregister the single function.
    wasm_native_unregister_wasi_p2_module_func(module_name, func_to_test);

    // Check that it's no longer resolved.
    func_ptr1 = wasm_native_resolve_symbol(module_name, func_to_test, nullptr,
                                           nullptr, nullptr, nullptr);
    EXPECT_EQ(func_ptr1, nullptr);

    // Re-register the whole module so other tests are not affected.
    EXPECT_TRUE(wasm_native_register_wasi_p2_module(module_name));
}