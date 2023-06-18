# regionlock

[![Version](https://img.shields.io/badge/version-1.0.0-blue.svg?style=flat-square)](https://semver.org)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg?style=flat-square)](https://opensource.org/licenses/MIT)

`regionlock` is a region locking C library based on POSIX locks. It provides an API that can be used to implement robust and efficient inter-process locking of file regions, allowing for fine-grained concurrency control in multi-threaded applications.

## Overview

The `regionlock` library defines several types and functions, which are used to create, manage, and manipulate file locks. The central concept is a region lock, which can be either a read lock or a write lock. A read lock allows concurrent reads but no writes, and a write lock allows exclusive access to a region of a file.

## Key Features

- **Region Locks**: Read and write locks that can be applied to specific regions of a file.
- **Thread Safety**: The library is safe for use by multiple threads concurrently.
- **Efficient Resource Management**: The library uses POSIX locks to efficiently manage resources.
- **Inter-Process Communication**: Enables robust inter-process communication through shared memory objects.

## Key Structures

- **rl_owner**: Represents a lock owner, characterized by a thread id and a descriptor of the physical file.
- **rl_lock**: Represents a lock on a region of a file.
- **rl_file**: Represents a file with multiple locks.
- **rl_descriptor**: Represents a file descriptor that includes a reference to the file and the descriptor of the physical file.

## API

The library provides the following API:

- **rl_initialize**: Initialize the library.
- **rl_open**: Open a file and return a descriptor.
- **rl_close**: Close a descriptor.
- **rl_dup**: Duplicate a descriptor.
- **rl_dup2**: Duplicate a descriptor to a specific descriptor number.
- **rl_fork**: Fork the current process.
- **rl_fcntl**: Perform a command on a file descriptor.
- **rl_util_show_global_state**: Display the global state for debugging purposes.

## Prerequisites

The `regionlock` library requires a POSIX compliant operating system to function correctly. The library makes use of several POSIX-specific features, such as file locks, threads, and shared memory.

## Building

To build `regionlock`, include the `regionlock` header in your C file:

```c
#include "region_lock_library.h"
```

Then, compile your program with `gcc` or a similar compiler:

```bash
gcc -o my_program my_program.c -lpthread
```

Make sure to link the pthread library to enable multithreading support.

## Contributing

Contributions are welcome! Please feel free to submit a pull request.

## License

`regionlock` is released under the MIT License. See the `LICENSE` file for more details.