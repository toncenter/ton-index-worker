cmake_minimum_required(VERSION 3.16)

add_executable(pipeline-poc
        src/main.cpp
        src/Scheduler.cpp
        src/DbReader.cpp
        src/IndexOptions.cpp
        src/IndexOptions.h
        src/PipelineBlock.cpp
        src/PipelineBlock.h
        src/IndexDataContainer.cpp
        src/IndexDataContainer.h
        src/Sandbox.h
        src/Sandbox.cpp
)

target_include_directories(pipeline-poc
    PUBLIC external/ton
    PUBLIC external/libpqxx
    PUBLIC tondb-scanner/src
    PUBLIC src/
)

target_link_directories(pipeline-poc
    PUBLIC external/ton
    PUBLIC external/libpqxx
)
target_compile_features(pipeline-poc PRIVATE cxx_std_17)
target_link_libraries(pipeline-poc adnl tl_api dht overlay tdutils tdactor src_parser
        catchain validatorsession ton_crypto validator-disk ton_validator validator-disk smc-envelope tondb-scanner)
 target_link_options(pipeline-poc PUBLIC -rdynamic)

install(TARGETS pipeline-poc RUNTIME DESTINATION bin)
