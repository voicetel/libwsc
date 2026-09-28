find_package(Threads REQUIRED)

foreach(t test_functional test_stall test_close)
  add_executable(${t} ${t}.cpp)
  target_link_libraries(${t} PRIVATE libwsc Threads::Threads)
endforeach()
