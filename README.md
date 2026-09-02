# feed-handler

NASDAQ TotalView-ITCH 5.0 feed handler in C++17. Parses the raw exchange
protocol off a UDP multicast feed and reconstructs live limit order books for
every symbol.

Built in three versions so the cost of each design choice can be measured
rather than guessed at.

## Status

Work in progress. Order book and receivers are done and benchmarked for
throughput, latency and packet loss. Write-up is next.

## Architecture

```
  replay_sender                          receiver
  ─────────────                          ────────
  sample.itch50                    ┌──────────────────┐
       │  mmap                     │  UDP multicast   │
       ▼                           │  239.1.1.1:30000 │
  ItchFileReader                   └────────┬─────────┘
       │  one message at a time             │
       ▼                                    ▼
  batch ~35 messages          blocking recvfrom (v1)  /  io_uring (v2, v3)
       │                                    │
       ▼                                    ▼
  MoldUDP64 header                 unwrap MoldUDP64 header
  session | seq | count                     │
       │                                    ▼
       ▼                           sequence gap tracking
    sendto  ──────────────────►    (detect loss, tolerate reordering)
                                            │
                                            ▼
                                  dispatch on message type
                                   A  E  X  D  U
                                            │
                                            ▼
                                  per-symbol order book
                                            │
                                            ▼
                                     top of book
```

The wire format parser, the MoldUDP64 framing and the file reader are shared by
all three versions and live in `common/`. Only the book and the receive path
change between versions, so a measured difference is attributable to one thing.

Inside the v3 book:

```
  price ──► levels_[]                     pool (one flat array, shared)
            ┌──────────────┐              ┌──────────────────────────┐
   bids     │ 0    .. 1023 │   head_idx   │ next | prev | qty | lvl  │
   asks     │ 1024 .. 2047 │ ───────────► │ next | prev | qty | lvl  │
   overflow │ 2048 ..      │              │ ...                      │
            └──────────────┘              └──────────────────────────┘
                                                    ▲
  order ref ──► RefTable  ───────────────────────────┘
                (flat open addressing)
```

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
| v1 | 0.944 s | 0.852 s | 126 ns |
| v3 | 0.637 s | 0.545 s | 80 ns |

1.56x on book work, 1.48x end to end.

v3 is checked against v1 message by message. `test_v1_v3_equivalence` replays
the same file through both books and compares top of book after every update:
6,773,112 comparisons, no mismatches.

### Latency

Per-message book update time, measured with `rdtsc` around the book call only —
the symbol lookup happens before the first timestamp, so it is excluded from
both. Exact percentiles over all 6,773,112 samples, median of 3 runs. The
18.6 ns cost of the two timestamp reads is included in every figure below
rather than subtracted.

| | v1 | v3 | |
| --- | --- | --- | --- |
| p50 | 125.3 ns | 63.7 ns | 1.97x |
| p90 | 199.0 ns | 128.2 ns | 1.55x |
| p99 | 354.4 ns | 292.8 ns | 1.21x |
| p99.9 | 814.0 ns | 660.8 ns | 1.23x |
| p99.99 | 6285 ns | 2191 ns | 2.87x |

Subtracting the timer overhead, the median is 106.7 ns against 45.1 ns, a 2.4x
improvement on real work. v3 is also more repeatable: its p50 came out at
63.7 ns in all three runs, while v1 wandered between 123.9 and 126.7 ns.

Three fixes got the tail there, none of which showed up in throughput at all:

- Growing the price level vector reallocated and copied the whole array.
  Reserving headroom took p99.99 from 18.6 us to 2.2 us.
- The constructor built the level array and *then* reserved, so every symbol
  paid two allocations and a 32 KB copy. Reserving first took p99 from 385 ns
  to 290 ns.
- Scanning for the next best price after a level emptied walked up to 1024
  entries. One bit per level plus `__builtin_ctzll` took p99.9 from 795 ns to
  652 ns.

The bitmap costs some throughput — it adds a set and a clear on every link and
unlink — in exchange for the tail improvement. That tradeoff is why the
throughput figures above are lower than an earlier build's.

`max` is deliberately not reported. It is dominated by scheduler preemption —
the same binary produced 55 us and 496 us on consecutive runs.

### The optimized book dropped packets the slow one didn't

Replaying the feed over multicast at 1M messages/sec, v1 and v2 lost nothing
but v3 lost about 44,000 messages — the version with the *faster* book. Two
guesses at the cause were both wrong. `perf stat` settled it:

| | v2 | v3 |
| --- | --- | --- |
| page faults | 3,164 | 53,330 |
| user time | 1.53 s | 1.07 s |

v3 does 30% less work in user space and takes 17x the page faults. Each symbol's
book eagerly builds a 2048-entry price level array, so the first message for a
symbol faults in 40 KB. Across ~6,000 symbols that is 238 MB faulted in lazily,
mid-feed, and the receive loop stalls while the kernel services it. v1 grows a
`std::map` incrementally and never pays it in one go.

The fix is to construct every symbol's book at startup, before the socket is
armed, which is what real handlers do before market open. Page faults went *up*
(83,231, since it now pre-creates 9,000 books rather than the ~6,000 the file
uses) but they all happen before any packet arrives. Loss went to zero, at the
default 208 KB socket buffer with no sysctl tuning.

The lesson is that the throughput benchmark could not have caught this. A file
replay has no deadline, so a stall just makes it finish later; on a live feed
the same stall drops data.

## Design decisions

**Array of price levels instead of a tree.** Every node of a `std::map` is a
separate heap allocation, so walking it means chasing pointers to unpredictable
addresses and missing cache constantly. An array is a subtraction and an indexed
read, and nearby prices sit next to each other in memory.

**A window of prices, not the whole range.** Order books are sparse. A symbol
has only a handful of active prices but they can sit anywhere in a wide range,
so a dense array over the whole range would be mostly empty. Instead each symbol
gets a window centred on the first price it trades at, and anything outside falls
back to a map. Sub-penny prices go there too, since they cannot be indexed by
cent without colliding.

**32 bit indices instead of pointers.** There are few enough live orders to
address them with a 32 bit number. That keeps the order and price level structs
small enough that several fit in one cache line, which is the entire point. A
`static_assert` fails the build if either struct grows.

**Intrusive lists in a pool.** The next and previous links live inside the order
itself rather than in a separate node wrapping it. That makes each order smaller,
keeps them all in one contiguous block, and means removing an order needs nothing
but the order itself.

**A flat hash table for order lookup.** `std::unordered_map` stores every entry
in its own little heap node, so each lookup chases a pointer and each insert
allocates. Putting keys and values in one array avoids both. Deletion shifts
later entries back instead of leaving tombstones, because this workload deletes
constantly and tombstones would pile up.

**One pool and one table shared by every symbol.** Order references are unique
across the whole market and the number of live orders is small, so giving each
symbol its own would waste a lot of memory.

**Books are built before the socket starts listening.** Each book allocates its
price level array up front, and doing that lazily while the feed is running
stalled the receiver long enough to drop packets. Real handlers allocate
everything before market open for the same reason.

**Sequence tracking remembers which ranges are missing, not just the next
expected number.** A single number cannot tell new data apart from data already
processed, so a packet that merely arrived late gets thrown away as a duplicate.
That never happens with one read in flight, and happens immediately with many.

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
./build/receiver_v3 &
./build/replay_sender data/sample.itch50
```

