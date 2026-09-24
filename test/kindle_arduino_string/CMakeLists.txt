add_executable(KindleArduinoStringTest
  KindleArduinoStringTest.cpp
  ${REPO_ROOT}/lib/hal/posix/ArduinoString.cpp
)

target_include_directories(KindleArduinoStringTest PRIVATE
  ${REPO_ROOT}/lib/hal/posix
)

target_link_libraries(KindleArduinoStringTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KindleArduinoStringTest)
