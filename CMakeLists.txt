cmake_minimum_required(VERSION 3.15)
project(vulkan_ps4 VERSION 0.1.0 LANGUAGES C CXX)

# vulkan-ps4 — Vulkan 1.0 ICD over OpenGNM
#
# Translates Vulkan API calls into GNM/PM4 commands via opengnm,
# enabling any Vulkan application to run with GPU acceleration on
# jailbroken PS4 hardware.
#
# See ../opengnm/VULKAN_PS4_PLAN.md for the full plan.

set(VK_PS4_PLATFORM "auto" CACHE STRING "vulkan-ps4 platform: auto, orbis, or generic")
set_property(CACHE VK_PS4_PLATFORM PROPERTY STRINGS auto orbis generic)

if(VK_PS4_PLATFORM STREQUAL "orbis")
    set(VK_PS4_ORBIS ON)
elseif(VK_PS4_PLATFORM STREQUAL "generic")
    set(VK_PS4_ORBIS OFF)
else()
    if(ORBIS OR PLATFORM_PS4 OR CMAKE_SYSTEM_NAME STREQUAL "OpenOrbis")
        set(VK_PS4_ORBIS ON)
    else()
        set(VK_PS4_ORBIS OFF)
    endif()
endif()

# Vulkan-Headers — type definitions only (no loader)
set(VK_HEADERS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../Vulkan-Headers" CACHE PATH "Vulkan-Headers root")
if(NOT EXISTS "${VK_HEADERS_ROOT}/include/vulkan/vulkan_core.h")
    message(FATAL_ERROR "Vulkan-Headers not found at ${VK_HEADERS_ROOT}. "
        "Set VK_HEADERS_ROOT to the Vulkan-Headers checkout.")
endif()

# opengnm — GNM API + PM4 builders
set(OPENGNM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../opengnm" CACHE PATH "opengnm root")
if(NOT EXISTS "${OPENGNM_ROOT}/include/gnmdriver.h")
    message(FATAL_ERROR "opengnm not found at ${OPENGNM_ROOT}. "
        "Set OPENGNM_ROOT to the opengnm checkout.")
endif()

# libpsbc — SPIR-V to GCN shader compiler (optional, stub if not found)
set(PSBC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../opengnm-psbc" CACHE PATH "opengnm-psbc root")
set(VK_PS4_HAVE_PSBC OFF)
if(EXISTS "${PSBC_ROOT}/libpsbc/psbc_compile.h")
    set(VK_PS4_HAVE_PSBC ON)
    message(STATUS "libpsbc found — real shader compilation enabled")
else()
    message(STATUS "libpsbc not found — using stub shader path")
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${VK_HEADERS_ROOT}/include
    ${OPENGNM_ROOT}/include
)

if(VK_PS4_HAVE_PSBC)
    include_directories(${PSBC_ROOT}/libpsbc)
    include_directories(${PSBC_ROOT}/include)
endif()

# Vulkan ICD loader interface version
add_compile_definitions(
    VK_PS4_VERSION_MAJOR=1
    VK_PS4_VERSION_MINOR=1
    VK_PS4_VERSION_PATCH=0
)
if(VK_PS4_ORBIS)
    add_compile_definitions(VK_USE_PLATFORM_PS4 ORBIS __ORBIS__ __PS4__)
endif()

# ICD sources
# As functions get real implementations, they move from vk_ps4_stubs.c
# to their own files (vk_ps4_memory.c, vk_ps4_command.c, etc.).
set(VK_PS4_SOURCES
    src/vk_ps4_entry.c
    src/vk_ps4_dispatch.c
    src/vk_ps4_entrypoints.c
    src/vk_ps4_instance.c
    src/vk_ps4_device.c
    src/vk_ps4_format.c
    src/vk_ps4_memory.c
    src/vk_ps4_buffer.c
    src/vk_ps4_image.c
    src/vk_ps4_render_pass.c
    src/vk_ps4_shader.c
    src/vk_ps4_pipeline.c
    src/vk_ps4_descriptor.c
    src/vk_ps4_command.c
    src/vk_ps4_swapchain.c
    src/vk_ps4_queue.c
    src/vk_ps4_sync.c
    src/vk_ps4_query.c
    src/vk_ps4_stubs.c
    src/vk_ps4_vulkan11.c
)

# Build as a shared library (ICD) on all platforms.
# On PS4, this is linked statically into the app or loaded via a custom path.
add_library(vulkan_ps4 SHARED ${VK_PS4_SOURCES})

# Link against opengnm — try imported target first, then fall back to
# finding the built library in the opengnm build directory.
if(TARGET opengnm)
    target_link_libraries(vulkan_ps4 PRIVATE opengnm)
else()
    # Find the opengnm static library in its build directory
    find_library(OPENGNM_LIBRARY
        NAMES opengnm
        PATHS ${OPENGNM_ROOT}/build ${OPENGNM_ROOT}/build/${CMAKE_BUILD_TYPE}
        NO_DEFAULT_PATH
    )
    if(OPENGNM_LIBRARY)
        message(STATUS "Found opengnm: ${OPENGNM_LIBRARY}")
        target_link_libraries(vulkan_ps4 PRIVATE ${OPENGNM_LIBRARY})
    else()
        message(STATUS "opengnm library not found — building without GNM linkage (stub mode)")
        # Allow building without opengnm for header validation only.
        # Real linkage requires building opengnm first.
    endif()
endif()

if(VK_PS4_HAVE_PSBC)
    # Find the libpsbc static library
    find_library(PSBC_LIBRARY
        NAMES psbc
        PATHS ${PSBC_ROOT}
        NO_DEFAULT_PATH
    )
    if(PSBC_LIBRARY)
        message(STATUS "Found libpsbc: ${PSBC_LIBRARY}")
        target_link_libraries(vulkan_ps4 PRIVATE ${PSBC_LIBRARY})
        # libpsbc is C++ (Mesa ACO) — need C++ standard library
        if(APPLE)
            target_link_libraries(vulkan_ps4 PRIVATE "-lc++")
        else()
            target_link_libraries(vulkan_ps4 PRIVATE "-lstdc++")
        endif()
    else()
        message(STATUS "libpsbc.a not found — building without shader compilation (stub mode)")
        set(VK_PS4_HAVE_PSBC OFF)
    endif()
    target_compile_definitions(vulkan_ps4 PRIVATE VK_PS4_HAVE_PSBC=1)
endif()

# Tests (host only)
if(NOT VK_PS4_ORBIS)
    enable_testing()
    add_executable(vk_ps4_format_test tests/test_format.c)
    target_link_libraries(vk_ps4_format_test PRIVATE vulkan_ps4)
    add_test(NAME format_test COMMAND vk_ps4_format_test)

    # Phase 1 triangle test — exercises full ICD pipeline
    add_executable(vk_ps4_triangle_test tests/test_triangle.c)
    target_link_libraries(vk_ps4_triangle_test PRIVATE vulkan_ps4)
    add_test(NAME triangle_test COMMAND vk_ps4_triangle_test
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    # Phase 2 descriptor test — exercises descriptor sets, samplers, updates
    add_executable(vk_ps4_descriptor_test tests/test_descriptor.c)
    target_link_libraries(vk_ps4_descriptor_test PRIVATE vulkan_ps4)
    add_test(NAME descriptor_test COMMAND vk_ps4_descriptor_test
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    # Validation layer test — goes through the Vulkan loader with VVL enabled
    # Links ONLY against the system Vulkan loader (not our ICD directly).
    # The loader discovers our ICD via VK_ICD_FILENAMES at runtime.
    add_executable(vk_ps4_validation_test tests/test_validation.c)
    find_library(VK_LOADER_LIBRARY NAMES vulkan PATHS /opt/homebrew/lib NO_DEFAULT_PATH)
    if(VK_LOADER_LIBRARY)
        target_link_libraries(vk_ps4_validation_test PRIVATE ${VK_LOADER_LIBRARY})
        message(STATUS "Found Vulkan loader: ${VK_LOADER_LIBRARY}")
    else()
        target_link_libraries(vk_ps4_validation_test PRIVATE "-lvulkan")
        message(STATUS "Using -lvulkan for loader")
    endif()
    add_test(NAME validation_test COMMAND vk_ps4_validation_test
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(validation_test PROPERTIES
        ENVIRONMENT
            "VK_ICD_FILENAMES=${CMAKE_BINARY_DIR}/vulkan_ps4_icd.json;VK_LAYER_PATH=/opt/homebrew/opt/vulkan-validationlayers/share/vulkan/explicit_layer.d;DYLD_LIBRARY_PATH=/opt/homebrew/opt/vulkan-validationlayers/lib")

    # Dolphin compatibility test — mimics Dolphin's VulkanContext init path
    add_executable(vk_ps4_dolphin_compat_test tests/test_dolphin_compat.c)
    target_link_libraries(vk_ps4_dolphin_compat_test PRIVATE ${VK_LOADER_LIBRARY})
    add_test(NAME dolphin_compat_test COMMAND vk_ps4_dolphin_compat_test
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(dolphin_compat_test PROPERTIES
        ENVIRONMENT
            "VK_ICD_FILENAMES=${CMAKE_BINARY_DIR}/vulkan_ps4_icd.json")
endif()

# Install
include(GNUInstallDirs)
install(TARGETS vulkan_ps4
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vulkan_ps4
)

# Generate ICD manifest with the correct library path for the build tree
if(NOT VK_PS4_ORBIS)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/vulkan_ps4_icd.json.in
        ${CMAKE_BINARY_DIR}/vulkan_ps4_icd.json
        @ONLY
    )
endif()
