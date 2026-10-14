add_executable(BibleLayoutTest
  BibleLayoutTest.cpp
  ${REPO_ROOT}/lib/BiblePack/BibleLayout.cpp
)

target_include_directories(BibleLayoutTest PRIVATE
  ${REPO_ROOT}/lib/BiblePack
)

target_link_libraries(BibleLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BibleLayoutTest)
