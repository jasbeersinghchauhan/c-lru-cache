# C LRU Cache

![Language](https://img.shields.io/badge/language-C-blue.svg)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)

A thread-safe, high-performance, and generic Least Recently Used (LRU) Cache implemented in C. This project is built upon a generic hash map and is designed for applications requiring efficient memory management for frequently accessed data, such as a web proxy server.

The implementation is self-contained and uses standard C libraries, making it highly portable.

---

## Features

* **Thread-Safe**: All public API calls are protected by a mutex, making it safe for use in multithreaded applications.
* **Generic Implementation**: The underlying hash map and cache use `void*` for keys and values, allowing it to store any data type.
* **Customizable Behavior**: The hash map can be configured with user-defined function pointers for hashing, key comparison, and memory deallocation.
* **LRU Eviction Policy**: The cache automatically evicts the least recently used items when its maximum capacity (`MAX_CACHE_SIZE`) is reached.
* **High Performance**: Achieves average **O(1)** time complexity for `add`, `find`, and `update` operations thanks to its hash map backend.

---

## How to Build

This project consists of standard C source files (`.c`) and headers (`.h`) and does not depend on any specific build system.

You can build the code using one of the following methods:

#### 1. Using a C Compiler (GCC/Clang)

You can compile all the source files directly on the command line.

```bash
# Compile the library and the test runner
gcc -o test_cache hashmap.c proxy_cache.c test_main.c -lpthread

# Run the tests
./test_cache

Note: On Windows with MinGW, you may not need the -lpthread flag.

2. In an IDE (like Visual Studio or CLion)

    Create a new empty C/C++ project.

    Add all the source files (hashmap.c, proxy_cache.c, test_main.c) to your project.

    Add the header files (hashmap.h, proxy_cache.h) to your project's include path.

    Build and run the project.

How to Use (API Example)

The API is simple and straightforward.
C

#include <stdio.h>
#include <string.h>
#include "proxy_cache.h"

int main(void) {
    // 1. Initialize the cache system
    cache_init();

    // 2. Add items to the cache
    const char* url1 = "[http://example.com/page1](http://example.com/page1)";
    const char* data1 = "<html>Page 1 Content</html>";
    cache_add(url1, data1, strlen(data1));
    printf("Added '%s' to the cache.\n", url1);

    // 3. Find items in the cache
    cache_element* found_element = cache_find(url1);

    if (found_element) {
        printf("Found '%s'! Data length is %zu.\n", found_element->url, found_element->len);
        // Note: Do not free the returned element. The cache owns it.
    } else {
        printf("Item not found in cache.\n");
    }

    // 4. Clean up all cache resources when done
    cache_destroy();

    return 0;
}

Running Tests
  
The project includes a comprehensive test suite in test_main.c that validates all functionality, including:

    Basic add and find operations.

    Correct LRU eviction.

    Updating existing items.

    Thread safety under heavy concurrent load.

To run the tests, simply compile and execute the test_main.c file as described in the "How to Build" section.
