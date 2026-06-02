cmake_minimum_required(VERSION 3.14)
project(user_locks C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /wd4996)
else()
    add_compile_options(-Wall -Wextra -g)
endif()

add_executable(test_lock
    test_lock.c
    unity/unity.c
)

target_compile_definitions(test_lock PRIVATE LOCK_TESTING)

target_include_directories(test_lock PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/unity
)

# WaitOnAddress / WakeByAddressAll live in synchronization.lib on Windows.
target_link_libraries(test_lock PRIVATE synchronization)

enable_testing()
add_test(NAME lock_tests COMMAND test_lock)
