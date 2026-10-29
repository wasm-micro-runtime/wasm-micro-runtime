/*
 * Copyright (C) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "rt_wamr.h"
#include "platform_api_vmcore.h"

#ifndef RT_USING_SEMAPHORE
#error "The RT-Thread buffer runner requires RT_USING_SEMAPHORE"
#endif

struct run_context {
    const struct rt_wamr_request *request;
    const struct rt_wamr_config *config;
    struct rt_wamr_result *result;
    struct rt_semaphore done;
};

static struct rt_spinlock run_lock = RT_SPINLOCK_INIT;
static bool running;

void
rt_wamr_config_init(struct rt_wamr_config *config)
{
    memset(config, 0, sizeof(*config));
    config->native_stack_size = 16 * 1024;
    config->operand_stack_size = 16 * 1024;
    config->priority = RT_THREAD_PRIORITY_MAX / 2;
}

static void
run_worker(void *argument)
{
    struct run_context *context = argument;
    const struct rt_wamr_request *request = context->request;
    const struct rt_wamr_config *config = context->config;
    struct rt_wamr_result *result = context->result;
    RuntimeInitArgs init_args;
    struct InstantiationArgs2 *inst_args = NULL;
    wasm_module_t module = NULL;
    wasm_module_inst_t instance = NULL;
    wasm_exec_env_t exec_env = NULL;
    wasm_function_inst_t function;
    uint8_t *buffer = NULL;
    const char *exception;
    bool runtime_initialized = false, thread_initialized = false;

    result->status = -RT_ERROR;
    memset(&init_args, 0, sizeof(init_args));
    init_args.mem_alloc_type = Alloc_With_Allocator;
    init_args.mem_alloc_option.allocator.malloc_func = os_malloc;
    init_args.mem_alloc_option.allocator.realloc_func = os_realloc;
    init_args.mem_alloc_option.allocator.free_func = os_free;
    init_args.native_module_name = config->native_module;
    init_args.native_symbols = config->native_symbols;
    init_args.n_native_symbols = config->native_symbol_count;
    if (!wasm_runtime_full_init(&init_args)) {
        snprintf(result->error, sizeof(result->error),
                 "runtime initialization failed");
        goto done;
    }
    runtime_initialized = true;
    if (!wasm_runtime_init_thread_env()) {
        snprintf(result->error, sizeof(result->error),
                 "thread initialization failed");
        goto done;
    }
    thread_initialized = true;
    buffer = os_malloc(request->size);
    if (!buffer) {
        snprintf(result->error, sizeof(result->error),
                 "module buffer allocation failed");
        result->status = -RT_ENOMEM;
        goto done;
    }
    memcpy(buffer, request->buffer, request->size);
    module = wasm_runtime_load(buffer, request->size, result->error,
                               sizeof(result->error));
    if (!module)
        goto done;
    if (!wasm_runtime_instantiation_args_create(&inst_args)) {
        snprintf(result->error, sizeof(result->error),
                 "instantiation arguments allocation failed");
        goto done;
    }
    wasm_runtime_instantiation_args_set_default_stack_size(
        inst_args, config->operand_stack_size);
    wasm_runtime_instantiation_args_set_host_managed_heap_size(
        inst_args, config->host_heap_size);
    instance = wasm_runtime_instantiate_ex2(module, inst_args, result->error,
                                            sizeof(result->error));
    wasm_runtime_instantiation_args_destroy(inst_args);
    inst_args = NULL;
    if (!instance)
        goto done;
    exec_env =
        wasm_runtime_create_exec_env(instance, config->operand_stack_size);
    if (!exec_env) {
        snprintf(result->error, sizeof(result->error),
                 "execution environment allocation failed");
        goto done;
    }
    function = wasm_runtime_lookup_function(instance, request->function);
    if (!function) {
        snprintf(result->error, sizeof(result->error), "export not found: %s",
                 request->function);
        goto done;
    }
    if (!wasm_runtime_call_wasm_a(exec_env, function, request->result_count,
                                  request->results, request->argument_count,
                                  request->arguments)) {
        exception = wasm_runtime_get_exception(instance);
        snprintf(result->error, sizeof(result->error), "%s",
                 exception ? exception : "Wasm execution failed");
        goto done;
    }
    result->status = RT_EOK;

done:
    if (exec_env)
        wasm_runtime_destroy_exec_env(exec_env);
    if (instance)
        wasm_runtime_deinstantiate(instance);
    if (inst_args)
        wasm_runtime_instantiation_args_destroy(inst_args);
    if (module)
        wasm_runtime_unload(module);
    os_free(buffer);
    if (thread_initialized)
        wasm_runtime_destroy_thread_env();
    if (runtime_initialized)
        wasm_runtime_destroy();
    /* Last access to caller storage: it may disappear after the wakeup. */
    rt_sem_release(&context->done);
}

int
rt_wamr_run_buffer(const struct rt_wamr_request *request,
                   const struct rt_wamr_config *config,
                   struct rt_wamr_result *result)
{
    struct run_context context;
    rt_thread_t worker;
    rt_base_t level;
    int status;

    if (!result)
        return -RT_EINVAL;
    memset(result, 0, sizeof(*result));
    result->status = -RT_EINVAL;
    if (!request || !config || !request->buffer || request->size < 8
        || !request->function || !rt_thread_self() || rt_interrupt_get_nest()
        || config->priority >= RT_THREAD_PRIORITY_MAX
        || config->native_stack_size < 4096 || !config->operand_stack_size
        || (request->argument_count && !request->arguments)
        || (request->result_count && !request->results)
        || (config->native_symbol_count
            && (!config->native_module || !config->native_symbols))) {
        snprintf(result->error, sizeof(result->error),
                 "invalid buffer runner arguments");
        return result->status;
    }
    level = rt_spin_lock_irqsave(&run_lock);
    if (running) {
        rt_spin_unlock_irqrestore(&run_lock, level);
        result->status = -RT_EBUSY;
        snprintf(result->error, sizeof(result->error),
                 "runtime is already in use");
        return result->status;
    }
    running = true;
    rt_spin_unlock_irqrestore(&run_lock, level);

    context.request = request;
    context.config = config;
    context.result = result;
    status = rt_sem_init(&context.done, "wamrdone", 0, RT_IPC_FLAG_FIFO);
    if (status != RT_EOK)
        goto release;
    worker = rt_thread_create("wamr", run_worker, &context,
                              config->native_stack_size,
                              (rt_uint8_t)config->priority, 10);
    if (!worker) {
        status = -RT_ENOMEM;
        goto detach;
    }
    status = rt_thread_startup(worker);
    if (status != RT_EOK) {
        rt_thread_delete(worker);
        goto detach;
    }
    /* Do not return until the worker has released every runtime resource. */
    while (rt_sem_take(&context.done, RT_WAITING_FOREVER) != RT_EOK)
        ;
    status = result->status;
detach:
    rt_sem_detach(&context.done);
release:
    level = rt_spin_lock_irqsave(&run_lock);
    running = false;
    rt_spin_unlock_irqrestore(&run_lock, level);
    if (status != RT_EOK && !result->error[0])
        snprintf(result->error, sizeof(result->error),
                 "worker setup failed: %d", status);
    result->status = status;
    return status;
}
