add_executable(data-test data-test.c ../src/mqtt_pub.c)

target_link_libraries(data-test data ${SDR_LIBRARIES})
