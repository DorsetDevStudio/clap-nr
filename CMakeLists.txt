cmake_minimum_required(VERSION 3.20)

# Read version numbers from the single source of truth: src/version.h
file(READ "${CMAKE_SOURCE_DIR}/src/version.h" _ver_header)
string(REGEX MATCH "CLAP_NR_VERSION_MAJOR[ \t]+([0-9]+)" _ ${_ver_header})
set(CLAP_NR_MAJOR ${CMAKE_MATCH_1})
string(REGEX MATCH "CLAP_NR_VERSION_MINOR[ \t]+([0-9]+)" _ ${_ver_header})
set(CLAP_NR_MINOR ${CMAKE_MATCH_1})
string(REGEX MATCH "CLAP_NR_VERSION_PATCH[ \t]+([0-9]+)" _ ${_ver_header})
set(CLAP_NR_PATCH ${CMAKE_MATCH_1})

project(clap-nr
    VERSION ${CLAP_NR_MAJOR}.${CLAP_NR_MINOR}.${CLAP_NR_PATCH}
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# -----------------------------------------------------------------------
# Paths to bundled libraries (used on Windows; Unix uses system packages)
# -----------------------------------------------------------------------
set(FFTW_DIR       "${CMAKE_SOURCE_DIR}/libs/fftw")
set(RNNOISE_DIR    "${CMAKE_SOURCE_DIR}/libs/rnnoise")
set(SPECBLEACH_DIR "${CMAKE_SOURCE_DIR}/libs/specbleach")
set(CLAP_INCLUDE   "${CMAKE_SOURCE_DIR}/include")
set(IMGUI_DIR      "${CMAKE_SOURCE_DIR}/libs/imgui")

# -----------------------------------------------------------------------
# NR DSP source files  (platform-agnostic)
# -----------------------------------------------------------------------
set(NR_SOURCES
    src/nr/anr.c
    src/nr/emnr.c
    src/nr/rnnr.c
    src/nr/sbnr.c
    src/nr/nr0.c
    src/nr/calculus.c
    src/nr/zetaHat.c
    src/nr/FDnoiseIQ.c
)

# -----------------------------------------------------------------------
# Dear ImGui source files  (core, shared across all platforms)
# -----------------------------------------------------------------------
set(IMGUI_CORE_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)

if(WIN32)
    # Windows: Win32 platform layer + DirectX 11 renderer
    list(APPEND IMGUI_SOURCES
        ${IMGUI_CORE_SOURCES}
        ${IMGUI_DIR}/imgui_impl_win32.cpp
        ${IMGUI_DIR}/imgui_impl_dx11.cpp
    )
elseif(APPLE)
    # macOS: native Cocoa backend (NSWindow + CVDisplayLink) — no GLFW
    list(APPEND IMGUI_SOURCES
        ${IMGUI_CORE_SOURCES}
        ${IMGUI_DIR}/imgui_impl_opengl3.cpp
    )
else()
    # Linux: GLFW platform layer + OpenGL 3 renderer
    list(APPEND IMGUI_SOURCES
        ${IMGUI_CORE_SOURCES}
        ${IMGUI_DIR}/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/imgui_impl_opengl3.cpp
    )
endif()

# -----------------------------------------------------------------------
# CLAP plugin entry-point + GUI
# -----------------------------------------------------------------------
set(PLUGIN_SOURCES
    src/clap_nr.c
    ${IMGUI_SOURCES}
)
if(APPLE)
    # Objective-C++ file for native Cocoa GUI backend
    list(APPEND PLUGIN_SOURCES src/gui_imgui.mm)
else()
    list(APPEND PLUGIN_SOURCES src/gui_imgui.cpp)
endif()

# -----------------------------------------------------------------------
# Build the plugin as a shared library (.clap on all platforms)
# -----------------------------------------------------------------------
add_library(clap-nr SHARED ${NR_SOURCES} ${PLUGIN_SOURCES})

# Common headers
target_include_directories(clap-nr PRIVATE
    src
    src/nr
    ${CLAP_INCLUDE}
    ${IMGUI_DIR}
)

# -----------------------------------------------------------------------
# Platform-specific configuration
# -----------------------------------------------------------------------
if(WIN32)
    # --- Bundled .lib imports -----------------------------------------------
    target_include_directories(clap-nr PRIVATE
        ${FFTW_DIR}
        ${RNNOISE_DIR}
        ${SPECBLEACH_DIR}
    )
    target_link_libraries(clap-nr PRIVATE
        "${FFTW_DIR}/libfftw3-3.lib"
        "${RNNOISE_DIR}/rnnoise.lib"
        "${SPECBLEACH_DIR}/specbleach.lib"
        comctl32 shell32 d3d11 dxgi dwmapi delayimp
    )
    # Delay-load companion DLLs so LoadLibrary(clap-nr.clap) succeeds before
    # entry_init() pre-loads them from the plugin's own directory.
    target_link_options(clap-nr PRIVATE
        /DELAYLOAD:libfftw3-3.dll
        /DELAYLOAD:rnnoise.dll
        /DELAYLOAD:specbleach.dll
    )
    target_compile_definitions(clap-nr PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX CLAP_NR_STANDALONE=1)

elseif(APPLE)
    # --- macOS: prefer universal static libs (libs/mac-universal/) if present,
    #     otherwise fall back to Homebrew dylibs for local dev builds.
    find_package(OpenGL REQUIRED)

    set(UNIVERSAL_LIB_DIR "${CMAKE_SOURCE_DIR}/libs/mac-universal")
    if(EXISTS "${UNIVERSAL_LIB_DIR}/libfftw3.a"
       AND EXISTS "${UNIVERSAL_LIB_DIR}/librnnoise.a"
       AND EXISTS "${UNIVERSAL_LIB_DIR}/libspecbleach.a")

        message(STATUS "Using universal static libs from ${UNIVERSAL_LIB_DIR}")
        target_include_directories(clap-nr PRIVATE "${UNIVERSAL_LIB_DIR}/include")
        target_link_libraries(clap-nr PRIVATE
            "${UNIVERSAL_LIB_DIR}/libfftw3.a"
            "${UNIVERSAL_LIB_DIR}/libfftw3f.a"
            "${UNIVERSAL_LIB_DIR}/librnnoise.a"
            "${UNIVERSAL_LIB_DIR}/libspecbleach.a"
            OpenGL::GL
            pthread
            "-framework Cocoa"
            "-framework CoreFoundation"
        )

    else()

        message(STATUS "Universal libs not found -- using Homebrew dylibs (arm64 only)")
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(FFTW3      REQUIRED IMPORTED_TARGET fftw3)
        pkg_check_modules(RNNOISE    REQUIRED IMPORTED_TARGET rnnoise)
        pkg_check_modules(SPECBLEACH REQUIRED IMPORTED_TARGET libspecbleach)
        target_include_directories(clap-nr PRIVATE
            ${FFTW3_INCLUDE_DIRS}
            ${RNNOISE_INCLUDE_DIRS}
            ${SPECBLEACH_INCLUDE_DIRS}
        )
        target_link_libraries(clap-nr PRIVATE
            PkgConfig::FFTW3
            PkgConfig::RNNOISE
            PkgConfig::SPECBLEACH
            OpenGL::GL
            pthread
            "-framework Cocoa"
            "-framework CoreFoundation"
        )

    endif()
    target_compile_definitions(clap-nr PRIVATE CLAP_NR_STANDALONE=1)
    # Hide all symbols by default; only CLAP_EXPORT entry point is visible
    set_target_properties(clap-nr PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        C_VISIBILITY_PRESET   hidden
    )

else()
    # --- Linux: prefer static libs for standalone distribution, fall back to system libs
    find_package(OpenGL REQUIRED)
    
    set(LINUX_STATIC_DIR "${CMAKE_SOURCE_DIR}/libs/linux-static")
    if(EXISTS "${LINUX_STATIC_DIR}/lib/libfftw3.a"
       AND EXISTS "${LINUX_STATIC_DIR}/lib/librnnoise.a"
       AND EXISTS "${LINUX_STATIC_DIR}/lib/libspecbleach.a")
        
        message(STATUS "Using static libs from ${LINUX_STATIC_DIR} (standalone build)")
        
        # Use static libraries for fftw3, rnnoise, specbleach
        target_include_directories(clap-nr PRIVATE "${LINUX_STATIC_DIR}/include")
        target_link_libraries(clap-nr PRIVATE
            "${LINUX_STATIC_DIR}/lib/libfftw3.a"
            "${LINUX_STATIC_DIR}/lib/libfftw3f.a"
            "${LINUX_STATIC_DIR}/lib/librnnoise.a"
            "${LINUX_STATIC_DIR}/lib/libspecbleach.a"
        )
        
        # Still need glfw and OpenGL from system (they're universally available)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GLFW3 REQUIRED IMPORTED_TARGET glfw3)
        target_link_libraries(clap-nr PRIVATE
            PkgConfig::GLFW3
            OpenGL::GL
            pthread
            dl  # for dladdr() to locate plugin path
            m   # math library
        )
        
    else()
        
        message(STATUS "Static libs not found -- using system libraries via pkg-config")
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(FFTW3   REQUIRED IMPORTED_TARGET fftw3)
        pkg_check_modules(RNNOISE REQUIRED IMPORTED_TARGET rnnoise)
        pkg_check_modules(SPECBLEACH REQUIRED IMPORTED_TARGET libspecbleach)
        pkg_check_modules(GLFW3 REQUIRED IMPORTED_TARGET glfw3)
        
        target_include_directories(clap-nr PRIVATE
            ${FFTW3_INCLUDE_DIRS}
            ${RNNOISE_INCLUDE_DIRS}
            ${SPECBLEACH_INCLUDE_DIRS}
        )
        target_link_libraries(clap-nr PRIVATE
            PkgConfig::FFTW3
            PkgConfig::RNNOISE
            PkgConfig::SPECBLEACH
            PkgConfig::GLFW3
            OpenGL::GL
            pthread
            dl
        )
        
    endif()
    
    target_compile_definitions(clap-nr PRIVATE CLAP_NR_STANDALONE=1)
    # Hide all symbols by default; only CLAP_EXPORT entry point is visible
    set_target_properties(clap-nr PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        C_VISIBILITY_PRESET   hidden
    )
    # Embed the RPATH so the plugin finds system .so files when installed
    set_target_properties(clap-nr PROPERTIES
        BUILD_RPATH   "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

# -----------------------------------------------------------------------
# Plugin output name: clap-nr.clap on all platforms
# -----------------------------------------------------------------------
if(APPLE)
    set_target_properties(clap-nr PROPERTIES
        OUTPUT_NAME "clap-nr"
        SUFFIX      ".clap"
        PREFIX      ""
    )
else()
    set_target_properties(clap-nr PROPERTIES
        OUTPUT_NAME "clap-nr"
        SUFFIX      ".clap"
        PREFIX      ""
    )
endif()

# -----------------------------------------------------------------------
# Install
# -----------------------------------------------------------------------
install(TARGETS clap-nr DESTINATION .)
if(WIN32)
    install(FILES
        "${FFTW_DIR}/libfftw3-3.dll"
        "${RNNOISE_DIR}/rnnoise.dll"
        "${SPECBLEACH_DIR}/specbleach.dll"
        "${RNNOISE_DIR}/rnnoise_weights_small.bin"
        "${RNNOISE_DIR}/rnnoise_weights_large.bin"
        DESTINATION .
    )
endif()

