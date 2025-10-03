cmake_minimum_required(VERSION 3.16)
project(SnoopGuard VERSION "1.0.0" LANGUAGES C)

# Pre-release tag (can be empty)
set(PROJECT_VERSION_SUFFIX "-beta2")

# Compose full version (e.g., 1.0.0-beta1). If suffix is empty, it's just the version.
set(PROJECT_VERSION_FULL "${PROJECT_VERSION}${PROJECT_VERSION_SUFFIX}")

configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/src/version.h
        @ONLY
)

# Common requirements
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0 gio-2.0 gobject-2.0)

# Shared compile/link settings
add_library(sg_common INTERFACE)
target_compile_features(sg_common INTERFACE c_std_11)

target_compile_options(sg_common INTERFACE
        -Wall -Wextra -Wformat=2 -Wstrict-prototypes
        -Wunreachable-code -Wwrite-strings -Wpointer-arith -Wbad-function-cast
        -Wcast-qual -Wcast-align -fdiagnostics-color=always
        -D_FILE_OFFSET_BITS=64
        $<$<CONFIG:Debug>:-O0 -U_FORTIFY_SOURCE>
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-O2 -D_FORTIFY_SOURCE=3>
)

target_link_options(sg_common INTERFACE
        -Wl,-O1,--sort-common,--as-needed,-z,relro
)

target_link_libraries(sg_common INTERFACE
        PkgConfig::GLIB
)

target_include_directories(sg_common INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}/src
)

target_compile_definitions(sg_common INTERFACE
        SW_VERSION="${PROJECT_VERSION}"
)

# Executables
add_executable(sg-daemon
        src/daemon/list-webcam.c
        src/daemon/main.c
        src/daemon/load-config-file.c
        src/daemon/get-ppid-from-pname.c
        src/daemon/check-webcam.c
        src/daemon/sg-notification.c
        src/daemon/sg-logging.c
        src/daemon/check-mic.c
        src/daemon/sg-dbus.c
        src/daemon/sg-state.c
)
target_link_libraries(sg-daemon PRIVATE sg_common)

add_executable(sg-ctl src/cli/sgctl.c)
target_link_libraries(sg-ctl PRIVATE sg_common)
