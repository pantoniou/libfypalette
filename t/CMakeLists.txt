# One CTest case per named test in fypal-test.c.
add_executable(fypal-test fypal-test.c)
target_link_libraries(fypal-test PRIVATE fypalette_static)
if(ASAN_C_FLAGS)
    target_compile_options(fypal-test PRIVATE ${ASAN_C_FLAGS})
    target_link_options(fypal-test PRIVATE ${ASAN_C_FLAGS})
endif()

set(_fypal_tests
    lab_roundtrip
    lch_reference
    gamut_map
    xterm256
    ansi16
    contrast
    sgr_rgb
    color_parse
    theme_builtin
    theme_expr
    theme_sections
    theme_errors
    theme_api
    param_strings
    theme_ground
    theme_ansi16
    theme_terminal16
    theme_file
    ember_gamut
    ember_ramp
    ember_contrast
    ember_ground
    role_lookup
    role_fallback
    role_inherit
    role_caps
    role_yaml
    role_invalidate
    role_style_sgr
    role_many
    glyph_yaml
    glyph_api
    glyph_errors
    ember_glyphs
)

foreach(_t ${_fypal_tests})
    add_test(NAME fypalette.${_t} COMMAND fypal-test ${_t})
endforeach()
set_tests_properties(fypalette.theme_file PROPERTIES
    ENVIRONMENT "FYPAL_THEME_DIR=${PROJECT_SOURCE_DIR}/themes")
