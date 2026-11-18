# Copyright (C) 2026, RT-Thread Development Team
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""RT-Thread feature and target mapping; test profiles belong to the tests."""

import os
import re
import shlex


FEATURES = (
    "INTERP", "FAST_INTERP", "AOT", "JIT", "FAST_JIT", "LIBC_BUILTIN",
    "LIBC_WASI", "LIBC_EMCC", "LIB_PTHREAD", "LIB_PTHREAD_SEMAPHORE",
    "LIB_WASI_THREADS", "THREAD_MGR", "SHARED_MEMORY", "MULTI_MODULE",
    "SPEC_TEST", "BULK_MEMORY", "MINI_LOADER", "SIMD", "MEMORY_PROFILING",
    "MEMORY_TRACING", "CUSTOM_NAME_SECTION", "TAIL_CALL", "REF_TYPES",
    "INSTRUCTION_METERING", "GC", "MEMORY64", "MULTI_MEMORY", "DEBUG_INTERP",
    "MODULE_INST_CONTEXT", "BULK_MEMORY_OPT", "CALL_INDIRECT_OVERLONG",
)


def feature_config(enabled):
    """Mirror dependencies used by runtime_lib.cmake, without a P0 allowlist."""
    result = {name: int(bool(enabled("WAMR_BUILD_" + name)))
              for name in FEATURES}
    if result["JIT"] or result["FAST_JIT"]:
        result["INTERP"] = 1
        result["FAST_INTERP"] = 0
    if result["JIT"]:
        result["AOT"] = 1
    if result["LIB_PTHREAD_SEMAPHORE"]:
        result["LIB_PTHREAD"] = 1
    if result["LIB_PTHREAD"] or result["LIB_WASI_THREADS"]:
        result["THREAD_MGR"] = 1
        result["SHARED_MEMORY"] = 1
        result["BULK_MEMORY"] = 1
    if result["DEBUG_INTERP"]:
        result["THREAD_MGR"] = 1
        result["FAST_INTERP"] = 0
    if result["LIBC_WASI"]:
        result["MODULE_INST_CONTEXT"] = 1
    if result["GC"]:
        result["REF_TYPES"] = 1
    if result["BULK_MEMORY"]:
        result["BULK_MEMORY_OPT"] = 1
    if result["REF_TYPES"]:
        result["CALL_INDIRECT_OVERLONG"] = 1
    result["AOT_INTRINSICS"] = result["AOT"]
    result["QUICK_AOT_ENTRY"] = result["AOT"]
    return result


def target_config(rtconfig):
    if getattr(rtconfig, "PLATFORM", "gcc") != "gcc":
        raise ValueError("WAMR RT-Thread native assembly requires the GCC SCons toolchain")
    arch = rtconfig.ARCH
    cpu = getattr(rtconfig, "CPU", "").lower()
    flags = shlex.split(getattr(rtconfig, "CFLAGS", ""))
    if arch == "arm":
        thumb = cpu.startswith("cortex-m")
        hard_float = False
        for flag in flags:
            if flag in ("-mthumb", "-marm"):
                thumb = flag == "-mthumb"
            if flag in ("-msoft-float", "-mhard-float"):
                hard_float = flag == "-mhard-float"
            if flag.startswith("-mfloat-abi="):
                hard_float = flag == "-mfloat-abi=hard"
        name = "thumb" if thumb else "arm"
        suffix = "_vfp" if hard_float else ""
        return ("BUILD_TARGET_" + name.upper() + suffix.upper(),
                "invokeNative_" + name + suffix + ".s")
    targets = {
        "ia32": ("BUILD_TARGET_X86_32", "invokeNative_ia32.s"),
        "x86_64": ("BUILD_TARGET_X86_64", "invokeNative_em64.s"),
        "aarch64": ("BUILD_TARGET_AARCH64", "invokeNative_aarch64.s"),
    }
    if arch not in targets:
        raise ValueError("WAMR RT-Thread: no native-call mapping for " + arch)
    return targets[arch]


def aot_target_config(rtconfig):
    """Validate the relocation selections in core/iwasm/aot/SConscript.

    This checks source/ABI consistency, not executable-memory or board support.
    """
    macro, unused_source = target_config(rtconfig)
    cpu = getattr(rtconfig, "CPU", "").lower()
    if rtconfig.ARCH == "ia32":
        return "X86_32", "aot_reloc_x86_32.c"
    if rtconfig.ARCH == "arm" and cpu.startswith("cortex-m"):
        prefix, relocation = "THUMB", "aot_reloc_thumb.c"
    elif rtconfig.ARCH == "arm" and cpu.startswith("cortex-a"):
        prefix, relocation = "ARM", "aot_reloc_arm.c"
    else:
        raise ValueError(
            "WAMR AOT: core/iwasm/aot/SConscript has no relocation selection "
            "for ARCH=%s CPU=%s" % (rtconfig.ARCH, cpu))
    target = getattr(rtconfig, "WAMR_BUILD_TARGET", "").upper()
    if not target:
        raise ValueError(
            "WAMR AOT requires an explicit rtconfig.WAMR_BUILD_TARGET "
            "(for example ARMV7 or THUMBV7M, with _VFP for hard-float)")
    if not re.fullmatch(prefix + r"[A-Z0-9.]*(_VFP)?", target):
        raise ValueError("WAMR AOT target does not match its relocation backend: " + target)
    hard_float = macro.endswith("_VFP")
    if target.endswith("_VFP") != hard_float:
        raise ValueError("WAMR AOT target _VFP suffix must match the C calling ABI")
    expected_macro = "BUILD_TARGET_" + prefix + ("_VFP" if hard_float else "")
    if macro != expected_macro:
        raise ValueError("WAMR AOT relocation and native-call instruction states differ")
    return target[:-4] if hard_float else target, relocation


def validate_config(enabled, features=None):
    features = features if features is not None else feature_config(enabled)
    if not enabled("RT_USING_HEAP") or not enabled("RT_USING_MUTEX"):
        raise ValueError("WAMR requires RT_USING_HEAP and RT_USING_MUTEX")
    if not (features["INTERP"] or features["AOT"]):
        raise ValueError("WAMR requires at least one of INTERP or AOT")
    if features["FAST_INTERP"] and not features["INTERP"]:
        raise ValueError("WAMR_BUILD_FAST_INTERP requires WAMR_BUILD_INTERP")
    if features["SPEC_TEST"] and not features["LIBC_BUILTIN"]:
        raise ValueError("WAMR_BUILD_SPEC_TEST requires WAMR_BUILD_LIBC_BUILTIN "
                         "for the spectest native exports")
    if (enabled("WAMR_RTT_BUILD_RUNNER") or features["THREAD_MGR"]) \
            and not enabled("RT_USING_SEMAPHORE"):
        raise ValueError("The RT worker/thread backend requires RT_USING_SEMAPHORE")
    if features["LIBC_WASI"] or enabled("WAMR_RTT_BUILD_IWASM"):
        if not enabled("RT_USING_POSIX_FS"):
            raise ValueError("WASI/file iwasm requires RT_USING_POSIX_FS")
    if enabled("WAMR_RTT_BUILD_IWASM") and not enabled("RT_USING_FINSH"):
        raise ValueError("The iwasm command requires RT_USING_FINSH")


def compile_definitions(rtconfig, enabled, config_value=None):
    features = feature_config(enabled)
    validate_config(enabled, features)
    target, unused_source = target_config(rtconfig)
    definitions = [
        target, "BH_PLATFORM_RTT", "BH_HAS_DLFCN=0",
        "BH_DEBUG=" + str(int(getattr(rtconfig, "BUILD", "") == "debug")),
        "BH_MALLOC=wasm_runtime_malloc", "BH_FREE=wasm_runtime_free",
    ]
    definitions += ["WASM_ENABLE_%s=%d" % item for item in features.items()]
    # Unlike a disabled Kconfig bool, an integer 0 survives in rtconfig.h.
    # Preserve core/config.h's default when older package menus omit it.
    shrunk_memory = (config_value("WAMR_BUILD_SHRUNK_MEMORY")
                     if config_value is not None else "")
    if shrunk_memory in ("", None):
        shrunk_memory = 1
    if shrunk_memory not in (0, 1, "0", "1"):
        raise ValueError("WAMR_BUILD_SHRUNK_MEMORY must be an integer 0 or 1")
    definitions += ["WASM_ENABLE_SHRUNK_MEMORY=" + str(int(shrunk_memory))]
    disable_hw = bool(enabled("WAMR_DISABLE_HW_BOUND_CHECK"))
    disable_stack = disable_hw or bool(enabled("WAMR_DISABLE_STACK_HW_BOUND_CHECK"))
    definitions += [
        "WASM_DISABLE_HW_BOUND_CHECK=" + str(int(disable_hw)),
        "WASM_DISABLE_STACK_HW_BOUND_CHECK=" + str(int(disable_stack)),
    ]
    if features["AOT"]:
        aot_target, unused_relocation = aot_target_config(rtconfig)
        definitions += [("BUILD_TARGET", '\\"' + aot_target + '\\"')]
    return definitions


def runtime_scripts(features, root):
    scripts = []
    entries = (
        ("INTERP", "core/iwasm/interpreter/SConscript"),
        ("AOT", "core/iwasm/aot/SConscript"),
        ("JIT", "core/iwasm/compilation/SConscript"),
        ("FAST_JIT", "core/iwasm/fast-jit/SConscript"),
        ("GC", "core/iwasm/common/gc/SConscript"),
        ("DEBUG_INTERP", "core/iwasm/libraries/debug-engine/SConscript"),
        ("LIBC_BUILTIN", "core/iwasm/libraries/libc-builtin/SConscript"),
        ("LIBC_WASI", "core/iwasm/libraries/libc-wasi/SConscript"),
        ("LIB_PTHREAD", "core/iwasm/libraries/lib-pthread/SConscript"),
        ("THREAD_MGR", "core/iwasm/libraries/thread-mgr/SConscript"),
        ("LIBC_EMCC", "core/iwasm/libraries/libc-emcc/SConscript"),
        ("LIB_WASI_THREADS", "core/iwasm/libraries/lib-wasi-threads/SConscript"),
    )
    for feature, path in entries:
        if features[feature]:
            if not os.path.isfile(os.path.join(root, path)):
                raise ValueError("WAMR_BUILD_%s selected, but SCons entry is missing: %s"
                                 % (feature, path))
            scripts.append(path)
    if features["LIBC_WASI"]:
        scripts += ["core/shared/platform/common/posix/SConscript",
                    "core/shared/platform/common/libc-util/SConscript"]
    scripts += [
        "core/shared/platform/rt-thread/SConscript",
        "core/shared/mem-alloc/SConscript",
        "core/iwasm/common/SConscript",
        "core/shared/utils/SConscript",
    ]
    return scripts


def interpreter_sources(enabled):
    features = feature_config(enabled)
    if not features["INTERP"]:
        return []
    return ["wasm_runtime.c",
            "wasm_interp_fast.c" if features["FAST_INTERP"] else "wasm_interp_classic.c",
            "wasm_mini_loader.c" if features["MINI_LOADER"] else "wasm_loader.c"]


def platform_sources(enabled):
    sources = ["rtt_platform.c", "rtt_thread.c"]
    if enabled("WAMR_BUILD_LIBC_WASI"):
        sources += ["rtt_file.c", "rtt_socket.c"]
    return sources


def product_sources(enabled):
    sources = []
    if enabled("WAMR_RTT_BUILD_RUNNER"):
        sources.append("rt_wamr.c")
    if enabled("WAMR_RTT_BUILD_IWASM"):
        sources.append("iwasm.c")
    return sources
