# WAMR project

 In this section, we will introduce the basics of project WAMR to you. In each brief introduction section, you are more than welcome to jump to details of that section triggering your interest.

## What is it?

WebAssembly Micro Runtime (WAMR) is a lightweight standalone WebAssembly (WASM) runtime. It has a small footprint, high performance, and highly configurable features for applications across from embedded, IoT, edge to Trusted Execution Environment (TEE), smart contract, cloud-native, and so on.

## Why you may want to use it

<!-- TODO: link  -->

As we explained in the previous section, WebAssembly is great for code reuse on the server side with the help of runtime like our Project WAMR. So the most straightforward way is to use WAMR to run your WASM program.

It's not limited to simply being a command line application that runs your wasm program. You could also use it as a library, integrated into your application to run any wasm program inside your application. Although most user cases are embedding WAMR in their C/C++ program, we do support other [language-binding](../../tutorial/language-embedding/README.md) so that you could use WAMR in some language you prefer.

## Component of WAMR

<!-- TODO: link -->

There are four parts of WAMR. Two main parts of WAMR are:

1. The "iwasm" VM core to run WASM applications. It has many features and achieves several functionalities. The complete list of features and examples demonstrating it can be found in [Features](../../features/README.md). Here are some brief introductions of some features that may interest you:

   - Flexibility: It supports multiple running modes to provide the ideal responsive time and performance on your demand. The running mode includes interpreter mode, AOT mode (Ahead-of-Time compilation), and JIT modes (Just-in-Time compilation, LLVM JIT, and Fast JIT are supported). Details on how to build and use each mode properly and where you may want to use it can be found in [Tutorial](../../tutorial/README.md)

   - High Performance: WAMR achieves nearly native speed by  AOT and JIT modes. It also has a small runtime binary size (~85K for interpreter and ~50K for AOT) and low memory usage

   - Portability: It supports many architectures and platforms.

     The architectures it supports:

     - X86-64, X86-32
     - ARM, THUMB (ARMV7 Cortex-M7 and Cortex-A15 are tested)
     - AArch64 (Cortex-A57 and Cortex-A53 are tested)
     - RISCV64, RISCV32 (RISC-V LP64 and RISC-V LP64D are tested)
     - XTENSA, MIPS, ARC

     The  platforms it supports:

     - [Linux](../../../doc/build_wamr.md#linux),  [Linux SGX (Intel Software Guard Extension)](../../../doc/linux_sgx.md),  [MacOS](../../../doc/build_wamr.md#macos),  [Android](../../../doc/build_wamr.md#android), [Windows](../../../doc/build_wamr.md#windows), [Windows (MinGW)](../../../doc/build_wamr.md#mingw)

     - [Zephyr](../../../doc/build_wamr.md#zephyr),  [AliOS-Things](../../../doc/build_wamr.md#alios-things),  [VxWorks](../../../doc/build_wamr.md#vxworks), [NuttX](../../../doc/build_wamr.md#nuttx), [RT-Thread](../../../doc/build_wamr.md#RT-Thread), [ESP-IDF](../../../doc/build_wamr.md#esp-idf)

     It enables true cross-platform development experience. You can even port WAMR to a new platform following [this tutorial](../../../doc/port_wamr.md). Though it's unlikely since we support many platforms, having such features is comforting.

   - Security: It has Linux SGX (Intel Software Guard Extension) support. Through this unique application isolation technology, your application data is as safe as it can be.

2. The "wamrc" AOT compiler to compile WASM files into AOT files for best performance and smaller runtime footprint, which is run by "iwasm" VM Core

   Both the wasm binary files and AOT files are supported by iwasm. The wamrc AOT compiler compiles a wasm binary file to an AOT file, which can also be run by iwasm. The speed by AOT and JIT are near to native.

The other 2 parts are:

1. Application framework:

   The WAMR application manager supports remote application management from the host environment or the cloud through any physical communications such as TCP, UPD, UART, BLE, etc. Its modular design makes it able to support application management for different managed runtimes.

2. Application manager:

   By using the iwasm VM core, we are flexible to build different application frameworks for specific domains, although it would take quite some effort.

   The WAMR has offered a comprehensive framework for programming WASM applications for device and IoT usages. The framework supports running multiple applications that are based on the event-driven programming model. Here are the supporting API sets by the WAMR application framework library :

   - Timer, Inter-app communication (request/response and pub/sub), Sensor, Connectivity and data transmission, 2D graphic UI
