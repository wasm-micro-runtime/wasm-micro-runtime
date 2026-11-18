/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "test_helper.h"
#include "gtest/gtest.h"

#include "bh_read_file.h"
#include "bh_platform.h"
#include "wasm_export.h"
#include "wasm_memory.h"
#include "wasm_runtime.h"
#include "wasm_runtime_common.h"

#include <sys/mman.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#define WASM_PAGE 65536u

/* ------------------------------------------------------------------ */
/* Allocator front end: wasm_runtime_memory_init() & wasm_runtime_*alloc */
/* ------------------------------------------------------------------ */

namespace {

struct AllocCounters {
    int malloc_calls = 0;
    int realloc_calls = 0;
    int free_calls = 0;
};

AllocCounters g_counters;

void *
counting_malloc(unsigned int size)
{
    g_counters.malloc_calls++;
    return malloc(size);
}

void *
counting_realloc(void *ptr, unsigned int size)
{
    g_counters.realloc_calls++;
    return realloc(ptr, size);
}

void
counting_free(void *ptr)
{
    g_counters.free_calls++;
    free(ptr);
}

alignas(8) char g_pool[256 * 1024];

} // namespace

class wasm_memory_alloc_test : public testing::Test
{
  protected:
    void SetUp() override { g_counters = AllocCounters(); }
};

TEST_F(wasm_memory_alloc_test, uninitialized_runtime_rejects_everything)
{
    int local = 0;
    mem_alloc_info_t info;

    EXPECT_EQ(nullptr, wasm_runtime_malloc(8));
    EXPECT_EQ(nullptr, wasm_runtime_realloc(nullptr, 8));
    EXPECT_EQ(nullptr, wasm_runtime_aligned_alloc(8, 8));
    EXPECT_FALSE(wasm_runtime_get_mem_alloc_info(&info));
    EXPECT_EQ(UINT32_MAX, wasm_runtime_memory_pool_size());

    /* Only logs a warning. */
    wasm_runtime_free(&local);
    wasm_runtime_free(nullptr);
}

TEST_F(wasm_memory_alloc_test, init_rejects_invalid_arguments)
{
    MemAllocOption option;

    memset(&option, 0, sizeof(option));
    EXPECT_FALSE(wasm_runtime_memory_init((mem_alloc_type_t)0x7fff, &option));

    /* pool too small to hold the allocator metadata */
    option.pool.heap_buf = g_pool;
    option.pool.heap_size = 8;
    EXPECT_FALSE(wasm_runtime_memory_init(Alloc_With_Pool, &option));

    /* missing malloc / free */
    memset(&option, 0, sizeof(option));
    option.allocator.realloc_func = (void *)counting_realloc;
    EXPECT_FALSE(wasm_runtime_memory_init(Alloc_With_Allocator, &option));

    option.allocator.malloc_func = (void *)counting_malloc;
    EXPECT_FALSE(wasm_runtime_memory_init(Alloc_With_Allocator, &option));

    /* malloc and free must be different functions */
    option.allocator.free_func = (void *)counting_malloc;
    EXPECT_FALSE(wasm_runtime_memory_init(Alloc_With_Allocator, &option));

    /* none of the failures left the runtime initialized */
    EXPECT_EQ(nullptr, wasm_runtime_malloc(8));
}

TEST_F(wasm_memory_alloc_test, system_allocator)
{
    MemAllocOption option;
    mem_alloc_info_t info;
    void *p;

    memset(&option, 0, sizeof(option));
    ASSERT_TRUE(wasm_runtime_memory_init(Alloc_With_System_Allocator, &option));

    EXPECT_EQ(UINT32_MAX, wasm_runtime_memory_pool_size());
    EXPECT_FALSE(wasm_runtime_get_mem_alloc_info(&info));

    /* size zero is bumped to one byte */
    p = wasm_runtime_malloc(0);
    ASSERT_NE(nullptr, p);
    p = wasm_runtime_realloc(p, 128);
    ASSERT_NE(nullptr, p);
    wasm_runtime_free(p);
    wasm_runtime_free(nullptr);

    /* aligned alloc is POOL mode only */
    EXPECT_EQ(nullptr, wasm_runtime_aligned_alloc(16, 16));

    wasm_runtime_memory_destroy();
}

TEST_F(wasm_memory_alloc_test, user_allocator)
{
    MemAllocOption option;
    void *p;

    memset(&option, 0, sizeof(option));
    option.allocator.malloc_func = (void *)counting_malloc;
    option.allocator.realloc_func = (void *)counting_realloc;
    option.allocator.free_func = (void *)counting_free;
    ASSERT_TRUE(wasm_runtime_memory_init(Alloc_With_Allocator, &option));

    p = wasm_runtime_malloc(32);
    ASSERT_NE(nullptr, p);
    p = wasm_runtime_realloc(p, 64);
    ASSERT_NE(nullptr, p);
    wasm_runtime_free(p);

    EXPECT_EQ(1, g_counters.malloc_calls);
    EXPECT_EQ(1, g_counters.realloc_calls);
    EXPECT_EQ(1, g_counters.free_calls);

    EXPECT_EQ(nullptr, wasm_runtime_aligned_alloc(16, 16));
    wasm_runtime_memory_destroy();
}

