find_package(Threads REQUIRED)

add_executable(anyproperty_tests anyproperty_tests.cpp)
target_link_libraries(anyproperty_tests PRIVATE
  AnyProperty::AnyProperty
  Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(anyproperty_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME anyproperty_tests COMMAND anyproperty_tests)