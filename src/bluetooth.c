#include <stdio.h>
#include <errno.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/drivers/sensor.h>

#include <zephyr/init.h>

#include "version.h"
#include "battery.h"
#include "bluetooth.h"
#include "gas.h"
#include "hhs_util.h"
#include "bme680_app.h"

LOG_MODULE_REGISTER(HHS_BT, CONFIG_APP_LOG_LEVEL);

const unsigned char build_time[] = {BUILD_YEAR_CH0,
				    BUILD_YEAR_CH1,
				    BUILD_YEAR_CH2,
				    BUILD_YEAR_CH3,
				    '-',
				    BUILD_MONTH_CH0,
				    BUILD_MONTH_CH1,
				    '-',
				    BUILD_DAY_CH0,
				    BUILD_DAY_CH1,
				    'T',
				    BUILD_HOUR_CH0,
				    BUILD_HOUR_CH1,
				    ':',
				    BUILD_MIN_CH0,
				    BUILD_MIN_CH1,
				    ':',
				    BUILD_SEC_CH0,
				    BUILD_SEC_CH1,
				    '\0'};

struct bt_conn *my_conn = NULL;
/* Create variable that holds callback for MTU negotiation */
static struct bt_gatt_exchange_params exchange_params;

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
		       /* Create and add the MYSENSOR characteristic and its CCCD  */
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

/* Define the function to update the connection's data length */
static void update_data_length(struct bt_conn *conn)
{
	int err;
	struct bt_conn_le_data_len_param my_data_len = {
#define BT_GAP_DATA_LEN_40 0x28
		.tx_max_len = BT_GAP_DATA_LEN_MAX,
		.tx_max_time = BT_GAP_DATA_TIME_DEFAULT,
	};
	err = bt_conn_le_data_len_update(my_conn, &my_data_len);
	if (err) {
		LOG_ERR("data_len_update failed (err %d)", err);
	}
}

/* Implement callback function for MTU exchange */
static void exchange_func(struct bt_conn *conn, uint8_t att_err,
			  struct bt_gatt_exchange_params *params)
{
	LOG_INF("MTU exchange %s", att_err == 0 ? "successful" : "failed");
	if (!att_err) {
		uint16_t payload_mtu =
			bt_gatt_get_mtu(conn) - 3; // 3 bytes used for Attribute headers.
		LOG_INF("New MTU: %d bytes", payload_mtu);
	}
}

static void update_mtu(struct bt_conn *conn)
{
	int err;
	exchange_params.func = exchange_func;

	err = bt_gatt_exchange_mtu(conn, &exchange_params);
	if (err) {
		LOG_ERR("bt_gatt_exchange_mtu failed (err %d)", err);
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

	/* Update the data length and MTU */
	update_data_length(my_conn);
	update_mtu(my_conn);
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

/* Write a callback function to inform about updates in data length */
void on_le_data_len_updated(struct bt_conn *conn, struct bt_conn_le_data_len_info *info)
{
	uint16_t tx_len = info->tx_max_len;
	uint16_t tx_time = info->tx_max_time;
	uint16_t rx_len = info->rx_max_len;
	uint16_t rx_time = info->rx_max_time;
	LOG_INF("Data length updated. Length %d/%d bytes, time %d/%d us", tx_len, rx_len, tx_time,
		rx_time);
}

struct bt_conn_cb connection_callbacks = {
	.connected = on_connected,
	.disconnected = on_disconnected,
	/* Add the callback for connection parameter updates */
	.le_param_updated = on_le_param_updated,
	/* Add the callback for PHY mode updates */
	.le_phy_updated = on_le_phy_updated,
	/* Add the callback for data length updates */
	.le_data_len_updated = on_le_data_len_updated,
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
	char logstr[20];
	size_t len = strlen(sensor_value);
	sprintf(logstr, "tx data : %d", len);
	LOG_HEXDUMP_INF(sensor_value, strlen(sensor_value), logstr);

	return bt_gatt_notify(NULL, &bt_hhs_svc.attrs[4], (void *)sensor_value, len);
}

static void bt_thread(void)
{
	static char app_sensor_value[60] = {0};
	k_sleep(K_SECONDS(2));

	struct tm tm;
	time_t epoch = 0;
	if (strptime(build_time, "%Y-%m-%dT%X", &tm) != NULL) {
		epoch = mktime(&tm);
	}

	while (1) {
		/* Send notification, the function sends notifications only if a client is
		 * subscribed */
		uint32_t events =
			k_event_wait(&bt_event, bt_tx_event_sum, true, K_SECONDS(TIMEOUT_SEC));
		char str[20];
		CODE_IF_ELSE((events == TIMEOUT), sprintf(str, "%s second", xstr(TIMEOUT_SEC)),
			     sprintf(str, "type 0x%02X", events));
		LOG_INF("event : \t%s(%s)", enum_to_str(events), str);

		if (!notify_gas_enabled) {
			continue;
		}

		struct gas_sensor_value o2 = get_gas_value(O2);
		struct gas_sensor_value gas = get_gas_value(GAS);
		struct batt_value batt = get_batt_percent();
		time_t rawtime = (k_uptime_get() / 1000 + epoch);
		char timestamp[20];
		strftime(timestamp, 20, "%Y-%m-%dT%X", gmtime(&rawtime));

		sprintf(app_sensor_value, "[%s] %d.%d;%d.%d;%d.%d;%d;%d;%d;%d\n", timestamp,
			o2.val1, o2.val2, gas.val1, gas.val2, batt.val1, batt.val2,
			bme680.temp.val1, bme680.press.val1, bme680.humidity.val1, bme680.iaq.val1);

		bt_gas_notify(app_sensor_value);
	}
}

SYS_INIT(bt_setup, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
K_THREAD_DEFINE(bt_thread_id, STACKSIZE, bt_thread, NULL, NULL, NULL, PRIORITY, 0, 0);
