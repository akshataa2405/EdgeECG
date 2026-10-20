add_executable(test_firmware test_firmware.c)
target_link_libraries(test_firmware edgeecg_core m)
add_test(NAME firmware_unit_tests COMMAND test_firmware)
