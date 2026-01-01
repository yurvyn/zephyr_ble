#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <string.h>
#include "mem_cache.h"

/* Define a structure to hold the cache and its metadata. */
struct mem_cache_t {
    sensor_sample_t data[CONFIG_CACHE_SIZE];   /* Array to store sensor samples */
    size_t write_idx;                          /* Index of the next head position */
    size_t read_idx;                           /* Index of the next tail position */
    size_t count;                              /* Current count of samples in the cache */
    struct k_mutex lock;                       /* Mutex for thread safety */
};

/* Create the module instance. */
static struct mem_cache_t cache;

/**
 * @brief Push a sample into the FIFO cache.
 *
 * @param sample Pointer to the sensor sample to be added to the cache.
 * @return true if the sample was added successfully, false if the cache is full.
 */
bool mem_cache_push(const sensor_sample_t *sample) 
{
    k_mutex_lock(&cache.lock, K_FOREVER);

    if (cache.count == CONFIG_CACHE_SIZE) {
        k_mutex_unlock(&cache.lock);
        return false;
    }

    memcpy(&cache.data[cache.write_idx], sample, sizeof(sensor_sample_t));
    cache.write_idx = (cache.write_idx + 1) % CONFIG_CACHE_SIZE;
    cache.count++;

    k_mutex_unlock(&cache.lock);
    return true;
}

/**
 * @brief Pop the oldest sample from the cache.
 *
 * @param out Pointer to where the oldest sample will be stored.
 * @return true if a sample was popped successfully, false if the cache is empty.
 */
bool mem_cache_pop(sensor_sample_t *out)
{
    k_mutex_lock(&cache.lock, K_FOREVER);

    if (cache.count == 0) {
        k_mutex_unlock(&cache.lock);
        return false;
    }

    memcpy(out, &cache.data[cache.read_idx], sizeof(sensor_sample_t));
    cache.read_idx = (cache.read_idx + 1) % CONFIG_CACHE_SIZE;
    cache.count--;

    k_mutex_unlock(&cache.lock);
    return true;
}

/**
 * @brief Get the current count of samples in the cache.
 *
 * @return The number of samples currently stored in the cache.
 */
size_t mem_cache_count(void)
{
    k_mutex_lock(&cache.lock, K_FOREVER);
    size_t c = cache.count;
    k_mutex_unlock(&cache.lock);
    return c;
}

/**
 * @brief Initialize the memory cache.
 *
 * This function initializes the mutex and resets the head, tail, and count
 * to zero, preparing the memory cache for use.
 */
static int mem_cache_init(void)
{
    cache.count = 0;
    cache.read_idx = 0;
    cache.write_idx = 0;
    k_mutex_init(&cache.lock);

    return 0;
}

SYS_INIT(mem_cache_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
