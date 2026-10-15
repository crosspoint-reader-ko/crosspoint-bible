add_executable(BibleInputTest
  BibleInputTest.cpp
  ${REPO_ROOT}/lib/BiblePack/HangulCompose.cpp
  ${REPO_ROOT}/lib/BiblePack/BibleBookmarks.cpp
)

target_include_directories(BibleInputTest PRIVATE
  ${REPO_ROOT}/lib/BiblePack
)

target_link_libraries(BibleInputTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BibleInputTest)
