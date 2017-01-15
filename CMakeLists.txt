cmake_minimum_required(VERSION 3.5)
project(SnoopGuard)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c11 -Wall -Wextra -O3 -Wformat=2 -fstack-protector-all -fstack-protector-strong
-pie -fpie -fdiagnostics-color=always -Wstrict-prototypes -Wunreachable-code  -Wwrite-strings -Wpointer-arith -Wbad-function-cast
-Wcast-qual -Wcast-align -Wl,-O1,--sort-common,--as-needed,-z,relro -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3")

set(SOURCE_FILES
        src/server/list-webcam.c
        src/server/main.c
        src/server/load-config-file.c
        src/server/get-ppid-from-pname.c
        src/server/check-webcam.c
        src/server/sg-notification.c
        src/server/sg-logging.c
        src/server/check-mic.c)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
include_directories(${GTK3_INCLUDE_DIRS})
link_directories(${GTK3_LIBRARY_DIRS})
add_definitions(${GTK3_CFLAGS_OTHER})

add_executable(SnoopGuard ${SOURCE_FILES})
target_link_libraries(SnoopGuard ${GTK3_LIBRARIES} -lasound -lnotify)
