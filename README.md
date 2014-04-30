# bkchaind

Blockchain explorer daemon.
http://bkchain.org

## Features
* JSON API: block and tx info, address balance, operations and unspent outputs
* Websocket live blocks/transactions
* Multicurrency (BTC, LTC, PPC, DOGE)
* Linux and Visual Studio
* Grab data directly from both HDD blockchain data (fast initial setup) and bitcon RPC (live mode)
* Fast, typical blockchain initial import time with SSD: PPC 1 min, LTC approx 10 min, BTC a few hours (can probably be improved)

## Install
* Build with CMake: mkdir build && cd build && cmake .. && make bkchaind
* Build with MSVC2013: get boost 1.55 in boost_1_55_0 subfolder, build it (static), open bkchaind.sln and build
* Linux only: since it read data directly from HDD blockchain, a very small patch is required on bitcoind, litecoind, ppcoind or dogecoind (in daemon-patches)

## Run
* Typical command line: --blockchain=C:\Users\[yourusername]\AppData\Roaming\Bitcoin --database=btc --write-buffer=128 (all data will go into a "btc" subfolder)

## TODO

* Live address TX
* Improve code quality, security and comments
* Various documentation for database formats
* Other features...
* ...

## Dependencies

Most of them are included:
* websocketpp (submodule)
* json-rpc-cpp (submodule)
* leveldb (submodule)
* blockchain (https://code.google.com/p/blockchain/, with various changes)

## Donations

Donation appreciated, to support development and server costs

* BTC: 1LLqMFskaSaZ3w2LuH6dbQaULcy1Bu1b2R
* PPC: PEiZ7r4KR85izAhfzQKJgJxdQkQ6PPXXEX
* LTC: LXzHvrRMQVEHj6gvK4rsZ7HGopgqfxS7PS