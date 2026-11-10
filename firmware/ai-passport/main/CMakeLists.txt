set(direct_sources)
set(app_entry "main.c")
if(QQMUSIC_DEVICE_PREFLIGHT)
    if(NOT CONFIG_NETEASE_DIRECT)
        message(FATAL_ERROR "QQMUSIC_DEVICE_PREFLIGHT requires the direct product configuration")
    endif()
    set(app_entry "../tests/qqmusic_device_preflight.c")
    list(APPEND direct_sources "../tests/qqmusic_preflight_wifi.c")
endif()
if(CONFIG_NETEASE_DIRECT)
    list(APPEND direct_sources "netease_direct_adapter.c" "music_artwork.c" "music_provider.c" "music_qr_image.c")
endif()
idf_component_register(
	    SRCS ${app_entry} ${direct_sources}
	         "ui_pixel.c"
	         "ui_pixel_math.c"
	         "device_music_ui_spec.c"
	         "device_music_ui.c"
                 "display_policy.c" "display_settings.c"
	         "fonts/netease_ui_font_8.c"
	         "fonts/netease_ui_font_9.c"
	         "fonts/netease_ui_font_10.c"
	         "fonts/netease_ui_font_12.c"
	         "fonts/netease_ui_font_13.c"
	         "fonts/netease_ui_font_14.c"
	         "fonts/netease_ui_font_17.c"
	         "fonts/netease_ui_font_18.c"
	         "fonts/netease_ui_font_22.c"
	         "fonts/netease_ui_font_24.c"
         "fonts/netease_ui_font_43.c"
         "fonts/netease_ui_font_46.c"
         "fonts/netease_ui_font_60.c"
         "fonts/netease_ui_font_80.c"
	         "demo_radio.c"
		         "netease_app_logic.c" "music_lyrics.c" "music_playback_order.c"
	         "netease_app.c"
         "wifi_provisioning_logic.c"
         "wifi_provisioning_page.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_usb_serial_jtag esp_http_server json esp_app_format audio_stream bsp esp_event esp_hw_support esp_netif esp_timer esp_wifi lwip nvs_flash netease_client netease_direct_client qqmusic_direct_client
)
