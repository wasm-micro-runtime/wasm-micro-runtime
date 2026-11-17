# Functions that are known to access misaligned addresses, shared with
# WAMR_BUILD_SANITIZER=ubsan
set(UBSAN_IGNORELIST ${REPO_ROOT_DIR}/build-scripts/ubsan_ignorelist.txt)

if(NOT IN_OSS_FUZZ)
  message(STATUS "Enable ASan and UBSan in non-oss-fuzz environment for vmlib")

  add_compile_options(-fprofile-instr-generate -fcoverage-mapping)

  #
  # Sync up with the content of infra/base-images/base-builder/Dockerfile in oss-fuzz
  #

  # SANITIZER_FLAGS_address
  add_compile_options(-fsanitize=address -fsanitize-address-use-after-scope)

  # SANITIZER_FLAGS_undefined, and the alignment check, which oss-fuzz leaves
  # out
  add_compile_options(
    -O1
    -fsanitize=alignment,array-bounds,bool,builtin,enum,function,integer-divide-by-zero,null,object-size,return,returns-nonnull-attribute,shift,signed-integer-overflow,unsigned-integer-overflow,unreachable,vla-bound,vptr
    -fno-sanitize-recover=alignment,array-bounds,bool,builtin,enum,function,integer-divide-by-zero,null,object-size,return,returns-nonnull-attribute,shift,signed-integer-overflow,unreachable,vla-bound,vptr
    -fsanitize-ignorelist=${UBSAN_IGNORELIST}
  )

  add_link_options(-fsanitize=address,undefined -fprofile-instr-generate)
elseif("$ENV{SANITIZER}" STREQUAL "undefined")
  # oss-fuzz's undefined build takes its flags from the environment. Add the
  # alignment check to them
  add_compile_options(
    -fsanitize=alignment
    -fno-sanitize-recover=alignment
    -fsanitize-ignorelist=${UBSAN_IGNORELIST}
  )
endif()

# Always disable unsigned-integer-overflow 
if(CMAKE_C_COMPILER_ID MATCHES ".*Clang")
  add_compile_options(-fno-sanitize=unsigned-integer-overflow)
endif()

# '-fsanitize=vptr' not allowed with '-fno-rtti
# But, LLVM by default, disables the use of `rtti` in the compiler
add_compile_options(-fsanitize=fuzzer -fno-sanitize=vptr)
add_link_options(-fsanitize=fuzzer -fno-sanitize=vptr)
