/**
 * @file proxy_cache.c
 * @brief A thread-safe, high-performance LRU cache implementation.
 *
 * This file implements a Least Recently Used (LRU) cache using a hash map
 * for O(1) lookups and a doubly-linked list to maintain the usage order.
 * It is designed for use in a multithreaded proxy server.
 */

#include "proxy_cache.h"
#include "hashmap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h> // For defensive programming assertions

#ifdef _WIN32
    #include <Windows.h> // For CRITICAL_SECTION on Windows
#else
    #include <pthread.h> // For pthread_mutex_t on POSIX (Linux, macOS)
#endif


 /*=============================================================================
  * 1. Type Definitions & Static Globals
  *===========================================================================*/

  /**
   * @brief Internal state of the cache system.
   */
typedef struct {
	map_t* map;          // Maps URL -> cache_element* for O(1) lookups.
	cache_element* head; // Head of the list (Most Recently Used).
	cache_element* tail; // Tail of the list (Least Recently Used).

	size_t current_size; // Current total size of all data in cache.

	#ifdef _WIN32
        CRITICAL_SECTION mutex; // Mutex for Windows
    #else
        pthread_mutex_t mutex;  // Mutex for POSIX
    #endif
} proxy_cache_t;

/**
 * @brief The single, global instance of the cache.
 */
static proxy_cache_t g_cache;

/*=============================================================================
 * 2. Static Helper Functions (Internal Logic)
 *===========================================================================*/

 /**
  * @brief Detaches a node from the doubly-linked list.
  * @details This function is not thread-safe and must be called from
  * within a locked critical section.
  * @param element The element to detach.
  */

static void detach_node_unlocked(cache_element* element) {
	if (!element)
		return;

	if (element->prev)
		element->prev->next = element->next;
	else
		g_cache.head = element->next;

	if (element->next)
		element->next->prev = element->prev;
	else
		g_cache.tail = element->prev;
}

/**
 * @brief Attaches a node to the front (head) of the list.
 * @details This function is not thread-safe and must be called from
 * within a locked critical section.
 * @param element The element to attach.
 */

static void attach_node_to_head_unlocked(cache_element* element) {
	if (!element)
		return;

	element->next = g_cache.head;
	element->prev = NULL;

	if (g_cache.head) {
		g_cache.head->prev = element;
	}

	g_cache.head = element;

	if (!g_cache.tail) {
		g_cache.tail = element; //First element in the list
	}
}

/**
 * @brief Evicts the least-recently-used element from the cache.
 * @details This function is not thread-safe and must be called from
 * within a locked critical section.
 */

static void remove_lru_element_unlocked() {
	cache_element* lru_element = g_cache.tail;
	if (!lru_element)
		return;  // Cache is empty, nothing to evict

	// 1. Unlink from the list and map.
	detach_node_unlocked(lru_element);
	g_cache.current_size -= lru_element->len;

	// Now, just erase from the map. The map will automatically call 'free' on the key (URL)
	// and 'free_cache_element' on the value, cleaning everything up.
	map_erase(g_cache.map, lru_element->url);
}


/**
 * @brief Custom free function for the hash map to completely destroy a cache element.
 */
static void free_cache_element(void* data) {
	if (!data) return;
	cache_element* element = (cache_element*)data;
	free(element->data); // Free the cached content
	free(element);       // Free the struct itself
}

/*=============================================================================
 * 3. Public API Functions
 *===========================================================================*/

void cache_init() {
	g_cache.head = NULL;
	g_cache.tail = NULL;
	g_cache.current_size = 0;

	#ifdef _WIN32
        InitializeCriticalSection(&g_cache.mutex);
    #else
        pthread_mutex_init(&g_cache.mutex, NULL);
    #endif

	g_cache.map = map_create(1024, 0.75f, NULL, NULL, free, free_cache_element);
	if (g_cache.map == NULL) {
		fprintf(stderr, "Fatal: Failed to initialize proxy cache map.\n\n");
		exit(EXIT_FAILURE);
	}
}


void cache_destroy() {
	#ifdef _WIN32
        EnterCriticalSection(&g_cache.mutex);
    #else
        pthread_mutex_lock(&g_cache.mutex);
    #endif

	// Destroying the map will call 'free' on all URL keys it contains.
	map_destroy(g_cache.map);

	// Reset cache state
	g_cache.head = NULL;
	g_cache.tail = NULL;
	g_cache.current_size = 0;
	g_cache.map = NULL;

	// Now, delete the synchronization object
	#ifdef _WIN32
        LeaveCriticalSection(&g_cache.mutex);
        DeleteCriticalSection(&g_cache.mutex);
    #else
        pthread_mutex_unlock(&g_cache.mutex);
        pthread_mutex_destroy(&g_cache.mutex);
    #endif
}


