

## Background
The test under complex network condition is an important part of the stability and reliability test of the server software stack. Complex network condition includes various waveforms of traffic, different rates of traffic, and the characteristics of traffic burst.In the current network domain，the common method of constructing a specific waveform through the combination of machines in the load generating cluster often fails to obtain the ideal waveform，software applied to a single load generating machine has limited scalability in simulating complex network conditions.
## Introduction
We proposes a method by introducing traffic shaping forwarding nodes between client and server architectures.receives the traffic from the consolidated client and forwards the traffic to the server shaping.In this way, the rate and waveform of traffic finally reaching the server are controlled and the emergent characteristics of traffic in complex networks are provided.shaping tool L2shaping based on DPDK development, provides the following functions:traffic shaping, rate control, network packet loss simulation.The test results of this tool effectively shapes irregular traffic into continuous rate traffic, and its rate control method is better than the traditional software rate control method in terms of stability and accuracy, which provides convenience for the test under complex network conditions.

## Installation

### DPDK intsall

```bash
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


## Release Note
### August 24, 2022
* **Features updated for rate control**

In the previous version, L2shaping is used for rate control by mixing invalid fixed length packets , which limits the flexibility of the rate control configuration and makes the granularity of the original rate control one-tenth of the line rate. In the current version, L2shaping is used for rate control by mixing invalid packets of variable length. The granularity of the rate control is updated to 1/10000 of the line rate.

* **Features added**

In addition to speed control, L2shaping adds the following functions in this update.

**Packet interval distribution control**   l2shaping supports setting the packet interval of microsecond accuracy, for example 50 microseconds. Based on the packet interval control, L2shaping provides the function of shaping the packet interval distribution image of the test load, for example, shaping the packet interval of the traffic load into a fixed interval or random distribution interval.

**Delay simulation**  l2shaping realizes the simulation of fixed delay and random delay. In the stochastic delay, the stochastic fluctuation delay with correlation coefficient and the distributed delay conforming to the statistical distribution model are provided.

**Disorder Simulation**   l2shaping implements a specified proportion of in-stream disorder for a specified range of streams.

* **Statistical distribution support**

l2shaping supports specific statistical distribution in terms of packet interval distribution and delay distribution.

You can use the tool in the **tools** directory to generate a distribution model file using the distribution data and configure the model file in run.sh.

In the **dist** directory, we provide some commonly used model files of statistical distribution, such as normal distribution, Pareto distribution, Poisson distribution with LAMDA value of 2,4, and 6, and Chi-square distribution with freedom of 6.


