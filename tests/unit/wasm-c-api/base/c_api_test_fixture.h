/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#pragma once

#include "bh_platform.h"
#include "gtest/gtest.h"
#include "test_helper.h"
#include "wasm_c_api.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

/* wasm_c_api.h #undef's its `own` qualifier at the end, and test_helper.h has
   already pulled in wasm_export.h, so the cases below name the types without
   it. */

/* The compiler sees wasm_byte_t (a typedef of char) while C++ code holds
   fixture bytes as uint8_t; this is the one conversion between the two. */
static inline const wasm_byte_t *
as_wasm_bytes(const uint8_t *bytes)
{
    return reinterpret_cast<const wasm_byte_t *>(bytes);
}

/* The wasm-c-api tests do not use test_helper.h's WAMRRuntimeRAII.

   wasm_c_api.c owns a process-wide singleton engine: wasm_engine_new()
   performs wasm_runtime_full_init() on the first call, and wasm_engine_delete()
   performs wasm_runtime_destroy() once the last reference is gone.  A fixture
   that also drives wasm_runtime_full_init()/wasm_runtime_destroy() itself would
   tear the runtime down underneath the engine the cases use, so these cases
   only ever go through the engine's own reference counting. */
class CApiTestBase : public ::testing::Test
{
  protected:
    wasm_engine_t *engine = nullptr;
    wasm_store_t *store = nullptr;

    void SetUp() override
    {
        bh_log_set_verbose_level(5);
        engine = wasm_engine_new();
        ASSERT_NE(nullptr, engine);

        store = wasm_store_new(engine);
        ASSERT_NE(nullptr, store);
    }

    void TearDown() override
    {
        /* The store must go before the engine: wasm_store_delete() removes its
           modules and instances from a runtime that still has to be alive. */
        if (store) {
            wasm_store_delete(store);
            store = nullptr;
        }
        if (engine) {
            wasm_engine_delete(engine);
            engine = nullptr;
        }
    }
};

/* The fixtures are addressed by one file name whose extension depends on the
   run mode: base/CMakeLists.txt selects ".aot" for the aot mode and ".wasm"
   for the interpreter modes. */
#ifndef WASM_C_API_FIXTURE_EXT
#define WASM_C_API_FIXTURE_EXT ".wasm"
#endif

/* Bytes of one generated fixture, read from the directory the test binary sits
   in (that is where base/CMakeLists.txt puts the .wasm/.aot files). */
static inline std::vector<uint8_t>
read_fixture_file(const std::string &name)
{
    std::ifstream file(get_test_binary_dir() + "/" + name
                           + WASM_C_API_FIXTURE_EXT,
                       std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file),
                                std::istreambuf_iterator<char>());
}

/* A wasm_byte_vec_t holding a fixture, plus the wasm_module_t built from it.

   wasm_module_new() clones the binary, so the original byte vector is released
   by this wrapper; the module reference itself is released here too.

   The store it loads into does not exist yet when gtest constructs the
   fixture members (they are built before SetUp() runs), so loading is a
   separate init() the case calls from SetUp(). */
class FixtureModule
{
  public:
    FixtureModule() = default;

    ~FixtureModule() { reset(); }

    FixtureModule(const FixtureModule &) = delete;
    FixtureModule &operator=(const FixtureModule &) = delete;

    /* Load `name`.wasm into `store`; leaves get() as nullptr on failure. */
    void init(wasm_store_t *store, const std::string &name, bool load = true)
    {
        reset();

        bytes_ = read_fixture_file(name);

        if (!bytes_.empty())
            wasm_byte_vec_new(&binary_, bytes_.size(),
                              as_wasm_bytes(bytes_.data()));

        if (load && !bytes_.empty())
            module_ = wasm_module_new(store, &binary_);
    }

    void reset()
    {
        if (module_) {
            wasm_module_delete(module_);
            module_ = nullptr;
        }
        wasm_byte_vec_delete(&binary_);
        binary_ = wasm_byte_vec_t{ 0 };
        bytes_.clear();
    }

