/**
 * @file src/main.c - The starting point of the user application
 *
 * @brief The operation of the main() function, which is the first function
 * called after kernel initialization
 *
 * @author bradkim06@gmail.com
 */
#include <hal/nrf_power.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/pm.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>

#include "gas.h"
#include "version.h"

/* ───── 설정 값 ───── */
#define LONG_PRESS_MS 1000 /* 길게 누름 시간 */

static const struct gpio_dt_spec sw0 = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback sw_cb;
static struct k_timer long_press_timer;
static volatile bool off_pending; /* 뗐을 때 power-off 플래그 */
static atomic_t pressed;

/* 타이머 만료 → 1 초 이상 눌림 확인 */
static void long_press_handler(struct k_timer *timer) {
    if (!atomic_get(&pressed))
        return;

    printk("Long press detected – release to power-off\n");
    off_pending = true; /* 뗐을 때 끌 것 */
}

/* 전원 끄기 전 GPIO 핀을 연결 해제하여 누설 전류를 최소화합니다. */
static void configure_gpios_for_poweroff(void) {
    const struct device *const gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));

    if (!device_is_ready(gpio0)) {
        printk("GPIO0 not ready\n");
        return;
    }

    for (uint8_t pin = 0; pin < 32; pin++) {
        switch (pin) {
        case 31: /* wake-up 버튼 – 건드리지 않음 */
            break;

        case 29: /* VBATT 전원 스위치 및 LED 게이트 전원 차단 */
        case 26: /* LED 게이트는 VBATT 스위치를 Low 로 당겨 공유 차단 */
        case 27: /* LED 게이트는 VBATT 스위치를 Low 로 당겨 공유 차단 */
            gpio_pin_configure(gpio0, 29, GPIO_OUTPUT_LOW);
            break;

        default: /* 나머지 – 완전 Hi-Z */
            gpio_pin_configure(gpio0, pin, GPIO_DISCONNECTED);
            break;
        }
    }
}

static void button_cb(const struct device *dev, struct gpio_callback *cb,
                      uint32_t pins) {
    bool val = gpio_pin_get_dt(&sw0);

    if (val) { /* High → 눌림 시작 */
        atomic_set(&pressed, 1);
        k_timer_start(&long_press_timer, K_MSEC(LONG_PRESS_MS), K_NO_WAIT);
    } else { /* Low → 뗌 */
        atomic_set(&pressed, 0);
        k_timer_stop(&long_press_timer);

        if (off_pending) { /* 길게 누른 뒤 손 뗐음 → 끄기 */
            off_pending = false;

            /* 깨우기용: 다음 Rising(High) 에 반응 */
            gpio_pin_interrupt_configure_dt(&sw0, GPIO_INT_LEVEL_ACTIVE);

            configure_gpios_for_poweroff();

            hwinfo_clear_reset_cause();
            sys_poweroff(); /* System-OFF, 전류 ≈ 0.3 µA */
        }
    }
}

LOG_MODULE_REGISTER(MAIN, CONFIG_APP_LOG_LEVEL);
FIRMWARE_INFO();

/**
 * @brief This thread performs kernel initialization, then calls the
application’s main() function (if one is defined).

By default, the main thread uses the highest configured preemptible thread
priority (i.e. 0). If the kernel is not configured to support preemptible
threads, the main thread uses the lowest configured cooperative thread priority
(i.e. -1).

The main thread is an essential thread while it is performing kernel
initialization or executing the application’s main() function; this means a
fatal system error is raised if the thread aborts. If main() is not defined, or
if it executes and then does a normal return, the main thread terminates
normally and no error is raised.
 *
 * @return must be 0(none error)
 */
int main(void) {
    LOG_INF("Firmware : %s", firmware_info);
    LOG_INF("Board:%s  SoC:%s  ROM:%dkB  RAM:%dkB", CONFIG_BOARD, CONFIG_SOC,
            CONFIG_FLASH_SIZE, CONFIG_SRAM_SIZE);

    /* ① 버튼 핀 설정 */
    gpio_pin_configure_dt(&sw0, GPIO_INPUT | GPIO_PULL_DOWN);
    /* 풀다운 저항이 안정될 시간을 줌 */
    k_sleep(K_MSEC(10));

    gpio_pin_interrupt_configure_dt(&sw0, GPIO_INT_EDGE_BOTH);
    gpio_init_callback(&sw_cb, button_cb, BIT(sw0.pin));
    gpio_add_callback(sw0.port, &sw_cb);

    k_timer_init(&long_press_timer, long_press_handler, NULL);

    /* ② 부팅 원인 확인 */
    uint32_t cause;
    hwinfo_get_reset_cause(&cause);
    printk("Hold P0.31 for 1 s to enter deep sleep.\n");

    /* ③ 애플리케이션 루프 */
    while (1) {
        k_sleep(K_FOREVER);
    }

    return 0;
}

#if DT_HAS_COMPAT_STATUS_OKAY(nordic_nrf_wdt)
/* Maximum and minimum window for watchdog timer in milliseconds */
#define WDT_MAX_WINDOW 5000U
#define WDT_MIN_WINDOW 0U

/* Interval for feeding the watchdog timer(ms) */
#define WDT_FEED_INTERVAL 1000U

/* Option for watchdog timer */
#define WDT_OPER_MODE WDT_OPT_PAUSE_HALTED_BY_DBG
#endif

/**
 * @brief Watchdog thread function.
 *
 * This function sets up the watchdog timer and continuously feeds it.
 */
static void watchdog_thread_fn(void) {
    int setup_error_status;  // wdt_setup 결과 코드 저장
    int watchdog_channel_id; // 설치된 watchdog 채널 식별자
    const struct device *const watchdog_device =
        DEVICE_DT_GET(DT_ALIAS(watchdog0)); // watchdog 주변장치 핸들

    // Check if the device is ready before proceeding
    if (!device_is_ready(watchdog_device)) {
        LOG_ERR("%s: device not ready.\n", watchdog_device->name);
        return;
    }

    struct wdt_timeout_cfg watchdog_configuration = {
        /* Reset SoC when watchdog timer expires. */
        .flags = WDT_FLAG_RESET_SOC,

        /* Expire watchdog after max window */
        .window.min = WDT_MIN_WINDOW,
        .window.max = WDT_MAX_WINDOW,
    };

    // Install timeout for the watchdog
    watchdog_channel_id =
        wdt_install_timeout(watchdog_device, &watchdog_configuration);
    if (watchdog_channel_id < 0) {
        LOG_ERR("Error installing watchdog timeout\n");
        return;
    }

    // Set up the watchdog
    setup_error_status = wdt_setup(watchdog_device, WDT_OPER_MODE);
    if (setup_error_status < 0) {
        printk("Error setting up watchdog\n");
        return;
    }

    // Feed the watchdog
    while (1) {
        wdt_feed(watchdog_device, watchdog_channel_id);
        k_sleep(K_MSEC(WDT_FEED_INTERVAL));
    }
}

#define STACKSIZE 1024
#define PRIORITY 14
K_THREAD_DEFINE(watchdog_thread_id, STACKSIZE, watchdog_thread_fn, NULL, NULL,
                NULL, PRIORITY, 0, 0);
