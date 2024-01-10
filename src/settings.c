#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <errno.h>

#include "settings.h"

LOG_MODULE_REGISTER(config, CONFIG_APP_LOG_LEVEL);

#define FAIL_MSG "fail (err %d)"

/* Definitions used to store and retrieve BSEC state from the settings API */
#define SETTINGS_NAME_CONF    "config"
#define SETTINGS_KEY_OXYGEN   "oxygen"
#define SETTINGS_OXYGEN_VALUE SETTINGS_NAME_CONF "/" SETTINGS_KEY_OXYGEN

struct k_condvar config_condvar;
struct k_mutex config_mutex;
struct k_event config_event;

static unsigned int oxygen_mV = DEFAULT_O2_VALUE;

/**
 * @brief Sets the configuration for a given setting based on its name.
 *
 * This function is responsible for setting the configuration of a particular setting
 * identified by its name. It uses a callback function to read the necessary data.
 * In this example, it specifically handles the setting for "oxygen" level.
 *
 * @param name The name of the setting to be configured.
 * @param len The length of the data to be read for the setting.
 * @param read_cb The callback function used to read the setting's data.
 * @param cb_arg A pointer to user data that will be passed to the callback function.
 * @return 0 on success, -EINVAL if the length of the data does not match the expected size,
 *         -ENOENT if the setting name does not match any known setting, or a negative error
 *         code from the callback function on other failures.
 */
static int config_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	// Pointer to the next character in the setting name after a match is found.
	const char *next;
	// Return code from the callback function.
	int rc;

	// Check if the setting name matches "oxygen" and that there are no additional characters
	// after the match.
	if (settings_name_steq(name, SETTINGS_KEY_OXYGEN, &next) && !next) {
		// Verify that the length of the data is equal to the expected size for the oxygen
		// setting.
		if (len != sizeof(oxygen_mV)) {
			return -EINVAL; // Return error if the length does not match.
		}

		// Use the callback function to read the data for the oxygen setting.
		rc = read_cb(cb_arg, &oxygen_mV, sizeof(oxygen_mV));
		if (rc >= 0) {
			return 0; // Return success if the callback function was successful.
		}

		return rc; // Return the error code from the callback function.
	}

	// Return an error if the setting name does not match any known setting.
	return -ENOENT;
}

struct settings_handler my_conf = {.name = SETTINGS_NAME_CONF, .h_set = config_set};

bool update_config(enum config_event type, unsigned int value)
{
	// This line seems redundant as the same assignment is done inside the if block.
	// It might be a mistake or placeholder for future event types that also use 'value'.
	oxygen_mV = value;

	// Check if the event type is OXYGEN_CALIBRATION
	if (type == OXYGEN_CALIBRATION) {
		// Update the oxygen calibration value with the new value provided
		oxygen_mV = value;

		// Log the new calibration value for debugging or informational purposes
		LOG_INF("new oxygen calibration value : %d", oxygen_mV);
	}

	// Return true to indicate success
	return true;
}

unsigned int get_config(enum config_event type)
{
	unsigned int ret_value = 0; // Default return value if the event type is not supported

	// Check if the event type is OXYGEN_CALIBRATION
	if (type == OXYGEN_CALIBRATION) {
		ret_value = oxygen_mV; // Retrieve the oxygen calibration millivolt value
		LOG_INF("oxygen_mV: %d",
			oxygen_mV); // Log the retrieved value for debugging purposes
	}

	// Return the retrieved configuration value, or 0 if not found
	return ret_value;
}

/**
 * @brief Initialize the configuration system and handle configuration events in a loop.
 *
 * This function sets up the configuration system, registers a configuration subtree,
 * and then enters an infinite loop to handle configuration events such as saving settings.
 */
static void config_thread(void)
{
	// Initialize a kernel event object that will be used for synchronization.
	k_event_init(&config_event);

	// Lock a mutex to protect the configuration system initialization.
	k_mutex_lock(&config_mutex, K_FOREVER);

	// Initialize the settings subsystem and check for errors.
	int err = settings_subsys_init();
	if (err) {
		LOG_ERR("settings_subsys_init, error: %d", err);
		return;
	}
	LOG_INF("settings subsys initialization: OK.");

	// Register the settings handler for a specific configuration subtree.
	err = settings_register(&my_conf);
	if (err) {
		LOG_ERR("subtree '%s' handler registered: fail (err %d)", my_conf.name, err);
		return;
	}
	LOG_INF("subtree '%s' handler registered: OK", my_conf.name);

	// Load settings from persistent storage and check for errors.
	err = settings_load();
	if (err) {
		LOG_ERR("settings_load, error: %d", err);
		return;
	}
	LOG_INF("settings load, OK.");

	// Signal a condition variable to indicate that the configuration is initialized.
	k_condvar_signal(&config_condvar);

	// Unlock the mutex as the initialization is complete.
	k_mutex_unlock(&config_mutex);

	// Enter an infinite loop to handle configuration events.
	while (1) {
		uint32_t events;
		int rc;

		// Wait for a specific configuration event (e.g., OXYGEN_CALIBRATION) indefinitely.
		events = k_event_wait(&config_event, OXYGEN_CALIBRATION, true, K_FOREVER);

		// Check if the expected event occurred.
		if (events == OXYGEN_CALIBRATION) {
			// Save the oxygen calibration value to persistent storage and check for
			// errors.
			rc = settings_save_one(SETTINGS_OXYGEN_VALUE, &oxygen_mV,
					       sizeof(oxygen_mV));
			if (rc) {
				LOG_ERR("settings_save, error: %d", rc);
			}
		}
	}
}

/* Define the stack size and priority for the LED thread */
#define STACK_SIZE 1024
#define PRIORITY   10
/* Define the thread for the LED */
K_THREAD_DEFINE(config_id, STACK_SIZE, config_thread, NULL, NULL, NULL, PRIORITY, 0, 0);
