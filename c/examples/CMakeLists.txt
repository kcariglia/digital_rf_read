add_custom_target(examples)

function(BuildExample example_name file)
    set(CMAKE_BUILD_TYPE Debug)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -g")
    add_executable(${example_name} ${file})
    add_dependencies(examples ${example_name})
    set_target_properties(${example_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY .)
    target_link_libraries(${example_name} digital_rf)
endfunction()

BuildExample(benchmark_rf_write_hdf5 benchmark_rf_write_hdf5.c)
BuildExample(example_rf_write_hdf5 example_rf_write_hdf5.c)
BuildExample(example_with_sigint_handler example_with_sigint_handler.c)
BuildExample(example_rf_read_hdf5 example_rf_read_hdf5.c)
