cmake_minimum_required(VERSION 3.16)

add_executable(sandbox-cpp
        src/main.cpp
        src/SandboxActor.cpp
)

target_include_directories(sandbox-cpp
        PUBLIC external/ton
        PUBLIC external/libpqxx
        PUBLIC tondb-scanner/src
        PUBLIC src/
)

target_link_directories(sandbox-cpp
        PUBLIC external/ton
        PUBLIC external/libpqxx
)
target_compile_features(sandbox-cpp PRIVATE cxx_std_17)
target_link_libraries(sandbox-cpp adnl tl_api dht overlay tdutils tdactor src_parser
        catchain validatorsession ton_crypto validator-disk ton_validator validator-disk smc-envelope tondb-scanner)
target_link_options(sandbox-cpp PUBLIC -rdynamic)

install(TARGETS sandbox-cpp RUNTIME DESTINATION bin)
