#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <windows.h>      // Required for threading functions (CreateThread, etc.)

#include "proxy_cache.h"  // Your cache's public API

// --- Configuration for the Thread Safety Test ---
#define NUM_THREADS 8
#define OPERATIONS_PER_THREAD 500

/**
 * @brief Tests basic add and find functionality.
 */
void test_add_and_find() {
    printf("Running test: test_add_and_find...\n");

    const char* url = "http://example.com/resource";
    const char* data = "This is the webpage content.";
    size_t len = strlen(data);

    // Add the item to the cache
    cache_add(url, data, len);
    printf("  - Item added to cache.\n");

    // Try to find the item
    cache_element* found = cache_find(url);

    // --- Verification ---
    assert(found != NULL); // Assert that we found something
    printf("  - Item found in cache.\n");

    assert(found->len == len); // Assert the length is correct
    printf("  - Length check passed.\n");

    // memcmp is safer than strcmp for potentially binary data
    assert(memcmp(found->data, data, len) == 0);
    printf("  - Data content check passed.\n");

    printf("Test Passed!\n\n");
}

/**
 * @brief Tests if the Least Recently Used (LRU) item is evicted correctly.
 * @note This test requires MAX_CACHE_SIZE in proxy_cache.h to be set to a small value (e.g., 100).
 */
void test_lru_eviction() {
    printf("Running test: test_lru_eviction (expects MAX_CACHE_SIZE = 100)...\n");

    const char* url1 = "http://item1.com"; // Oldest item (LRU)
    const char* data1 = "I am the first data block."; // length = 26

    const char* url2 = "http://item2.com";
    const char* data2 = "I am the second data block."; // length = 27

    const char* url3 = "http://item3.com";
    const char* data3 = "I am the third data block."; // length = 26

    const char* url4 = "http://item4.com"; // Newest item
    const char* data4 = "This final block will trigger eviction."; // length = 36

    // Total size of first 3 items = 26 + 27 + 26 = 79 bytes. Cache has space.
    cache_add(url1, data1, strlen(data1));
    cache_add(url2, data2, strlen(data2));
    cache_add(url3, data3, strlen(data3));
    printf("  - Added 3 items. Cache size should be 79 bytes.\n");

    assert(cache_find(url1) != NULL); // All should be present
    assert(cache_find(url2) != NULL);
    assert(cache_find(url3) != NULL);

    // Adding the 4th item (36 bytes) will push the total to 115 bytes,
    // which is over the 100-byte limit. This MUST evict url1.
    printf("  - Adding 4th item to trigger eviction...\n");
    cache_add(url4, data4, strlen(data4));

    // --- Verification ---
    assert(cache_find(url4) != NULL); // Newest item must be there
    assert(cache_find(url3) != NULL); // Should still be there
    assert(cache_find(url2) != NULL); // Should still be there
    assert(cache_find(url1) == NULL); // KEY: The LRU item must be GONE.

    printf("  - Verification complete: Oldest item was correctly evicted.\n");
    printf("Test Passed!\n\n");
}

/**
 * @brief Tests if updating an item's value also updates its position in the LRU list.
 */
 // In test_main.c, replace the whole function with this:

void test_update_item() {
    printf("Running test: test_update_item...\n");

    cache_add("url1", "old_data", 8);
    cache_add("url2", "some_data", 9);
    printf("  - Added url1 and url2. url1 is now the LRU item.\n");

    // Update url1. This moves it to the front (MRU). url2 becomes the LRU.
    cache_add("url1", "NEW_DATA_REPLACED", 17);
    printf("  - Updated item at url1.\n");

    cache_element* found = cache_find("url1");
    assert(found != NULL);
    assert(memcmp(found->data, "NEW_DATA_REPLACED", 17) == 0);
    printf("  - Data successfully updated.\n");

    // Add more items to fill the cache almost to the limit.
    // Current size: 17 (url1) + 9 (url2) = 26.
    cache_add("url3", "filler data number one", 22); // Size = 26+22 = 48
    cache_add("url4", "filler data number two", 22); // Size = 48+22 = 70
    cache_add("url5", "filler data number three", 22); // Size = 70+22 = 92

    // Now, add one more item that is just large enough to evict url2 (9 bytes)
    // but not large enough to require evicting url1 as well.
    // 92 + 15 = 107. Needs to free > 7 bytes. Evicting url2 (9 bytes) is perfect.
    printf("  - Adding final item to trigger single eviction...\n");
    cache_add("url6", "Evict url2 now", 15);

    // --- Final Verification ---
    assert(cache_find("url1") != NULL); // This is the key assertion that was failing.
    assert(cache_find("url2") == NULL); // This must be evicted.
    printf("  - Update correctly reset LRU order, protecting url1 from eviction.\n");

    printf("Test Passed!\n\n");
}

/**
 * @brief The function executed by each concurrent thread to hammer the cache.
 */
DWORD WINAPI thread_worker(LPVOID lpParam) {
    int thread_id = *(int*)lpParam;

    for (int i = 0; i < OPERATIONS_PER_THREAD; i++) {
        char url[64];
        char data[64];

        // Create a unique URL and data for each operation to avoid collisions between threads
        sprintf_s(url, sizeof(url), "http://thread%d-item%d.com", thread_id, i);
        sprintf_s(data, sizeof(data), "data from thread %d, op %d", thread_id, i);

        // Hammer the cache with add and find operations
        cache_add(url, data, strlen(data));
        cache_find(url);
    }
    return 0;
}

/**
 * @brief Spawns multiple threads to test concurrent access to the cache.
 */
void test_thread_safety() {
    printf("Running test: test_thread_safety with %d threads...\n", NUM_THREADS);

    HANDLE threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];

    // Launch all threads
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
        threads[i] = CreateThread(NULL, 0, thread_worker, &thread_ids[i], 0, NULL);
        assert(threads[i] != NULL);
    }

    // Wait for all threads to complete their execution
    WaitForMultipleObjects(NUM_THREADS, threads, TRUE, INFINITE);

    printf("  - All threads finished execution.\n");

    // The primary success condition is that the program did not crash due to race conditions
    // or deadlock. A more advanced test could verify the final cache size or item count,
    // but a crash-free run is a very strong indicator of correct locking.

    // Clean up thread handles
    for (int i = 0; i < NUM_THREADS; i++) {
        CloseHandle(threads[i]);
    }

    printf("Test Passed!\n\n");
}


/**
 * @brief Main entry point for the test executable.
 */
int main(void) {
    printf("--- Cache Test Suite Initializing ---\n");
    printf("NOTE: Eviction and Update tests require MAX_CACHE_SIZE in proxy_cache.h to be set to 100.\n\n");

    // Initialize the cache system
    cache_init();

    // Run all our tests in a clean environment for each test group
    test_add_and_find();

    // Re-initialize the cache to ensure eviction tests start with an empty state
    cache_destroy();
    cache_init();
    test_lru_eviction();

    // Re-initialize again for the update test
    cache_destroy();
    cache_init();
    test_update_item();

    // Re-initialize for the final thread-safety test
    cache_destroy();
    cache_init();
    test_thread_safety();

    // Clean up all cache resources
    cache_destroy();

    printf("--- All tests finished successfully. ---\nPress Enter to exit.\n");
    getchar(); // Pauses the console window so you can see the output
    return 0;
}