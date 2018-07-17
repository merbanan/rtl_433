add_executable(data-test data-test.c)

target_link_libraries(data-test data)

add_executable(baseband-test baseband-test.c ../src/baseband.c)

if(UNIX)
target_link_libraries(baseband-test m)
endif()
