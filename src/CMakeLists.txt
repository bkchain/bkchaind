cmake_minimum_required(VERSION 2.8 FATAL_ERROR)
project(bkchaind)
ADD_DEFINITIONS(-std=c++11)
include_directories(../leveldb ../leveldb/include ../boost_1_55 ../websocketpp .)

# Force NDEBUG so that assert are always active, even in release build
ADD_DEFINITIONS( -UNDEBUG )

add_subdirectory(../json-rpc-cpp json-rpc-cpp)

add_custom_command(OUTPUT ${PROJECT_SOURCE_DIR}/../leveldb/libleveldb.a COMMAND make WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/../leveldb)
add_custom_target(libleveldb DEPENDS ${PROJECT_SOURCE_DIR}/../leveldb/libleveldb.a)

add_executable(bkchaind
  AddressOperations.cpp
  Blocks.cpp
  CacherServer.cpp
  Database.cpp
  httpclientasio.cpp
  main.cpp
  BitcoinTX.cpp
  BitcoinRPC.cpp
  BitcoinLiveTX.cpp
  ScriptDecoder.cpp
  llvm/Support/SmallVector.cpp
  ../blockchain/Base58.cpp
  ../blockchain/BitcoinAddress.cpp
  ../blockchain/BitcoinScript.cpp
  ../blockchain/BitcoinTransactions.cpp
  ../blockchain/BlockChain.cpp
  ../blockchain/RIPEMD160.cpp
  ../blockchain/SHA256.cpp)

add_dependencies(bkchaind libleveldb)
add_library(leveldb STATIC IMPORTED)
add_dependencies(leveldb libleveldb)
add_definitions(-D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS)
set_property(TARGET leveldb PROPERTY IMPORTED_LOCATION ${PROJECT_SOURCE_DIR}/../leveldb/libleveldb.a)
find_package(Boost 1.49.0 COMPONENTS program_options system filesystem thread)
target_link_libraries(bkchaind
  jsonrpcStatic
  ${Boost_PROGRAM_OPTIONS_LIBRARY}
  ${Boost_FILESYSTEM_LIBRARY}
  ${Boost_SYSTEM_LIBRARY}
  ${Boost_THREAD_LIBRARY}
  leveldb)
