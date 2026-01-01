#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/logging/log.h>
#include <zephyr/init.h>
#include <math.h>
#include "mem_cache.h"
#include <stdlib.h>

LOG_MODULE_REGISTER(sensor_mock, LOG_LEVEL_INF);


#define UINT16_TO_DOUBLE_SCALABLE 0

#if UINT16_TO_DOUBLE_SCALABLE
#define TEMP_MIN  (-60)
#define TEMP_MAX  (120)
#endif /* UINT16_TO_DOUBLE_SCALABLE */


static struct k_timer sample_timer;

/**
 * @brief Convert 16-bit unsigned value to double-precision value.
 * @param u16 Raw 16-bit raw value.
 * @return Converted value as double-precision floating point.
 */
static double uint16_to_double(uint16_t u16)
{
#if UINT16_TO_DOUBLE_SCALABLE
    double normalized = (double)u16 / 65535.0;
    return TEMP_MIN + (normalized * (TEMP_MAX - TEMP_MIN));
#else
/*
double 16 bit:
     S | EEEEE | MMMMMMMMMM
    15 | 14 10 | 9        0
 
double 64 bit:
     S | EEEEEEEEEEE | MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM
    63 | 62       52 | 51                                                 0
*/
    uint16_t sign = (u16 >> 15) & 0x1;  /* Bit 15: sign (0 = positive, 1 = negative) */
    uint16_t exp  = (u16 >> 10) & 0x1F; /* Bits 10–14: exponent (5 bits) */
    uint16_t mant = u16 & 0x03FF;       /* Bits 0–9: mantissa (10 bits) */

    /* Case 1: subnormal numbers (exponent == 0) */
    if (exp == 0) {
        /* Value = fraction × 2^(-24) */
        double value = ldexp((double)mant, -24);
        return sign ? -value : value;
    }

    /* Case 2: infinity or NaN (exponent == 31) */
    if (exp == 31) {
        return mant ? NAN : (sign ? -INFINITY : INFINITY);
    }

    /* Case 3: normalized numbers */
    /* Value = (1 + fraction/1024) × 2^(exp − 15) */
    double value = ldexp(1.0 + (mant / 1024.0), exp - 15);
    return sign ? -value : value;
#endif /* UINT16_TO_DOUBLE_SCALABLE */
}

/**
 * @brief Timer callback for generating mock sensor samples.
 *
 * This handler is called periodically by a Zephyr kernel timer.
 * It generates mock IMU and temperature samples and pushes them
 * into the memory cache.
 *
 * If the cache is full, the generated sample is dropped and
 * a warning is logged.
 *
 * @param timer Pointer to the kernel timer that triggered the callback.
 */
static void sample_timer_handler(struct k_timer *timer)
{
    sensor_sample_t sample;

    /* Generate IMU data */
    for (int i = 0; i < IMU_SAMPLE_LEN; i++) {
        /* Using rand() since sys_rand32_get is not supported by my board */
        sample.imu[i] = rand();
    }

    /* Generate float16 temperature samples */
    for (int i = 0; i < TEMP_SAMPLE_LEN; i++) {
        uint16_t raw = rand();
        sample.temp[i] = uint16_to_double(raw);
    }

    if (!mem_cache_push(&sample)) {
        LOG_WRN("Sample cache full, dropping sample");
    }
}

/**
 * @brief Initialize mock sensor module.
 *
 * This function initializes and starts a periodic kernel timer
 * used to generate mock sensor samples.
 *
 * It is automatically executed during the application initialization
 * phase.
 *
 * @return 0 on successful initialization.
 */
static int sensor_mock_init(void)
{
    srand(k_cycle_get_32());
    k_timer_init(&sample_timer, sample_timer_handler, NULL);
    k_timer_start(&sample_timer,
                  K_SECONDS(CONFIG_SAMPLE_INTERVAL_SEC),
                  K_SECONDS(CONFIG_SAMPLE_INTERVAL_SEC));

    LOG_INF("Sensor mock initialized");
    return 0;
}

SYS_INIT(sensor_mock_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
