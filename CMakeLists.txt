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
option(ENABLE_PERFETTO "Enable Perfetto tracing support" OFF)

# Add compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -pedantic)
endif()

# Add sanitizers if requested
if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address -fsanitize=undefined)
    add_link_options(-fsanitize=address -fsanitize=undefined)
endif()

# Configure Perfetto if requested
if(ENABLE_PERFETTO)
    message(STATUS "Perfetto tracing enabled")
    
    # Disable tests and examples for retrobus-perfetto to avoid CI issues
    set(BUILD_TESTS_SAVED ${BUILD_TESTS})
    set(BUILD_EXAMPLES_SAVED ${BUILD_EXAMPLES})
    set(BUILD_TESTS OFF)
    set(BUILD_EXAMPLES OFF)
    
    # Add retrobus-perfetto subdirectory
    add_subdirectory(third_party/retrobus-perfetto/cpp)
    
    # Restore original values
    set(BUILD_TESTS ${BUILD_TESTS_SAVED})
    set(BUILD_EXAMPLES ${BUILD_EXAMPLES_SAVED})
    
    # Define the compile flag
    add_compile_definitions(ENABLE_PERFETTO=1)
else()
    message(STATUS "Perfetto tracing disabled")
endif()

# Note: vasm assembler integration is optional
# The test includes a manually encoded version of the program
# If you want to use vasm, uncomment the section below and ensure vasm builds correctly
# 
# if(BUILD_TESTS)
#     include(ExternalProject)
#     ExternalProject_Add(vasm
#         GIT_REPOSITORY https://github.com/dbuchwald/vasm.git
#         GIT_TAG master
#         CMAKE_ARGS 
#             -DVASM_CPU=m68k 
#             -DVASM_SYNTAX=mot
#             -DCMAKE_BUILD_TYPE=Release
#         BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR>
#         INSTALL_COMMAND ""
#     )
#     set(VASM_EXECUTABLE ${CMAKE_BINARY_DIR}/vasm-prefix/src/vasm-build/vasmm68k_mot)
# endif()

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
set(MUSASHI_CORE_SOURCES
    m68kcpu.c
    m68kdasm.c
    m68ktrace.cc
    m68k_memory_bridge.cc
    softfloat/softfloat.c
    ${CMAKE_CURRENT_BINARY_DIR}/m68kops.c
)

# Conditionally add Perfetto support
if(ENABLE_PERFETTO)
    list(APPEND MUSASHI_CORE_SOURCES m68k_perfetto.cc)
endif()

add_library(musashi_core STATIC ${MUSASHI_CORE_SOURCES})

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

set_source_files_properties(myfunc.cc m68ktrace.cc m68k_memory_bridge.cc PROPERTIES LANGUAGE CXX)

# Conditionally set C++ properties for Perfetto files
if(ENABLE_PERFETTO)
    set_source_files_properties(m68k_perfetto.cc PROPERTIES LANGUAGE CXX)
endif()

target_link_libraries(musashi_api PUBLIC musashi_core)

# Link Perfetto if enabled
if(ENABLE_PERFETTO)
    target_link_libraries(musashi_core PUBLIC retrobus_perfetto)
endif()