TEST_F(wasm_memory_alloc_test, user_allocator_without_realloc)
{
    MemAllocOption option;
    void *p;

    memset(&option, 0, sizeof(option));
    option.allocator.malloc_func = (void *)counting_malloc;
    option.allocator.free_func = (void *)counting_free;
    ASSERT_TRUE(wasm_runtime_memory_init(Alloc_With_Allocator, &option));

    p = wasm_runtime_malloc(32);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ(nullptr, wasm_runtime_realloc(p, 64));
    wasm_runtime_free(p);

    wasm_runtime_memory_destroy();
}

TEST_F(wasm_memory_alloc_test, pool_allocator)
{
    MemAllocOption option;
    mem_alloc_info_t info;
    void *p, *q;

    memset(&option, 0, sizeof(option));
    option.pool.heap_buf = g_pool;
    option.pool.heap_size = sizeof(g_pool);
    ASSERT_TRUE(wasm_runtime_memory_init(Alloc_With_Pool, &option));

    EXPECT_EQ(sizeof(g_pool), wasm_runtime_memory_pool_size());

    memset(&info, 0, sizeof(info));
    EXPECT_TRUE(wasm_runtime_get_mem_alloc_info(&info));
    EXPECT_GT(info.total_size, 0u);

    /* size zero is bumped to one byte */
    p = wasm_runtime_malloc(0);
    ASSERT_NE(nullptr, p);
    p = wasm_runtime_realloc(p, 256);
    ASSERT_NE(nullptr, p);
    wasm_runtime_free(p);
    wasm_runtime_free(nullptr);

    /* zero alignment is invalid, zero size gets one alignment unit */
    EXPECT_EQ(nullptr, wasm_runtime_aligned_alloc(16, 0));
    p = wasm_runtime_aligned_alloc(0, 64);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ(0u, (uintptr_t)p % 64);
    q = wasm_runtime_aligned_alloc(96, 16);
    ASSERT_NE(nullptr, q);
    EXPECT_EQ(0u, (uintptr_t)q % 16);
    wasm_runtime_free(p);
    wasm_runtime_free(q);

    wasm_runtime_memory_destroy();
    EXPECT_EQ(UINT32_MAX, wasm_runtime_memory_pool_size());
}

/* ------------------------------------------------------------------ */
/* Module instance fixture                                             */
/* ------------------------------------------------------------------ */

namespace {

struct EnlargeEvent {
    int count = 0;
    uint32_t inc_page_count = 0;
    uint64_t current_memory_size = 0;
    enlarge_memory_error_reason_t reason = INTERNAL_ERROR;
    wasm_module_inst_t instance = nullptr;
};

void
record_enlarge_error(uint32_t inc_page_count, uint64_t current_memory_size,
                     uint32_t memory_index,
                     enlarge_memory_error_reason_t failure_reason,
                     wasm_module_inst_t instance, wasm_exec_env_t exec_env,
                     void *user_data)
{
    EnlargeEvent *ev = (EnlargeEvent *)user_data;

    (void)memory_index;
    (void)exec_env;
    ev->count++;
    ev->inc_page_count = inc_page_count;
    ev->current_memory_size = current_memory_size;
    ev->reason = failure_reason;
    ev->instance = instance;
}

} // namespace

class wasm_memory_test : public testing::Test
{
  protected:
    void SetUp() override
    {
        std::string path = get_test_binary_dir() + "/memory.wasm";
        char error_buf[128] = { 0 };

        buf_ = (uint8_t *)bh_read_file_to_buffer(path.c_str(), &buf_size_);
        ASSERT_NE(nullptr, buf_);
        module_ =
            wasm_runtime_load(buf_, buf_size_, error_buf, sizeof(error_buf));
        ASSERT_NE(nullptr, module_) << error_buf;
        inst_ = wasm_runtime_instantiate(module_, 16 * 1024, 0, error_buf,
                                         sizeof(error_buf));
        ASSERT_NE(nullptr, inst_) << error_buf;
    }

    void TearDown() override
    {
        wasm_runtime_set_enlarge_mem_error_callback(nullptr, nullptr);
        if (inst_) {
            wasm_runtime_detach_shared_heap(inst_);
            wasm_runtime_deinstantiate(inst_);
        }
        if (module_)
            wasm_runtime_unload(module_);
        if (buf_)
            wasm_runtime_free(buf_);
    }

    WASMModuleInstance *mi() { return (WASMModuleInstance *)inst_; }

    WASMMemoryInstance *mem() { return wasm_get_default_memory(mi()); }

    uint8_t *base()
    {
        return (uint8_t *)wasm_runtime_addr_app_to_native(inst_, 0);
    }

    bool has_oob_exception()
    {
        const char *e = wasm_runtime_get_exception(inst_);
        bool ret = e && strstr(e, "out of bounds memory access");

        wasm_runtime_clear_exception(inst_);
        return ret;
    }

    WAMRRuntimeRAII<1024 * 1024> runtime_;
    uint32_t buf_size_ = 0;
    uint8_t *buf_ = nullptr;
    wasm_module_t module_ = nullptr;
    wasm_module_inst_t inst_ = nullptr;
};

TEST_F(wasm_memory_test, validate_app_addr)
{
    EXPECT_TRUE(wasm_runtime_validate_app_addr(inst_, 0, 1));
    EXPECT_TRUE(wasm_runtime_validate_app_addr(inst_, 0, WASM_PAGE));
    EXPECT_TRUE(wasm_runtime_validate_app_addr(inst_, WASM_PAGE - 1, 1));
    /* an empty range at the very end is fine */
    EXPECT_TRUE(wasm_runtime_validate_app_addr(inst_, WASM_PAGE, 0));
    EXPECT_FALSE(has_oob_exception());

    EXPECT_FALSE(wasm_runtime_validate_app_addr(inst_, WASM_PAGE - 1, 2));
    EXPECT_TRUE(has_oob_exception());

    EXPECT_FALSE(wasm_runtime_validate_app_addr(inst_, WASM_PAGE, 1));
    EXPECT_TRUE(has_oob_exception());

    /* size larger than the biggest linear memory */
    EXPECT_FALSE(wasm_runtime_validate_app_addr(inst_, 0, UINT64_MAX));
    EXPECT_TRUE(has_oob_exception());

    /* offset + size overflows */
    EXPECT_FALSE(wasm_runtime_validate_app_addr(inst_, UINT64_MAX, 1));
    EXPECT_TRUE(has_oob_exception());
    EXPECT_FALSE(wasm_runtime_validate_app_addr(inst_, UINT32_MAX, 2));
    EXPECT_TRUE(has_oob_exception());
}

TEST_F(wasm_memory_test, validate_app_str_addr)
{
    uint8_t *data = base();

    strcpy((char *)data + 100, "abc");
    EXPECT_TRUE(wasm_runtime_validate_app_str_addr(inst_, 100));

    /* the string runs into the end of the linear memory */
    memset(data + WASM_PAGE - 8, 'x', 8);
    EXPECT_FALSE(wasm_runtime_validate_app_str_addr(inst_, WASM_PAGE - 8));
    EXPECT_TRUE(has_oob_exception());

    /* start offset outside of the linear memory */
    EXPECT_FALSE(wasm_runtime_validate_app_str_addr(inst_, WASM_PAGE));
    EXPECT_TRUE(has_oob_exception());
    EXPECT_FALSE(wasm_runtime_validate_app_str_addr(inst_, UINT64_MAX));
    EXPECT_TRUE(has_oob_exception());
}

TEST_F(wasm_memory_test, validate_native_addr)
{
    uint8_t *data = base();

    EXPECT_TRUE(wasm_runtime_validate_native_addr(inst_, data, 1));
    EXPECT_TRUE(wasm_runtime_validate_native_addr(inst_, data, WASM_PAGE));
    EXPECT_TRUE(wasm_runtime_validate_native_addr(inst_, data + 10, 20));

    EXPECT_FALSE(wasm_runtime_validate_native_addr(inst_, data - 1, 1));
    EXPECT_TRUE(has_oob_exception());
    EXPECT_FALSE(
        wasm_runtime_validate_native_addr(inst_, data + WASM_PAGE - 1, 2));
    EXPECT_TRUE(has_oob_exception());

    /* size larger than the biggest linear memory */
    EXPECT_FALSE(wasm_runtime_validate_native_addr(inst_, data, UINT64_MAX));
    EXPECT_TRUE(has_oob_exception());

    /* addr + size overflows the address space */
    EXPECT_FALSE(wasm_runtime_validate_native_addr(
        inst_, (void *)(UINTPTR_MAX - 8), 64));
    EXPECT_TRUE(has_oob_exception());
}

TEST_F(wasm_memory_test, address_conversion)
{
    uint8_t *data = base();

    ASSERT_NE(nullptr, data);
    EXPECT_EQ(data + 100, wasm_runtime_addr_app_to_native(inst_, 100));
    EXPECT_EQ(data + WASM_PAGE - 1,
              wasm_runtime_addr_app_to_native(inst_, WASM_PAGE - 1));
    EXPECT_EQ(nullptr, wasm_runtime_addr_app_to_native(inst_, WASM_PAGE));

    EXPECT_EQ(100u, wasm_runtime_addr_native_to_app(inst_, data + 100));
    EXPECT_EQ(WASM_PAGE - 1u,
              wasm_runtime_addr_native_to_app(inst_, data + WASM_PAGE - 1));
    EXPECT_EQ(0u, wasm_runtime_addr_native_to_app(inst_, data + WASM_PAGE));
    EXPECT_EQ(0u, wasm_runtime_addr_native_to_app(inst_, data - 1));
    EXPECT_EQ(0u, wasm_runtime_addr_native_to_app(inst_, nullptr));
}

TEST_F(wasm_memory_test, address_ranges)
{
    uint64_t start = 1, end = 1;
    uint8_t *nstart = nullptr, *nend = nullptr;
    uint8_t *data = base();

    EXPECT_TRUE(wasm_runtime_get_app_addr_range(inst_, 10, &start, &end));
    EXPECT_EQ(0u, start);
    EXPECT_EQ(WASM_PAGE, end);
    /* both out parameters are optional */
    EXPECT_TRUE(wasm_runtime_get_app_addr_range(inst_, 10, nullptr, nullptr));
    EXPECT_FALSE(
        wasm_runtime_get_app_addr_range(inst_, WASM_PAGE, &start, &end));

    EXPECT_TRUE(
        wasm_runtime_get_native_addr_range(inst_, data + 5, &nstart, &nend));
    EXPECT_EQ(data, nstart);
    EXPECT_EQ(data + WASM_PAGE, nend);
    EXPECT_TRUE(
        wasm_runtime_get_native_addr_range(inst_, data + 5, nullptr, nullptr));
    EXPECT_FALSE(
        wasm_runtime_get_native_addr_range(inst_, data - 1, &nstart, &nend));
    EXPECT_FALSE(wasm_runtime_get_native_addr_range(inst_, data + WASM_PAGE,
                                                    &nstart, &nend));
}

