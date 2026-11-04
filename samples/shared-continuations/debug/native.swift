// Copyright (C) 2026 Matt Hargett. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
@_silgen_name("native_checkpoint") func nativeCheckpoint(_ value:Int32) -> Int32
@_cdecl("native_bridge")
@inline(never)
public func nativeSwift(_ value:Int32) -> Int32 {
    return nativeCheckpoint(value)+1
}

#if DIAGNOSTIC_ERRORS
private enum NativeFailure: Error { case failed }
@_cdecl("native_swift_error")
@inline(never)
public func nativeSwiftError() -> Int32 {
    do { throw NativeFailure.failed }
    catch { return -13 }
}
#endif
