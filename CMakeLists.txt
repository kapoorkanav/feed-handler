cmake_minimum_required(VERSION 3.16)
project(feed_handler CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

include_directories(include)

# Shared across every version (v1, v2, v3): the wire-format parser, the
# MoldUDP64 header helpers, and the mmap file reader never change as we
# optimize -- only the order book / receiver / threading model do.
add_library(common_lib
    src/common/itch_reader.cpp
)

add_executable(replay_sender src/common/replay_sender.cpp)
target_link_libraries(replay_sender PRIVATE common_lib)

# Control measurement: reads and parses every message but never touches a book,
# giving the fixed cost that both v1 and v3 timings include.
add_executable(bench_parse_only src/common/bench_parse_only.cpp)
target_link_libraries(bench_parse_only PRIVATE common_lib)

# --- v1: naive baseline (std::map book, blocking sockets) ---
add_library(v1_lib
    src/v1/order_book.cpp
)

add_executable(feed_v1 src/v1/main.cpp)
target_link_libraries(feed_v1 PRIVATE common_lib v1_lib)

add_executable(receiver_v1 src/v1/receiver.cpp)
target_link_libraries(receiver_v1 PRIVATE common_lib v1_lib)

# --- v2: io_uring receiver (same book/parser as v1, async batched ingestion) ---
add_executable(receiver_v2 src/v2/receiver.cpp)
target_link_libraries(receiver_v2 PRIVATE common_lib v1_lib uring)

# --- v3: cache-optimized book (price-level array + object pool + intrusive lists) ---
add_library(v3_lib
    src/v3/order_book.cpp
)

add_executable(feed_v3 src/v3/main.cpp)
target_link_libraries(feed_v3 PRIVATE common_lib v3_lib)

# Per-message latency percentiles via rdtsc. x86 only; needs both books.
add_executable(bench_latency src/common/bench_latency.cpp)
target_link_libraries(bench_latency PRIVATE common_lib v1_lib v3_lib)

enable_testing()

add_executable(test_v1_v3_equivalence tests/v3/test_v1_v3_equivalence.cpp)
target_link_libraries(test_v1_v3_equivalence PRIVATE common_lib v1_lib v3_lib)

add_executable(trace_symbol tests/v3/trace_symbol.cpp)
target_link_libraries(trace_symbol PRIVATE common_lib v1_lib v3_lib)

add_executable(test_itch_parse tests/common/test_itch_parse.cpp)

add_executable(test_v1_order_book tests/v1/test_order_book.cpp)
target_link_libraries(test_v1_order_book PRIVATE v1_lib)

add_test(NAME itch_parse COMMAND test_itch_parse)
add_test(NAME v1_order_book COMMAND test_v1_order_book)
