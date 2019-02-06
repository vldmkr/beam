set (TARGET_NAME "mnemonic")

set(MNEMONIC_SRC
    dictionary.cpp
    mnemonic.cpp
)

add_library(${TARGET_NAME} STATIC ${MNEMONIC_SRC})
add_dependencies(${TARGET_NAME} core)
target_link_libraries(${TARGET_NAME} core)

add_subdirectory(unittests)
