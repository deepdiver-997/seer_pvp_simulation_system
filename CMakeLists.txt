cmake_minimum_required(VERSION 3.18)
project(SimulationBattleSystem LANGUAGES CXX)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BUILD_SMOKE_TEST "Build smoke test executable" ON)
option(BUILD_TRAINING_CLI "Build training command-line client" ON)
option(BATTLE_FSM_VERBOSE_DEFAULT "Enable verbose FSM trace by default" OFF)

find_package(Boost REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

set(SIM_CORE_SOURCES
    src/primitives/battle_primitives.cpp
    src/db/official_data_repository.cpp
    src/effects/effect.cpp
    src/effects/effect_meta.cpp
    src/effects/effect_unit.cpp
    src/effects/effect_unit_parser.cpp
    src/effects/pendingEffect.cpp
    src/effects/timed_bucket.cpp
    src/effects/skill_invalid_center.cpp
    src/effects/damage_pipeline.cpp
    src/entities/pet_factory.cpp
    src/entities/numerical-properties.cpp
    src/entities/elemental-attributes.cpp
    src/entities/skills.cpp
    src/entities/soul_mark_manager.cpp
    src/fsm/battleContext.cpp
    src/fsm/battleFsm.cpp
    src/fsm/iControlBlock.cpp
    src/utils/dynamic_library.cpp
)

add_library(sim_core STATIC ${SIM_CORE_SOURCES})
target_include_directories(sim_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(TARGET Boost::headers)
    target_link_libraries(sim_core PUBLIC Boost::headers)
else()
    target_include_directories(sim_core PUBLIC ${Boost_INCLUDE_DIRS})
endif()
target_link_libraries(sim_core
    PUBLIC
        SQLite::SQLite3
        Threads::Threads
        ${CMAKE_DL_LIBS}
)
target_compile_definitions(sim_core PUBLIC BOOST_ERROR_CODE_HEADER_ONLY)

if(BATTLE_FSM_VERBOSE_DEFAULT)
    target_compile_definitions(sim_core PUBLIC BATTLE_FSM_VERBOSE_DEFAULT=1)
endif()

add_executable(sim_server
    src/server/server.cpp
    src/server/main.cpp
)
target_include_directories(sim_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(sim_server PRIVATE sim_core)

if(BUILD_SMOKE_TEST)
    add_executable(sim_smoke_test test/test.cpp)
    target_include_directories(sim_smoke_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(sim_smoke_test PRIVATE sim_core)

    # 场景化对战测试（每场景一个二进制；共用 test/scenario/scenario_harness.h）
    add_executable(sim_scenario_a test/scenario/scenario_a.cpp)
    target_include_directories(sim_scenario_a PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(sim_scenario_a PRIVATE sim_core)

    add_executable(sim_scenario_b test/scenario/scenario_b.cpp)
    target_include_directories(sim_scenario_b PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(sim_scenario_b PRIVATE sim_core)

    add_executable(sim_scenario_c test/scenario/scenario_c.cpp)
    target_include_directories(sim_scenario_c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(sim_scenario_c PRIVATE sim_core)

    add_executable(sim_scenario_d test/scenario/scenario_d.cpp)
    target_include_directories(sim_scenario_d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(sim_scenario_d PRIVATE sim_core)

    add_executable(sim_scenario_e test/scenario/scenario_e.cpp)
    target_include_directories(sim_scenario_e PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(sim_scenario_e PRIVATE sim_core)

    add_executable(sim_scenario_f test/scenario/scenario_f.cpp)
    target_include_directories(sim_scenario_f PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(sim_scenario_f PRIVATE sim_core)

    add_executable(sim_scenario_g test/scenario/scenario_g.cpp)
    target_include_directories(sim_scenario_g PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(sim_scenario_g PRIVATE sim_core)

    add_executable(sim_scenario_h test/scenario/scenario_h.cpp)
    target_include_directories(sim_scenario_h PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(sim_scenario_h PRIVATE sim_core)

    add_executable(sim_scenario_i test/scenario/scenario_i.cpp)
    target_include_directories(sim_scenario_i PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(sim_scenario_i PRIVATE sim_core)

    add_executable(sim_scenario_j test/scenario/scenario_j.cpp)
    target_include_directories(sim_scenario_j PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(sim_scenario_j PRIVATE sim_core)

    add_executable(sim_scenario_k test/scenario/scenario_k.cpp)
    target_include_directories(sim_scenario_k PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(sim_scenario_k PRIVATE sim_core)

    add_executable(sim_scenario_l test/scenario/scenario_l.cpp)
    target_include_directories(sim_scenario_l PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(sim_scenario_l PRIVATE sim_core)

    add_executable(sim_scenario_m test/scenario/scenario_m.cpp)
    target_include_directories(sim_scenario_m PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(sim_scenario_m PRIVATE sim_core)

    add_executable(sim_scenario_n test/scenario/scenario_n.cpp)
    target_include_directories(sim_scenario_n PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(sim_scenario_n PRIVATE sim_core)

    add_executable(sim_scenario_o test/scenario/scenario_o.cpp)
    target_include_directories(sim_scenario_o PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(sim_scenario_o PRIVATE sim_core)

    add_executable(sim_scenario_p test/scenario/scenario_p.cpp)
    target_include_directories(sim_scenario_p PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(sim_scenario_p PRIVATE sim_core)

    add_executable(sim_scenario_q test/scenario/scenario_q.cpp)
    target_include_directories(sim_scenario_q PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(sim_scenario_q PRIVATE sim_core)
    add_executable(sim_scenario_r test/scenario/scenario_r.cpp)
    target_include_directories(sim_scenario_r PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(sim_scenario_r PRIVATE sim_core)
    add_executable(sim_scenario_s test/scenario/scenario_s.cpp)
    target_include_directories(sim_scenario_s PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(sim_scenario_s PRIVATE sim_core)
    add_executable(sim_scenario_t test/scenario/scenario_t.cpp)
    target_include_directories(sim_scenario_t PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(sim_scenario_t PRIVATE sim_core)
endif()

if(BUILD_TRAINING_CLI)
    add_executable(sim_training_cli tools/training_cli.cpp)
    target_include_directories(sim_training_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    if(TARGET Boost::headers)
        target_link_libraries(sim_training_cli PRIVATE Boost::headers)
    else()
        target_include_directories(sim_training_cli PRIVATE ${Boost_INCLUDE_DIRS})
    endif()
    target_compile_definitions(sim_training_cli PRIVATE BOOST_ERROR_CODE_HEADER_ONLY)
    target_link_libraries(sim_training_cli PRIVATE Threads::Threads)
endif()

add_library(moves_plugin SHARED resources/moves_lib/lib_1.cpp)
target_include_directories(moves_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(moves_plugin PROPERTIES
    OUTPUT_NAME "lib_1"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/moves_lib"
)

add_library(soul_plugin SHARED resources/soul_lib/lib_1.cpp)
target_include_directories(soul_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(soul_plugin PROPERTIES
    OUTPUT_NAME "lib_1"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/soul_lib"
)

add_custom_command(TARGET moves_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/resources/moves_lib"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:moves_plugin>" "${CMAKE_CURRENT_SOURCE_DIR}/resources/moves_lib/"
)

add_custom_command(TARGET soul_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/resources/soul_lib"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:soul_plugin>" "${CMAKE_CURRENT_SOURCE_DIR}/resources/soul_lib/"
)
