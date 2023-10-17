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
#include "hhs_util.h"
#include "zephyr/init.h"

LOG_MODULE_REGISTER(HHS_BT, CONFIG_APP_LOG_LEVEL);

struct bt_conn *my_conn = NULL;

struct k_event bt_event;
K_EVENT_DEFINE(bt_event);

DEFINE_ENUM(bt_tx_event, BT_EVENT_LIST)

static struct bt_le_adv_param *adv_param = BT_LE_ADV_PARAM(
	(BT_LE_ADV_OPT_CONNECTABLE |
	 BT_LE_ADV_OPT_USE_IDENTITY), /* Connectable advertising and use identity address */
	800,			      /* Min Advertising Interval 500ms (800*0.625ms) */
	3200,			      /* Max Advertising Interval 2000ms (3200*0.625ms) */
	NULL);			      /* Set to NULL for undirected advertising */

#define DEVICE_NAME	CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define STACKSIZE 1024
#define PRIORITY  10

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

bool notify_gas_enabled = false;
static bool is_connect = false;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),

};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_HHS_VAL),
};

static void update_phy(struct bt_conn *conn)
{
	int err;
	const struct bt_conn_le_phy_param preferred_phy = {
		.options = BT_CONN_LE_PHY_OPT_NONE,
		.pref_rx_phy = BT_GAP_LE_PHY_2M,
		.pref_tx_phy = BT_GAP_LE_PHY_2M,
	};
	err = bt_conn_le_phy_update(conn, &preferred_phy);
	if (err) {
		LOG_ERR("bt_conn_le_phy_update() returned %d", err);
	}
}

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("Connection failed (err %u)", err);
		return;
	}

	LOG_INF("Connected");
	is_connect = true;
	my_conn = bt_conn_ref(conn);

	/* Declare a structure to store the connection parameters */
	struct bt_conn_info info;
	err = bt_conn_get_info(conn, &info);
	if (err) {
		LOG_ERR("bt_conn_get_info() returned %d", err);
		return;
	}

	/* Add the connection parameters to your log */
	double connection_interval = info.le.interval * 1.25; // in ms
	uint16_t supervision_timeout = info.le.timeout * 10;  // in ms
	LOG_INF("Connection parameters: interval %.2f ms, latency %d intervals, timeout %d ms",
		connection_interval, info.le.latency, supervision_timeout);

	/* Update the PHY mode */
	update_phy(my_conn);
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected (reason %u)", reason);
	is_connect = false;
	bt_conn_unref(my_conn);
}

void on_le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency,
			 uint16_t timeout)
{
	double connection_interval = interval * 1.25; // in ms
	uint16_t supervision_timeout = timeout * 10;  // in ms
	LOG_INF("Connection parameters updated: interval %.2f ms, latency %d intervals, timeout %d "
		"ms",
		connection_interval, latency, supervision_timeout);
}

/* Write a callback function to inform about updates in the PHY */
void on_le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *param)
{
	// PHY Updated
	if (param->tx_phy == BT_CONN_LE_TX_POWER_PHY_1M) {
		LOG_INF("PHY updated. New PHY: 1M");
	} else if (param->tx_phy == BT_CONN_LE_TX_POWER_PHY_2M) {
		LOG_INF("PHY updated. New PHY: 2M");
	} else if (param->tx_phy == BT_CONN_LE_TX_POWER_PHY_CODED_S8) {
		LOG_INF("PHY updated. New PHY: Long Range");
	}
}

struct bt_conn_cb connection_callbacks = {
	.connected = on_connected,
	.disconnected = on_disconnected,
	/* Add the callback for connection parameter updates */
	.le_param_updated = on_le_param_updated,
	/* Add the callback for PHY mode updates */
	.le_phy_updated = on_le_phy_updated,
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

	err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return err;
	}

	LOG_INF("Advertising successfully started");

	return 0;
}

static int bt_gas_notify(char *sensor_value)
{
	LOG_HEXDUMP_INF(sensor_value, strlen(sensor_value), "tx data");

	return bt_gatt_notify(NULL, &bt_hhs_svc.attrs[4], (void *)sensor_value,
			      strlen(sensor_value));
}

static void bt_thread(void)
{
	static char app_sensor_value[20] = {0};
	k_sleep(K_SECONDS(2));

	while (1) {
		/* Send notification, the function sends notifications only if a client is
		 * subscribed */
		uint32_t events =
			k_event_wait(&bt_event, bt_tx_event_sum, true, K_SECONDS(TIMEOUT_SEC));
		char str[20];
		CODE_IF_ELSE((events == TIMEOUT), sprintf(str, "%s second", xstr(TIMEOUT_SEC)),
			     sprintf(str, "type 0x%02X", events));
		LOG_INF("event : \t%s(%s)", enum_to_str(events), str);

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
