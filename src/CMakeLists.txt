add_library(qe_common INTERFACE)
target_include_directories(qe_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(qe_storage
  storage/disk_manager.cpp
  storage/buffer_pool.cpp
)
target_include_directories(qe_storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qe_storage PUBLIC qe_common)
