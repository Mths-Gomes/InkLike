cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(CompletedBooksTests LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
  enable_testing()
endif()

find_path(ARDUINOJSON_INCLUDE_DIR ArduinoJson.h
  PATHS "${REPO_ROOT}/.pio/libdeps/default/ArduinoJson/src")
if(NOT ARDUINOJSON_INCLUDE_DIR)
  include(FetchContent)
  FetchContent_Declare(completed_books_json
    GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
    GIT_TAG v7.4.2)
  FetchContent_MakeAvailable(completed_books_json)
  set(ARDUINOJSON_INCLUDE_DIR "${completed_books_json_SOURCE_DIR}/src")
endif()

add_executable(CompletedBooksTest CompletedBooksTest.cpp
  ${REPO_ROOT}/src/activities/reader/CompletedBooksStore.cpp)
target_include_directories(CompletedBooksTest PRIVATE
  stubs ${ARDUINOJSON_INCLUDE_DIR}
  ${REPO_ROOT}/src/activities/reader ${REPO_ROOT}/lib/I18n)
target_compile_options(CompletedBooksTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME CompletedBooksTest COMMAND CompletedBooksTest)
