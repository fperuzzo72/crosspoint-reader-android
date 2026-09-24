add_executable(KindleCryptoTest
  KindleCryptoTest.cpp
  ${REPO_ROOT}/lib/hal/posix/CryptoPosix.cpp
  ${REPO_ROOT}/lib/hal/posix/ArduinoString.cpp
)

target_include_directories(KindleCryptoTest PRIVATE
  ${REPO_ROOT}/lib/hal/posix
  ${REPO_ROOT}/lib/hal/posix/arduino-shim
)

target_link_libraries(KindleCryptoTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KindleCryptoTest)
