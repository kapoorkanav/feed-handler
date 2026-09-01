# feed-handler

NASDAQ TotalView-ITCH 5.0 feed handler in C++17. Parses the raw exchange
protocol off a UDP multicast feed and reconstructs live limit order books for
every symbol.

Built in three versions so the cost of each design choice can be measured
rather than guessed at.

## Status

Work in progress. The order book is done, benchmarked for throughput and
latency. Wiring the v3 book into the io_uring receiver is next.

## Versions

- **v1** — baseline. Blocking sockets, `std::map` price levels, `std::list` for
  orders at a level, `std::unordered_map` for order lookup.
- **v2** — io_uring receiver with batched submission and completion draining.
  Same book as v1.
- **v3** — price-level array indexed by cent offset, intrusive linked lists
  inside a preallocated object pool, and a flat open-addressing hash table for
  order lookup.

## Results

Replaying 7,042,255 real messages from a NASDAQ sample file, single threaded,
median of 5 runs. Book work is isolated by subtracting a parse-only baseline
(0.092 s) that reads and decodes every message but touches no book.

Azure F2s_v2, Intel Xeon Platinum 8370C (Ice Lake) @ 2.8 GHz, Ubuntu 24.04,
gcc 13, -O2.

| | total | book only | per message |
| --- | --- | --- | --- |
| v1 | 0.942 s | 0.850 s | 126 ns |
| v3 | 0.574 s | 0.482 s | 71 ns |

1.76x on book work, 1.64x end to end.

v3 is checked against v1 message by message. `test_v1_v3_equivalence` replays
the same file through both books and compares top of book after every update:
6,773,112 comparisons, no mismatches.

### Latency

Per-message book update time, measured with `rdtsc` around the book call only —
the symbol lookup happens before the first timestamp, so it is excluded from
both. Exact percentiles over all 6,773,112 samples. The 17.9 ns cost of the two
timestamp reads is included in every figure below rather than subtracted.

| | v1 | v3 |
| --- | --- | --- |
| p50 | 124.6 ns | 67.3 ns |
| p90 | 201.2 ns | 143.2 ns |
| p99 | 368.0 ns | 384.5 ns |
| p99.9 | 853.4 ns | 651.5 ns |
| p99.99 | 6298 ns | 2393 ns |

v3 wins everywhere except p99, where the two are level. The likely reason is
the overflow path: prices outside the 1024 cent window fall back to a
`std::map`, which is exactly v1's structure, and that is about 6% of orders.

Getting here took two fixes that were invisible in throughput and only showed
up in the tail. Growing the price level vector reallocated and copied the whole
array, costing 18 us at p99.99; reserving headroom cut that to 2.2 us. Scanning
for the next best price after a level emptied walked up to 1024 entries;
replacing it with one bit per level and `__builtin_ctzll` took p99.9 from
795 ns to 652 ns.

`max` is deliberately not reported. It is dominated by scheduler preemption —
the same binary produced 55 us and 496 us on consecutive runs.

## Building

Linux, needs `liburing-dev`.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cd build && ctest
```

## Sample data

Not in the repo. Pull a chunk of a public NASDAQ file:

```
mkdir -p data
curl -r 0-83886079 "https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/07302019.NASDAQ_ITCH50.gz" -o /tmp/s.gz
gzip -dc /tmp/s.gz > data/sample.itch50 2>/dev/null || true
```

gzip will complain about a truncated stream. That is expected, we only want the
first 80 MB.

Then:

```
./build/feed_v1 data/sample.itch50
./build/feed_v3 data/sample.itch50
```

For the multicast path, run the receiver and the replay sender together:

```
./build/receiver_v2 &
./build/replay_sender data/sample.itch50
```

