cmake_minimum_required(VERSION 3.16)

add_executable(ton-trace-emulator
    src/main.cpp
    src/TraceEmulator.cpp
    src/TraceInserter.cpp
    src/OverlayListener.cpp
    src/BlockEmulator.cpp
    src/TraceScheduler.cpp
    src/TraceInterfaceDetector.cpp
)

target_include_directories(ton-trace-emulator
    PUBLIC external/ton
    PUBLIC src/
    PUBLIC tondb-scanner/src
    PUBLIC external/hiredis
    PUBLIC external/redis-plus-plus/src
    PUBLIC ${CMAKE_BINARY_DIR}/external/redis-plus-plus/src
    PUBLIC external/msgpack-c
)

target_link_directories(ton-trace-emulator
    PUBLIC external/ton
    PUBLIC external/hiredis
    PUBLIC external/redis-plus-plus
    PUBLIC external/msgpack-c
)

target_compile_features(ton-trace-emulator PRIVATE cxx_std_17)
target_link_libraries(ton-trace-emulator tondb-scanner overlay tdutils tdactor adnl tl_api dht ton_crypto
        catchain validatorsession validator-disk ton_validator validator-disk smc-envelope emulator pqxx hiredis redis++ msgpack-cxx)

install(TARGETS ton-trace-emulator RUNTIME DESTINATION bin)
