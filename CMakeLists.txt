cmake_minimum_required(VERSION 3.10)
project(Valora C RC)

set(CMAKE_C_STANDARD 11)
# Use Unicode if your system defaults to it; ANSI is fine for this app
add_definitions(-DUNICODE -D_UNICODE)

add_executable(valora main.c resources.rc)
if (MINGW)
    target_link_options(valora PRIVATE -mconsole)
endif()
target_link_libraries(valora PRIVATE comdlg32 shell32 ole32 wininet ws2_32)
