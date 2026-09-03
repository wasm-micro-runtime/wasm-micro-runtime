# WAMR Dev Container

This devcontainer provides a full WAMR development environment. The image is
built from `Dockerfile` (multi-stage) using the repo root as the build
context (`.devcontainer/devcontainer.json` sets `"context": ".."`).

## Prebuilt LLVM

The image ships a **prebuilt LLVM** (LLVM 18.1.8, the same `llvmorg-18.1.8`
tag used by CI) at `/opt/llvm`:

- It is compiled **once at image build time** by the repo's own
  `build-scripts/build_llvm.py` in the `llvm-builder` stage (Dockerfile).
  Only the repackaged install tree (`bin/include/lib/libexec/share`, about
  1.5-2 GB) is copied into the final image; the source clone, build tree and
  CPack tarball are removed as intermediate outputs.
- At container creation, `.devcontainer/setup_llvm.sh` (run via
  `postCreateCommand`) symlinks `core/deps/llvm/build -> /opt/llvm` in the
  workspace. That is the exact location WAMR's cmake requires, so **wamrc
  (AOT) and `-DWAMR_BUILD_JIT=1` (LLVM JIT) builds work with zero extra
  cmake flags**. `core/deps/**` is git-ignored, so the symlink never pollutes
  `git status`.

### Configuring the LLVM backends

The `LLVM_ARCH` build argument (default `X86`) controls which LLVM targets
are compiled. Override it in `.devcontainer/devcontainer.json`:

```jsonc
"args": {
  "LLVM_ARCH": "AArch64 ARM Mips RISCV X86"   // longer image build time
}
```

Notes:

- On a non-x86_64 host (e.g. Apple Silicon), the container is arm64; the
  default `X86` backend still cross-compiles X86 AOT, but for native AOT set
  `LLVM_ARCH=AArch64` (or `ARM`, ...).
- Rebuilding the image is fast when only unrelated final-stage layers change:
  the `llvm-builder` stage is reused from the layer cache. An LLVM upgrade
  means changing the version/tag in `build-scripts/build_llvm.py`, which
  automatically invalidates the builder stage on the next image build - no
  `--no-cache` needed.

### Overriding the prebuilt LLVM

To build your own LLVM (e.g. different targets) inside the container instead:

```sh
rm core/deps/llvm/build            # remove the symlink
cd wamr-compiler && ./build_llvm.sh --arch "AArch64 ARM Mips RISCV X86"
```

This clones llvm-project into the workspace `core/deps/llvm` and builds there
(30-60 min), replacing the image-provided libraries for this workspace.
