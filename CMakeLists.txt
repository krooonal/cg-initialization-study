cmake_minimum_required(VERSION 3.16)
project(cg_initialization_study LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -O3 -DNDEBUG")

# OR-Tools installation directory can be passed via -DORTOOLS_DIR=/path/to/ortools or environment variable
if(NOT ORTOOLS_DIR AND DEFINED ENV{ORTOOLS_DIR})
    set(ORTOOLS_DIR "$ENV{ORTOOLS_DIR}")
endif()

if(ORTOOLS_DIR)
    set(CMAKE_PREFIX_PATH ${ORTOOLS_DIR} ${CMAKE_PREFIX_PATH})
    if(NOT TARGET BZip2::BZip2 AND EXISTS "${ORTOOLS_DIR}/lib/libbz2.so")
        add_library(BZip2::BZip2 SHARED IMPORTED)
        set_target_properties(BZip2::BZip2 PROPERTIES
            IMPORTED_LOCATION ${ORTOOLS_DIR}/lib/libbz2.so
            INTERFACE_INCLUDE_DIRECTORIES ${ORTOOLS_DIR}/include
        )
    endif()
    link_directories(${ORTOOLS_DIR}/lib)
    find_package(ortools REQUIRED PATHS ${ORTOOLS_DIR})
else()
    find_package(ortools REQUIRED)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

set(SOURCES
    src/graph.cc
    src/instance.cc
    src/shortest_path.cc
    src/lp_solver.cc
    src/rmp_model.cc
    src/stats.cc
    src/run_report.cc
    src/column_generation.cc
    src/methods/big_m.cc
    src/methods/two_phase.cc
    src/methods/farkas.cc
    src/main.cpp
)

add_executable(cg_study ${SOURCES})

target_link_libraries(cg_study
    ortools::ortools
)

target_include_directories(cg_study PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_custom_target(run_toy_feasible
    COMMAND ./cg_study instances/toy_feasible.json big_m
    DEPENDS cg_study
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_custom_target(run_toy_infeasible
    COMMAND ./cg_study instances/toy_infeasible.json farkas
    DEPENDS cg_study
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

enable_testing()

add_test(NAME toy_feasible_big_m
    COMMAND ./cg_study instances/toy_feasible.json big_m
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_test(NAME toy_feasible_two_phase
    COMMAND ./cg_study instances/toy_feasible.json two_phase
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_test(NAME toy_feasible_farkas
    COMMAND ./cg_study instances/toy_feasible.json farkas
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_test(NAME toy_infeasible_big_m
    COMMAND ./cg_study instances/toy_infeasible.json big_m
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_test(NAME toy_infeasible_two_phase
    COMMAND ./cg_study instances/toy_infeasible.json two_phase
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_test(NAME toy_infeasible_farkas
    COMMAND ./cg_study instances/toy_infeasible.json farkas
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

find_package(GTest QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

if(NOT TARGET GTest::GTest)
    add_library(GTest::GTest ALIAS gtest)
    add_library(GTest::Main ALIAS gtest_main)
endif()

set(UNIT_TEST_SOURCES
    tests/unit_tests.cc
    src/graph.cc
    src/instance.cc
    src/shortest_path.cc
    src/lp_solver.cc
    src/rmp_model.cc
    src/stats.cc
    src/run_report.cc
    src/column_generation.cc
    src/methods/big_m.cc
    src/methods/two_phase.cc
    src/methods/farkas.cc
)

add_executable(unit_tests ${UNIT_TEST_SOURCES})
target_link_libraries(unit_tests
    ortools::ortools
    GTest::GTest GTest::Main
)
target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME unit_tests COMMAND unit_tests)

set(INTEGRATION_TEST_SOURCES
    tests/integration_tests.cc
    src/graph.cc
    src/instance.cc
    src/shortest_path.cc
    src/lp_solver.cc
    src/rmp_model.cc
    src/stats.cc
    src/run_report.cc
    src/column_generation.cc
    src/methods/big_m.cc
    src/methods/two_phase.cc
    src/methods/farkas.cc
)

add_executable(integration_tests ${INTEGRATION_TEST_SOURCES})
target_link_libraries(integration_tests
    ortools::ortools
    GTest::GTest GTest::Main
)
target_include_directories(integration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME integration_tests COMMAND integration_tests)