TEST_F(wasm_memory_test, check_app_addr_and_convert)
{
    uint8_t *data = base();
    void *native = nullptr;

    strcpy((char *)data + 64, "hello");
    EXPECT_TRUE(wasm_check_app_addr_and_convert(mi(), false, 64, 16, &native));
    EXPECT_EQ(data + 64, native);
    EXPECT_TRUE(wasm_check_app_addr_and_convert(mi(), true, 64, 0, &native));
    EXPECT_EQ(data + 64, native);

#ifndef OS_ENABLE_HW_BOUND_CHECK
    /* the range is only checked by software when no hardware trap is used */
    EXPECT_FALSE(
        wasm_check_app_addr_and_convert(mi(), false, WASM_PAGE, 1, &native));
    EXPECT_TRUE(has_oob_exception());
    EXPECT_FALSE(wasm_check_app_addr_and_convert(mi(), false, WASM_PAGE - 1, 2,
                                                 &native));
    EXPECT_TRUE(has_oob_exception());

    memset(data + WASM_PAGE - 8, 'x', 8);
    EXPECT_FALSE(
        wasm_check_app_addr_and_convert(mi(), true, WASM_PAGE - 8, 0, &native));
    EXPECT_TRUE(has_oob_exception());
#endif
}

TEST_F(wasm_memory_test, memory_lookup_and_getters)
{
    WASMMemoryInstance *m = wasm_runtime_get_default_memory(inst_);

    ASSERT_NE(nullptr, m);
    EXPECT_EQ(m, wasm_runtime_get_memory(inst_, 0));
    EXPECT_EQ(nullptr, wasm_runtime_get_memory(inst_, 1));
    EXPECT_EQ(nullptr, wasm_get_memory_with_idx(mi(), 1));
    EXPECT_EQ(m, wasm_runtime_lookup_memory(inst_, "memory"));
    /* without multi-memory the name is not looked at */
    EXPECT_EQ(m, wasm_runtime_lookup_memory(inst_, "any-name"));

    EXPECT_EQ(1u, wasm_memory_get_cur_page_count(m));
    EXPECT_EQ(65536u, wasm_memory_get_max_page_count(m));
    EXPECT_EQ(WASM_PAGE, wasm_memory_get_bytes_per_page(m));
    EXPECT_FALSE(wasm_memory_get_shared(m));
    EXPECT_EQ(base(), wasm_memory_get_base_address(m));
}

TEST_F(wasm_memory_test, missing_memory_instance)
{
    WASMMemoryInstance **saved = mi()->memories;
    uint8_t *data = base();
    uint64_t start, end;
    uint8_t *nstart, *nend;
    void *native;

    mi()->memories = nullptr;

    EXPECT_EQ(nullptr, wasm_get_default_memory(mi()));
    EXPECT_EQ(nullptr, wasm_get_memory_with_idx(mi(), 0));
    EXPECT_FALSE(wasm_runtime_validate_app_addr(inst_, 0, 1));
    EXPECT_TRUE(has_oob_exception());
    EXPECT_FALSE(wasm_runtime_validate_app_str_addr(inst_, 0));
    EXPECT_TRUE(has_oob_exception());
    EXPECT_FALSE(wasm_runtime_validate_native_addr(inst_, data, 1));
    EXPECT_TRUE(has_oob_exception());
    EXPECT_EQ(nullptr, wasm_runtime_addr_app_to_native(inst_, 0));
    EXPECT_EQ(0u, wasm_runtime_addr_native_to_app(inst_, data));
    EXPECT_FALSE(wasm_runtime_get_app_addr_range(inst_, 0, &start, &end));
    EXPECT_FALSE(
        wasm_runtime_get_native_addr_range(inst_, data, &nstart, &nend));
    EXPECT_FALSE(wasm_check_app_addr_and_convert(mi(), false, 0, 1, &native));
    EXPECT_TRUE(has_oob_exception());

    mi()->memories = saved;
}

TEST_F(wasm_memory_test, enlarge_memory)
{
    EnlargeEvent ev;
    uint8_t *data;

    wasm_runtime_set_enlarge_mem_error_callback(record_enlarge_error, &ev);

    /* growing by zero pages always succeeds */
    EXPECT_TRUE(wasm_runtime_enlarge_memory(inst_, 0));

    /* the data survives the (possible) relocation */
    base()[5] = 0x5a;
    EXPECT_TRUE(wasm_runtime_enlarge_memory(inst_, 1));
    EXPECT_EQ(2u, wasm_memory_get_cur_page_count(mem()));
    data = base();
    EXPECT_EQ(0x5a, data[5]);
    data[2 * WASM_PAGE - 1] = 0x7f;
    EXPECT_TRUE(wasm_runtime_validate_app_addr(inst_, 2 * WASM_PAGE - 1, 1));
    EXPECT_FALSE(wasm_runtime_validate_app_addr(inst_, 2 * WASM_PAGE, 1));
    wasm_runtime_clear_exception(inst_);
    EXPECT_EQ(0, ev.count);

    /* more pages than the 32-bit page counter can express */
    EXPECT_FALSE(wasm_runtime_enlarge_memory(inst_, (uint64_t)UINT32_MAX + 1));
    EXPECT_EQ(0, ev.count);

    /* page counter overflow */
    EXPECT_FALSE(wasm_enlarge_memory(mi(), UINT32_MAX));
    EXPECT_EQ(1, ev.count);
    EXPECT_EQ(INTERNAL_ERROR, ev.reason);
    EXPECT_EQ(inst_, ev.instance);
    EXPECT_EQ(UINT32_MAX, ev.inc_page_count);
    EXPECT_EQ(2 * WASM_PAGE, ev.current_memory_size);

    /* more than the declared maximum */
    EXPECT_FALSE(wasm_runtime_enlarge_memory(inst_, 65536));
    EXPECT_EQ(2, ev.count);
    EXPECT_EQ(MAX_SIZE_REACHED, ev.reason);
    EXPECT_EQ(2u, wasm_memory_get_cur_page_count(mem()));
}

