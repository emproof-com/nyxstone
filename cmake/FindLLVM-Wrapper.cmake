# This is an INTERFACE target for LLVM, usage:
#   target_link_libraries(${PROJECT_NAME} <PRIVATE|PUBLIC|INTERFACE> LLVM-Wrapper)
# The include directories and compile definitions will be properly handled.

if(LLVM-Wrapper_FOUND OR TARGET LLVM-Wrapper)
    return()
endif()

set(CMAKE_FOLDER_LLVM "${CMAKE_FOLDER}")
if(CMAKE_FOLDER)
    set(CMAKE_FOLDER "${CMAKE_FOLDER}/LLVM")
else()
    set(CMAKE_FOLDER "LLVM")
endif()

# Extract the arguments passed to find_package
# Documentation: https://cmake.org/cmake/help/latest/manual/cmake-developer.7.html#find-modules
list(APPEND FIND_ARGS "${LLVM-Wrapper_FIND_VERSION}")
if(LLVM-Wrapper_FIND_QUIETLY)
    list(APPEND FIND_ARGS "QUIET")
endif()
if(LLVM-Wrapper_FIND_REQUIRED)
    list(APPEND FIND_ARGS "REQUIRED")
endif()

# Whitelist of supported major versions, ordered newest-first. Any minor/patch
# within these majors is accepted (LLVMConfigVersion.cmake requires exact
# major.minor matching, but we resolve the install before invoking
# find_package, so a new 20.2 or 19.3 works without a repo change).
#
# LLVM 21 and 22 are not yet supported: their reworked
# applyFixup/fixupNeedsRelaxationAdvanced APIs are MCFragment-centric and
# need deeper integration than our detached dummy fragment provides (ARM
# Thumb fixups crash inside libLLVM).
set(ALLOWED_LLVM_MAJORS 20 19 18 17 16 15)

# When NYXSTONE_LLVM_PREFIX is set in CMakeLists.txt it pins CMAKE_PREFIX_PATH
# and disables system search paths, so find_package will resolve to the
# user-pinned install. Otherwise probe known per-major install layouts
# newest-first so that, with multiple LLVM versions present on the system, we
# pick the newest supported one rather than whatever happens to be first in
# CMake's default search order.
if(NOT DEFINED LLVM_DIR AND NOT DEFINED ENV{NYXSTONE_LLVM_PREFIX})
    foreach(MAJOR ${ALLOWED_LLVM_MAJORS})
        foreach(CANDIDATE
            "/usr/lib/llvm-${MAJOR}/lib/cmake/llvm"
            "/opt/homebrew/opt/llvm@${MAJOR}/lib/cmake/llvm"
            "/usr/local/opt/llvm@${MAJOR}/lib/cmake/llvm")
            if(EXISTS "${CANDIDATE}/LLVMConfig.cmake")
                set(LLVM_DIR "${CANDIDATE}" CACHE PATH "LLVMConfig.cmake directory")
                break()
            endif()
        endforeach()
        if(DEFINED LLVM_DIR)
            break()
        endif()
    endforeach()
endif()

find_package(LLVM QUIET ${FIND_ARGS} CONFIG)
unset(FIND_ARGS)

if(NOT ${LLVM_FOUND})
    message(FATAL_ERROR "Did not find LLVM. Allowed major versions are ${ALLOWED_LLVM_MAJORS}.")
endif()

if(NOT LLVM_VERSION_MAJOR IN_LIST ALLOWED_LLVM_MAJORS)
    message(FATAL_ERROR
        "Found LLVM ${LLVM_PACKAGE_VERSION}, but major version ${LLVM_VERSION_MAJOR} is not supported. "
        "Supported major versions: ${ALLOWED_LLVM_MAJORS}.")
endif()

if(NOT LLVM-Wrapper_FIND_QUIETLY)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
endif()

# Split the definitions properly (https://weliveindetail.github.io/blog/post/2017/07/17/notes-setup.html)
separate_arguments(LLVM_DEFINITIONS)

# https://github.com/JonathanSalwan/Triton/issues/1082#issuecomment-1030826696
if(LLVM_LINK_LLVM_DYLIB)
    set(LLVM-Wrapper_LIBS LLVM)
elseif(LLVM-Wrapper_FIND_COMPONENTS)
    if(NOT LLVM-Wrapper_FIND_QUIETLY)
        message(STATUS "LLVM components: ${LLVM-Wrapper_FIND_COMPONENTS}")
    endif()
    llvm_map_components_to_libnames(LLVM-Wrapper_LIBS ${LLVM-Wrapper_FIND_COMPONENTS})
else()
    set(LLVM-Wrapper_LIBS ${LLVM_AVAILABLE_LIBS})
endif()

# Some diagnostics (https://stackoverflow.com/a/17666004/1806760)
if(NOT LLVM-Wrapper_FIND_QUIETLY)
    message(STATUS "LLVM libraries: ${LLVM-Wrapper_LIBS}")
    message(STATUS "LLVM includes: ${LLVM_INCLUDE_DIRS}")
    message(STATUS "LLVM definitions: ${LLVM_DEFINITIONS}")
    message(STATUS "LLVM tools: ${LLVM_TOOLS_BINARY_DIR}")
endif()

add_library(LLVM-Wrapper INTERFACE IMPORTED)
target_include_directories(LLVM-Wrapper SYSTEM INTERFACE ${LLVM_INCLUDE_DIRS})
target_link_libraries(LLVM-Wrapper INTERFACE ${LLVM-Wrapper_LIBS})
target_compile_definitions(LLVM-Wrapper INTERFACE ${LLVM_DEFINITIONS})

# Set the appropriate minimum C++ standard
if(LLVM_VERSION VERSION_GREATER_EQUAL "16.0.0")
    # https://releases.llvm.org/16.0.0/docs/CodingStandards.html#c-standard-versions
    target_compile_features(LLVM-Wrapper INTERFACE cxx_std_17)
elseif(LLVM_VERSION VERSION_GREATER_EQUAL "10.0.0")
    # https://releases.llvm.org/10.0.0/docs/CodingStandards.html#c-standard-versions
    target_compile_features(LLVM-Wrapper INTERFACE cxx_std_14)
else()
    # https://releases.llvm.org/9.0.0/docs/CodingStandards.html#c-standard-versions
    target_compile_features(LLVM-Wrapper INTERFACE cxx_std_11)
endif()

if(WIN32)
    target_compile_definitions(LLVM-Wrapper INTERFACE NOMINMAX)

    # This target has an unnecessary diaguids.lib embedded in the installation
    if(TARGET LLVMDebugInfoPDB)
        get_target_property(LLVMDebugInfoPDB_LIBS LLVMDebugInfoPDB INTERFACE_LINK_LIBRARIES)
        foreach(LLVMDebugInfoPDB_LIB ${LLVMDebugInfoPDB_LIBS})
            if(LLVMDebugInfoPDB_LIB MATCHES "diaguids.lib")
                list(REMOVE_ITEM LLVMDebugInfoPDB_LIBS "${LLVMDebugInfoPDB_LIB}")
                break()
            endif()
        endforeach()
        set_target_properties(LLVMDebugInfoPDB PROPERTIES
            INTERFACE_LINK_LIBRARIES "${LLVMDebugInfoPDB_LIBS}"
        )
        unset(LLVMDebugInfoPDB_LIBS)
    endif()
endif()

set(CMAKE_FOLDER "${CMAKE_FOLDER_LLVM}")
unset(CMAKE_FOLDER_LLVM)

set(LLVM-Wrapper_FOUND ON)
