add_library(qe_common INTERFACE)
target_include_directories(qe_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(qe_storage
  storage/disk_manager.cpp
  storage/buffer_pool.cpp
)
target_include_directories(qe_storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qe_storage PUBLIC qe_common)

add_library(qe_heap heap/heap_file.cpp)
target_include_directories(qe_heap PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qe_heap PUBLIC qe_storage)

add_library(qe_catalog catalog/catalog.cpp)
target_link_libraries(qe_catalog PUBLIC qe_heap)

add_library(qe_gen gen/data_generator.cpp)
target_link_libraries(qe_gen PUBLIC qe_heap)

add_executable(gen_data tools/gen_data.cpp)
target_link_libraries(gen_data PRIVATE qe_gen qe_catalog qe_heap qe_storage)

add_library(qe_exec INTERFACE)
target_link_libraries(qe_exec INTERFACE qe_heap)

add_library(qe_ref_executor tools/ref_executor.cpp)
target_link_libraries(qe_ref_executor PUBLIC qe_heap)

add_executable(run_query tools/run_query.cpp)
target_link_libraries(run_query PRIVATE qe_exec qe_heap qe_storage)
