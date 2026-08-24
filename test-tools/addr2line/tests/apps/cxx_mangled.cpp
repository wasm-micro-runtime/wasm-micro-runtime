/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* C++ source with a templated and namespaced function. addr2line.py
   should pass the mangled symbol through llvm-cxxfilt and produce a
   readable demangled name in the output. */

namespace app {
namespace ns {

template<typename T>
__attribute__((noinline)) static void
crash_with(T)
{
    __builtin_trap();
}

class Worker
{
  public:
    __attribute__((noinline)) void run() { crash_with<int>(42); }
};

} // namespace ns
} // namespace app

extern "C" void
app_main(void)
{
    app::ns::Worker w;
    w.run();
}
