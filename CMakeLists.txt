cmake_minimum_required(VERSION 3.14)
project(musashi_tests VERSION 4.10.0 LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Options
option(BUILD_TESTS "Build unit tests" ON)
option(ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

# Add compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -pedantic)
endif()

# Add sanitizers if requested
if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address -fsanitize=undefined)
    add_link_options(-fsanitize=address -fsanitize=undefined)
endif()

# Generate M68k operation files first
add_executable(m68kmake m68kmake.c)

# Custom command to generate m68kops files
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/m68kops.c ${CMAKE_CURRENT_BINARY_DIR}/m68kops.h
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/m68kmake ${CMAKE_CURRENT_BINARY_DIR}/ ${CMAKE_CURRENT_SOURCE_DIR}/m68k_in.c
    DEPENDS m68kmake m68k_in.c
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating M68k operation files"
)

# Create the main Musashi library (static for testing)
# Note: m68kfpu.c is included by m68kcpu.c, not compiled separately
add_library(musashi_core STATIC
    m68kcpu.c
    m68kdasm.c
    m68ktrace.cc
    softfloat/softfloat.c
    ${CMAKE_CURRENT_BINARY_DIR}/m68kops.c
)

target_include_directories(musashi_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/softfloat
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Disable some warnings for the core library (legacy code)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(musashi_core PRIVATE 
        -Wno-unused-parameter 
        -Wno-sign-compare
        -Wno-unused-variable
    )
endif()

# Create the myfunc library (C++ wrapper)
add_library(musashi_api STATIC
    myfunc.cc
)

set_source_files_properties(myfunc.cc m68ktrace.cc PROPERTIES LANGUAGE CXX)

target_link_libraries(musashi_api PUBLIC musashi_core)

target_include_directories(musashi_api PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Build tests if requested
if(BUILD_TESTS)
    enable_testing()
    
    # Find or fetch GoogleTest
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    
    FetchContent_MakeAvailable(googletest)
    
    # Test executable for myfunc API
    add_executable(test_myfunc
        tests/test_myfunc.cpp
    )
    
    target_link_libraries(test_myfunc
        musashi_api
        GTest::gtest_main
    )
    
    # Test executable for basic M68k functionality
    add_executable(test_m68k
        tests/test_m68k.cpp
    )
    
    target_link_libraries(test_m68k
        musashi_api
        GTest::gtest_main
    )
    
    # Register tests with CTest
    include(GoogleTest)
    gtest_discover_tests(test_myfunc)
    gtest_discover_tests(test_m68k)
    
    # Add a custom target to run all tests
    add_custom_target(run_tests
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS test_myfunc test_m68k
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running all tests"
    )
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "Configuration Summary:")
message(STATUS "  CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
message(STATUS "  BUILD_TESTS: ${BUILD_TESTS}")
message(STATUS "  ENABLE_SANITIZERS: ${ENABLE_SANITIZERS}")
message(STATUS "  C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "  C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "")