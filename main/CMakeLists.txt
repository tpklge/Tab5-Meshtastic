idf_component_register(
    SRCS
        "main.cpp"
        "board/lcd_tools.cpp"
        "board/keyboard.cpp"
        "board/tab5_audio.cpp"
        "ui/ui_shell.cpp"
        "ui/screens/settings_screen.cpp"
        "app/app_state.cpp"
        "app/settings.cpp"
        "mesh/mesh_proto.cpp"
        "ble/ble_transport.cpp"
        # Transport abstraction layer
        "transport/serial_framer.cpp"
        "transport/uart_transport.cpp"
        # Protocol session
        "protocol/mesh_session.cpp"
        # Persistent storage
        "storage/app_storage.cpp"
        "storage/message_store.cpp"
        "storage/settings_store.cpp"
    INCLUDE_DIRS
        "."
        "board"
        "ui"
        "ui/screens"
        "app"
        "mesh"
        "ble"
        "transport"
        "protocol"
        "storage"
        "../components/m5_tab5_component/src/drivers"
    # esp_driver_sdmmc: esp_hosted.h transitively includes driver/sdmmc_host.h
    # but esp_hosted doesn't propagate it, so we pull it in here.
    REQUIRES
        nvs_flash
        bt
        esp_hosted
        esp_wifi_remote
        esp_driver_sdmmc
        esp_driver_uart
        esp_driver_i2s
        esp_driver_i2c
        esp_driver_ledc
        esp_timer
        spiffs
        esp_rom
        m5_tab5_component
        m5_tab5_keyboard_component
        meshtastic_protos
        espressif__i2c_bus
)
