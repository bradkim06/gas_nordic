#include <stdio.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include "bluetooth.h"
#include "zephyr/init.h"

LOG_MODULE_REGISTER(HHS_BT);

struct k_sem bt_sem;
K_SEM_DEFINE(bt_sem, 0, 1);

static struct bt_le_adv_param *adv_param = BT_LE_ADV_PARAM(
	(BT_LE_ADV_OPT_CONNECTABLE |
	 BT_LE_ADV_OPT_USE_IDENTITY), /* Connectable advertising and use identity address */
	32,			      /* Min Advertising Interval 20ms (32*0.625ms) */
	3200,			      /* Max Advertising Interval 2000ms (3200*0.625ms) */
	NULL);			      /* Set to NULL for undirected advertising */

#define DEVICE_NAME	CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define STACKSIZE 1024
#define PRIORITY  7

#define NOTIFY_INTERVAL 2000

static bool notify_gas_enabled = false;
static bool is_connect = false;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),

};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_HHS_VAL),
};

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_INF("Connection failed (err %u)\n", err);
		return;
	}

	LOG_INF("Connected\n");
	is_connect = true;
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected (reason %u)\n", reason);
	is_connect = false;
}

static void mylbsbc_ccc_gas_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	notify_gas_enabled = (value == BT_GATT_CCC_NOTIFY);
	if (notify_gas_enabled) {
		k_sem_give(&bt_sem);
	}
}

/* Service Declaration */
BT_GATT_SERVICE_DEFINE(bt_hhs_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_HHS),
		       BT_GATT_CHARACTERISTIC(BT_UUID_HHS_LED, BT_GATT_CHRC_WRITE,
					      BT_GATT_PERM_WRITE, NULL, NULL, NULL),
		       /* STEP 12 - Create and add the MYSENSOR characteristic and its CCCD  */
		       BT_GATT_CHARACTERISTIC(BT_UUID_HHS_GAS, BT_GATT_CHRC_NOTIFY,
					      BT_GATT_PERM_NONE, NULL, NULL, NULL),
		       BT_GATT_CCC(mylbsbc_ccc_gas_cfg_changed,
				   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

);

struct bt_conn_cb connection_callbacks = {
	.connected = on_connected,
	.disconnected = on_disconnected,
};

int bt_setup(void)
{
	int err;

	err = bt_enable(NULL);
	if (err) {
		LOG_INF("Bluetooth init failed (err %d)\n", err);
		return err;
	}
	bt_conn_cb_register(&connection_callbacks);

	LOG_INF("Bluetooth initialized\n");

	err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		LOG_INF("Advertising failed to start (err %d)\n", err);
		return err;
	}

	LOG_INF("Advertising successfully started\n");

	k_sem_init(&bt_sem, 0, 1);

	return 0;
}

static int bt_hhs_gas_notify(char *sensor_value)
{
	if (!notify_gas_enabled) {
		return -EACCES;
	}

	LOG_HEXDUMP_INF(sensor_value, strlen(sensor_value), "tx data");

	return bt_gatt_notify(NULL, &bt_hhs_svc.attrs[4], (void *)sensor_value,
			      strlen(sensor_value));
}

static void send_data_thread(void)
{
	static char app_sensor_value[11] = {0};
	sprintf(app_sensor_value, "%d\n", 12345678);

	while (1) {
		/* Send notification, the function sends notifications only if a client is
		 * subscribed */
		if (k_sem_take(&bt_sem, K_SECONDS(61)) != 0) {
			LOG_ERR("semaphore timeout");
		} else {
			/* fetch available data */
			LOG_INF("tx ble");
			bt_hhs_gas_notify(app_sensor_value);
		}
	}
}

SYS_INIT(bt_setup, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
K_THREAD_DEFINE(send_data_thread_id, STACKSIZE, send_data_thread, NULL, NULL, NULL, PRIORITY, 0, 0);
