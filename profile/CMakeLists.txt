set(PROFILE_FILES
    profile_scenario
)

foreach(PROFILE ${PROFILE_FILES})
    add_executable(${PROFILE} ${PROFILE}.cpp)
    target_link_libraries(${PROFILE} opera)
endforeach()
