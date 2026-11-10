cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(ReserveFuelTests LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
  add_library(crosspoint_test_common INTERFACE)
  target_compile_options(crosspoint_test_common INTERFACE -Wall -Wextra -Werror)
  enable_testing()
endif()

add_executable(ReserveFuelTest
  ReserveFuelTest.cpp
  ${REPO_ROOT}/src/activities/reader/ReserveFuel.cpp
  ${REPO_ROOT}/src/activities/reader/ReserveFuelStore.cpp
)
target_include_directories(ReserveFuelTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/book_reading_stats_atomic/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/activities/reader
)
target_link_libraries(ReserveFuelTest PRIVATE crosspoint_test_common)
add_test(NAME ReserveFuelTest COMMAND ReserveFuelTest)

add_executable(ReserveFuelIntegrationTest
  ReserveFuelIntegrationTest.cpp
  ${REPO_ROOT}/src/activities/reader/GlobalReadingStats.cpp
  ${REPO_ROOT}/src/activities/reader/ReadingStatsUtils.cpp
  ${REPO_ROOT}/src/activities/reader/ReserveFuel.cpp
  ${REPO_ROOT}/src/activities/reader/ReserveFuelStore.cpp
)
target_include_directories(ReserveFuelIntegrationTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/book_reading_stats_atomic/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/activities/reader
)
target_link_libraries(ReserveFuelIntegrationTest PRIVATE crosspoint_test_common)
add_test(NAME ReserveFuelIntegrationTest COMMAND ReserveFuelIntegrationTest)
