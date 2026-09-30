cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(ClickBetweenFrames)

file(GLOB SOURCE_FILES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/dllmain.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/geode.cpp)
add_library(cbf STATIC ${SOURCE_FILES})
add_library(${PROJECT_NAME} SHARED src/dllmain.cpp)

# this is so stupid
set(CMAKE_SIZEOF_VOID_P 4)

if (${CMAKE_CXX_COMPILER_ID} STREQUAL Clang)
  # ensure 32 bit on clang
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -target i386-pc-windows-msvc")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -target i386-pc-windows-msvc")
  add_definitions("--target=i386-pc-windows-msvc")
endif()

target_include_directories(cbf PRIVATE
  libraries/minhook/include
  libraries/megahack
)

add_subdirectory(libraries/minhook)
add_subdirectory(libraries/cocos-headers)

target_link_libraries(cbf PRIVATE minhook cocos2d ${CMAKE_CURRENT_SOURCE_DIR}/libraries/megahack/hackpro.lib delayimp)
target_link_libraries(${PROJECT_NAME} cbf)
target_link_options(${PROJECT_NAME} PRIVATE /DELAYLOAD:hackpro.dll)

if (DEFINED ENV{GEODE_SDK} AND EXISTS $ENV{GEODE_SDK}/VERSION)
  file(READ $ENV{GEODE_SDK}/VERSION GEODE_SDK_VERSION)
endif()

if (GEODE_SDK_VERSION MATCHES "^1\\.")
  add_library(ClickBetweenFramesGeode SHARED src/geode.cpp)
  add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
  target_link_libraries(ClickBetweenFramesGeode cbf)
  target_link_options(ClickBetweenFramesGeode PRIVATE /DELAYLOAD:hackpro.dll)
  setup_geode_mod(ClickBetweenFramesGeode DONT_INSTALL)
endif()
