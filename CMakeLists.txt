cmake_minimum_required(VERSION 3.16)
project(feed_handler CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

include_directories(include)

# Shared library: everything main_naive and the tests both need,
# so we compile order_book.cpp/itch_reader.cpp exactly once.
add_library(feedhandler_core
    src/order_book.cpp
    src/itch_reader.cpp
)

add_executable(feed_naive src/main_naive.cpp)
target_link_libraries(feed_naive PRIVATE feedhandler_core)

add_executable(replay_sender src/replay_sender.cpp)
target_link_libraries(replay_sender PRIVATE feedhandler_core)

add_executable(feed_receiver src/feed_receiver.cpp)
target_link_libraries(feed_receiver PRIVATE feedhandler_core)

enable_testing()

add_executable(test_itch_parse tests/test_itch_parse.cpp)

add_executable(test_order_book tests/test_order_book.cpp)
target_link_libraries(test_order_book PRIVATE feedhandler_core)

add_test(NAME itch_parse COMMAND test_itch_parse)
add_test(NAME order_book COMMAND test_order_book)
