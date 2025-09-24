/**
 * @file src/gas.c - electrochemical gas sensor adc application code
 *
 * @brief Program for Reading electrochemical Gas Sensor ADC Information Using
 * Nordic's SAADC.
 *
 * This file contains the code for reading gas sensor values using the ADC
 * interface, applying temperature compensation, and calculating moving
 * averages. It also checks for any changes in gas sensor values and posts an
 * event accordingly.
 *
 * @author
 * bradkim06@gmail.com
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "bluetooth.h"
#include "bme680_app.h"
#include "ema.h"
#include "gas.h"
#include "hhs_math.h"
#include "hhs_util.h"
#include "settings.h"

static ema_t ema_o2, ema_gas;
/* 전역 가스 오프셋 (mV) : filtered 값에 더해줄 보정치 */
static atomic_t g_gas_offset_mv = ATOMIC_INIT(0);

/* 현재 가스 오프셋(mV) 읽기 */
static inline int32_t current_gas_offset(void) {
    return (int32_t)atomic_get(&g_gas_offset_mv);
}

/* 오프셋 업데이트: 필요 시 클램프/로깅 추가 가능 */
static inline void set_gas_offset_mv(int32_t new_offset_mv) {
    atomic_set(&g_gas_offset_mv, (atomic_val_t)new_offset_mv);
}
/* ======================================================================= */

/* Module registration for Gas Monitor with the specified log level. */
LOG_MODULE_REGISTER(GAS_MON, CONFIG_APP_LOG_LEVEL);

/* Enumeration of gas devices from the DEVICE_LIST. */
DEFINE_ENUM(gas_device, DEVICE_LIST)

/* Semaphore used for mutual exclusion of gas sensor data. */
K_SEM_DEFINE(gas_sem, 1, 1);

GAS_LEVEL_POINT_STRUCT();
GAS_COEFFICIENT_STRUCT();
/* Current value of the gas sensor. */
static struct gas_sensor_value gas_data[3];
static bool is_temperature_invalid;

#define DATA_BUFFER_SIZE 30 // 데이터 버퍼 크기
#define SIGMA_MULTIPLIER 3  // 3-시그마 규칙을 위한 승수

typedef struct {
    int32_t buffer[DATA_BUFFER_SIZE];
    int index;
    bool is_full;
} CircularBuffer;

static CircularBuffer adc_buffer[2] = {0}; // O2와 GAS를 위한 두 개의 버퍼

// === Tunables (기존 매크로 유지 + 보강) ======================================
// 1 mV/sec 변화 8mV = 0.1%
#define O2_DERIVATIVE_THRESHOLD 8.0f // mV/s (기존 유지)
// 기준값과 차이가 10 mV(0.125%) 이상이면 보정 필요
#define O2_BASELINE_TOLERANCE_LOW 10 // mV
// 기준값과 차이가 80 mV(1.0%) 이하이면 보정 필요
#define O2_BASELINE_TOLERANCE_HIGH 80 // mV

// [신규] 안정 유지 시간(히스테리시스)와 쿨다운, 워밍업
#define O2_STABLE_HOLD_SEC                                                     \
    10 // 파생값이 임계 미만으로 연속 유지되어야 하는 시간
#define O2_MIN_CAL_INTERVAL_SEC 60 // 보정 쿨다운(5분)
#define O2_WARMUP_SEC 60 // 부팅 직후 강제 보정 윈도(기존 의도 유지)

// [가독성] 기대 O2(%)
#define O2_EXPECTED_PERCENT 20.9f
#define O2_EXPECTED_PERCENT_STR "20.9"

// 기대 raw(mV) 계산 헬퍼(25% 기준 테이블을 쓰는 기존 로직 일반화)
static inline int32_t expected_o2_raw_from_percent(float percent) {
    // measurement_range[O2][0].lvl_mV : 25% 기준 mV 라고 가정한 기존 코드의
    // 의도 유지
    float raw_f = (float)measurement_range[O2][0].lvl_mV * (percent / 25.0f);
    // 정밀도 손실 없게 반올림 후 정수화
    return (int32_t)lrintf(raw_f);
}

