add_executable(JpegProgressiveTest
  JpegProgressiveTest.cpp
  ${REPO_ROOT}/third_party/JPEGDEC.cpp
)

target_include_directories(JpegProgressiveTest PRIVATE
  ${REPO_ROOT}/third_party
)

target_link_libraries(JpegProgressiveTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(JpegProgressiveTest)
