# Each test is its own executable (no GoogleTest): plain assert-style
# checks via test_util.h, registered individually with CTest.
function(add_qe_test name)
  add_executable(${name} ${ARGN})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${name} PRIVATE qe_common qe_storage qe_heap qe_catalog qe_gen qe_exec qe_index qe_ref_executor)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_executable(test_smoke test_smoke.cpp)
target_include_directories(test_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME test_smoke COMMAND test_smoke)

add_qe_test(test_disk_manager test_disk_manager.cpp)
add_qe_test(test_buffer_pool test_buffer_pool.cpp)
add_qe_test(test_heap_file test_heap_file.cpp)
add_qe_test(test_data_generator test_data_generator.cpp)
add_qe_test(test_operators test_operators.cpp)
add_qe_test(test_bnlj test_bnlj.cpp)
add_qe_test(test_external_sort test_external_sort.cpp)
add_qe_test(test_smj test_smj.cpp)
add_qe_test(test_ghj test_ghj.cpp)
add_qe_test(test_bplus_tree test_bplus_tree.cpp)
add_qe_test(test_inlj test_inlj.cpp)
