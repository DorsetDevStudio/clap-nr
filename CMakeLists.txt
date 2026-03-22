cmake_minimum_required(VERSION 3.20)
project(clap-nr
    VERSION 1.0.0
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# -----------------------------------------------------------------------
# Paths to bundled libraries
# -----------------------------------------------------------------------
set(FFTW_DIR      "${CMAKE_SOURCE_DIR}/libs/fftw")
set(RNNOISE_DIR   "${CMAKE_SOURCE_DIR}/libs/rnnoise")
set(SPECBLEACH_DIR "${CMAKE_SOURCE_DIR}/libs/specbleach")
set(CLAP_INCLUDE  "${CMAKE_SOURCE_DIR}/include")
set(IMGUI_DIR     "${CMAKE_SOURCE_DIR}/libs/imgui")

# -----------------------------------------------------------------------
# NR DSP source files
# -----------------------------------------------------------------------
set(NR_SOURCES
    src/nr/anr.c
    src/nr/emnr.c
    src/nr/rnnr.c
    src/nr/sbnr.c
    src/nr/calculus.c
    src/nr/zetaHat.c
    src/nr/FDnoiseIQ.c
)

# -----------------------------------------------------------------------
# Dear ImGui source files (vendored in libs/imgui)
# -----------------------------------------------------------------------
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_win32.cpp
    ${IMGUI_DIR}/imgui_impl_dx11.cpp
)

# -----------------------------------------------------------------------
# CLAP plugin entry-point + ImGui GUI
# -----------------------------------------------------------------------
set(PLUGIN_SOURCES
    src/clap_nr.c
    src/gui_imgui.cpp
    ${IMGUI_SOURCES}
)

# -----------------------------------------------------------------------
# Build the plugin as a shared library (.clap is just a renamed .dll)
# -----------------------------------------------------------------------
add_library(clap-nr SHARED ${NR_SOURCES} ${PLUGIN_SOURCES})

# Headers
target_include_directories(clap-nr PRIVATE
    src
    src/nr
    ${FFTW_DIR}
    ${RNNOISE_DIR}
    ${SPECBLEACH_DIR}
    ${CLAP_INCLUDE}
    ${IMGUI_DIR}
)

# Import libraries
target_link_libraries(clap-nr PRIVATE
    "${FFTW_DIR}/libfftw3-3.lib"
    "${RNNOISE_DIR}/rnnoise.lib"
    "${SPECBLEACH_DIR}/specbleach.lib"
)

# Windows-specific
if(WIN32)
    target_link_libraries(clap-nr PRIVATE comctl32 shell32 d3d11 dxgi dwmapi delayimp)
    # Delay-load companion DLLs so LoadLibrary(clap-nr.clap) succeeds before
    # entry_init() pre-loads them from the plugin's own directory.
    target_link_options(clap-nr PRIVATE
        /DELAYLOAD:libfftw3-3.dll
        /DELAYLOAD:rnnoise.dll
        /DELAYLOAD:specbleach.dll
    )
    target_compile_definitions(clap-nr PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX CLAP_NR_STANDALONE=1)
    # CLAP plugins are .clap files — just rename the output DLL
    set_target_properties(clap-nr PROPERTIES
        OUTPUT_NAME "clap-nr"
        SUFFIX ".clap"
        PREFIX ""
    )
endif()

# -----------------------------------------------------------------------
# Install: copy DLL dependencies next to the plugin
# -----------------------------------------------------------------------
install(TARGETS clap-nr DESTINATION .)
install(FILES
    "${FFTW_DIR}/libfftw3-3.dll"
    "${RNNOISE_DIR}/rnnoise.dll"
    "${SPECBLEACH_DIR}/specbleach.dll"
    DESTINATION .
)
