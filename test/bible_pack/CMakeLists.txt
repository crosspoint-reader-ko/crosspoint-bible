add_executable(BiblePackTest
  BiblePackTest.cpp
  ${REPO_ROOT}/lib/BiblePack/BiblePack.cpp
  ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/lib/uzlib/src/tinflate.c
  uzlib_checksum_stubs.c
)

target_include_directories(BiblePackTest PRIVATE
  ${REPO_ROOT}/lib/BiblePack
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/uzlib/src
)

target_compile_definitions(BiblePackTest PRIVATE
  BIBLE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixture.cpb"
)

target_link_libraries(BiblePackTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BiblePackTest)
