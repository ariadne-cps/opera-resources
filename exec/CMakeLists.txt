set(EXECS
        input_check
        input_plot
        process
        ice_demo
        output_plot
        compared_check
    )

foreach(EXEC ${EXECS})
    add_executable(${EXEC} ${EXEC}.cpp)
    target_link_libraries(${EXEC} opera)
endforeach()
