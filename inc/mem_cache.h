#pragma once
#include <stdbool.h>
#include <stddef.h>

#define IMU_SAMPLE_LEN 20
#define TEMP_SAMPLE_LEN 3


// REVIEW: why is the __attribute__((packed)) here? what are the benefits of doing that?
typedef struct __attribute__((packed)) {
    uint32_t imu[IMU_SAMPLE_LEN];
    double temp[TEMP_SAMPLE_LEN];
} sensor_sample_t;

/**
 * @brief Push a sample into the FIFO cache.
 *
 * @param sample Pointer to the sensor sample to be added to the cache.
 * @return true if the sample was added successfully, false if the cache is full.
 */
bool mem_cache_push(const sensor_sample_t *sample);

/**
 * @brief Pop the oldest sample from the cache.
 *
 * @param out Pointer to where the oldest sample will be stored.
 * @return true if a sample was popped successfully, false if the cache is empty.
 */
bool mem_cache_pop(sensor_sample_t *out);

/**
 * @brief Get the current count of samples in the cache.
 *
 * @return The number of samples currently stored in the cache.
 */
size_t mem_cache_count(void);
