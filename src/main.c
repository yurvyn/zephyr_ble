#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>

#include "mem_cache.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);


/******************************************************************************
 * Data Types
 ******************************************************************************/

/* Device connection states for the main loop state machine */
enum {
    STATE_DISCONNECTED,
    STATE_CONNECTED,
    STATE_MAX,
};

typedef struct {
	struct bt_conn *current_conn;  	/* Active BLE connection handle */
	struct k_timer tx_timer;	   	/* Periodic transmit timer */
	bool notify_enabled;			/* Notification enable flag */
	atomic_t state;					/* Atomic application state bitmap */
} app_data_t;


/******************************************************************************
 * Macro
 ******************************************************************************/

/* Custom 128-bit UUID for the Sensor Service */
#define BT_UUID_SENSOR_SERVICE \
    BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0xf0debc9a, 0x7856, 0x3412, 0x7856, 0x341278563412))

/* Custom 128-bit UUID for the Sensor Data Characteristic (Notify) */
#define BT_UUID_SENSOR_DATA \
    BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0xf1debc9a, 0x7856, 0x3412, 0x7856, 0x341278563412))

/* Custom 128-bit UUID for the Sample Count Characteristic (Read) */
#define BT_UUID_SAMPLE_COUNT \
    BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0xf2debc9a, 0x7856, 0x3412, 0x7856, 0x341278563412))


/******************************************************************************
 * Static Variables
 ******************************************************************************/

/* Application data */
static app_data_t app_data;

/* Advertising data packets */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* Scan response data packets */
static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};


/******************************************************************************
 * GATT Callbacks
 ******************************************************************************/

/**
 * @brief Read callback for the Sample Count characteristic.
 * 
 * @param conn   The connection object.
 * @param attr   The attribute being read.
 * @param buf    Buffer to store the read data.
 * @param len    Length of the buffer.
 * @param offset Read offset.
 * @return Number of bytes read or GATT error code.
 */
static ssize_t read_sample_count(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                 void *buf, uint16_t len, uint16_t offset)
{
    uint32_t count = mem_cache_count();
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &count, sizeof(count));
}

/**
 * @brief Client Configuration Characteristic (CCC) change callback.
 * 
 * @param attr  The CCC attribute.
 * @param value The configuration value (Notify vs Disabled).
 */
static void ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    app_data.notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Notifications %s", app_data.notify_enabled ? "enabled" : "disabled");
}

/* GATT Service Definition */
BT_GATT_SERVICE_DEFINE(sensor_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_SENSOR_SERVICE),

    BT_GATT_CHARACTERISTIC(BT_UUID_SENSOR_DATA,
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE,
                           NULL, NULL, NULL),

    BT_GATT_CCC(ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

    BT_GATT_CHARACTERISTIC(BT_UUID_SAMPLE_COUNT,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           read_sample_count, NULL, NULL),
);


/******************************************************************************
 * TX and MTU exchange handling
 ******************************************************************************/

/**
 * @brief Periodic timer handler for transmitting sensor data.
 * 
 * Pops a sample from the memory cache and sends a GATT notification 
 * if a connection is active and notifications are enabled.
 * 
 * @param timer Pointer to the kernel timer.
 */
static void tx_timer_handler(struct k_timer *timer)
{
    if (!app_data.current_conn || !app_data.notify_enabled) {
        return;
    }

    sensor_sample_t sample;
    if (!mem_cache_pop(&sample)) {
        return;
    }

    /* Attributes index 1 points to the SENSOR_DATA characteristic */
    int err = bt_gatt_notify(app_data.current_conn, &sensor_svc.attrs[1], &sample, sizeof(sample));
    if (err) {
        LOG_WRN("Notify failed (err %d), re-pushing sample to cache", err);
        mem_cache_push(&sample);
    }
}

/**
 * @brief Callback for GATT MTU exchange completion.
 * 
 * @param conn   The connection object.
 * @param err    Error status of the exchange (0 for success).
 * @param params Exchange parameters.
 */
static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_exchange_params *params)
{
    LOG_INF("MTU exchange %s, current MTU: %u", 
            err == 0U ? "successful" : "failed", 
            bt_gatt_get_mtu(conn));
}

/* MTU exchange data */
static struct bt_gatt_exchange_params mtu_exchange_params = {
    .func = mtu_exchange_cb
};


/******************************************************************************
 * Connection Callbacks
 ******************************************************************************/

/**
 * @brief Connection established callback.
 * 
 * @param conn The connection object.
 * @param err  HCI error code (0 for success).
 */
static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("Connection failed (err 0x%02x)", err);
    } else {
        app_data.current_conn = bt_conn_ref(conn);
        atomic_set_bit(&app_data.state, STATE_CONNECTED);
        
        /* Initiate MTU exchange to optimize packet size */
        bt_gatt_exchange_mtu(conn, &mtu_exchange_params);
        LOG_INF("Connected");
    }
}

/**
 * @brief Connection disconnected callback.
 * 
 * @param conn   The connection object.
 * @param reason HCI disconnect reason code.
 */
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    if (app_data.current_conn) {
        bt_conn_unref(app_data.current_conn);
        app_data.current_conn = NULL;
    }
    atomic_set_bit(&app_data.state, STATE_DISCONNECTED);
    LOG_INF("Disconnected (reason 0x%02x)", reason);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};


/******************************************************************************
 * Initialization
 ******************************************************************************/

/**
 * @brief Initializes the BLE service timer and intervals.
 */
void ble_service_init(void)
{
    k_timer_init(&app_data.tx_timer, tx_timer_handler, NULL);
    k_timer_start(&app_data.tx_timer, 
                  K_SECONDS(CONFIG_TRANSMIT_INTERVAL_SEC), 
                  K_SECONDS(CONFIG_TRANSMIT_INTERVAL_SEC));

    LOG_INF("BLE service initialized");
}

/**
 * @brief Application entry point.
 * 
 * Initializes Bluetooth, starts the advertising state machine, 
 * and maintains the main application loop.
 */
int main(void)
{
	int err;
    LOG_INF("Starting BLE Sensor Application");

    ble_service_init();
    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth enable failed (err %d)", err);
        return 0;
    }

    atomic_set_bit(&app_data.state, STATE_DISCONNECTED);

    while (1) {
        k_sleep(K_SECONDS(1));

        if (atomic_test_and_clear_bit(&app_data.state, STATE_CONNECTED)) {
            /* Connected handler */
        } 
        else if (atomic_test_and_clear_bit(&app_data.state, STATE_DISCONNECTED)) {
            /* Disconnected handler */
            LOG_INF("Starting Advertising...");
            err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
            if (err) {
                LOG_ERR("Advertising failed to start (err %d)", err);
                return 0;
            }
        }
    }
}
