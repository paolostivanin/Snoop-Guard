cmake_minimum_required(VERSION 3.16)
project(SnoopGuard VERSION "1.1.0" LANGUAGES C)

set(PROJECT_VERSION_SUFFIX "-dev")
set(PROJECT_VERSION_FULL "${PROJECT_VERSION}${PROJECT_VERSION_SUFFIX}")

include(GNUInstallDirs)
include(CheckCCompilerFlag)
include(CheckPIESupported)
include(CTest)

configure_file(src/version.h.in src/version.h @ONLY)
configure_file(packaging/snoop-guard.service.in snoop-guard.service @ONLY)
configure_file(packaging/org.snoopguard.Service.service.in
               org.snoopguard.Service.service @ONLY)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET
                  glib-2.0 gio-2.0 gobject-2.0 gio-unix-2.0)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)

add_library(sg_compile INTERFACE)
target_compile_features(sg_compile INTERFACE c_std_11)
target_include_directories(sg_compile INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/src
)
target_compile_options(sg_compile INTERFACE
    -Wall -Wextra -Wformat=2 -Wstrict-prototypes -Wmissing-prototypes
    -Wshadow -Wunreachable-code -Wwrite-strings -Wpointer-arith
    -Wbad-function-cast -Wcast-qual -Wcast-align
    -D_FILE_OFFSET_BITS=64
    $<$<CONFIG:Debug>:-O0>
    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-O2>
)

foreach(flag -fstack-protector-strong -fno-plt)
    string(MAKE_C_IDENTIFIER "${flag}" flag_var)
    check_c_compiler_flag("${flag}" "HAS_${flag_var}")
    if(HAS_${flag_var})
        target_compile_options(sg_compile INTERFACE "${flag}")
    endif()
endforeach()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sg_compile INTERFACE
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-D_FORTIFY_SOURCE=3>
    )
    target_link_options(sg_compile INTERFACE
        -Wl,-O1,--as-needed,-z,relro,-z,now,-z,noexecstack
    )
endif()

check_pie_supported()

add_executable(sg-daemon
    src/daemon/list-webcam.c
    src/daemon/main.c
    src/daemon/load-config-file.c
    src/daemon/check-webcam.c
    src/daemon/sg-notification.c
    src/daemon/sg-logging.c
    src/daemon/check-mic.c
    src/daemon/sg-dbus.c
    src/daemon/sg-state.c
    src/daemon/sg-policy.c
)
target_link_libraries(sg-daemon PRIVATE sg_compile PkgConfig::GLIB PkgConfig::PIPEWIRE)
set_property(TARGET sg-daemon PROPERTY POSITION_INDEPENDENT_CODE TRUE)

add_executable(sg-ctl src/cli/sgctl.c src/cli/sg-json.c)
target_link_libraries(sg-ctl PRIVATE sg_compile PkgConfig::GLIB)
set_property(TARGET sg-ctl PROPERTY POSITION_INDEPENDENT_CODE TRUE)

install(TARGETS sg-daemon sg-ctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/snoop-guard.service"
        DESTINATION lib/systemd/user)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.snoopguard.Service.service"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
install(FILES packaging/snoop-guard.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES snoop-guard.ini
        DESTINATION ${CMAKE_INSTALL_DATADIR}/snoop-guard
        RENAME snoop-guard.ini.example)
install(FILES README.md SECURITY.md CHANGELOG.md CONTRIBUTING.md RELEASE.md LICENSE
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES docs/sg-daemon.1 docs/sg-ctl.1
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

if(BUILD_TESTING)
    add_executable(sg-core-tests
        tests/test-core.c
        src/daemon/load-config-file.c
        src/daemon/sg-state.c
        src/daemon/sg-logging.c
        src/daemon/sg-policy.c
        src/cli/sg-json.c
    )
    target_link_libraries(sg-core-tests PRIVATE sg_compile PkgConfig::GLIB)
    target_compile_definitions(sg-core-tests PRIVATE
        TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_test(NAME core COMMAND sg-core-tests)

    add_test(NAME cli-invalid-recent COMMAND sg-ctl recent 1001)
    set_tests_properties(cli-invalid-recent PROPERTIES WILL_FAIL TRUE)
    add_test(NAME cli-json-option-order COMMAND sg-ctl --version --json)
    add_test(NAME version-consistency
        COMMAND ${CMAKE_COMMAND}
                -DEXPECTED_VERSION=${PROJECT_VERSION_FULL}
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyVersion.cmake)

    find_program(DBUS_RUN_SESSION dbus-run-session)
    if(DBUS_RUN_SESSION)
        add_test(NAME dbus-integration
            COMMAND ${DBUS_RUN_SESSION} --
                    bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.sh
                    $<TARGET_FILE:sg-daemon> $<TARGET_FILE:sg-ctl>)
        set_tests_properties(dbus-integration PROPERTIES TIMEOUT 20)
    endif()
endif()

set(CPACK_PACKAGE_NAME "snoop-guard")
set(CPACK_PACKAGE_VENDOR "Snoop Guard contributors")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION_FULL}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Linux webcam and microphone activity monitor")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
set(CPACK_GENERATOR "TGZ")
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_SOURCE_IGNORE_FILES
    "/\\\\.git/;/\\\\.claude/;/\\\\.agents/;/\\\\.codex/;"
    "/cmake-build[^/]*/;/build/;/\\\\.idea/;~$"
)
include(CPack)
