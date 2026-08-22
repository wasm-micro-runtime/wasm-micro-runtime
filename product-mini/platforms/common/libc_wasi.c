/*
 * Copyright (C) 2023 Amazon.com Inc. or its affiliates. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "libc_wasi.h"

static void
libc_wasi_print_help(void)
{
    printf("  --env=<env>              Pass wasi environment variables with "
           "\"key=value\"\n");
    printf("                           to the program, for example:\n");
    printf("                             --env=\"key1=value1\" "
           "--env=\"key2=value2\"\n");
    printf("  --dir=<dir>              Grant wasi access to the given host "
           "directories\n");
    printf("                           to the program, for example:\n");
    printf("                             --dir=<dir1> --dir=<dir2>\n");
    printf("  --map-dir=<guest::host>  Grant wasi access to the given host "
           "directories\n");
    printf("                           to the program at a specific guest "
           "path, for example:\n");
    printf("                             --map-dir=<guest-path1::host-path1> "
           "--map-dir=<guest-path2::host-path2>\n");
    printf("  --addr-pool=<addr/mask>  Grant wasi access to the given network "
           "addresses in\n");
    printf("                           CIDR notation to the program, separated "
           "with ',',\n");
    printf("                           for example:\n");
    printf("                             --addr-pool=1.2.3.4/15,2.3.4.5/16\n");
    printf("  --allow-resolve=<domain> Allow the lookup of the specific domain "
           "name or domain\n");
    printf("                           name suffixes using a wildcard, for "
           "example:\n");
    printf("                           --allow-resolve=example.com # allow the "
           "lookup of the specific domain\n");
    printf("                           --allow-resolve=*.example.com # allow "
           "the lookup of all subdomains\n");
    printf("                           --allow-resolve=* # allow any lookup\n");
    printf("  -S  [sandbox options]      Set Sandbox environment flags \n");
    printf("                              cli[=y|n] -- Enable support for WASI "
           "CLI APIs, including filesystems, sockets, clocks, and random.\n");
    printf("                           common[=y|n] -- Deprecated alias for "
           "`cli`\n");
    printf("                  inherit-network[=y|n] -- Flag for WASI preview2 "
           "to inherit the host's network within the guest so it has full "
           "access to all addresses/ports/etc.\n");
    printf("             allow-ip-name-lookup[=y|n] -- Indicates whether "
           "`wasi:sockets/ip-name-lookup` is enabled or not.\n");
    printf("                              tcp[=y|n] -- Indicates whether "
           "`wasi:sockets` TCP support is enabled or not.\n");
    printf("                              udp[=y|n] -- Indicates whether "
           "`wasi:sockets` UDP support is enabled or not.\n");
    printf("                      inherit-env[=y|n] -- Inherit all environment "
           "variables from the parent process.\n");
}

static bool
validate_env_str(char *env)
{
    char *p = env;
    int key_len = 0;

    while (*p != '\0' && *p != '=') {
        key_len++;
        p++;
    }

    if (*p != '=' || key_len == 0)
        return false;

    return true;
}

libc_wasi_parse_result_t
libc_wasi_parse(char *arg, libc_wasi_parse_context_t *ctx)
{
    if (!strncmp(arg, "--dir=", 6)) {
        if (arg[6] == '\0')
            return LIBC_WASI_PARSE_RESULT_NEED_HELP;
        if (ctx->dir_list_size >= sizeof(ctx->dir_list) / sizeof(char *)) {
            printf("Only allow max dir number %d\n",
                   (int)(sizeof(ctx->dir_list) / sizeof(char *)));
            return LIBC_WASI_PARSE_RESULT_BAD_PARAM;
        }
        ctx->dir_list[ctx->dir_list_size++] = arg + 6;
    }
    else if (!strncmp(arg, "--map-dir=", 10)) {
        if (arg[10] == '\0')
            return LIBC_WASI_PARSE_RESULT_NEED_HELP;
        if (ctx->map_dir_list_size
            >= sizeof(ctx->map_dir_list) / sizeof(char *)) {
            printf("Only allow max map dir number %d\n",
                   (int)(sizeof(ctx->map_dir_list) / sizeof(char *)));
            return LIBC_WASI_PARSE_RESULT_NEED_HELP;
        }
        ctx->map_dir_list[ctx->map_dir_list_size++] = arg + 10;
    }
    else if (!strncmp(arg, "--env=", 6)) {
        char *tmp_env;

        if (arg[6] == '\0')
            return LIBC_WASI_PARSE_RESULT_NEED_HELP;
        if (ctx->env_list_size >= sizeof(ctx->env_list) / sizeof(char *)) {
            printf("Only allow max env number %d\n",
                   (int)(sizeof(ctx->env_list) / sizeof(char *)));
            return LIBC_WASI_PARSE_RESULT_BAD_PARAM;
        }
        tmp_env = arg + 6;
        if (validate_env_str(tmp_env))
            ctx->env_list[ctx->env_list_size++] = tmp_env;
        else {
            printf("Wasm parse env string failed: expect \"key=value\", "
                   "got \"%s\"\n",
                   tmp_env);
            return LIBC_WASI_PARSE_RESULT_NEED_HELP;
        }
    }
    /* TODO: parse the configuration file via --addr-pool-file */
    else if (!strncmp(arg, "--addr-pool=", strlen("--addr-pool="))) {
        /* like: --addr-pool=100.200.244.255/30 */
        char *token = NULL;

        if ('\0' == arg[12])
            return LIBC_WASI_PARSE_RESULT_NEED_HELP;

        token = strtok(arg + strlen("--addr-pool="), ",");
        while (token) {
            if (ctx->addr_pool_size
                >= sizeof(ctx->addr_pool) / sizeof(char *)) {
                printf("Only allow max address number %d\n",
                       (int)(sizeof(ctx->addr_pool) / sizeof(char *)));
                return LIBC_WASI_PARSE_RESULT_BAD_PARAM;
            }

            ctx->addr_pool[ctx->addr_pool_size++] = token;
            token = strtok(NULL, ",");
        }
    }
    else if (!strncmp(arg, "--allow-resolve=", 16)) {
        if (arg[16] == '\0')
            return LIBC_WASI_PARSE_RESULT_NEED_HELP;
        if (ctx->ns_lookup_pool_size
            >= sizeof(ctx->ns_lookup_pool) / sizeof(ctx->ns_lookup_pool[0])) {
            printf("Only allow max ns lookup number %d\n",
                   (int)(sizeof(ctx->ns_lookup_pool)
                         / sizeof(ctx->ns_lookup_pool[0])));
            return LIBC_WASI_PARSE_RESULT_BAD_PARAM;
        }
        ctx->ns_lookup_pool[ctx->ns_lookup_pool_size++] = arg + 16;
    }
    else {
        return LIBC_WASI_PARSE_RESULT_NEED_HELP;
    }
    return LIBC_WASI_PARSE_RESULT_OK;
}

