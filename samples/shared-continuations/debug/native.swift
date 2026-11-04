// Copyright (C) 2026 Matt Hargett. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
@_silgen_name("native_checkpoint") func nativeCheckpoint(_ value:Int32) -> Int32
@_cdecl("native_bridge")
@inline(never)
public func nativeSwift(_ value:Int32) -> Int32 {
    return nativeCheckpoint(value)+1
}