TEST_F(wasm_memory_test, enlarge_memory_by_index)
{
    EnlargeEvent ev;

    wasm_runtime_set_enlarge_mem_error_callback(record_enlarge_error, &ev);

    EXPECT_FALSE(wasm_enlarge_memory_with_idx(mi(), 1, 1));
    EXPECT_TRUE(wasm_enlarge_memory_with_idx(mi(), 1, 0));
    EXPECT_EQ(2u, wasm_memory_get_cur_page_count(mem()));
    EXPECT_EQ(0, ev.count);

    EXPECT_FALSE(wasm_memory_enlarge(nullptr, 1));
}

/* TODO: wasm_memory_enlarge() passes a NULL module to
   wasm_enlarge_memory_internal(), which calls get_shared_heap(NULL) and
   dereferences it when WASM_ENABLE_SHARED_HEAP is enabled.  Enable this test
   once that is fixed. */
TEST_F(wasm_memory_test, DISABLED_enlarge_memory_object)
{
    EXPECT_TRUE(wasm_memory_enlarge(mem(), 1));
    EXPECT_EQ(2u, wasm_memory_get_cur_page_count(mem()));
    EXPECT_FALSE(wasm_memory_enlarge(mem(), 65536));
}

/* ------------------------------------------------------------------ */
/* Shared heap                                                         */
/* ------------------------------------------------------------------ */

namespace {

/* A PROT_NONE reservation that can serve as a "pre-allocated" shared heap
   without committing any memory; fine as long as nothing dereferences it. */
class Reservation
{
  public:
    explicit Reservation(size_t size)
      : size_(size)
    {
        addr_ = mmap(nullptr, size, PROT_NONE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (addr_ == MAP_FAILED)
            addr_ = nullptr;
    }
    ~Reservation()
    {
        if (addr_)
            munmap(addr_, size_);
    }
    void *get() const { return addr_; }

  private:
    void *addr_;
    size_t size_;
};

/* A readable and writable, page aligned buffer. */
class PageBuffer
{
  public:
    explicit PageBuffer(size_t size)
      : size_(size)
    {
        addr_ = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (addr_ == MAP_FAILED)
            addr_ = nullptr;
    }
    ~PageBuffer()
    {
        if (addr_)
            munmap(addr_, size_);
    }
    uint8_t *get() const { return (uint8_t *)addr_; }

