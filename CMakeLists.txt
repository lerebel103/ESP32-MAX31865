
# Add include directory to INCLUDE path
set(COMPONENT_ADD_INCLUDEDIRS "${CMAKE_CURRENT_SOURCE_DIR}/include")

set(COMPONENT_SRCS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Max31865.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/numerics.cpp"
        )

register_component()

SET(CMAKE_CXX_STANDARD -std=c++14)
SET(CMAKE_C_STANDARD -std=c11)
