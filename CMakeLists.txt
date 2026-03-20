#
# TriX - Deterministic AI Runtime
# Production-grade CMake build system
#
# CRITICAL ITEM 7: Build System
#

cmake_minimum_required(VERSION 3.15)

project(TriX
    VERSION 0.1.0
    DESCRIPTION "TriX - Frozen Computation + Learned Routing = Deterministic AI"
    LANGUAGES C CXX
)

# Project-wide settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(TRIX_BUILD_TESTS "Build unit tests" ON)
option(TRIX_BUILD_TOOLS "Build command-line tools" ON)
option(TRIX_BUILD_EXAMPLES "Build examples" ON)
option(TRIX_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(TRIX_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(TRIX_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(TRIX_ENABLE_COVERAGE "Enable code coverage" OFF)

# Compiler flags
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
        -Wno-unused-function
        -Wno-strict-prototypes
        -Wno-gnu-zero-variadic-macro-arguments
    )
endif()

# Sanitizers
if(TRIX_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

if(TRIX_ENABLE_TSAN)
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
endif()

if(TRIX_ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
endif()

# Code coverage
if(TRIX_ENABLE_COVERAGE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
        add_link_options(--coverage)
    else()
        message(WARNING "Code coverage is only supported with GCC or Clang")
    endif()
endif()

# Platform detection
if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

# Find dependencies
find_package(Threads REQUIRED)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/zor/include)

#═══════════════════════════════════════════════════════════════════════════
# TriX Runtime Library (zor)
#═══════════════════════════════════════════════════════════════════════════

set(TRIX_RUNTIME_SOURCES
    zor/src/errors.c
    zor/src/logging.c
    zor/src/validation.c
    zor/src/thread.c
    zor/src/version.c
    zor/src/runtime.c
    zor/src/metrics.c
    zor/src/hsos.c
    zor/src/hsos_infer.c
)

set(TRIX_RUNTIME_HEADERS
    zor/include/trixc/errors.h
    zor/include/trixc/logging.h
    zor/include/trixc/memory.h
    zor/include/trixc/validation.h
    zor/include/trixc/thread.h
    zor/include/trixc/version.h
    zor/include/trixc/runtime.h
    zor/include/trixc/metrics.h
)

# Static library
add_library(trix_runtime_static STATIC ${TRIX_RUNTIME_SOURCES})
target_include_directories(trix_runtime_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/zor/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(trix_runtime_static PUBLIC Threads::Threads)
if(UNIX)
    target_link_libraries(trix_runtime_static PUBLIC m)
endif()

set_target_properties(trix_runtime_static PROPERTIES
    OUTPUT_NAME trix_runtime
    PUBLIC_HEADER "${TRIX_RUNTIME_HEADERS}"
)

# Shared library (optional)
add_library(trix_runtime_shared SHARED ${TRIX_RUNTIME_SOURCES})
target_include_directories(trix_runtime_shared PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/zor/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(trix_runtime_shared PUBLIC Threads::Threads)
if(UNIX)
    target_link_libraries(trix_runtime_shared PUBLIC m)
endif()

set_target_properties(trix_runtime_shared PROPERTIES
    OUTPUT_NAME trix_runtime
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER "${TRIX_RUNTIME_HEADERS}"
)

# Alias for consistency
add_library(TriX::Runtime ALIAS trix_runtime_static)

#═══════════════════════════════════════════════════════════════════════════
# TriX Tools Library
#═══════════════════════════════════════════════════════════════════════════

if(TRIX_BUILD_TOOLS)
    set(TRIX_TOOLS_SOURCES
        tools/src/softchip.c
        tools/src/codegen.c
        tools/src/linear_forge.c
        tools/src/trix_run.c
    )
    
    set(TRIX_TOOLS_HEADERS
        tools/include/softchip.h
        tools/include/codegen.h
        tools/include/linear_forge.h
    )
    
    add_library(trix_tools STATIC ${TRIX_TOOLS_SOURCES})
    target_include_directories(trix_tools PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tools/include>
        $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(trix_tools PUBLIC TriX::Runtime)
    
    set_target_properties(trix_tools PROPERTIES
        PUBLIC_HEADER "${TRIX_TOOLS_HEADERS}"
    )
    
    # Command-line tool
    add_executable(trix tools/src/trix.c)
    target_link_libraries(trix PRIVATE trix_tools TriX::Runtime)
endif()

#═══════════════════════════════════════════════════════════════════════════
# Unit Tests
#═══════════════════════════════════════════════════════════════════════════

if(TRIX_BUILD_TESTS)
    enable_testing()
    
    # Helper function to add a test
    function(add_trix_test test_name test_source)
        add_executable(${test_name} ${test_source})
        target_link_libraries(${test_name} PRIVATE TriX::Runtime)
        add_test(NAME ${test_name} COMMAND ${test_name})
        
        # Set test properties
        set_tests_properties(${test_name} PROPERTIES
            TIMEOUT 30
            LABELS "unit"
        )
    endfunction()
    
    # Runtime tests - Core functionality
    add_trix_test(test_errors zor/test/test_errors.c)
    add_trix_test(test_memory zor/test/test_memory.c)
    add_trix_test(test_validation zor/test/test_validation.c)
    add_trix_test(test_thread zor/test/test_thread.c)
    add_trix_test(test_version zor/test/test_version.c)
    add_trix_test(test_integration zor/test/test_integration.c)
    add_trix_test(test_runtime zor/test/test_runtime.c)
    add_trix_test(test_hsos_infer zor/test/test_hsos_infer.c)

    # Logging demo
    add_executable(test_logging_demo zor/test/test_logging_demo.c)
    target_link_libraries(test_logging_demo PRIVATE TriX::Runtime)
    
    # Toolchain integration tests (P0 for pilots)
    function(add_toolchain_test test_name test_source)
        add_executable(${test_name} ${test_source})
        target_link_libraries(${test_name} PRIVATE trix_tools TriX::Runtime)
        add_test(NAME ${test_name} COMMAND ${test_name})
        
        set_tests_properties(${test_name} PROPERTIES
            TIMEOUT 60
            LABELS "toolchain"
        )
    endfunction()
    
    add_toolchain_test(test_toolchain_integration tools/test/test_toolchain_integration.c)
    add_toolchain_test(test_parser_security tools/test/test_parser_security.c)
    
    # Custom test target to run all tests
    add_custom_target(check
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS test_errors test_memory test_validation test_thread test_version test_integration test_toolchain_integration test_hsos_infer
        COMMENT "Running all unit tests"
    )
    
    # Coverage report target
    if(TRIX_ENABLE_COVERAGE)
        find_program(LCOV_PATH lcov)
        find_program(GENHTML_PATH genhtml)
        
        if(LCOV_PATH AND GENHTML_PATH)
            add_custom_target(coverage
                COMMAND ${LCOV_PATH} --directory . --zerocounters
                COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
                COMMAND ${LCOV_PATH} --directory . --capture --output-file coverage.info
                COMMAND ${LCOV_PATH} --remove coverage.info '/usr/*' '*/test/*' --output-file coverage.info
                COMMAND ${GENHTML_PATH} coverage.info --output-directory coverage_report
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                DEPENDS test_errors test_memory test_validation test_thread test_version test_integration test_hsos_infer
                COMMENT "Generating code coverage report"
            )
            message(STATUS "Code coverage report target 'coverage' available")
        else()
            message(WARNING "lcov and genhtml required for coverage reports")
        endif()
    endif()
endif()

#═══════════════════════════════════════════════════════════════════════════
# Examples
#═══════════════════════════════════════════════════════════════════════════

if(TRIX_BUILD_EXAMPLES)
    # TODO: Add examples when ready
    # add_subdirectory(examples)
endif()

#═══════════════════════════════════════════════════════════════════════════
# Installation
#═══════════════════════════════════════════════════════════════════════════

include(GNUInstallDirs)

# Install runtime library
install(TARGETS trix_runtime_static trix_runtime_shared
    EXPORT TriXTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/trixc
)

# Install tools
if(TRIX_BUILD_TOOLS)
    install(TARGETS trix_tools
        EXPORT TriXTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/trix
    )
    
    install(TARGETS trix
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# Install CMake config files
install(EXPORT TriXTargets
    FILE TriXTargets.cmake
    NAMESPACE TriX::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TriX
)

# Generate config file
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/TriXConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/TriXConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TriX
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/TriXConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/TriXConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/TriXConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TriX
)

#═══════════════════════════════════════════════════════════════════════════
# Package Configuration
#═══════════════════════════════════════════════════════════════════════════

set(CPACK_PACKAGE_NAME "TriX")
set(CPACK_PACKAGE_VENDOR "TriX Project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Deterministic AI Runtime")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

include(CPack)

#═══════════════════════════════════════════════════════════════════════════
# Summary
#═══════════════════════════════════════════════════════════════════════════

message(STATUS "")
message(STATUS "═══════════════════════════════════════════════════════════")
message(STATUS "  TriX Configuration Summary")
message(STATUS "═══════════════════════════════════════════════════════════")
message(STATUS "  Version:              ${PROJECT_VERSION}")
message(STATUS "  Build type:           ${CMAKE_BUILD_TYPE}")
message(STATUS "  C Compiler:           ${CMAKE_C_COMPILER}")
message(STATUS "  CXX Compiler:         ${CMAKE_CXX_COMPILER}")
message(STATUS "")
message(STATUS "  Build tests:          ${TRIX_BUILD_TESTS}")
message(STATUS "  Build tools:          ${TRIX_BUILD_TOOLS}")
message(STATUS "  Build examples:       ${TRIX_BUILD_EXAMPLES}")
message(STATUS "")
message(STATUS "  AddressSanitizer:     ${TRIX_ENABLE_ASAN}")
message(STATUS "  ThreadSanitizer:      ${TRIX_ENABLE_TSAN}")
message(STATUS "  UBSanitizer:          ${TRIX_ENABLE_UBSAN}")
message(STATUS "  Code Coverage:        ${TRIX_ENABLE_COVERAGE}")
message(STATUS "")
message(STATUS "  Install prefix:       ${CMAKE_INSTALL_PREFIX}")
message(STATUS "═══════════════════════════════════════════════════════════")
message(STATUS "")