  private:
    void *addr_;
    size_t size_;
};

wasm_shared_heap_t
create_heap(uint32_t size, void *pre_allocated = nullptr)
{
    SharedHeapInitArgs args;

    memset(&args, 0, sizeof(args));
    args.size = size;
    args.pre_allocated_addr = pre_allocated;
    return wasm_runtime_create_shared_heap(&args);
}

} // namespace

class wasm_memory_shared_heap_test : public wasm_memory_test
{
  protected:
    const uint32_t page_ = (uint32_t)os_getpagesize();
};

TEST_F(wasm_memory_shared_heap_test, create_validates_arguments)
{
    PageBuffer buffer(2 * page_);
    wasm_shared_heap_t heap;

    EXPECT_EQ(nullptr, create_heap(0));
    /* a runtime managed heap above the supported maximum */
    EXPECT_EQ(nullptr, create_heap(APP_HEAP_SIZE_MAX + page_));
    /* a pre-allocated buffer must be page aligned in size */
    EXPECT_EQ(nullptr, create_heap(page_ + 1, buffer.get()));

    /* the size is rounded up to the page size for a managed heap */
    heap = create_heap(1);
    EXPECT_NE(nullptr, heap);
    heap = create_heap(page_, buffer.get());
    EXPECT_NE(nullptr, heap);
}

TEST_F(wasm_memory_shared_heap_test, create_fails_when_runtime_is_out_of_memory)
{
    std::vector<void *> blocks;
    void *p;

    while ((p = wasm_runtime_malloc(512)) != nullptr)
        blocks.push_back(p);
    while ((p = wasm_runtime_malloc(8)) != nullptr)
        blocks.push_back(p);

    EXPECT_EQ(nullptr, create_heap(page_));

    for (void *b : blocks)
        wasm_runtime_free(b);
}

TEST_F(wasm_memory_shared_heap_test, attach_and_detach)
{
    wasm_shared_heap_t a = create_heap(page_);
    wasm_shared_heap_t b = create_heap(page_);
    uint64_t saved_size = mem()->memory_data_size;

    ASSERT_NE(nullptr, a);
    ASSERT_NE(nullptr, b);
    EXPECT_EQ(nullptr, wasm_runtime_get_shared_heap(inst_));

    /* the linear memory must not overlap with the shared heap */
    mem()->memory_data_size = UINT32_MAX;
    EXPECT_FALSE(wasm_runtime_attach_shared_heap(inst_, a));
    mem()->memory_data_size = saved_size;

    EXPECT_TRUE(wasm_runtime_attach_shared_heap(inst_, a));
    EXPECT_EQ(a, wasm_runtime_get_shared_heap(inst_));
    /* only one shared heap (chain) can be attached */
    EXPECT_FALSE(wasm_runtime_attach_shared_heap(inst_, b));
    EXPECT_FALSE(wasm_runtime_attach_shared_heap(inst_, a));

    wasm_runtime_detach_shared_heap(inst_);
    EXPECT_EQ(nullptr, wasm_runtime_get_shared_heap(inst_));
    /* detaching twice is harmless */
    wasm_runtime_detach_shared_heap(inst_);

    EXPECT_TRUE(wasm_runtime_attach_shared_heap(inst_, b));
    EXPECT_EQ(b, wasm_runtime_get_shared_heap(inst_));
}

TEST_F(wasm_memory_shared_heap_test, attach_without_memory_fails)
{
    wasm_shared_heap_t heap = create_heap(page_);
    WASMMemoryInstance **saved = mi()->memories;

    ASSERT_NE(nullptr, heap);
    mi()->memories = nullptr;
    EXPECT_FALSE(wasm_runtime_attach_shared_heap(inst_, heap));
    mi()->memories = saved;
}

TEST_F(wasm_memory_shared_heap_test, chain_rules)
{
    PageBuffer buf_a(page_), buf_b(page_), buf_c(page_);
    wasm_shared_heap_t a = create_heap(page_, buf_a.get());
    wasm_shared_heap_t b = create_heap(page_, buf_b.get());
    wasm_shared_heap_t c = create_heap(page_, buf_c.get());
    wasm_shared_heap_t m1 = create_heap(page_);
    wasm_shared_heap_t m2 = create_heap(page_);

    ASSERT_TRUE(a && b && c && m1 && m2);

    EXPECT_EQ(nullptr, wasm_runtime_chain_shared_heaps(nullptr, b));
    EXPECT_EQ(nullptr, wasm_runtime_chain_shared_heaps(a, nullptr));

    /* a -> b */
    EXPECT_EQ(a, wasm_runtime_chain_shared_heaps(a, b));
    /* b is already a body, a is already a head */
    EXPECT_EQ(nullptr, wasm_runtime_chain_shared_heaps(c, b));
    EXPECT_EQ(nullptr, wasm_runtime_chain_shared_heaps(a, c));
    /* b is the body of a, so it can't be the head of another chain */
    EXPECT_EQ(nullptr, wasm_runtime_chain_shared_heaps(b, a));

    /* only one runtime managed heap per chain */
    EXPECT_EQ(m1, wasm_runtime_chain_shared_heaps(m1, c));
    EXPECT_EQ(nullptr, wasm_runtime_chain_shared_heaps(m2, m1));
    EXPECT_EQ(nullptr, wasm_runtime_chain_shared_heaps(a, m2));
}

TEST_F(wasm_memory_shared_heap_test, chain_requires_detached_heaps)
{
    PageBuffer buf_a(page_), buf_b(page_);
    wasm_shared_heap_t a = create_heap(page_, buf_a.get());
    wasm_shared_heap_t b = create_heap(page_, buf_b.get());

    ASSERT_TRUE(a && b);
    ASSERT_TRUE(wasm_runtime_attach_shared_heap(inst_, a));
    EXPECT_EQ(nullptr, wasm_runtime_chain_shared_heaps(a, b));
    EXPECT_EQ(nullptr, wasm_runtime_chain_shared_heaps(b, a));
    EXPECT_FALSE(wasm_runtime_reset_shared_heap_chain(a));
    wasm_runtime_detach_shared_heap(inst_);
    EXPECT_EQ(a, wasm_runtime_chain_shared_heaps(a, b));

    ASSERT_TRUE(wasm_runtime_attach_shared_heap(inst_, a));
    EXPECT_EQ(nullptr, wasm_runtime_unchain_shared_heaps(a, true));
    wasm_runtime_detach_shared_heap(inst_);
}

TEST_F(wasm_memory_shared_heap_test, unchain)
{
    PageBuffer buf_a(page_), buf_b(page_), buf_c(page_);
    wasm_shared_heap_t a = create_heap(page_, buf_a.get());
    wasm_shared_heap_t b = create_heap(page_, buf_b.get());
    wasm_shared_heap_t c = create_heap(page_, buf_c.get());

    ASSERT_TRUE(a && b && c);

    EXPECT_EQ(nullptr, wasm_runtime_unchain_shared_heaps(nullptr, true));
    /* not a chain */
    EXPECT_EQ(nullptr, wasm_runtime_unchain_shared_heaps(a, true));

    ASSERT_EQ(b, wasm_runtime_chain_shared_heaps(b, c));
    ASSERT_EQ(a, wasm_runtime_chain_shared_heaps(a, b));

    /* only the head leaves the chain, the rest stays chained */
    EXPECT_EQ(b, wasm_runtime_unchain_shared_heaps(a, false));
    EXPECT_EQ(nullptr, wasm_runtime_unchain_shared_heaps(a, false));

    ASSERT_EQ(a, wasm_runtime_chain_shared_heaps(a, b));
    /* every link is removed, the last heap is returned */
    EXPECT_EQ(c, wasm_runtime_unchain_shared_heaps(a, true));
    EXPECT_EQ(nullptr, wasm_runtime_unchain_shared_heaps(b, true));
}

TEST_F(wasm_memory_shared_heap_test, reset_chain)
{
    PageBuffer buf(page_);
    wasm_shared_heap_t pre = create_heap(page_, buf.get());
    wasm_shared_heap_t managed = create_heap(page_);
    uint64_t off;
    void *native = nullptr;

    ASSERT_TRUE(pre && managed);
    ASSERT_EQ(pre, wasm_runtime_chain_shared_heaps(pre, managed));
    EXPECT_FALSE(wasm_runtime_reset_shared_heap_chain(nullptr));

    ASSERT_TRUE(wasm_runtime_attach_shared_heap(inst_, pre));
    off = wasm_runtime_shared_heap_malloc(inst_, 64, &native);
    ASSERT_NE(0u, off);
    memset(native, 0xab, 64);
    memset(buf.get(), 0xcd, page_);
    wasm_runtime_detach_shared_heap(inst_);

    EXPECT_TRUE(wasm_runtime_reset_shared_heap_chain(pre));
    EXPECT_EQ(0, buf.get()[0]);
    EXPECT_EQ(0, buf.get()[page_ - 1]);

    /* the managed heap is usable again after being recreated */
    ASSERT_TRUE(wasm_runtime_attach_shared_heap(inst_, pre));
    off = wasm_runtime_shared_heap_malloc(inst_, 64, &native);
    ASSERT_NE(0u, off);
    EXPECT_EQ(0, ((uint8_t *)native)[0]);
}

TEST_F(wasm_memory_shared_heap_test, malloc_and_free)
{
    wasm_shared_heap_t managed = create_heap(page_);
    PageBuffer buf(page_);
    wasm_shared_heap_t pre = create_heap(page_, buf.get());
    void *native = nullptr;
    uint64_t off;

    ASSERT_TRUE(managed && pre);

    /* nothing attached yet */
    EXPECT_EQ(0u, wasm_runtime_shared_heap_malloc(inst_, 16, &native));
    wasm_runtime_shared_heap_free(inst_, UINT32_MAX);

    ASSERT_TRUE(wasm_runtime_attach_shared_heap(inst_, managed));
    off = wasm_runtime_shared_heap_malloc(inst_, 64, &native);
    ASSERT_NE(0u, off);
    ASSERT_NE(nullptr, native);
    EXPECT_EQ(0u, off >> 32);
    EXPECT_GE(off, UINT32_MAX - page_ + 1);
    /* the native address out parameter is optional */
    EXPECT_NE(0u, wasm_runtime_shared_heap_malloc(inst_, 16, nullptr));
    /* does not fit */
    EXPECT_EQ(0u, wasm_runtime_shared_heap_malloc(inst_, 16 * page_, nullptr));

    EXPECT_TRUE(wasm_runtime_validate_app_addr(inst_, off, 64));
    EXPECT_TRUE(wasm_runtime_validate_native_addr(inst_, native, 64));
    EXPECT_EQ(native, wasm_runtime_addr_app_to_native(inst_, off));
    EXPECT_EQ(off, wasm_runtime_addr_native_to_app(inst_, native));

    wasm_runtime_shared_heap_free(inst_, off);
    /* below the heap / above the 32-bit address space */
    wasm_runtime_shared_heap_free(inst_, 16);
    wasm_runtime_shared_heap_free(inst_, (uint64_t)UINT32_MAX + 1);
    wasm_runtime_detach_shared_heap(inst_);

    /* a pre-allocated heap can't allocate */
    ASSERT_TRUE(wasm_runtime_attach_shared_heap(inst_, pre));
    EXPECT_EQ(0u, wasm_runtime_shared_heap_malloc(inst_, 16, &native));
    wasm_runtime_shared_heap_free(inst_, UINT32_MAX - 15);
}

TEST_F(wasm_memory_shared_heap_test, malloc_and_free_without_memory)
{
    wasm_shared_heap_t heap = create_heap(page_);
    WASMMemoryInstance **saved = mi()->memories;

    ASSERT_NE(nullptr, heap);
    ASSERT_TRUE(wasm_runtime_attach_shared_heap(inst_, heap));
    mi()->memories = nullptr;
    EXPECT_EQ(0u, wasm_runtime_shared_heap_malloc(inst_, 16, nullptr));
    wasm_runtime_shared_heap_free(inst_, UINT32_MAX);
    mi()->memories = saved;
}

TEST_F(wasm_memory_shared_heap_test, address_checks_inside_heap)
{
    wasm_shared_heap_t heap = create_heap(page_);
    void *native = nullptr;
    uint64_t off;
    uint8_t *last;
    void *converted = nullptr;

    ASSERT_NE(nullptr, heap);
    ASSERT_TRUE(wasm_runtime_attach_shared_heap(inst_, heap));
    off = wasm_runtime_shared_heap_malloc(inst_, 64, &native);
    ASSERT_NE(0u, off);

    strcpy((char *)native, "shared");
    EXPECT_TRUE(wasm_runtime_validate_app_str_addr(inst_, off));
    EXPECT_TRUE(
        wasm_check_app_addr_and_convert(mi(), true, off, 0, &converted));
    EXPECT_EQ(native, converted);

    /* zero bytes are treated like one byte */
    EXPECT_TRUE(wasm_runtime_validate_app_addr(inst_, off, 0));
    /* bigger than any shared heap, so the linear memory check rejects it */
    EXPECT_FALSE(wasm_runtime_validate_app_addr(inst_, off, 2u << 30));
    EXPECT_TRUE(has_oob_exception());
    /* the end of the heap is the end of the 32-bit address space */
    EXPECT_TRUE(wasm_runtime_validate_app_addr(inst_, UINT32_MAX, 1));
    EXPECT_FALSE(wasm_runtime_validate_app_addr(inst_, UINT32_MAX, 2));
    EXPECT_TRUE(has_oob_exception());

    /* a string that runs off the end of the heap */
    last = (uint8_t *)wasm_runtime_addr_app_to_native(inst_, UINT32_MAX);
    ASSERT_NE(nullptr, last);
    *last = 'x';
    EXPECT_FALSE(wasm_runtime_validate_app_str_addr(inst_, UINT32_MAX));
    EXPECT_TRUE(has_oob_exception());
    EXPECT_FALSE(
        wasm_check_app_addr_and_convert(mi(), true, UINT32_MAX, 0, &converted));
    EXPECT_TRUE(has_oob_exception());

    /* native addresses around the heap */
    EXPECT_FALSE(wasm_runtime_validate_native_addr(inst_, last + 1, 1));
    EXPECT_TRUE(has_oob_exception());
    EXPECT_FALSE(wasm_runtime_validate_native_addr(
        inst_, (uint8_t *)native - page_ * 2, 1));
    EXPECT_TRUE(has_oob_exception());
}

TEST_F(wasm_memory_shared_heap_test, address_checks_in_heap_chain)
{
    PageBuffer buf(page_);
    wasm_shared_heap_t head = create_heap(page_, buf.get());
    wasm_shared_heap_t body = create_heap(page_);
    uint64_t head_start = UINT32_MAX - 2ull * page_ + 1;
    uint64_t body_start = UINT32_MAX - 1ull * page_ + 1;
    void *native = nullptr;
    uint64_t off;

    ASSERT_TRUE(head && body);
    ASSERT_EQ(head, wasm_runtime_chain_shared_heaps(head, body));
    ASSERT_TRUE(wasm_runtime_attach_shared_heap(inst_, head));

    /* allocation goes to the one runtime managed heap in the chain */
    off = wasm_runtime_shared_heap_malloc(inst_, 32, &native);
    ASSERT_NE(0u, off);
    EXPECT_GE(off, body_start);
    wasm_runtime_shared_heap_free(inst_, off);

    /* alternate between both heaps so the last used heap is re-resolved */
    EXPECT_TRUE(wasm_runtime_validate_app_addr(inst_, body_start, 16));
    EXPECT_TRUE(wasm_runtime_validate_app_addr(inst_, head_start, 16));
    EXPECT_TRUE(wasm_runtime_validate_app_addr(inst_, body_start + 8, 8));
    EXPECT_EQ(buf.get() + 4,
              wasm_runtime_addr_app_to_native(inst_, head_start + 4));
    EXPECT_EQ(head_start + 4,
              wasm_runtime_addr_native_to_app(inst_, buf.get() + 4));
    EXPECT_EQ(body_start + 4, wasm_runtime_addr_native_to_app(
                                  inst_, wasm_runtime_addr_app_to_native(
                                             inst_, body_start + 4)));
    EXPECT_TRUE(wasm_runtime_validate_native_addr(inst_, buf.get() + 8, 8));

    /* spanning the border between the heaps is not within one heap */
    EXPECT_FALSE(wasm_runtime_validate_app_addr(inst_, body_start - 4, 8));
    EXPECT_TRUE(has_oob_exception());
    /* just before the chain */
    EXPECT_FALSE(wasm_runtime_validate_app_addr(inst_, head_start - 1, 1));
    EXPECT_TRUE(has_oob_exception());

    /* growing the linear memory into the chain is refused */
    EXPECT_FALSE(wasm_runtime_enlarge_memory(inst_, 65535));
    wasm_runtime_clear_exception(inst_);
}

TEST_F(wasm_memory_shared_heap_test, enlarge_must_not_overlap_shared_heap)
{
    Reservation reserved(APP_HEAP_SIZE_MAX);
    EnlargeEvent ev;
    wasm_shared_heap_t heap;

    ASSERT_NE(nullptr, reserved.get());
    heap = create_heap(APP_HEAP_SIZE_MAX, reserved.get());
    ASSERT_NE(nullptr, heap);
    ASSERT_TRUE(wasm_runtime_attach_shared_heap(inst_, heap));
    wasm_runtime_set_enlarge_mem_error_callback(record_enlarge_error, &ev);

    /* the heap starts at 3 GiB; 3.1 GiB of linear memory would overlap it */
    EXPECT_FALSE(wasm_runtime_enlarge_memory(inst_, 50000));
    EXPECT_EQ(1, ev.count);
    EXPECT_EQ(1u, wasm_memory_get_cur_page_count(mem()));

    wasm_runtime_detach_shared_heap(inst_);
}
