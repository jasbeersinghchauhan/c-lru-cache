#ifndef PROXY_CACHE_H
#define PROXY_CACHE_H

#include <stddef.h>

/*=============================================================================
 * 1. Constants
 *===========================================================================*/

#define MAX_CACHE_SIZE 100 // 10 MiB: The total maximum size of all objects in the cache.
//10485760
 /*=============================================================================
  * 2. Public Data Structures
  *===========================================================================*/

  /**
   * @brief Represents a single element in the cache.
   * @details This is an opaque handle returned by cache_find(). The user should
   * not modify its contents directly.
   */
typedef struct cache_element {
    char* url;
    char* data;
    size_t len;
    struct cache_element* next;
    struct cache_element* prev;
} cache_element;

/*=============================================================================
 * 3. Public API Functions
 *===========================================================================*/

 /**
  * @brief Initializes the cache system. Must be called once at startup.
  */
void cache_init();

/**
 * @brief Frees all memory used by the cache. Must be called once at shutdown.
 */
void cache_destroy();

/**
 * @brief Finds an element in the cache by its URL.
 *
 * @details This operation is thread-safe and runs in O(1) average time.
 * If the element is found, it is automatically marked as the
 * most-recently-used.
 *
 * @param url The URL to search for (null-terminated string).
 * @return A read-only pointer to the cache_element if found, or NULL otherwise.
 * NOTE: This is an internal pointer. Do NOT free or modify it.
 * The data is valid until it is evicted by the cache.
 */
cache_element* cache_find(const char* url);

/**
 * @brief Adds a new data object to the cache.
 *
 * @details This operation is thread-safe and runs in O(1) average time.
 * If the cache is full, it will evict the least-recently-used
 * element(s) to make space. The function copies the data into
 * an internal buffer.
 *
 * @param url The URL of the object (acts as the key).
 * @param data A pointer to the data to be cached.
 * @param length The size of the data in bytes.
 */
void cache_add(const char* url, const char* data, size_t length);

#endif