#ifndef __APP_SETTINGS_H__
#define __APP_SETTINGS_H__

#include <stdbool.h>

#include "hhs_util.h"

#define DEVICE_LIST(X)                                                                             \
	/* O2 gas sensor*/                                                                         \
	X(O2, = 0)                                                                                 \
	/* optional select gas sensor(H2S, CO, NH3, SO2) are common */                             \
	X(GAS, )
DECLARE_ENUM(gas_device, DEVICE_LIST)

/* Define a list of Bluetooth events with their corresponding values. */
#define CONFIG_EVENT_LIST(X)                                                                       \
	/* event oxygen calibration */                                                             \
	X(OXYGEN_CALIBRATION, = 0x01)
DECLARE_ENUM(config_event, CONFIG_EVENT_LIST)

/*  Voltage(0.1%) = (Currently measured voltage value) / ((1+2000/10.7) * (20.9*0.001*0.001*100)) */
#define DEFAULT_O2_VALUE 2494

extern struct k_condvar config_condvar;
extern struct k_mutex config_mutex;
extern struct k_event config_event;

/**
 * @brief Updates the configuration based on the type of event and the provided value.
 *
 * This function is responsible for updating the configuration settings of a system.
 * It currently handles the oxygen calibration event by updating the calibration value.
 * The function can be extended to handle other types of configuration events.
 *
 * @param type The type of configuration event that is triggering the update.
 *             This is an enumerated type that should list all possible configuration events.
 * @param value The value associated with the event. For oxygen calibration, this is the new
 * calibration value in millivolts.
 * @return Returns true to indicate the update was successful. Currently, it always returns true.
 */
bool update_config(enum config_event type, unsigned int value);

/**
 * @brief Retrieves a configuration value based on the specified event type.
 *
 * This function looks up a configuration value associated with a particular
 * event type. For example, if the event type is OXYGEN_CALIBRATION, it returns
 * the millivolt value for oxygen calibration. The function currently supports
 * a limited set of events, and can be expanded to include more configuration
 * types as needed.
 *
 * @param type The type of configuration event for which the value is requested.
 *             This is an enumerated type that specifies the particular
 *             configuration value to retrieve.
 * @return The configuration value associated with the given event type. If the
 *         event type is not supported, the function returns 0.
 */
unsigned int get_config(enum config_event type);

#endif
