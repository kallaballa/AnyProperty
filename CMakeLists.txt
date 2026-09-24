cmake_minimum_required(VERSION 3.14)
project(AnyProperty VERSION 1.0.0 LANGUAGES CXX)

add_library(anyproperty INTERFACE)
add_library(AnyProperty::AnyProperty ALIAS anyproperty)
target_include_directories(anyproperty INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>)
target_compile_features(anyproperty INTERFACE cxx_std_17)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()