FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/include"
                       REQUIRES esp_http_server spiffs esp_wifi nvs_flash lwip esp_timer esp_netif)
