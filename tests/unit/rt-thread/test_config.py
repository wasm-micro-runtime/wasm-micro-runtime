# Copyright (C) 2026, RT-Thread Development Team
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest

root = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location(
    "rtthread_config", root / "build-scripts/rtthread_config.py")
config = importlib.util.module_from_spec(spec)
spec.loader.exec_module(config)


def options(*names):
    return {"RT_USING_HEAP", "RT_USING_MUTEX", *names}.__contains__


def target(arch="arm", cpu="cortex-a", flags="", **kwargs):
    return SimpleNamespace(ARCH=arch, CPU=cpu, CFLAGS=flags, PLATFORM="gcc",
                           **kwargs)


class ConfigurationTests(unittest.TestCase):
    def test_native_trampolines_exist(self):
        for arch, cpu, flags, expected in [
            ("arm", "cortex-m3", "", "BUILD_TARGET_THUMB"),
            ("arm", "cortex-m4", "-mfloat-abi=hard", "BUILD_TARGET_THUMB_VFP"),
            ("arm", "cortex-a", "-mfloat-abi=softfp", "BUILD_TARGET_ARM"),
            ("arm", "cortex-r5", "-mfloat-abi=hard", "BUILD_TARGET_ARM_VFP"),
            ("arm", "cortex-a", "-mthumb", "BUILD_TARGET_THUMB"),
            ("ia32", "", "", "BUILD_TARGET_X86_32"),
            ("aarch64", "", "", "BUILD_TARGET_AARCH64"),
            ("x86_64", "", "", "BUILD_TARGET_X86_64"),
        ]:
            with self.subTest(arch=arch, cpu=cpu):
                macro, source = config.target_config(target(arch, cpu, flags))
                self.assertEqual(macro, expected)
                self.assertTrue((root / "core/iwasm/common/arch" / source).is_file())

    def test_last_float_abi_flag_wins(self):
        macro, unused = config.target_config(
            target(flags="-mfloat-abi=hard -mfloat-abi=softfp"))
        self.assertEqual(macro, "BUILD_TARGET_ARM")

    def test_unknown_target_and_toolchain_fail(self):
        with self.assertRaisesRegex(ValueError, "no native-call mapping"):
            config.target_config(target("risc-v", "unknown"))
        with self.assertRaisesRegex(ValueError, "GCC SCons toolchain"):
            config.target_config(SimpleNamespace(PLATFORM="iar"))

    def test_vmcore_does_not_require_runner_dependencies(self):
        enabled = options("WAMR_BUILD_INTERP")
        config.validate_config(enabled)
        self.assertEqual(config.product_sources(enabled), [])
        config.validate_config(options("WAMR_BUILD_AOT"))
        with self.assertRaisesRegex(ValueError, "at least one"):
            config.validate_config(options())
        with self.assertRaisesRegex(ValueError, "FAST_INTERP requires"):
            config.validate_config(options("WAMR_BUILD_AOT", "WAMR_BUILD_FAST_INTERP"))

    def test_host_entries_are_optional(self):
        with self.assertRaisesRegex(ValueError, "SEMAPHORE"):
            config.validate_config(options("WAMR_BUILD_INTERP", "WAMR_RTT_BUILD_RUNNER"))
        runner = options("WAMR_BUILD_INTERP", "WAMR_RTT_BUILD_RUNNER",
                         "RT_USING_SEMAPHORE")
        config.validate_config(runner)
        self.assertEqual(config.product_sources(runner), ["rt_wamr.c"])
        with self.assertRaisesRegex(ValueError, "POSIX_FS"):
            config.validate_config(options("WAMR_BUILD_INTERP", "WAMR_RTT_BUILD_IWASM"))
        command = options("WAMR_BUILD_INTERP", "WAMR_RTT_BUILD_IWASM",
                          "RT_USING_POSIX_FS", "RT_USING_FINSH")
        config.validate_config(command)
        self.assertEqual(config.product_sources(command), ["iwasm.c"])

    def test_feature_mapping_and_derived_dependencies(self):
        enabled = options("WAMR_BUILD_INTERP", "WAMR_BUILD_LIB_PTHREAD",
                          "WAMR_BUILD_SPEC_TEST", "WAMR_BUILD_LIBC_BUILTIN",
                          "RT_USING_SEMAPHORE")
        config.validate_config(enabled)
        features = config.feature_config(enabled)
        for name in ("THREAD_MGR", "SHARED_MEMORY", "BULK_MEMORY", "SPEC_TEST"):
            self.assertEqual(features[name], 1)
        definitions = config.compile_definitions(target(), enabled)
        self.assertIn("WASM_ENABLE_SPEC_TEST=1", definitions)
        self.assertIn("WASM_ENABLE_LIB_PTHREAD=1", definitions)
        self.assertIn("WASM_ENABLE_AOT=0", definitions)
        wasi = options("WAMR_BUILD_INTERP", "WAMR_BUILD_LIBC_WASI",
                       "RT_USING_POSIX_FS")
        config.validate_config(wasi)
        self.assertEqual(config.feature_config(wasi)["MODULE_INST_CONTEXT"], 1)

    def test_bulk_and_reference_dependencies_match_cmake(self):
        base = options("WAMR_BUILD_INTERP")
        features = config.feature_config(base)
        self.assertEqual(features["BULK_MEMORY_OPT"], 0)
        self.assertEqual(features["CALL_INDIRECT_OVERLONG"], 0)
        for option, derived in (("BULK_MEMORY", "BULK_MEMORY_OPT"),
                                ("REF_TYPES", "CALL_INDIRECT_OVERLONG")):
            with self.subTest(option=option):
                enabled = options("WAMR_BUILD_INTERP", "WAMR_BUILD_" + option)
                definitions = config.compile_definitions(target(), enabled)
                self.assertIn("WASM_ENABLE_" + derived + "=1", definitions)
        features = config.feature_config(options("WAMR_BUILD_GC"))
        self.assertEqual(features["REF_TYPES"], 1)
        self.assertEqual(features["CALL_INDIRECT_OVERLONG"], 1)

    def test_spec_profile_has_spectest_native_source(self):
        incomplete = options("WAMR_BUILD_INTERP", "WAMR_BUILD_SPEC_TEST")
        with self.assertRaisesRegex(ValueError, "SPEC_TEST requires.*LIBC_BUILTIN"):
            config.validate_config(incomplete)
        self.assertEqual(config.feature_config(incomplete)["LIBC_BUILTIN"], 0)
        enabled = options("WAMR_BUILD_INTERP", "WAMR_BUILD_SPEC_TEST",
                          "WAMR_BUILD_LIBC_BUILTIN")
        config.validate_config(enabled)
        scripts = config.runtime_scripts(config.feature_config(enabled), root)
        self.assertIn("core/iwasm/libraries/libc-builtin/SConscript", scripts)

    def test_shrunk_memory_preserves_default_and_accepts_explicit_zero(self):
        enabled = options("WAMR_BUILD_INTERP")
        self.assertIn("WASM_ENABLE_SHRUNK_MEMORY=1",
                      config.compile_definitions(target(), enabled))
        for supplied, expected in (("", 1), (None, 1), (0, 0), ("0", 0),
                                   (1, 1), ("1", 1)):
            with self.subTest(supplied=supplied):
                lookup = {"WAMR_BUILD_SHRUNK_MEMORY": supplied}.get
                definitions = config.compile_definitions(target(), enabled, lookup)
                self.assertIn("WASM_ENABLE_SHRUNK_MEMORY=" + str(expected),
                              definitions)
        with self.assertRaisesRegex(ValueError, "integer 0 or 1"):
            config.compile_definitions(
                target(), enabled, {"WAMR_BUILD_SHRUNK_MEMORY": 2}.get)

    def test_hardware_check_options_are_independent(self):
        definitions = config.compile_definitions(target(), options("WAMR_BUILD_INTERP"))
        self.assertIn("WASM_DISABLE_HW_BOUND_CHECK=0", definitions)
        self.assertIn("WASM_DISABLE_STACK_HW_BOUND_CHECK=0", definitions)
        definitions = config.compile_definitions(
            target(), options("WAMR_BUILD_INTERP", "WAMR_DISABLE_STACK_HW_BOUND_CHECK"))
        self.assertIn("WASM_DISABLE_HW_BOUND_CHECK=0", definitions)
        self.assertIn("WASM_DISABLE_STACK_HW_BOUND_CHECK=1", definitions)
        definitions = config.compile_definitions(
            target(), options("WAMR_BUILD_INTERP", "WAMR_DISABLE_HW_BOUND_CHECK"))
        self.assertIn("WASM_DISABLE_HW_BOUND_CHECK=1", definitions)
        self.assertIn("WASM_DISABLE_STACK_HW_BOUND_CHECK=1", definitions)
        cmake = (root / "core/shared/platform/rt-thread/shared_platform.cmake").read_text()
        self.assertNotIn("-DWASM_DISABLE_HW_BOUND_CHECK", cmake)

    def test_conditional_sources_and_emcc_path(self):
        base = config.platform_sources(options("WAMR_BUILD_INTERP"))
        self.assertEqual(base, ["rtt_platform.c", "rtt_thread.c"])
        wasi = options("WAMR_BUILD_INTERP", "WAMR_BUILD_LIBC_WASI",
                       "WAMR_BUILD_LIBC_EMCC", "RT_USING_POSIX_FS")
        sources = config.platform_sources(wasi)
        self.assertIn("rtt_file.c", sources)
        self.assertIn("rtt_socket.c", sources)
        scripts = config.runtime_scripts(config.feature_config(wasi), root)
        self.assertIn("core/iwasm/libraries/libc-wasi/SConscript", scripts)
        self.assertIn("core/iwasm/libraries/libc-emcc/SConscript", scripts)
        self.assertIn("core/shared/platform/common/posix/SConscript", scripts)
        self.assertTrue(all((root / path).is_file() for path in scripts))

    def test_interpreter_sources_use_derived_mode(self):
        fast = options("WAMR_BUILD_INTERP", "WAMR_BUILD_FAST_INTERP")
        self.assertIn("wasm_interp_fast.c", config.interpreter_sources(fast))
        for feature in ("JIT", "FAST_JIT", "DEBUG_INTERP"):
            selected = options("WAMR_BUILD_INTERP", "WAMR_BUILD_FAST_INTERP",
                               "WAMR_BUILD_" + feature)
            with self.subTest(feature=feature):
                self.assertIn("wasm_interp_classic.c", config.interpreter_sources(selected))
                self.assertNotIn("wasm_interp_fast.c", config.interpreter_sources(selected))
        mini = options("WAMR_BUILD_INTERP", "WAMR_BUILD_MINI_LOADER")
        self.assertIn("wasm_mini_loader.c", config.interpreter_sources(mini))
        self.assertEqual(config.interpreter_sources(options("WAMR_BUILD_AOT")), [])

    def test_missing_scons_entries_fail_explicitly(self):
        for feature, fragment in [
            ("JIT", "compilation/SConscript"),
            ("FAST_JIT", "fast-jit/SConscript"),
            ("GC", "common/gc/SConscript"),
        ]:
            with self.subTest(feature=feature), self.assertRaisesRegex(ValueError, fragment):
                config.runtime_scripts(
                    config.feature_config(options("WAMR_BUILD_INTERP",
                                                   "WAMR_BUILD_" + feature)), root)

    def test_aot_target_and_relocation_contract(self):
        with self.assertRaisesRegex(ValueError, "explicit rtconfig.WAMR_BUILD_TARGET"):
            config.aot_target_config(target())
        for cpu, flags, name in [
            ("cortex-a", "", "ARMV7"),
            ("cortex-m3", "", "THUMBV7M"),
            ("cortex-m4", "-mfloat-abi=hard", "THUMBV7EM_VFP"),
        ]:
            with self.subTest(cpu=cpu):
                descriptor = target(cpu=cpu, flags=flags, WAMR_BUILD_TARGET=name)
                architecture, source = config.aot_target_config(descriptor)
                self.assertTrue((root / "core/iwasm/aot/arch" / source).is_file())
                definitions = config.compile_definitions(descriptor, options("WAMR_BUILD_AOT"))
                self.assertIn("WASM_ENABLE_AOT=1", definitions)
                self.assertIn(("BUILD_TARGET", '\\"' + architecture + '\\"'), definitions)
        with self.assertRaisesRegex(ValueError, "calling ABI"):
            config.aot_target_config(target(flags="-mfloat-abi=hard",
                                             WAMR_BUILD_TARGET="ARMV7"))
        with self.assertRaisesRegex(ValueError, "relocation selection"):
            config.aot_target_config(target(cpu="cortex-r5", WAMR_BUILD_TARGET="ARMV7"))
        with self.assertRaisesRegex(ValueError, "instruction states"):
            config.aot_target_config(target(flags="-mthumb", WAMR_BUILD_TARGET="ARMV7"))


if __name__ == "__main__":
    unittest.main()
