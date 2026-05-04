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
target_link_libraries(dexhero-gpu-bench PRIVATE Vulkan::Vulkan)

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
