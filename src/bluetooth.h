#ifndef __APP_BT_H__
#define __APP_BT_H__

#include "enum_macro.h"

/** @brief LBS Service UUID. */
#define BT_UUID_HHS_VAL	    BT_UUID_128_ENCODE(0x0000FFF0, 0x0000, 0x1000, 0x8000, 0x00805F9B34FB)
/** @brief Notify Characteristic UUID. */
#define BT_UUID_HHS_GAS_VAL BT_UUID_128_ENCODE(0x0000FFF1, 0x0000, 0x1000, 0x8000, 0x00805F9B34FB)
/** @brief Write Characteristic UUID. */
#define BT_UUID_HHS_LED_VAL BT_UUID_128_ENCODE(0x0000FFF2, 0x0000, 0x1000, 0x8000, 0x00805F9B34FB)

#define BT_UUID_HHS	BT_UUID_DECLARE_128(BT_UUID_HHS_VAL)
#define BT_UUID_HHS_GAS BT_UUID_DECLARE_128(BT_UUID_HHS_GAS_VAL)
#define BT_UUID_HHS_LED BT_UUID_DECLARE_128(BT_UUID_HHS_LED_VAL)

#define EVENT_LIST(X)                                                                              \
	X(GAS_NOTIFY_EN, = 1)                                                                      \
	X(ALARM, )                                                                                 \
	X(GAS_VAL_CHANGE, )

DECLARE_ENUM(bt_tx_event, EVENT_LIST)

extern struct k_event bt_event;

int bt_setup(void);

#endif

