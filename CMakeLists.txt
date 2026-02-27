
cmake_minimum_required(VERSION 3.25)

project(entt_scheduler_demo)

find_package(entt CONFIG REQUIRED)

add_executable(entt_scheduler_demo main.cpp)
target_compile_features(entt_scheduler_demo PRIVATE cxx_std_20)
target_include_directories(entt_scheduler_demo PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(
    entt_scheduler_demo
    PRIVATE
    EnTT::EnTT
)
