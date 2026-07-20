cmake_minimum_required(VERSION 3.15)
project(vulkan_ps4 VERSION 0.1.0 LANGUAGES C)

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
    VK_USE_PLATFORM_PS4
    VK_PS4_VERSION_MAJOR=1
    VK_PS4_VERSION_MINOR=0
    VK_PS4_VERSION_PATCH=0
)

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
    src/vk_ps4_stubs.c
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
    target_link_libraries(vulkan_ps4 PRIVATE psbc)
    target_compile_definitions(vulkan_ps4 PRIVATE VK_PS4_HAVE_PSBC=1)
endif()

# Tests (host only)
if(NOT VK_PS4_ORBIS)
    enable_testing()
    add_executable(vk_ps4_format_test tests/test_format.c)
    target_link_libraries(vk_ps4_format_test PRIVATE vulkan_ps4)
    add_test(NAME format_test COMMAND vk_ps4_format_test)
endif()

# Install
include(GNUInstallDirs)
install(TARGETS vulkan_ps4
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vulkan_ps4
)
