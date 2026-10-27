cmake_minimum_required(VERSION 3.20)
project(DLBHop VERSION 1.0.0 LANGUAGES C CXX ASM_MASM)

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Build with Visual Studio x64 and MASM.")
endif()

add_library(deadlock-directional-component SHARED
    native.cpp command_hook.asm
    third_party/minhook/MinHook.c
    third_party/minhook/buffer.c
    third_party/minhook/trampoline.c
    third_party/minhook/hde/hde64.c)
set_target_properties(deadlock-directional-component PROPERTIES OUTPUT_NAME deadlock-directional-hold)
add_executable(deadlock-directional launcher.cpp)

foreach(target deadlock-directional-component deadlock-directional)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
    set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_libraries(${target} PRIVATE user32)
endforeach()
target_link_libraries(deadlock-directional PRIVATE bcrypt)

enable_testing()
add_test(NAME analog-steering COMMAND deadlock-directional --self-test)
add_executable(command-stub-test stub_test.cpp stub_test.asm command_hook.asm)
set_property(TARGET command-stub-test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME command-trampoline COMMAND command-stub-test)
