# Wasm Proposals

This document is intended to describe the current status of WebAssembly proposals and WASI proposals in WAMR.

Only track proposals that are followed in the [WebAssembly proposals](https://github.com/WebAssembly/proposals) and [WASI proposals](https://github.com/WebAssembly/WASI/blob/main/docs/Proposals.md).

Normally, the document tracks proposals that are in phase 4. However, if a proposal in an earlier phase receives support, it will be added to the list below.

The _status_ represents the configuration _product-mini/platforms/linux/CMakeLists.txt_. There may be minor differences between the top-level CMakeLists and platform-specific CMakeLists.

Users can turn those features on or off by using compilation options. If a relevant compilation option is not available(`N/A`), it indicates that the feature is permanently enabled.

The _Spec Version_ column is the version of the [WebAssembly core specification](https://github.com/WebAssembly/proposals/blob/main/finished-proposals.md) that a proposal was merged into. `—` means the proposal is still a standalone proposal and has not been merged into any released version. To build a whole specification version with one switch, see [Wasm specification version preset](./build_wamr.md#wasm-specification-version-preset).

## On-by-default Wasm Proposals

| Proposal                              | >= Phase 4 | Spec Version | Compilation Option       |
| ------------------------------------- | ---------- | ------------ | ------------------------ |
| Bulk Memory Operations                | Yes        | 2.0          | `WAMR_BUILD_BULK_MEMORY` |
| Fixed-width SIMD[^1]                  | Yes        | 2.0          | `WAMR_BUILD_SIMD`        |
| Import/Export of Mutable Globals[^2]  | Yes        | 1.0          | N/A                      |
| Multi-value                           | Yes        | 2.0          | N/A                      |
| Non-trapping float-to-int Conversions | Yes        | 2.0          | N/A                      |
| Reference Types                       | Yes        | 2.0          | `WAMR_BUILD_REF_TYPES`   |
| Sign-extension Operators              | Yes        | 2.0          | N/A                      |
| WebAssembly C and C++ API             | No         | —            | N/A                      |

[^1]: llvm-jit and aot only.

[^2]: in WAMR's implementation, if a mutable global shared by several wasm instances, each instance maintains its own copy of the global rather than sharing it.

## Off-by-default Wasm Proposals

| Proposal                      | >= Phase 4 | Spec Version | Compilation Option               |
| ----------------------------- | ---------- | ------------ | -------------------------------- |
| Branch Hinting                | Yes        | 3.0          | `WASM_ENABLE_BRANCH_HINTS`       |
| Extended Constant Expressions | Yes        | 3.0          | `WAMR_BUILD_EXTENDED_CONST_EXPR` |
| Garbage Collection            | Yes        | 3.0          | `WAMR_BUILD_GC`                  |
| Legacy Exception Handling[^3] | No         | —            | `WAMR_BUILD_EXCE_HANDLING`       |
| Memory64                      | Yes        | 3.0          | `WAMR_BUILD_MEMORY64`            |
| Multiple Memories[^4]         | Yes        | 3.0          | `WAMR_BUILD_MULTI_MEMORY`        |
| Reference-Typed Strings       | No         | —            | `WAMR_BUILD_STRINGREF`           |
| Tail Call                     | Yes        | 3.0          | `WAMR_BUILD_TAIL_CALL`           |
| Threads[^5]                   | Yes        | —            | `WAMR_BUILD_SHARED_MEMORY`       |
| Typed Function References     | Yes        | 3.0          | `WAMR_BUILD_GC`                  |

[^3]:
    interpreter only. [a legacy version](https://github.com/WebAssembly/exception-handling/blob/main/proposals/exception-handling/legacy/Exceptions.md).
    This proposal is currently also known as the "legacy proposal" and still
    supported in the web, but can be deprecated in future and the use of
    this proposal is discouraged.

[^4]: interpreter only

[^5]: `WAMR_BUILD_LIB_PTHREAD` can also be used to enable

## Unimplemented Wasm Proposals

| Proposal                                    | >= Phase 4 | Spec Version |
| ------------------------------------------- | ---------- | ------------ |
| Custom Annotation Syntax in the Text Format | Yes        | 3.0          |
| Exception Handling[^6]                      | Yes        | 3.0          |
| JS String Builtins                          | Yes        | 3.0          |
| Relaxed SIMD                                | Yes        | 3.0          |

[^6]: [up-to-date version](https://github.com/WebAssembly/exception-handling/blob/main/proposals/exception-handling/Exceptions.md)

## On-by-default WASI Proposals

| Proposal | >= Phase 4 | Compilation Option |
| -------- | ---------- | ------------------ |

## Off-by-default WASI Proposals

| Proposal                   | >= Phase 4 | Compilation Option            |
| -------------------------- | ---------- | ----------------------------- |
| Machine Learning (wasi-nn) | No         | `WAMR_BUILD_WASI_NN`          |
| Threads                    | No         | `WAMR_BUILD_LIB_WASI_THREADS` |

## Unimplemented WASI Proposals

| Proposal | >= Phase 4 |
| -------- | ---------- |

## WAMR features

WAMR offers a variety of customizable features to create a highly efficient runtime. For more details, please refer to [build_wamr](./build_wamr.md).
