cmake_minimum_required(VERSION 3.12)

project(Cyberverse.BotHarness LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

# Include Valve's GameNetworkingSockets (same vcpkg port as server/Native and client/red4ext)
find_package(GameNetworkingSockets CONFIG REQUIRED)

add_executable(bot-harness
  src/main.cpp
  src/BotClient.cpp
)

# Reuse the exact protocol headers and zpp_bits the real client/server use — no protocol drift.
target_include_directories(bot-harness PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/protocol"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../server/Native/deps/zpp_bits"
)

target_link_libraries(bot-harness PRIVATE GameNetworkingSockets::shared)
