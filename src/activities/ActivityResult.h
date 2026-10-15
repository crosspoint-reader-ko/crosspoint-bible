#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

struct MenuResult {
  int action = -1;
};

struct IntervalResult {
  uint32_t value = 0;
};

struct FilePathResult {
  std::string path;
};

struct TextResult {
  std::string text;
};

struct BibleRefResult {
  uint8_t book = 0;      // 0-based
  uint16_t chapter = 1;  // 1-based
  uint16_t verse = 1;    // 1-based
};

using ResultVariant = std::variant<std::monostate, MenuResult, IntervalResult, FilePathResult, BibleRefResult, TextResult>;

struct ActivityResult {
  bool isCancelled = false;
  ResultVariant data;

  explicit ActivityResult() = default;

  template <typename ResultType>
    requires std::is_constructible_v<ResultVariant, ResultType&&>
  // cppcheck-suppress noExplicitConstructor
  ActivityResult(ResultType&& result) : data{std::forward<ResultType>(result)} {}
};

using ActivityResultHandler = std::function<void(const ActivityResult&)>;
