cmake_minimum_required(VERSION 3.4.3)
project(nyxstone VERSION 0.1.0)

find_package(LLVM 15 CONFIG PATHS $ENV{NYXSTONE_LLVM_PREFIX} NO_DEFAULT_PATH)
find_package(LLVM 15 CONFIG)

find_package(Boost 1.40 COMPONENTS program_options)
include_directories(${Boost_INCLUDE_DIR})

include_directories(${LLVM_INCLUDE_DIRS} include/ src/)
add_definitions(${LLVM_DEFINITIONS})

llvm_map_components_to_libnames(llvm_libs core mc AllTargetsCodeGens AllTargetsAsmParsers AllTargetsDescs AllTargetsDisassemblers AllTargetsInfos AllTargetsMCAs)

file(GLOB srcs
  "src/*.hpp"
  "src/*.cpp"
)

# add_compile_options(-fsanitize=address,undefined)
# add_link_options(-fsanitize=address,undefined)

add_library(nyxstone ${srcs})

add_executable(nstone examples/nyxstone-cli.cpp)
add_executable(sample examples/sample.cpp)

target_link_libraries(nstone nyxstone ${llvm_libs} ${Boost_LIBRARIES})
target_link_libraries(sample nyxstone ${llvm_libs})