static void
libc_wasi_set_init_args(struct InstantiationArgs2 *args, int argc, char **argv,
                        libc_wasi_parse_context_t *ctx)
{
    wasm_runtime_instantiation_args_set_wasi_arg(args, argv, argc);
    wasm_runtime_instantiation_args_set_wasi_env(args, ctx->env_list,
                                                 ctx->env_list_size);
    wasm_runtime_instantiation_args_set_wasi_dir(
        args, ctx->dir_list, ctx->dir_list_size, ctx->map_dir_list,
        ctx->map_dir_list_size);
    wasm_runtime_instantiation_args_set_wasi_addr_pool(args, ctx->addr_pool,
                                                       ctx->addr_pool_size);
    wasm_runtime_instantiation_args_set_wasi_ns_lookup_pool(
        args, ctx->ns_lookup_pool, ctx->ns_lookup_pool_size);
}

#if WASM_ENABLE_COMPONENT_MODEL != 0
void
libc_component_wasi_init(WASMComponent *wasm_component, int argc, char **argv,
                         libc_wasi_parse_context_t *ctx)
{
    wasm_component_runtime_set_wasi_args(
        wasm_component, ctx->dir_list, ctx->dir_list_size, ctx->map_dir_list,
        ctx->map_dir_list_size, ctx->env_list, ctx->env_list_size, argv, argc);

    wasm_component_runtime_set_wasi_options(wasm_component, &ctx->wasi_options);

    wasm_component_runtime_set_wasi_addr_pool(wasm_component, ctx->addr_pool,
                                              ctx->addr_pool_size);
    wasm_component_runtime_set_wasi_ns_lookup_pool(
        wasm_component, ctx->ns_lookup_pool, ctx->ns_lookup_pool_size);
}

