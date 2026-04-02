cmake_minimum_required(VERSION 3.10)
project(LlamaSetup C RC)

set(CMAKE_C_STANDARD 11)
# Use Unicode if your system defaults to it; ANSI is fine for this app
add_definitions(-DUNICODE -D_UNICODE)

add_executable(LlamaSetup WIN32 main.c resources.rc)
target_link_libraries(LlamaSetup PRIVATE comdlg32 shell32 ole32)