// === Dynamic calibration
// ======================================================
static void dynamic_oxygen_calibration(int32_t current_avg) {
    // 상태 유지 변수
    static int32_t prev_o2_avg = 0;
    static int64_t boot_time = 0;         // 초
    static int64_t prev_time = 0;         // 초
    static float stable_accum_sec = 0.0f; // [신규] 파생값 안정 누적 시간
    static int64_t last_cal_time = 0; // [신규] 마지막 보정 시각(초)
    static bool initialized = false;  // [신규] 첫 프레임 초기화 여부

    int64_t now = k_uptime_get() / 1000; // 현재 시간을 초 단위로 얻음
    if (boot_time == 0) {
        boot_time = now;
    }

    // [신규] 첫 호출 시점에 기준값 세팅(초기 파생 왜곡 방지)
    if (!initialized) {
        prev_time = now;
        prev_o2_avg = current_avg;
        initialized = true;
    }

    const int32_t expected_o2_raw =
        expected_o2_raw_from_percent(O2_EXPECTED_PERCENT);
    const int64_t since_boot = now - boot_time;

    // === 1) 부팅 후 워밍업 윈도 ===
    if (since_boot < O2_WARMUP_SEC) {
        if (llabs((int64_t)current_avg - (int64_t)expected_o2_raw) >
            O2_BASELINE_TOLERANCE_LOW) {
            LOG_INF("Initial dynamic O2 calibration (boot phase): "
                    "current_avg=%d expected=%d",
                    current_avg, expected_o2_raw);
            calibrate_oxygen(O2_EXPECTED_PERCENT_STR,
                             strlen(O2_EXPECTED_PERCENT_STR));
            last_cal_time = now; // [신규] 쿨다운 기준점
        }
        // 초기 구간에서도 추후 파생 안정 판정을 위해 기준 갱신
        prev_o2_avg = current_avg;
        prev_time = now;
        stable_accum_sec = 0.0f;
        return;
    }

    // === 2) 파생값 계산 (가드 포함) ===
    float dt = (float)(now - prev_time);
    if (dt <= 0.0f)
        dt = 1.0f; // 타임스탬프 역전/동일 프레임 가드

    float derivative_mvps = (float)(current_avg - prev_o2_avg) / dt; // mV/s
    LOG_DBG("O2 derivative: %.3f mV/s (dt=%.2fs)", derivative_mvps, dt);

    // === 3) 안정 누적 시간(히스테리시스) ===
    if (fabsf(derivative_mvps) < O2_DERIVATIVE_THRESHOLD) {
        stable_accum_sec += dt;
    } else {
        stable_accum_sec = 0.0f;
    }

    // === 4) 오차 계산 ===
    int32_t err_mv = current_avg - expected_o2_raw;
    int32_t abs_err_mv = err_mv >= 0 ? err_mv : -err_mv;

    // === 5) 보정 조건 ===
    bool in_error_window = (abs_err_mv > O2_BASELINE_TOLERANCE_LOW) &&
                           (abs_err_mv < O2_BASELINE_TOLERANCE_HIGH);

    bool stable_enough = (stable_accum_sec >= (float)O2_STABLE_HOLD_SEC);
    bool cooldown_ok = (last_cal_time == 0) ||
                       ((now - last_cal_time) >= O2_MIN_CAL_INTERVAL_SEC);

    if (in_error_window && stable_enough && cooldown_ok) {
        LOG_INF("Dynamic O2 calibration triggered: der=%.3f mV/s, hold=%.1fs, "
                "err=%d mV, cur=%d exp=%d",
                derivative_mvps, stable_accum_sec, err_mv, current_avg,
                expected_o2_raw);
        calibrate_oxygen(O2_EXPECTED_PERCENT_STR,
                         strlen(O2_EXPECTED_PERCENT_STR));
        last_cal_time = now;
        stable_accum_sec = 0.0f; // 보정 직후 다시 안정 누적
    }

    // === 6) 상태 갱신 ===
    prev_o2_avg = current_avg;
    prev_time = now;
}