void
libc_wasi_set_default_options(libc_wasi_parse_context_t *ctx)
{
    ctx->wasi_options.allow_ip_name_lookup = 1;
    ctx->wasi_options.cli = 1;
    ctx->wasi_options.common = 1;
    ctx->wasi_options.inherit_env = 0;
    ctx->wasi_options.inherit_network = 1;
    ctx->wasi_options.preview2 = 1;
    ctx->wasi_options.tcp = 1;
    ctx->wasi_options.udp = 1;
}

void
libc_wasi_set_field(const char *option, libc_wasi_parse_context_t *ctx,
                    uint32 val)
{
    if (!strcmp(option, "cli=")) {
        ctx->wasi_options.cli = val;
    }
    else if (!strcmp(option, "allow-ip-name-lookup=")) {
        ctx->wasi_options.allow_ip_name_lookup = val;
    }
    else if (!strcmp(option, "common=")) {
        ctx->wasi_options.common = val;
    }
    else if (!strcmp(option, "inherit-env=")) {
        ctx->wasi_options.inherit_env = val;
    }
    else if (!strcmp(option, "inherit-network=")) {
        ctx->wasi_options.inherit_network = val;
    }
    else if (!strcmp(option, "tcp=")) {
        ctx->wasi_options.tcp = val;
    }
    else if (!strcmp(option, "udp=")) {
        ctx->wasi_options.udp = val;
    }
}

bool
libc_wasi_check_option(const char *arg, libc_wasi_parse_context_t *ctx,
                       const char *option, int len,
                       libc_wasi_parse_result_t *res)
{

    if (strncmp(arg, option, len)) {
        return false;
    }
    if ((arg[len] == '\0') || (arg[len] != '\0' && arg[len + 1] != '\0'))
        *res = LIBC_WASI_PARSE_RESULT_NEED_HELP;
    if (arg[len] == 'y' || arg[len] == 'Y') {
        libc_wasi_set_field(option, ctx, 1);
        *res = LIBC_WASI_PARSE_RESULT_OK;
    }
    else if (arg[len] == 'n' || arg[len] == 'N') {
        libc_wasi_set_field(option, ctx, 0);
        *res = LIBC_WASI_PARSE_RESULT_OK;
    }
    else {
        printf("Expected Yes (y/Y) or No (n/N) answer, \'%c\' not allowed\n",
               arg[len]);
        *res = LIBC_WASI_PARSE_RESULT_BAD_PARAM;
    }
    return true;
}

libc_wasi_parse_result_t
libc_wasi_parse_options(const char *arg, libc_wasi_parse_context_t *ctx)
{
    libc_wasi_parse_result_t res = LIBC_WASI_PARSE_RESULT_NEED_HELP;
    if (libc_wasi_check_option(arg, ctx, "cli=", 4, &res)
        || libc_wasi_check_option(arg, ctx, "cli-exit-with-code=", 19, &res)
        || libc_wasi_check_option(arg, ctx, "common=", 7, &res)
        || libc_wasi_check_option(arg, ctx, "inherit-network=", 16, &res)
        || libc_wasi_check_option(arg, ctx, "allow-ip-name-lookup=", 21, &res)
        || libc_wasi_check_option(arg, ctx, "tcp=", 4, &res)
        || libc_wasi_check_option(arg, ctx, "udp=", 4, &res)
        || libc_wasi_check_option(arg, ctx, "inherit-env=", 12, &res)) {
        return res;
    }
    else {
        printf("Option %s not supported\n", arg);
        libc_wasi_print_help();
        return LIBC_WASI_PARSE_RESULT_NEED_HELP;
    }
    return LIBC_WASI_PARSE_RESULT_OK;
}
#endif