//Safe lookup
cache_element* cache_find(const char* url) {
	if (!url)
		return NULL;
	
	#ifdef _WIN32
        EnterCriticalSection(&g_cache.mutex);
    #else
        pthread_mutex_lock(&g_cache.mutex);
    #endif

	// 1. Find in map (O(1) average)
	cache_element* element = (cache_element*)map_find(g_cache.map, url);

	if (element) {
		// 2. Found! Move it to the front of the list to mark it as most-recently-used.
		detach_node_unlocked(element);
		attach_node_to_head_unlocked(element);
	}

	// 3. Release lock and return
    #ifdef _WIN32
        LeaveCriticalSection(&g_cache.mutex);
    #else
        pthread_mutex_unlock(&g_cache.mutex);
    #endif
	return element;
}


void cache_add(const char* url, const char* data, size_t length) {
	//Pre-condition checks (fail fast).
	if (url == NULL || data == NULL || length == 0 || length > MAX_CACHE_SIZE) {
		return;
	}

	// Acquire lock to modify the shared cache structure.
	#ifdef _WIN32
        EnterCriticalSection(&g_cache.mutex);
    #else
        pthread_mutex_lock(&g_cache.mutex);
    #endif

	cache_element* existing_element = (cache_element*)map_find(g_cache.map, url);

	// CASE 1: The item already exists. We need to UPDATE it.
	if (existing_element) {
		// Step 1: Account for the change in size BEFORE eviction.
		g_cache.current_size -= existing_element->len;

		// Step 2: Evict other elements if the new data requires more space than is available.
		while (g_cache.current_size + length > MAX_CACHE_SIZE) {
			remove_lru_element_unlocked();
		}

		detach_node_unlocked(existing_element);

		// Step 3: Free the old data, allocate for new data, and update metadata.
		free(existing_element->data);
		existing_element->data = malloc(length);
		if (existing_element->data == NULL) {
			// Severe issue: couldn't allocate. Remove the corrupt element.
			map_erase(g_cache.map, existing_element->url);
			// --- Unlock Mutex ---
			#ifdef _WIN32
				LeaveCriticalSection(&g_cache.mutex);
			#else
				pthread_mutex_unlock(&g_cache.mutex);
			#endif
			return;
		}
		memcpy(existing_element->data, data, length);
		existing_element->len = length;

		// Step 4: Add the updated size back and attach the node to the head (making it MRU).
		g_cache.current_size += length;
		attach_node_to_head_unlocked(existing_element);
	}
	// CASE 2: The item is new. We need to INSERT it.
	else {
		// Step 1: Evict old elements until there is enough space for the new one.
		while (g_cache.current_size + length > MAX_CACHE_SIZE) {
			remove_lru_element_unlocked();
		}
		cache_element* new_element = calloc(1, sizeof(cache_element));

		if (new_element == NULL) {
			// --- Unlock Mutex ---
			#ifdef _WIN32
				LeaveCriticalSection(&g_cache.mutex);
			#else
				pthread_mutex_unlock(&g_cache.mutex);
			#endif
			return;
		}

		new_element->url = strdup(url);
		new_element->data = malloc(length);

		if (!new_element->url || !new_element->data) {
			// Allocation failed, clean up and exit.
			free(new_element->url);
			free(new_element->data);
			free(new_element);
			
			#ifdef _WIN32
				LeaveCriticalSection(&g_cache.mutex);
			#else
				pthread_mutex_unlock(&g_cache.mutex);
			#endif
			return;
		}

		memcpy(new_element->data, data, length); //Copying data from *data to new->element of length size
		new_element->len = length;

		// 2. Add the new element to the map and the front of the list.
		attach_node_to_head_unlocked(new_element);
		map_insert(g_cache.map, new_element->url, new_element);
		g_cache.current_size += length;
	}

	// --- Unlock Mutex ---
    #ifdef _WIN32
        LeaveCriticalSection(&g_cache.mutex);
    #else
        pthread_mutex_unlock(&g_cache.mutex);
    #endif
}
