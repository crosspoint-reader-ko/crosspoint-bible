add_executable(HyphenationTest
  HyphenationTest.cpp
  ${REPO_ROOT}/lib/Hyphenation/Hyphenation.cpp
  ${REPO_ROOT}/lib/Hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/Hyphenation/LiangHyphenation.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(HyphenationTest PRIVATE
  ${REPO_ROOT}/lib/Hyphenation
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(HyphenationTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HyphenationTest)
