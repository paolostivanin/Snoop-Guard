cmake_minimum_required(VERSION 3.6)
project(WebMonit)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c11 -Wall -Wextra -O3 -Wformat=2 -fstack-protector-all -fstack-protector-strong
-pie -fpie -fdiagnostics-color=always -Wstrict-prototypes -Wunreachable-code  -Wwrite-strings -Wpointer-arith -Wbad-function-cast
-Wcast-qual -Wcast-align -Wl,-O1,--sort-common,--as-needed,-z,relro -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3")

set(SOURCE_FILES src/main.c)
add_executable(WebMonit ${SOURCE_FILES})