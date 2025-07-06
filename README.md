# POSIX IPC using shared memory and lock-free queues (`posixipc-cpp`)

[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](./LICENSE)
![Maintenance](https://img.shields.io/maintenance/yes/2025)

- POSIX based shared memory queues and utilities
- Single-producer, single-consumer lock-free queue
    - Transaction-like reading directly from shared memory without copying using `dequeue_begin` and `dequeue_commit` functions
- Header-only, C++23, GCC, Clang
- Supports only 64-bit builds, x86, ARM (AArch64)

## Contribute

Pull requests or issues are welcome, see [CONTRIBUTE.md](CONTRIBUTE.md).

## License

Distributed under the MIT license, see [LICENSE](LICENSE).
