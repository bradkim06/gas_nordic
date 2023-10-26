#ifndef __APP_BT_H__
#define __APP_BT_H__

#include <stdbool.h>

#include "hhs_util.h"

/** @brief LBS Service UUID. */
#define BT_UUID_HHS_VAL     BT_UUID_128_ENCODE(0x0000FFF0, 0x0000, 0x1000, 0x8000, 0x00805F9B34FB)
/** @brief Notify Characteristic UUID. */
#define BT_UUID_HHS_GAS_VAL BT_UUID_128_ENCODE(0x0000FFF1, 0x0000, 0x1000, 0x8000, 0x00805F9B34FB)
/** @brief Write Characteristic UUID. */
#define BT_UUID_HHS_LED_VAL BT_UUID_128_ENCODE(0x0000FFF2, 0x0000, 0x1000, 0x8000, 0x00805F9B34FB)

#define BT_UUID_HHS     BT_UUID_DECLARE_128(BT_UUID_HHS_VAL)
#define BT_UUID_HHS_GAS BT_UUID_DECLARE_128(BT_UUID_HHS_GAS_VAL)
#define BT_UUID_HHS_LED BT_UUID_DECLARE_128(BT_UUID_HHS_LED_VAL)

/** Product : 10sec **/
#define TIMEOUT_SEC 60

#define BT_EVENT_LIST(X)                                                                           \
	X(TIMEOUT, = 0x01)                                                                         \
	X(GAS_NOTIFY_EN, = 0x02)                                                                   \
	X(GAS_VAL_CHANGE, = 0x04)                                                                  \
	X(IAQ_VAL_THRESH, = 0x08)                                                                  \
	X(VOC_VAL_THRESH, = 0x10)                                                                  \
	X(CO2_VAL_THRESH, = 0x20)

DECLARE_ENUM(bt_tx_event, BT_EVENT_LIST)

extern struct k_event bt_event;
extern bool notify_gas_enabled;

int bt_setup(void);

#endif
