// Copyright (C) 2026 Matt Hargett. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = std::env::args_os().skip(1).collect();
    if args.len() != 4 || args[0] != "parse" || args[2] != "-o" {
        return Err("usage: stackless-component-tool parse input.wat -o output.wasm".into());
    }
    let bytes = wat::parse_file(&args[1])?;
    wasmparser::Validator::new_with_features(wasmparser::WasmFeatures::all())
        .validate_all(&bytes)?;
    std::fs::write(&args[3], bytes)?;
    Ok(())
}
