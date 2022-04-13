## Background
The test under complex network condition is an important part of the stability and reliability test of the server software stack. Complex network condition includes various waveforms of traffic, different rates of traffic, and the characteristics of traffic burst.In the current network domain，the common method of constructing a specific waveform through the combination of machines in the load generating cluster often fails to obtain the ideal waveform，software applied to a single load generating machine has limited scalability in simulating complex network conditions.
## Introduction
We proposes a method by introducing traffic shaping forwarding nodes between client and server architectures.receives the traffic from the consolidated client and forwards the traffic to the server shaping.In this way, the rate and waveform of traffic finally reaching the server are controlled and the emergent characteristics of traffic in complex networks are provided.shaping tool L2shaping based on DPDK development, provides the following functions:traffic shaping, rate control, network packet loss simulation.The test results of this tool effectively shapes irregular traffic into continuous rate traffic, and its speed control method is better than the traditional software speed control method in terms of stability and accuracy, which provides convenience for the test under complex network conditions.

## Installation

### DPDK intsall

```bash
install DPDK
$ cd <dpdk-home-path>/usertools/
$ ./dpdk-setup.sh
     - Press [38] x86_64-native-linux-gcc to compile the package
     - Press [45] Insert IGB UIO module to install the driver
     - Press [49] Setup hugepage mappings for NUMA systems to setup hugepages(20GB for each node best)
     - Press [51] Bind Ethernet/Baseband/Crypto device to IGB UIO module
     - Press [62] Exit Script to quit the tool
```


### Setup l2shaping

#### Compile
```bash
$ cd <path to l2shaping>
$ vim Makefile
  # Add two configurations at the beginning as below
  RTE_SDK= <dpdk-home-path>
  RTE_TARGET=x86_64-native-linuxapp-gcc
$ make
```

#### Run
```bash
$ ./run.sh 
```