target_include_directories(musashi_api PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Build vasm assembler for tests
if(BUILD_TESTS)
    # Build vasm as an external project
    add_custom_target(vasm_build
        COMMAND ${CMAKE_MAKE_PROGRAM} CPU=m68k SYNTAX=mot
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/third_party/vasm
        COMMENT "Building vasm assembler"
    )
    
    # Create vasm executable target that depends on the build
    add_executable(vasm IMPORTED GLOBAL)
    set_target_properties(vasm PROPERTIES
        IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/third_party/vasm/vasmm68k_mot
    )
    add_dependencies(vasm vasm_build)
endif()

# Build tests if requested
if(BUILD_TESTS)
    enable_testing()
    
    # Force consistent C++ standard library on macOS
    if(APPLE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")
    endif()
    
    # Find or fetch GoogleTest
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    
    # Prevent installing gtest and building gmock
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    
    FetchContent_MakeAvailable(googletest)
    
    # CRITICAL FIX: Force fetched GoogleTest headers to be used BEFORE any system headers
    # This prevents mixing Homebrew's GoogleTest headers with FetchContent's library
    get_target_property(GTEST_INCLUDES gtest INTERFACE_INCLUDE_DIRECTORIES)
    
    # Test executable for myfunc API
    add_executable(test_myfunc
        tests/test_myfunc.cpp
    )
    
    target_link_libraries(test_myfunc
        musashi_api
        GTest::gtest_main
    )
    
    # Force fetched gtest headers BEFORE system headers
    if(GTEST_INCLUDES)
        target_include_directories(test_myfunc PRIVATE BEFORE ${GTEST_INCLUDES})
    endif()
    
    # Test executable for basic M68k functionality
    add_executable(test_m68k
        tests/test_m68k.cpp
    )
    
    target_link_libraries(test_m68k
        musashi_api
        GTest::gtest_main
    )
    
    # Force fetched gtest headers BEFORE system headers
    if(GTEST_INCLUDES)
        target_include_directories(test_m68k PRIVATE BEFORE ${GTEST_INCLUDES})
    endif()
    
    # Test executable for Perfetto integration (conditional)
    if(ENABLE_PERFETTO)
        # Note: The test program is manually encoded in test_perfetto.cpp
        # If vasm is available and you want to use the .s file, uncomment the section below:
        #
        # add_custom_command(
        #     OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/perfetto_test_program.h
        #     COMMAND ${VASM_EXECUTABLE} -Fbin -o ${CMAKE_CURRENT_BINARY_DIR}/perfetto_test_program.bin
        #             ${CMAKE_CURRENT_SOURCE_DIR}/tests/perfetto_test_program.s
        #     COMMAND xxd -i perfetto_test_program.bin > ${CMAKE_CURRENT_BINARY_DIR}/perfetto_test_program.h
        #     DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/perfetto_test_program.s
        #     WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        #     COMMENT "Assembling M68k test program"
        # )
        # add_custom_target(perfetto_test_asm
        #     DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/perfetto_test_program.h
        # )
        
        add_executable(test_perfetto
            tests/test_perfetto.cpp
        )
        
        target_link_libraries(test_perfetto
            musashi_api
            GTest::gtest_main
        )
        
        # Force fetched gtest headers BEFORE system headers
        if(GTEST_INCLUDES)
            target_include_directories(test_perfetto PRIVATE BEFORE ${GTEST_INCLUDES})
        endif()
        
    endif()
    
    # Build test with vasm-assembled binary
    add_executable(test_vasm_binary
        tests/test_vasm_binary.cpp
    )
    
    target_link_libraries(test_vasm_binary
        musashi_api
        GTest::gtest_main
    )
    
    # Force fetched gtest headers BEFORE system headers
    if(GTEST_INCLUDES)
        target_include_directories(test_vasm_binary PRIVATE BEFORE ${GTEST_INCLUDES})
    endif()
    
    
    # Build example test showing new simplified approach
    add_executable(test_fixture_example
        tests/test_fixture_example.cpp
    )
    
    target_link_libraries(test_fixture_example
        musashi_api
        GTest::gtest_main
    )
    
    # Force fetched gtest headers BEFORE system headers
    if(GTEST_INCLUDES)
        target_include_directories(test_fixture_example PRIVATE BEFORE ${GTEST_INCLUDES})
    endif()
    
    # Build exception handling tests
    add_executable(test_exceptions
        tests/test_exceptions.cpp
    )
    
    target_link_libraries(test_exceptions
        musashi_api
        GTest::gtest_main
    )
    
    # Force fetched gtest headers BEFORE system headers
    if(GTEST_INCLUDES)
        target_include_directories(test_exceptions PRIVATE BEFORE ${GTEST_INCLUDES})
    endif()
    
    
    # Assemble test program with vasm
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/tests/test_program.bin
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/tests
        COMMAND ${CMAKE_SOURCE_DIR}/third_party/vasm/vasmm68k_mot 
                -Fbin 
                -o ${CMAKE_CURRENT_BINARY_DIR}/tests/test_program.bin
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_program.s
        DEPENDS vasm ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_program.s
        COMMENT "Assembling M68k test program with vasm"
    )
    
    add_custom_target(test_program_bin ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/tests/test_program.bin
    )
    
    # Assemble merge sort test program
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/tests/test_mergesort.bin
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/tests
        COMMAND ${CMAKE_SOURCE_DIR}/third_party/vasm/vasmm68k_mot 
                -Fbin 
                -o ${CMAKE_CURRENT_BINARY_DIR}/tests/test_mergesort.bin
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mergesort.s
        DEPENDS vasm ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mergesort.s
        COMMENT "Assembling M68k merge sort program with vasm"
    )
    
    add_custom_target(test_mergesort_bin ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/tests/test_mergesort.bin
    )
    
    # Register tests with CTest
    include(GoogleTest)
    gtest_discover_tests(test_myfunc)
    gtest_discover_tests(test_m68k)
    gtest_discover_tests(test_vasm_binary)
    gtest_discover_tests(test_fixture_example)
    gtest_discover_tests(test_exceptions)
    
    if(ENABLE_PERFETTO)
        gtest_discover_tests(test_perfetto)
    endif()
    
    # Add a custom target to run all tests
    add_custom_target(run_tests
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS test_myfunc test_m68k
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running all tests"
    )
    
    # Build example programs to ensure they compile
    # These are not tests but build verification targets
    add_executable(example_trace
        examples/example_trace.c
    )
    
    # Examples are standalone and only need the core library
    target_link_libraries(example_trace
        musashi_core
    )
    
    # Build Perfetto example only if Perfetto is enabled
    if(ENABLE_PERFETTO)
        # Perfetto example needs to be compiled as C++ due to Perfetto headers
        add_executable(example_perfetto_trace
            examples/example_perfetto_trace.c
        )
        
        set_source_files_properties(examples/example_perfetto_trace.c PROPERTIES LANGUAGE CXX)
        
        # Perfetto example also only needs the core library
        target_link_libraries(example_perfetto_trace
            musashi_core
        )
    endif()
    
    # Add a custom target to build all examples
    add_custom_target(examples
        DEPENDS example_trace
        COMMENT "Building example programs"
    )
    
    if(ENABLE_PERFETTO)
        add_dependencies(examples example_perfetto_trace)
    endif()
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