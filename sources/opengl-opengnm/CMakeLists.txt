cmake_minimum_required(VERSION 3.15)
project(opengnm VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# opengnm — clean-rewrite PS4 GNM library
# sceGnm*/sceGpa* drop-in API compatibility with the Sony official PS4 SDK.

set(OPENGNM_PLATFORM "auto" CACHE STRING "opengnm platform: auto, orbis, or generic")
set_property(CACHE OPENGNM_PLATFORM PROPERTY STRINGS auto orbis generic)

# Submit wrappers: when ON, opengnm intercepts sceGnmSubmit* calls to add
# argument validation and debug logging before forwarding to the firmware.
# When OFF (default), submit calls resolve directly to libSceGnmDriver.so
# via PLT. OFF is required for shadPS4 (HLE has no firmware module for
# resolvegnmdriversym to find) and is also the simpler path on real hardware.
option(OPENGNM_SUBMIT_WRAPPERS "Enable submit wrappers with validation and logging" OFF)

if(OPENGNM_PLATFORM STREQUAL "orbis")
    set(OPENGNM_ORBIS_PLATFORM ON)
elseif(OPENGNM_PLATFORM STREQUAL "generic")
    set(OPENGNM_ORBIS_PLATFORM OFF)
else()
    if(ORBIS OR PLATFORM_PS4 OR CMAKE_SYSTEM_NAME STREQUAL "OpenOrbis")
        set(OPENGNM_ORBIS_PLATFORM ON)
    else()
        set(OPENGNM_ORBIS_PLATFORM OFF)
    endif()
endif()

# Core GNM sources
set(OPENGNM_SOURCES
    src/commandbuffer.c
    src/dataformat.c
    src/depthrendertarget.c
    src/drawcommandbuffer.c
    src/error.c
    src/helpers.c
    src/rendertarget.c
    src/shader.c
    src/texture.c
)

# GCN assembler (internal — needed for fetch shader generation)
set(OPENGNM_GCN_SOURCES
    src/gcn/analyzer.c
    src/gcn/assembler.c
    src/gcn/decoder.c
    src/gcn/error.c
    src/gcn/format.c
    src/gcn/types.c
)

# PM4 encoding (internal)
set(OPENGNM_PM4_SOURCES
    src/pm4/decoder.c
    src/pm4/error.c
    src/pm4/format.c
    src/pm4/types.c
)

# GpuAddr — surface computation (AMD PAL-derived math)
set(OPENGNM_GPUADDR_SOURCES
    src/gpuaddr/decompress.c
    src/gpuaddr/error.c
    src/gpuaddr/surface.c
    src/gpuaddr/surfgen.c
    src/gpuaddr/tilemodes.c
    src/gpuaddr/tiler.c
)

list(APPEND OPENGNM_SOURCES
    ${OPENGNM_GCN_SOURCES}
    ${OPENGNM_PM4_SOURCES}
    ${OPENGNM_GPUADDR_SOURCES}
)

# Platform-specific sources
if(OPENGNM_ORBIS_PLATFORM)
    list(APPEND OPENGNM_SOURCES
        src/driver_orbis.c
        src/platform_orbis.c
    )
else()
    list(APPEND OPENGNM_SOURCES
        src/driver_generic.c
        src/platform_generic.c
    )
endif()

add_library(opengnm STATIC ${OPENGNM_SOURCES})
add_library(opengnm::opengnm ALIAS opengnm)

target_include_directories(opengnm PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_include_directories(opengnm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(opengnm PRIVATE -w)
endif()

if(OPENGNM_ORBIS_PLATFORM)
    target_compile_definitions(opengnm PUBLIC OPENGNM_ORBIS OPENGNM_REQUIRE_ABI)
    if(OPENGNM_SUBMIT_WRAPPERS)
        target_compile_definitions(opengnm PRIVATE OPENGNM_ENABLE_SUBMIT_WRAPPERS)
    endif()
    target_link_libraries(opengnm PUBLIC kernel SceGnmDriver SceVideoOut)
else()
    target_compile_definitions(opengnm PUBLIC OPENGNM_GENERIC)
endif()

# === Tests ===
option(OPENGNM_BUILD_TESTS "Build opengnm tests" ON)
if(OPENGNM_BUILD_TESTS)
    enable_testing()

    add_executable(opengnm_tests
        tests/test_main.c
        tests/test_surface.c
        tests/test_drawcmd.c
        tests/test_validate.c
        tests/test_api.c
        tests/test_compat.c
        tests/test_helpers.c
        tests/test_pm4.c
    )
    target_link_libraries(opengnm_tests PRIVATE opengnm)
    target_include_directories(opengnm_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    # Tests need to see internal headers (src/u/utility.h etc.)
    target_include_directories(opengnm_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(NAME opengnm_all_tests COMMAND opengnm_tests)
endif()

# === Install ===
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h")

install(TARGETS opengnm
        EXPORT opengnmTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT opengnmTargets
        NAMESPACE opengnm::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/opengnm)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/opengnmConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/opengnmConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/opengnm
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/opengnmConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/opengnmConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/opengnmConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/opengnm)

if(OPENGNM_ORBIS_PLATFORM)
    set(OPENGNM_PKGCONFIG_LIBS_PRIVATE "-lkernel -lSceGnmDriver -lSceVideoOut")
else()
    set(OPENGNM_PKGCONFIG_LIBS_PRIVATE "")
endif()
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/opengnm.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/opengnm.pc
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/opengnm.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
