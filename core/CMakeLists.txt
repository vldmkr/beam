set(CORE_SRC
    uintBig.cpp
    ecc.cpp
    ecc_bulletproof.cpp
    aes.cpp
    block_crypt.cpp
    block_rw.cpp
    block_validation.cpp
    difficulty.cpp
    chainwork.cpp
    navigator.cpp
    radixtree.cpp
    merkle.cpp
    proto.cpp
    peer_manager.cpp
    fly_client.cpp
    treasury.cpp
# ~etc
)

set(THIRD_CRYPTO_SRC
    ${PROJECT_SOURCE_DIR}/3rdparty/hmac_sha512.c
    ${PROJECT_SOURCE_DIR}/3rdparty/pkcs5_pbkdf2.c
    ${PROJECT_SOURCE_DIR}/3rdparty/sha512.c
    ${PROJECT_SOURCE_DIR}/3rdparty/zeroize.c
    )

add_library(core STATIC ${CORE_SRC} ${THIRD_CRYPTO_SRC})

add_dependencies(core p2p pow)
target_link_libraries(core p2p pow)

target_include_directories(core PUBLIC ${PROJECT_SOURCE_DIR}/3rdparty/secp256k1-zkp/src)
target_include_directories(core PUBLIC ${PROJECT_SOURCE_DIR}/3rdparty)

include_directories(${PROJECT_SOURCE_DIR}/core)

add_subdirectory(unittest)
