#include <stdio.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include "battery.h"
#include "bluetooth.h"
#include "gas.h"
#include "zephyr/init.h"

LOG_MODULE_REGISTER(HHS_BT, CONFIG_BOARD_HHS_LOG_LEVEL);

struct k_event bt_event;
K_EVENT_DEFINE(bt_event);

DEFINE_ENUM(bt_tx_event, BT_EVENT_LIST)

static struct bt_le_adv_param *adv_param = BT_LE_ADV_PARAM(
	(BT_LE_ADV_OPT_CONNECTABLE |
	 BT_LE_ADV_OPT_USE_IDENTITY), /* Connectable advertising and use identity address */
	160,			      /* Min Advertising Interval 100ms (160*0.625ms) */
	3200,			      /* Max Advertising Interval 2000ms (3200*0.625ms) */
	NULL);			      /* Set to NULL for undirected advertising */

#define DEVICE_NAME	CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define STACKSIZE 1024
#define PRIORITY  10

bool notify_gas_enabled = false;
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
		LOG_ERR("Connection failed (err %u)", err);
		return;
	}

	LOG_INF("Connected");
	is_connect = true;
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected (reason %u)", reason);
	is_connect = false;
}

static void mylbsbc_ccc_gas_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	notify_gas_enabled = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("notify cfg changed %d", notify_gas_enabled);
	if (notify_gas_enabled) {
		k_event_post(&bt_event, GAS_NOTIFY_EN);
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
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return err;
	}
	bt_conn_cb_register(&connection_callbacks);

	LOG_INF("Bluetooth initialized");

	return 0;
}

static int bt_gas_notify(char *sensor_value)
{
	LOG_HEXDUMP_DBG(sensor_value, strlen(sensor_value), "tx data");

	return bt_gatt_notify(NULL, &bt_hhs_svc.attrs[4], (void *)sensor_value,
			      strlen(sensor_value));
}

static void bt_thread(void)
{
	k_sleep(K_SECONDS(2));

	int err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return;
	}

	LOG_INF("Advertising successfully started");

	static char app_sensor_value[20] = {0};

	while (1) {
		/* Send notification, the function sends notifications only if a client is
		 * subscribed */
#define TIMEOUT_SEC 60
		uint32_t events =
			k_event_wait(&bt_event, bt_tx_event_sum, true, K_SECONDS(TIMEOUT_SEC));
		LOG_DBG("event : \t%s(0x%02X) ", enum_to_str(events), events);

		struct gas_sensor_value o2 = get_gas_value(O2);
		struct gas_sensor_value gas = get_gas_value(GAS);
		struct batt_value batt = get_batt_percent();
		sprintf(app_sensor_value, "%d.%d,%d.%d,%d.%d\n", o2.val1, o2.val2, gas.val1,
			gas.val2, batt.val1, batt.val2);

		if (!notify_gas_enabled) {
			continue;
		}

		bt_gas_notify(app_sensor_value);
	}
}

SYS_INIT(bt_setup, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
K_THREAD_DEFINE(bt_thread_id, STACKSIZE, bt_thread, NULL, NULL, NULL, PRIORITY, 0, 0);
