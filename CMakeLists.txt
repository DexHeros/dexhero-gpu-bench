cmake_minimum_required(VERSION 3.20)
project(dexhero-gpu-bench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to release if the user didn't pass -DCMAKE_BUILD_TYPE.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Vulkan loader — required for compute + raster phases. Encoder phase
# probes NVENC/VAAPI/VideoToolbox separately at runtime.
find_package(Vulkan REQUIRED)

# OpenSSL — sign.cpp uses EVP_Digest for keccak256 fallback path.
find_package(OpenSSL REQUIRED)

# libsecp256k1 — sign.cpp uses it for EIP-191 ECDSA recovery-id signing.
# Try pkg-config first (linux/macos via apt/brew), fall back to FetchContent
# (MSYS2 ucrt64 doesn't ship it as a package).
find_package(PkgConfig)
set(SECP256K1_FOUND FALSE)
if(PkgConfig_FOUND)
    pkg_check_modules(SECP256K1 IMPORTED_TARGET libsecp256k1)
endif()
if(NOT SECP256K1_FOUND)
    message(STATUS "libsecp256k1 not found via pkg-config; fetching source")
    include(FetchContent)
    set(SECP256K1_BUILD_BENCHMARK   OFF CACHE BOOL "" FORCE)
    set(SECP256K1_BUILD_TESTS       OFF CACHE BOOL "" FORCE)
    set(SECP256K1_BUILD_EXHAUSTIVE_TESTS OFF CACHE BOOL "" FORCE)
    set(SECP256K1_BUILD_EXAMPLES    OFF CACHE BOOL "" FORCE)
    set(SECP256K1_ENABLE_MODULE_RECOVERY ON CACHE BOOL "" FORCE)
    set(SECP256K1_INSTALL           OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(secp256k1
        GIT_REPOSITORY https://github.com/bitcoin-core/secp256k1.git
        GIT_TAG        v0.5.0
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(secp256k1)
endif()

add_executable(dexhero-gpu-bench
    bench.cpp
    bench_compute.cpp
    bench_raster.cpp
    bench_encoder.cpp
    json.cpp
    machine_id.cpp
    sign.cpp
)

target_include_directories(dexhero-gpu-bench PRIVATE ${Vulkan_INCLUDE_DIRS})
target_link_libraries(dexhero-gpu-bench PRIVATE
    Vulkan::Vulkan
    OpenSSL::Crypto
)
if(SECP256K1_FOUND)
    target_link_libraries(dexhero-gpu-bench PRIVATE PkgConfig::SECP256K1)
else()
    target_link_libraries(dexhero-gpu-bench PRIVATE secp256k1)
endif()

if(WIN32)
    target_link_libraries(dexhero-gpu-bench PRIVATE bcrypt)
endif()

if(UNIX AND NOT APPLE)
    # Linux: link libdl for dlopen of NVENC / VAAPI shared libs at runtime.
    target_link_libraries(dexhero-gpu-bench PRIVATE dl)
endif()

# Optimize for size — this binary is ~5 MB target, ships inside the host
# installer NSIS bundle. -fdata-sections + LTO get us under the cap.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(dexhero-gpu-bench PRIVATE
        -ffunction-sections -fdata-sections)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_link_options(dexhero-gpu-bench PRIVATE -Wl,--gc-sections)
    endif()
endif()

install(TARGETS dexhero-gpu-bench DESTINATION bin)

# Compile shaders to SPIR-V via glslangValidator (shipped with VulkanSDK).
find_program(GLSLANG glslangValidator REQUIRED)
set(SHADER_OUTPUTS)
foreach(SHADER compute.comp raster.vert raster.frag)
    set(IN  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}")
    set(OUT "${CMAKE_CURRENT_BINARY_DIR}/${SHADER}.spv")
    add_custom_command(OUTPUT ${OUT}
        COMMAND ${GLSLANG} -V ${IN} -o ${OUT}
        DEPENDS ${IN}
        COMMENT "Compiling ${SHADER} to SPIR-V")
    list(APPEND SHADER_OUTPUTS ${OUT})
endforeach()
add_custom_target(shaders ALL DEPENDS ${SHADER_OUTPUTS})
add_dependencies(dexhero-gpu-bench shaders)
install(FILES ${SHADER_OUTPUTS} DESTINATION bin)
