cmake_minimum_required(VERSION 3.16)

# esp_wifi_remote's Kconfig does `orsource "./idf_v$ESP_IDF_VERSION/..."`, but IDF's
# kconfgen only exports IDF_TARGET/IDF_TOOLCHAIN/IDF_ENV_FPGA/IDF_INIT_VERSION to Kconfig
# — ESP_IDF_VERSION is never set, so the orsource silently resolves to nothing and the
# slave-target choice (and all WIFI_RMT_* options) never get defined. Set it here so the
# real Kconfig fragment shipped under idf_v5.5/ gets picked up.
set(ENV{ESP_IDF_VERSION} "5.5")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_p4_ov5647_displacement)