// offset 변화율 임계값 (mV/s), 작으면 안정적임
#define GAS_OFFSET_DERIVATIVE_THRESHOLD 3.0f
// offset 차이가 이 값보다 커야 보정 진행
#define GAS_OFFSET_DIFF_TOLERANCE_LOW 2
// offset 차이가 이 값보다 작아야 보정 진행
#define GAS_OFFSET_DIFF_TOLERANCE_HIGH 15
#define GAS_REFERENCE_VOLTAGE 600 // mV

// [신규] 파생값 안정 '연속 유지' 시간(히스테리시스) & 쿨다운
#define GAS_STABLE_HOLD_SEC 10 // 연속 10초 이상 안정일 때만 보정
#define GAS_MIN_CAL_INTERVAL_SEC 60 // 보정 후 쿨다운(주석은 필요에 맞게 조정)

// [신규] 안전 범위 클램프(회로/ADC 범위에 맞춰 조정)
#define GAS_OFFSET_MIN_MV -1000
#define GAS_OFFSET_MAX_MV 1000

// [신규] 워밍업(부팅 60초)용 통계 설정
#define GAS_WARMUP_SEC 60
#define GAS_REFERENCE_ACCEPT_WINDOW_MV 100 // 레퍼런스(클린) 윈도
#define GAS_WARMUP_USE_MEDIAN 1 // 0=누적 평균(mean), 1=중앙값(median)
#define GAS_WARMUP_MEDIAN_WINDOW 31 // 중앙값 사용 시 창 크기(홀수 권장)

