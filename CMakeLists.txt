cmake_minimum_required(VERSION 3.16)

project(eventnet_controller_c C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(eventnet_controller
    src/audit.c
    src/command_adapters.c
    src/controller.c
    src/health_probe.c
    src/path_selection.c
    src/apply_plan.c
    src/render_commands.c
    src/state.c
    src/yaml_config.c
    src/strongswan_adapter_mock.c
    src/topology.c
    src/transition.c
    src/vpp_adapter_mock.c
)

target_include_directories(eventnet_controller PUBLIC include)

if(MSVC)
    target_compile_options(eventnet_controller PRIVATE /W4)
else()
    target_compile_options(eventnet_controller PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(eventnet_demo examples/demo.c)
target_link_libraries(eventnet_demo PRIVATE eventnet_controller)

add_executable(eventnet_yaml_demo examples/yaml_demo.c)
target_link_libraries(eventnet_yaml_demo PRIVATE eventnet_controller)

add_executable(eventnet_netns_plan examples/netns_plan.c)
target_link_libraries(eventnet_netns_plan PRIVATE eventnet_controller)

enable_testing()
add_executable(eventnet_tests tests/test_controller.c)
target_link_libraries(eventnet_tests PRIVATE eventnet_controller)
add_test(NAME eventnet_tests COMMAND eventnet_tests)
