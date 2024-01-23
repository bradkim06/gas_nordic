# Define the zephyr library
zephyr_library()

# Include directories for the Bosch-BSEC2-Library and BME68x-Sensor-API
zephyr_library_include_directories(${APP_ROOT}/lib/Bosch-BSEC2-Library/src/inc)
zephyr_library_include_directories(${APP_ROOT}/lib/BME68x-Sensor-API)

# Add the bme68x.c source file to the library
zephyr_library_sources(${APP_ROOT}/lib/BME68x-Sensor-API/bme68x.c)

# Define the BSEC_SAMPLE_PERIOD_S as 3
zephyr_library_compile_definitions(BSEC_SAMPLE_PERIOD_S=3)

# Define BSEC_SAMPLE_RATE and BSEC_SAMPLE_RATE_IAQ based on the configuration
# for IAQ sample rate
zephyr_library_compile_definitions_ifdef(
  CONFIG_BME68X_IAQ_SAMPLE_RATE_ULTRA_LOW_POWER
  BSEC_SAMPLE_RATE=BSEC_SAMPLE_RATE_ULP
  BSEC_SAMPLE_RATE_IAQ=BSEC_SAMPLE_RATE_ULP)

zephyr_library_compile_definitions_ifdef(
  CONFIG_BME68X_IAQ_SAMPLE_RATE_LOW_POWER BSEC_SAMPLE_RATE=BSEC_SAMPLE_RATE_LP
  BSEC_SAMPLE_RATE_IAQ=BSEC_SAMPLE_RATE_ULP)

# If the configuration is for a hard floating point ABI, import the appropriate
# library for the Cortex-M4 processor
if(CONFIG_FP_HARDABI)
  if(CONFIG_CPU_CORTEX_M4)
    zephyr_library_import(
      bosch_bsec_lib
      ${APP_ROOT}/lib/Bosch-BSEC2-Library/src/cortex-m4/fpv4-sp-d16-hard/libalgobsec.a
    )
  else()
    assert(0 "Unsupported configuration.")
  endif()
  # Otherwise, define BME68X_DO_NOT_USE_FPU and import the appropriate library
  # for the Cortex-M4 processor
else()
  zephyr_library_compile_definitions(BME68X_DO_NOT_USE_FPU)
  if(CONFIG_CPU_CORTEX_M4)
    zephyr_library_import(
      bosch_bsec_lib
      ${APP_ROOT}/lib/Bosch-BSEC2-Library/src/cortex-m4/libalgobsec.a)
  else()
    assert(0 "Unsupported configuration.")
  endif()
endif()

# Add the bme68x_iaq.c source file to the library
zephyr_library_sources(bme68x_iaq.c)
