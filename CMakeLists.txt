cmake_minimum_required(VERSION 3.12)

project(libfypalette
    VERSION "0.1.0"
    HOMEPAGE_URL "https://github.com/pantoniou/libfypalette"
    LANGUAGES C
)

# The tests and the swatch tool are not wanted when this project is pulled in
# as a subdirectory of a larger build, so both default off in that case.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_fypal_aux_default ON)
else()
    set(_fypal_aux_default OFF)
endif()

option(BUILD_FYPALETTE_TESTS "Build and register the tests" ${_fypal_aux_default})
option(BUILD_FYPALETTE_TOOLS "Build the fypalette-show tool" ${_fypal_aux_default})
option(ENABLE_ASAN "Enable ASAN support" OFF)

if(WIN32)
    option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)
else()
    option(BUILD_SHARED_LIBS "Build using shared libraries" ON)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE $ENV{CMAKE_BUILD_TYPE})
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release")
    endif()
endif()

if(CMAKE_C_COMPILER_ID MATCHES GNU|Clang)
    include(CheckCCompilerFlag)
    check_c_compiler_flag("-std=gnu2x" FYPAL_HAVE_GNU2X)
    if(FYPAL_HAVE_GNU2X)
        add_compile_options(-std=gnu2x)
    else()
        add_compile_options(-std=gnu11)
    endif()
    add_compile_options(-Wall -Wextra -Wdeclaration-after-statement)
    add_compile_definitions(_GNU_SOURCE)
elseif(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    add_compile_options(/W3)
endif()

set(ASAN_C_FLAGS)
if(ENABLE_ASAN)
    # an undefined-behaviour report fails the test instead of only printing
    set(ASAN_C_FLAGS -fsanitize=address -fsanitize=undefined
                     -fno-sanitize-recover=undefined
                     -fno-omit-frame-pointer)
endif()

add_subdirectory(src)

if(BUILD_FYPALETTE_TOOLS)
    add_subdirectory(bin)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/libfypalette-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/libfypalette-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libfypalette)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/libfypalette-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(EXPORT libfypalette-targets
    FILE libfypalette-targets.cmake
    NAMESPACE libfypalette::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libfypalette)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/libfypalette-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/libfypalette-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libfypalette)

if(BUILD_FYPALETTE_TESTS)
    enable_testing()
    add_subdirectory(t)
endif()