    wasm_module_t *get() const { return module_; }
    bool has_bytes() const { return !bytes_.empty(); }
    const wasm_byte_vec_t &binary() const { return binary_; }

  private:
    std::vector<uint8_t> bytes_;
    wasm_byte_vec_t binary_ = { 0, nullptr, 0, 0, nullptr };
    wasm_module_t *module_ = nullptr;
};

/* A wasm_instance_t plus the exports vector that wasm_instance_exports()
   copies on demand.

   The imports vector passed to wasm_instance_new() is owned by that caller, so
   it is released here right after the instantiation; the externs inside it are
   owned by the caller as well, and have to outlive the instance because
   wasm_c_api.c stores raw pointers to them. */
class FixtureInstance
{
  public:
    FixtureInstance() = default;

    ~FixtureInstance() { reset(); }

    FixtureInstance(const FixtureInstance &) = delete;
    FixtureInstance &operator=(const FixtureInstance &) = delete;

    /* Instantiate `module`; the same init()-instead-of-constructor reason as
       FixtureModule applies.

       wasm_instance_exports() copies the export vector into exports_, so one
       call is enough for all the lookups of a case and the copies stay valid
       until this wrapper is reset. */
    void init(wasm_store_t *store, wasm_module_t *module,
              const wasm_extern_vec_t *imports)
    {
        reset();
        module_ = module;
        instance_ = wasm_instance_new(store, module, imports, &trap_);

        if (instance_) {
            wasm_instance_exports(instance_, &exports_);
            wasm_module_exports(module, &types_);
            ASSERT_EQ(types_.num_elems, exports_.num_elems);
        }
    }

    void reset()
    {
        wasm_extern_vec_delete(&exports_);
        exports_ = wasm_extern_vec_t{ 0, nullptr, 0, 0, nullptr };

        wasm_exporttype_vec_delete(&types_);
        types_ = wasm_exporttype_vec_t{ 0, nullptr, 0, 0, nullptr };

        if (trap_) {
            wasm_trap_delete(trap_);
            trap_ = nullptr;
        }
        if (instance_) {
            wasm_instance_delete(instance_);
            instance_ = nullptr;
        }
        module_ = nullptr;
    }

    wasm_instance_t *get() const { return instance_; }
    wasm_trap_t *trap() const { return trap_; }

    size_t export_count() const { return exports_.num_elems; }

    /* Export number `index`, or nullptr. */
    wasm_extern_t *export_at(size_t index) const
    {
        return index < exports_.num_elems ? exports_.data[index] : nullptr;
    }

    /* Export called `name`; nullptr when there is no such export.  The name is
       taken from the module's export types, which are in the same order as the
       instance's exports.

       The comparison includes the terminator: wasm_name_t is NUL terminated as
       well as counted. */
    wasm_extern_t *export_by_name(const std::string &name) const
    {
        for (size_t i = 0; i < export_count(); i++)
            if (export_name_at(i) == name)
                return exports_.data[i];

        return nullptr;
    }

    /* Name of export `index` without its terminator, "" when there is no
       such export. */
    std::string export_name_at(size_t index) const
    {
        if (index >= types_.num_elems)
            return std::string();

        const wasm_name_t *name = wasm_exporttype_name(types_.data[index]);

        if (!name || !name->data)
            return std::string();

        return std::string(name->data, name->size ? name->size - 1 : 0);
    }

  private:
    wasm_module_t *module_ = nullptr;
    wasm_instance_t *instance_ = nullptr;
    wasm_trap_t *trap_ = nullptr;
    wasm_extern_vec_t exports_ = { 0, nullptr, 0, 0, nullptr };
    wasm_exporttype_vec_t types_ = { 0, nullptr, 0, 0, nullptr };
};

/* Value helpers.  wasm_val_t is a kind tag plus a union; every field the
   runtime reads is set here, including the paddings. */

static inline wasm_val_t
val_i32(int32_t v)
{
    wasm_val_t val = { 0 };

    val.kind = WASM_I32;
    val.of.i32 = v;
    return val;
}

static inline wasm_val_t
val_i64(int64_t v)
{
    wasm_val_t val = { 0 };

    val.kind = WASM_I64;
    val.of.i64 = v;
    return val;
}

