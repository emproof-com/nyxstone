cmake_minimum_required(VERSION 3.15)

project(nyxstone VERSION 0.1.0)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-tree builds are not supported. Run CMake from a separate directory: cmake -B build")
endif()

set(PROJECT_IS_TOP_LEVEL OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(PROJECT_IS_TOP_LEVEL ON)

    # Enable folder support
    set_property(GLOBAL PROPERTY USE_FOLDERS ON) 
endif()

option(NYXSTONE_BUILD_EXAMPLES "Build nyxstone examples" ${PROJECT_IS_TOP_LEVEL})
option(NYXSTONE_SANITIZERS "Enable sanitizers" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(DEFINED $ENV{NYXSTONE_LLVM_PREFIX})
    # Ignore LLVM_ROOT variables
    set(CMAKE_FIND_USE_PACKAGE_ROOT_PATH OFF)
    set(CMAKE_PREFIX_PATH $ENV{NYXSTONE_LLVM_PREFIX})
endif()

find_package(LLVM-Wrapper REQUIRED COMPONENTS
    core
    mc
    AllTargetsCodeGens
    AllTargetsAsmParsers
    AllTargetsDescs
    AllTargetsDisassemblers
    AllTargetsInfos
    AllTargetsMCAs
)

file(GLOB_RECURSE nyxstone_SOURCES
    "src/*.cpp"
    "src/*.h"
    "include/*.h"
    "include/*.hpp"
)

if(NYXSTONE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

add_library(nyxstone ${nyxstone_SOURCES})
add_library(nyxstone::nyxstone ALIAS nyxstone)

target_compile_features(nyxstone PUBLIC
    cxx_std_17
)
target_include_directories(nyxstone PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)
target_link_libraries(nyxstone PUBLIC
    LLVM-Wrapper
)
set_target_properties(nyxstone PROPERTIES
    COMPILE_PDB_NAME nyxstone-lib.pdb
)

if(NYXSTONE_BUILD_EXAMPLES)
    # Use -DBOOST_ROOT=C:/boost_1_80_0 to specify the boost root directory
    if(WIN32)
        set(Boost_USE_STATIC_LIBS ON) # only find static libs
        set(Boost_USE_MULTITHREADED ON)
        set(Boost_USE_STATIC_RUNTIME OFF)
    endif()
    find_package(Boost 1.40 COMPONENTS program_options REQUIRED)

    add_executable(nyxstone-bin examples/nyxstone-cli.cpp)
    set_target_properties(nyxstone-bin PROPERTIES
        OUTPUT_NAME nyxstone
    )
    target_link_libraries(nyxstone-bin PRIVATE
        nyxstone::nyxstone
        Boost::program_options
    )

    add_executable(example examples/example.cpp)
    target_link_libraries(example PRIVATE
        nyxstone::nyxstone
    )

    include(CTest)
    add_test(NAME TestExample COMMAND $<TARGET_FILE:example>)
endif()