// [신규] 간단 클램프 헬퍼
static inline int clamp_i32(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// [신규] 중앙값 계산(작은 창에서 삽입정렬 사용)
static int median_of_buffer(const int *buf, int n) {
    // GAS_WARMUP_MEDIAN_WINDOW는 상수이므로 자동 배열 사용 가능
    int tmp[GAS_WARMUP_MEDIAN_WINDOW];
    if (n <= 0)
        return 0;
    if (n > GAS_WARMUP_MEDIAN_WINDOW)
        n = GAS_WARMUP_MEDIAN_WINDOW;
    for (int i = 0; i < n; ++i)
        tmp[i] = buf[i];
    // insertion sort (n이 작으니 충분히 빠름)
    for (int i = 1; i < n; ++i) {
        int key = tmp[i], j = i - 1;
        while (j >= 0 && tmp[j] > key) {
            tmp[j + 1] = tmp[j];
            --j;
        }
        tmp[j + 1] = key;
    }
    if (n & 1)
        return tmp[n / 2];
    return (tmp[n / 2 - 1] + tmp[n / 2]) / 2;
}

static void dynamic_gas_offset_calibration(int32_t adc_value_mv) {
    static int64_t boot_time = 0; // s
    static int64_t last_time = 0; // s
    static int prev_offset = 0;

    // [신규] 히스테리시스/쿨다운 상태
    static float stable_accum_sec = 0.0f; // 파생값 안정 누적 시간
    static int64_t last_update_time = 0;  // 마지막 보정 시각

#if GAS_WARMUP_USE_MEDIAN
    // [신규] 워밍업 통계(누적 평균 or 중앙값)
    static int warm_ring[GAS_WARMUP_MEDIAN_WINDOW]; // 중앙값용 링버퍼
    static int warm_head = 0;                       // 링버퍼 write index
    static int warm_fill = 0; // 현재 채워진 개수(<=window)
#else
    static int64_t warm_sum = 0; // 누적 평균용 합
    static int warm_cnt = 0;     // 누적 평균용 표본수
#endif

    int64_t now = k_uptime_get() / 1000;
    if (boot_time == 0)
        boot_time = now;

    // [변경] 제안 오프셋 계산 후 안전 범위로 클램프
    int new_offset =
        clamp_i32(-adc_value_mv, GAS_OFFSET_MIN_MV, GAS_OFFSET_MAX_MV);

    // ====================== 워밍업: 60초 동안 "통계 기반 지속 보정"
    // ======================
    if ((now - boot_time) < GAS_WARMUP_SEC) {
        // [신규] 레퍼런스 윈도 안의 표본만 통계에 반영(갑작스런 오염/바람 등
        // 배제)
        if (abs(adc_value_mv - GAS_REFERENCE_VOLTAGE) <=
            GAS_REFERENCE_ACCEPT_WINDOW_MV) {
#if GAS_WARMUP_USE_MEDIAN
            // 중앙값: 고정 창 링버퍼에 저장
            warm_ring[warm_head] = new_offset;
            warm_head = (warm_head + 1) % GAS_WARMUP_MEDIAN_WINDOW;
            if (warm_fill < GAS_WARMUP_MEDIAN_WINDOW)
                warm_fill++;
#else
            // 누적 평균: Welford까지는 과하지만 n이 커질 수 있어 64-bit 합 사용
            warm_sum += (int64_t)new_offset;
            if (warm_cnt < INT32_MAX)
                warm_cnt++;
#endif
        }

        // [신규] 현재까지의 통계값으로 보정값 계산(표본이 없으면 new_offset로
        // fallback)
        int est_offset;
#if GAS_WARMUP_USE_MEDIAN
        est_offset = (warm_fill > 0) ? median_of_buffer(warm_ring, warm_fill)
                                     : new_offset;
#else
        est_offset = (warm_cnt > 0)
                         ? (int)(warm_sum / (warm_cnt ? warm_cnt : 1))
                         : new_offset;
#endif
        est_offset =
            clamp_i32(est_offset, GAS_OFFSET_MIN_MV, GAS_OFFSET_MAX_MV);

        set_gas_offset_mv(est_offset);
        LOG_INF("Warmup GAS offset (stat=%s): est=%d mV, samples=%d",
                GAS_WARMUP_USE_MEDIAN ? "MEDIAN" : "MEAN", est_offset,
#if GAS_WARMUP_USE_MEDIAN
                warm_fill
#else
                warm_cnt
#endif
        );

        // 파생 왜곡 방지: 다음 단계 대비 시간/직전값 갱신
        prev_offset = new_offset;
        last_time = now;
        return; // 워밍업 프레임은 여기서 종료
    }

    // =========================== 런타임 보정 로직 (기존)
    // =============================
    else if (abs(GAS_REFERENCE_VOLTAGE + new_offset) <=
             GAS_REFERENCE_ACCEPT_WINDOW_MV) {
        float dt = (float)(now - last_time);
        if (dt <= 0.0f)
            dt = 1.0f; // 방어

        float offset_derivative = (float)(new_offset - prev_offset) / dt;
        LOG_DBG("Gas offset derivative: %.2f mV/s", offset_derivative);

        // 파생값 안정 연속 유지 시간(히스테리시스)
        if (fabsf(offset_derivative) < GAS_OFFSET_DERIVATIVE_THRESHOLD) {
            stable_accum_sec += dt;
        } else {
            stable_accum_sec = 0.0f;
        }

        int cur = current_gas_offset();

        // 보정 조건: 기존 + 연속 안정 + 쿨다운
        bool small_step =
            (abs(new_offset - cur) > GAS_OFFSET_DIFF_TOLERANCE_LOW) &&
            (abs(new_offset - cur) < GAS_OFFSET_DIFF_TOLERANCE_HIGH);
        bool stable_enough = (stable_accum_sec >= (float)GAS_STABLE_HOLD_SEC);
        bool cooldown_ok =
            (last_update_time == 0) ||
            ((now - last_update_time) >= GAS_MIN_CAL_INTERVAL_SEC);

        LOG_DBG("Gas offset hold=%.1fs (need >= %ds), cooldown_ok=%d, new=%d "
                "cur=%d",
                stable_accum_sec, GAS_STABLE_HOLD_SEC, cooldown_ok, new_offset,
                cur);

        if (small_step &&
            fabsf(offset_derivative) < GAS_OFFSET_DERIVATIVE_THRESHOLD &&
            stable_enough && cooldown_ok) {

            set_gas_offset_mv(new_offset);
            last_update_time = now;  // 쿨다운 시작
            stable_accum_sec = 0.0f; // 보정 직후 초기화
            LOG_INF("Dynamic GAS offset updated: %d mV", new_offset);
        }
    }

    prev_offset = new_offset;
    last_time = now;
}

/**
 * @brief Converts ADC raw data to millivolts.
 *
 * This function takes the raw ADC data and converts it to millivolts based on
 * the ADC channel configuration. If the ADC channel is configured as
 * differential, the raw data is directly converted to millivolts. If the ADC
 * channel is not differential, the raw data is checked to be non-negative
 * before conversion. The converted value is then passed to
 * adc_raw_to_millivolts_dt() for further conversion based on device tree
 * specifications. If adc_raw_to_millivolts_dt() returns an error, a warning is
 * logged and the error code is returned.
 *
 * @param adc_channel Pointer to the ADC channel configuration structure.
 * @param raw_adc_data Raw ADC data to be converted.
 *
 * @return Converted value in millivolts if successful, error code otherwise.
 */
static int32_t convert_adc_to_mv(const struct adc_dt_spec *adc_channel,
                                 int16_t raw_adc_data) {
    int32_t millivolts = (adc_channel->channel_cfg.differential)
                             ? (int32_t)((int16_t)raw_adc_data)
                             : (int32_t)raw_adc_data;

    int err = adc_raw_to_millivolts_dt(adc_channel, &millivolts);
    if (err < 0) {
        LOG_WRN("Value in millivolts not available");
        return err;
    }
    return millivolts;
}

/**
 * @brief Calculate calibrated millivolts for a given gas type.
 *
 * This function calculates the calibrated millivolts for a given gas type.
 * It uses the temperature coefficient based on the gas type to calibrate the
 * millivolts.
 *
 * @param raw_mv The raw millivolts value.
 * @param gas_type The type of gas device (O2 or GAS).
 *
 * @return The calibrated millivolts value.
 */
static int32_t calculate_calibrated_mv(int32_t raw_mv,
                                       enum gas_device gas_type) {
    if (is_temperature_invalid) {
        return raw_mv;
    }

    struct bme680_data env_data = get_bme680_data();
    unsigned int temperature_celsius =
        env_data.temp.val1 * 100 + env_data.temp.val2;
    float temp_coeff = (float)calculate_level_pptt(temperature_celsius,
                                                   coeff_levels[gas_type]);

    temp_coeff = 1000.0f / temp_coeff;
    int32_t calibrated_mv = (int32_t)round(raw_mv * temp_coeff);
    LOG_DBG("Temperature coefficient : %f, Raw millivolts : %d, Calibrated "
            "millivolts : %d",
            temp_coeff, raw_mv, calibrated_mv);
    return calibrated_mv;
}

/**
 * @brief Update gas data based on the average millivolt and gas device type.
 *
 * This function calculates the current gas level based on the average
 * millivolt, compares it with the previous value, and updates the gas data if
 * the difference exceeds a certain threshold. It also ensures thread safety
 * when updating the gas data.
 *
 * @param avg_millivolt The average millivolt.
 * @param device_type The type of the gas device.
 *
 * @return True if the gas data is updated; false otherwise.
 */
static bool update_gas_data(int32_t avg_millivolt,
                            enum gas_device device_type) {
    bool is_gas_data_updated = false;
    int current_level =
        calculate_level_pptt(avg_millivolt, measurement_range[device_type]);

    switch (device_type) {
    case O2: {
        static int previous_o2_level = 0;
        const int O2_THRESHOLD = 2;

        if (abs(current_level - previous_o2_level) > O2_THRESHOLD) {
            is_gas_data_updated = true;
            previous_o2_level = current_level;
        }

        // Ensure thread safety when updating the gas data
        k_sem_take(&gas_sem, K_FOREVER);
        gas_data[device_type].raw = avg_millivolt;
        gas_data[device_type].val1 = current_level / 10;
        gas_data[device_type].val2 = current_level % 10;
        k_sem_give(&gas_sem);
    } break;
    case GAS: {
        static int previous_gas_level = 0;
        const int GAS_THRESHOLD = 2;

        if (abs(current_level - previous_gas_level) > GAS_THRESHOLD) {
            is_gas_data_updated = true;
            previous_gas_level = current_level;
        }

        // Ensure thread safety when updating the gas data
        k_sem_take(&gas_sem, K_FOREVER);
        gas_data[device_type].raw = avg_millivolt;
        gas_data[device_type].val1 = current_level / 10;
        gas_data[device_type].val2 = current_level % 10;
        k_sem_give(&gas_sem);
    } break;
    default:
        // TODO: Handle the case for other types.
        break;
    }

    return is_gas_data_updated;
}

static void add_to_buffer(CircularBuffer *cb, int32_t value) {
    cb->buffer[cb->index] = value;
    cb->index = (cb->index + 1) % DATA_BUFFER_SIZE;
    if (cb->index == 0) {
        cb->is_full = true;
    }
}

static void welford_stats(CircularBuffer *cb, float *mean, float *std) {
    int count = cb->is_full ? DATA_BUFFER_SIZE : cb->index;
    double m = 0.0, s = 0.0;
    for (int i = 0; i < count; i++) {
        double x = (double)cb->buffer[i];
        double delta = x - m;
        m += delta / (i + 1);
        s += delta * (x - m);
    }
    *mean = (float)m;
    *std = (count > 1) ? (float)sqrt(s / (count - 1)) : 0.0f;
}

// static float calculate_mean(CircularBuffer *cb) {
//     int32_t sum = 0;
//     int count = cb->is_full ? DATA_BUFFER_SIZE : cb->index;
//     for (int i = 0; i < count; i++) {
//         sum += cb->buffer[i];
//     }
//     return (float)sum / count;
// }

// static float calculate_std_dev(CircularBuffer *cb, float mean) {
//     float sum_squared_diff = 0;
//     int count = cb->is_full ? DATA_BUFFER_SIZE : cb->index;
//     for (int i = 0; i < count; i++) {
//         float diff = cb->buffer[i] - mean;
//         sum_squared_diff += diff * diff;
//     }
//     return sqrt(sum_squared_diff / count);
// }

static int32_t apply_3_sigma_rule(CircularBuffer *cb, int32_t value) {
    if (!cb->is_full)
        return value;
    float mean, std;
    welford_stats(cb, &mean, &std);
    if (std == 0.0f)
        return (int32_t)mean;
    float lo = mean - SIGMA_MULTIPLIER * std;
    float hi = mean + SIGMA_MULTIPLIER * std;
    return (value < lo || value > hi) ? (int32_t)lroundf(mean) : value;
}

// EMA를 적용하고, 평균값으로 update_gas_data() 하는 버전
static void perform_adc_measurement(const struct adc_dt_spec *adc_channel_spec,
                                    enum gas_device gas_device_type) {
    int16_t adc_raw = 0;
    struct adc_sequence seq = {.buffer = &adc_raw,
                               .buffer_size = sizeof(adc_raw)};

    int err = adc_sequence_init_dt(adc_channel_spec, &seq);
    if (err < 0) {
        LOG_WRN("ADC init fail (%d)", err);
        return;
    }

    err = adc_read(adc_channel_spec->dev, &seq);
    if (err < 0) {
        LOG_WRN("ADC read fail (%d)", err);
        return;
    }

    int32_t mv = convert_adc_to_mv(adc_channel_spec, adc_raw);
    if (mv < 0)
        mv = 0; // 필요 시 클램프 (오프셋 처리 뒤로 옮겨도 됨)

    // (선택) 온도 보정
    // mv = calculate_calibrated_mv(mv, gas_device_type);

    add_to_buffer(&adc_buffer[gas_device_type], mv);
    int32_t filtered = apply_3_sigma_rule(&adc_buffer[gas_device_type], mv);

    // 동적 오프셋/보정
    if (gas_device_type == GAS) {
        dynamic_gas_offset_calibration(filtered);

        int32_t offset_applied = filtered + current_gas_offset();
        if (offset_applied < 0)
            offset_applied = 0; // 필요 시 클램프
        filtered = offset_applied;
    } else {
        dynamic_oxygen_calibration(filtered);
    }

    // EMA 적용 (전역 ema_o2/ema_gas 사용)
    float ema_out = (gas_device_type == O2)
                        ? ema_apply(&ema_o2, (float)filtered)
                        : ema_apply(&ema_gas, (float)filtered);
    int32_t avg_mv = (int32_t)lroundf(ema_out);

    if (update_gas_data(avg_mv, gas_device_type)) {
        if (gas_device_type == O2)
            LOG_INF("O2 changed %d.%d%%", gas_data[O2].val1, gas_data[O2].val2);
        else
            LOG_INF("GAS changed %d.%dppm", gas_data[GAS].val1,
                    gas_data[GAS].val2);
        k_event_post(&bt_event, GAS_VAL_CHANGE);
    }

    if (gas_device_type == O2) {
        LOG_DBG("O2 ch%u: mv filt %ld, avg %ld => %d.%d%%",
                adc_channel_spec->channel_id, (long)filtered, (long)avg_mv,
                gas_data[O2].val1, gas_data[O2].val2);
    } else {
        LOG_DBG("GAS ch%u: mv filt %ld, avg %ld => %d.%dppm",
                adc_channel_spec->channel_id, (long)filtered, (long)avg_mv,
                gas_data[GAS].val1, gas_data[GAS].val2);
    }
}

/**
 * @brief Setup gas ADC
 *
 * This function configures individual channels before sampling.
 *
 * @param adc_channel ADC channel data structure
 *
 * @return 0 on success, negative error code otherwise
 */
static int setup_gas_adc(struct adc_dt_spec adc_channel) {
    /* Check if the device is ready */
    if (!device_is_ready(adc_channel.dev)) {
        LOG_ERR("ADC controller device %s not ready", adc_channel.dev->name);
        return -ENODEV;
    }

    /* Setup the ADC channel */
    int setup_error = adc_channel_setup_dt(&adc_channel);
    if (setup_error < 0) {
        LOG_ERR("Could not setup channel #%d (%d)", adc_channel.channel_id,
                setup_error);
        return -EIO;
    }

    /* Return 0 on success */
    return 0;
}

struct gas_sensor_value get_gas_data(enum gas_device gas_dev) {
    // function will block indefinitely until the semaphore is available.
    k_sem_take(&gas_sem, K_FOREVER);

    // Create a local copy of the gas sensor data for the specified gas device.
    struct gas_sensor_value gas_sensor_copy = gas_data[gas_dev];

    // Release the semaphore after the shared resource has been safely read.
    k_sem_give(&gas_sem);

    // Return the local copy of the gas sensor data.
    // This allows the caller to use the sensor data without worrying about
    // concurrent access
    return gas_sensor_copy;
}

void calibrate_gas(char *reference_value, int len) {
    // Create a buffer to hold the reference value string plus a null terminator
    uint8_t str[len + 1];
    // Copy the reference value into the buffer and ensure it's null-terminated
    snprintf(str, len + 1, "%s",
             reference_value); // Fixed the format specifier from "s" to "%s"

    // Convert the reference value string to a floating-point number
    float reference_ppm = atof(str);
    // 20ppm(NO2)
    reference_ppm = 20.0f / reference_ppm;

    // VDIFF = ISENSOR * RF(100k)
    unsigned int new_mV = gas_data[GAS].raw * reference_ppm;

    // Acquire the semaphore to ensure exclusive access to shared resources
    k_sem_take(&gas_sem, K_FOREVER);

    // Update the oxygen sensor's measurement range in millivolts
    measurement_range[GAS][0].lvl_mV = new_mV;

    // Release the semaphore
    k_sem_give(&gas_sem);

    // Update the sensor configuration with the new calibration value
    update_config(NO2_CALIBRATION, &new_mV);
    // Post an event to indicate that the oxygen sensor has been calibrated
    k_event_post(&config_event, NO2_CALIBRATION);
}

void calibrate_oxygen(char *reference_value, int len) {
    // Create a buffer to hold the reference value string plus a null terminator
    uint8_t str[len + 1];
    // Copy the reference value into the buffer and ensure it's null-terminated
    snprintf(str, len + 1, "%s",
             reference_value); // Fixed the format specifier from "s" to "%s"

    // Convert the reference value string to a floating-point number
    float reference_percent = atof(str);

    // Calculate the voltage based on the reference percent and the voltage
    // divider Note: The formula for voltage calculation is specific to the
    // sensor and circuit design
    float voltage =
        gas_data[O2].raw / ((1 + 200) * (reference_percent * 0.001 * 100));
    // Round down the voltage to two decimal places
    voltage = floor(voltage * 100) / 100;

    // Calculate the new maximum measurement value in millivolts for the oxygen
    // sensor
    unsigned int new_mV = (voltage * 25 * 0.001 * 100) * (1 + 200);

    // Acquire the semaphore to ensure exclusive access to shared resources
    k_sem_take(&gas_sem, K_FOREVER);

    // Update the oxygen sensor's measurement range in millivolts
    measurement_range[O2][0].lvl_mV = new_mV;

    // Release the semaphore
    k_sem_give(&gas_sem);

    // Update the sensor configuration with the new calibration value
    update_config(OXYGEN_CALIBRATION, &new_mV);
    // Post an event to indicate that the oxygen sensor has been calibrated
    k_event_post(&config_event, OXYGEN_CALIBRATION);
}

/**
 * @brief Gas sensor thread function.
 *
 * This function configures ADC channels, sets up moving averages, and performs
 * gas sensor measurements. It reads and processes gas sensor data based on
 * temperature compensation and checks for changes.
 *
 * thread period current consumption test result
 * 1Sec = 11uA
 * 2Sec = 5uA
 * 3Sec = 3uA
 */
#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) ||                                   \
    !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif // DT Node assert
static void gas_measurement_thread(void) {
    const uint8_t GAS_MEASUREMENT_INTERVAL_SEC = 2;
    /* Data of ADC io-channels specified in devicetree. */
    const struct adc_dt_spec gas_adc_channels[] = {
        // o2
        ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0),
        // gas
        ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1),
    };

    for (size_t idx = 0U; idx < ARRAY_SIZE(gas_adc_channels); idx++) {
        setup_gas_adc(gas_adc_channels[idx]);
    }

    ema_init(&ema_o2, 0.10f);
    ema_init(&ema_gas, 0.10f);

    k_condvar_wait(&config_condvar, &config_mutex, K_FOREVER);
    measurement_range[O2][0].lvl_mV =
        *(int16_t *)get_config(OXYGEN_CALIBRATION);
    measurement_range[GAS][0].lvl_mV = *(int16_t *)get_config(NO2_CALIBRATION);
    /* Unlock the mutex as the initialization is complete. */
    k_mutex_unlock(&config_mutex);
    LOG_WRN("test]] o2=%d gas=%d", measurement_range[O2][0].lvl_mV,
            measurement_range[GAS][0].lvl_mV);

    /* Wait for temperature data to become available. */
    if (k_sem_take(&temperature_semaphore, K_SECONDS(20)) != 0) {
        LOG_WRN("Temperature Input data not available!");
        is_temperature_invalid = true;
    } else {
        LOG_INF("Gas temperature sensing ok");
        is_temperature_invalid = false;
    }

    while (1) {
        // O2 채널 측정 및 moving average 계산
        perform_adc_measurement(&gas_adc_channels[O2], O2);

        // GAS 채널 측정
        perform_adc_measurement(&gas_adc_channels[GAS], GAS);

        k_sleep(K_SECONDS(GAS_MEASUREMENT_INTERVAL_SEC));
    }
}

#define STACKSIZE 1024
#define PRIORITY 4
K_THREAD_DEFINE(gas_id, STACKSIZE, gas_measurement_thread, NULL, NULL, NULL,
                PRIORITY, 0, 0);