static inline wasm_val_t
val_f32(float v)
{
    wasm_val_t val = { 0 };

    val.kind = WASM_F32;
    val.of.f32 = v;
    return val;
}

static inline wasm_val_t
val_f64(double v)
{
    wasm_val_t val = { 0 };

    val.kind = WASM_F64;
    val.of.f64 = v;
    return val;
}

/* A function type of `params` x i32 -> `results` x i32.  The value type
   vectors are consumed by wasm_functype_new(). */
static inline wasm_functype_t *
i32_functype(size_t params, size_t results)
{
    wasm_valtype_vec_t param_types = { 0 }, result_types = { 0 };
    wasm_valtype_t *param_list[8] = { 0 };
    wasm_valtype_t *result_list[8] = { 0 };
    size_t i = 0;

    EXPECT_LE(params, sizeof(param_list) / sizeof(param_list[0]));
    EXPECT_LE(results, sizeof(result_list) / sizeof(result_list[0]));

    for (i = 0; i < params; i++)
        param_list[i] = wasm_valtype_new_i32();
    for (i = 0; i < results; i++)
        result_list[i] = wasm_valtype_new_i32();

    wasm_valtype_vec_new(&param_types, params, param_list);
    wasm_valtype_vec_new(&result_types, results, result_list);
    return wasm_functype_new(&param_types, &result_types);
}

/* The host function that stands in for simple.wasm's
   (import "env" "imported" (func (param i32) (result i32))); it is given the
   same shape, so it doubles as a matching import and as the callback under
   test. */
static inline wasm_trap_t *
host_add(const wasm_val_vec_t *args, wasm_val_vec_t *results)
{
    EXPECT_EQ(2u, args->num_elems);
    EXPECT_EQ(1u, results->size);
    if (args->num_elems != 2 || results->size != 1)
        return nullptr;

    results->data[0] = val_i32(args->data[0].of.i32 + args->data[1].of.i32);
    return nullptr;
}

/* The one-argument host function for the cases that call simple.wat's
   imported function: (i32) -> i32, as the import declares it. */
static inline wasm_trap_t *
host_double(const wasm_val_vec_t *args, wasm_val_vec_t *results)
{
    EXPECT_EQ(1u, args->num_elems);
    EXPECT_EQ(1u, results->size);
    if (args->num_elems != 1 || results->size != 1)
        return nullptr;

    results->data[0] = val_i32(args->data[0].of.i32 * 2);
    return nullptr;
}

/* One host function import: the function, its type, and the extern vector
   wasm_instance_new() takes.

   The function and its type have to outlive any instance built with them,
   because wasm_c_api.c keeps raw pointers to the externs of the vector.

   wasm_func_as_extern() returns the very same object (a wasm_func_t embeds
   its wasm_extern_t), so once the vector was built from the function that
   vector owns it: releasing the vector releases the function, and this wrapper
   must not do it a second time. */
class HostImport
{
  public:
    HostImport(wasm_store_t *store, size_t params, size_t results,
               wasm_func_callback_t callback)
    {
        type_ = i32_functype(params, results);
        func_ = type_ ? wasm_func_new(store, type_, callback) : nullptr;

        if (func_) {
            list_[0] = wasm_func_as_extern(func_);
            wasm_extern_vec_new(&imports_, 1, list_);
            owns_func_ = false;
        }
    }

    ~HostImport()
    {
        wasm_extern_vec_delete(&imports_);
        if (owns_func_)
            wasm_func_delete(func_);
        if (type_)
            wasm_functype_delete(type_);
    }

    HostImport(const HostImport &) = delete;
    HostImport &operator=(const HostImport &) = delete;

    const wasm_extern_vec_t &as_vec() const { return imports_; }
    bool valid() const { return func_ != nullptr; }

  private:
    wasm_functype_t *type_ = nullptr;
    wasm_func_t *func_ = nullptr;
    wasm_extern_t *list_[1] = { nullptr };
    wasm_extern_vec_t imports_ = { 0 };
    bool owns_func_ = true;
};
