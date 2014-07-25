lwip-dpdk
=========

Takayuki is awesome!

## How to build lwip-dpdk

### Prepare all submodules

    $ git submodule init
    $ git submodule update

### Build DPDK

    $ cd dpdk
    $ make install T=x86_64-native-linuxapp-gcc

### Build lwip-dpdk

    $ ./configure
    $ make

## Show debug messages in lwIP

    $ ./configure --enable-debug
    $ make

    Then, use -d option with lwip-dpdk.

## How to enable PCAP Poll Mode Driver

    $ cd dpdk/x86_64-native-linuxapp-gcc
    $ vi .config

    Set `CONFIG_RTE_LIBRTE_PMD_PCAP` to y(es) which is n(o) by default.

    $ make
