# fypalette        - primary library (shared when BUILD_SHARED_LIBS)
# fypalette_static - always a static archive
include(GNUInstallDirs)

# A parent project may already provide libfyaml::libfyaml; reuse it, then
# try the libfyaml package config, then a raw path lookup.
if(NOT TARGET libfyaml::libfyaml)
    find_package(libfyaml QUIET CONFIG)
endif()
if(NOT TARGET libfyaml::libfyaml)
    find_path(LIBFYAML_INCLUDE_DIR NAMES libfyaml.h)
    find_library(LIBFYAML_LIBRARY NAMES fyaml)
    if(NOT LIBFYAML_INCLUDE_DIR OR NOT LIBFYAML_LIBRARY)
        message(FATAL_ERROR "libfyaml not found")
    endif()
    add_library(libfyaml::libfyaml UNKNOWN IMPORTED)
    set_target_properties(libfyaml::libfyaml PROPERTIES
        IMPORTED_LOCATION "${LIBFYAML_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBFYAML_INCLUDE_DIR}")
endif()

configure_file(${PROJECT_SOURCE_DIR}/cmake/libfypalette.pc.in
    libfypalette.pc @ONLY)

# The built-in themes are the files in themes/, compiled in as data. A theme
# is named by its file name without the extension.
file(GLOB _fypal_theme_files CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/themes/*.yaml")
list(SORT _fypal_theme_files)
set(_fypal_themes_c "/* Generated from themes/ by CMake. Do not edit. */\n")
string(APPEND _fypal_themes_c "#include <stddef.h>\n#include \"fypal-internal.h\"\n\n")
set(_fypal_themes_tab "")
foreach(_f ${_fypal_theme_files})
    get_filename_component(_name ${_f} NAME_WE)
    string(MAKE_C_IDENTIFIER "${_name}" _id)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_f})
    file(READ ${_f} _hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _hex "${_hex}")
    string(APPEND _fypal_themes_c "static const char theme_${_id}[] = {${_hex}0x00};\n")
    string(APPEND _fypal_themes_tab "\t{ \"${_name}\", theme_${_id} },\n")
endforeach()
string(APPEND _fypal_themes_c
    "\nconst struct fypal_builtin_theme fypal_builtin_themes_[] = {\n"
    "${_fypal_themes_tab}\t{ NULL, NULL },\n};\n")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/fypal-themes.c.tmp "${_fypal_themes_c}")
configure_file(${CMAKE_CURRENT_BINARY_DIR}/fypal-themes.c.tmp
    ${CMAKE_CURRENT_BINARY_DIR}/fypal-themes.c COPYONLY)

set(_fypal_sources
    fypal-color.c
    fypal-hash.c
    fypal-role.c
    fypal-term.c
    fypal-theme.c
    ${CMAKE_CURRENT_BINARY_DIR}/fypal-themes.c
)

set(_fypal_incdir ${PROJECT_SOURCE_DIR}/include)

# The header version and the project() version must agree.
set(_fypal_h ${_fypal_incdir}/libfypalette.h)
file(STRINGS ${_fypal_h} _v_major REGEX "^#define FYPAL_VERSION_MAJOR ")
file(STRINGS ${_fypal_h} _v_minor REGEX "^#define FYPAL_VERSION_MINOR ")
file(STRINGS ${_fypal_h} _v_patch REGEX "^#define FYPAL_VERSION_PATCH ")
string(REGEX REPLACE "[^0-9]" "" _v_major "${_v_major}")
string(REGEX REPLACE "[^0-9]" "" _v_minor "${_v_minor}")
string(REGEX REPLACE "[^0-9]" "" _v_patch "${_v_patch}")
if(NOT "${_v_major}.${_v_minor}.${_v_patch}" STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR
        "version mismatch: ${_fypal_h} says ${_v_major}.${_v_minor}.${_v_patch}, "
        "project() says ${PROJECT_VERSION}")
endif()

find_library(FYPAL_M_LIBRARY m)

add_library(fypalette        ${_fypal_sources})
add_library(fypalette_static STATIC ${_fypal_sources})

set(_fypal_static_output fypalette_static)
if(BUILD_SHARED_LIBS AND NOT WIN32)
    set(_fypal_static_output fypalette)
endif()

foreach(_t fypalette fypalette_static)
    target_include_directories(${_t} PUBLIC
        "$<BUILD_INTERFACE:${_fypal_incdir}>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
    target_include_directories(${_t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${_t} PUBLIC libfyaml::libfyaml)
    if(FYPAL_M_LIBRARY)
        target_link_libraries(${_t} PUBLIC m)
    endif()
    if(CMAKE_C_COMPILER_ID MATCHES GNU|Clang)
        target_compile_options(${_t} PRIVATE -fvisibility=hidden)
    endif()
    if(ASAN_C_FLAGS)
        target_compile_options(${_t} PRIVATE ${ASAN_C_FLAGS})
        target_link_options(${_t} PRIVATE ${ASAN_C_FLAGS} INTERFACE ${ASAN_C_FLAGS})
    endif()
endforeach()

set_target_properties(fypalette PROPERTIES
    OUTPUT_NAME fypalette
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME libfypalette
)
set_target_properties(fypalette_static PROPERTIES
    OUTPUT_NAME ${_fypal_static_output}
    POSITION_INDEPENDENT_CODE ON
    EXPORT_NAME libfypalette_static
)

add_library(libfypalette::libfypalette        ALIAS fypalette)
add_library(libfypalette::libfypalette_static ALIAS fypalette_static)

install(TARGETS fypalette fypalette_static
    EXPORT libfypalette-targets
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${_fypal_h} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libfypalette.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/themes/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/libfypalette/themes)
