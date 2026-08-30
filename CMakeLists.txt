cmake_minimum_required(VERSION 3.16)

# Zugangsdaten liegen in einer eigenen Datei, die nicht ins Repository
# gehoert. Fehlt sie, baut das Projekt trotzdem -- dann eben mit den
# Vorgabewerten aus Kconfig.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/local.defaults")
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;local.defaults")
else()
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(bitBlockfinder)
