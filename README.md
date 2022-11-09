# L2shapingL2shaping

L2shaping is a traffic shaping tool based on DPDK development. This tool mainly through accurate control the traffic workload rate, packet interval and other traffic characteristics , to achieve the simulation of multi-class network conditions.In detail,L2shaping buffer and transform the irregular traffic from the workload generator , make the traffic eventually reaches the server-side with the rate or packet interval distribution that simulate the actual scene, L2shaping provide rate control,packet interval distribution control, high latency simulation, in-flow out-of-order simulation, and packet loss simulation.L2shaping enables packet interval control with microsecond accuracy. 

Supported features
 - Rate control

    L2shaping uses placeholder pack filling for rate control. The granularity of the rate control is 0.01% of the line rate. 

 - Packet interval distribution control

    L2shaping supports setting the packet interval of microsecond accuracy, for example 50 microseconds. Based on the packet interval control, L2shaping provides the function of shaping the packet interval distribution image of the test load, for example, shaping the packet interval of the traffic load into a fixed interval or random distribution interval.

 - Delay simulation

    L2shaping realizes the simulation of fixed delay and random delay. In the stochastic delay, the stochastic fluctuation delay with correlation coefficient and the distributed delay conforming to the statistical distribution model are provided.

 - Out-of-order Simulation

    L2shaping implements a specified proportion of in-flow out-of-order for a specified range of streams.

 - Statistical distribution support

    L2shaping supports specific statistical distribution in terms of packet interval distribution and delay distribution.

    You can use the tool in the **tools** directory to generate a distribution model file using the distribution data and configure the model file in run.sh.

    In the **dist** directory, we provide some commonly used model files of statistical distribution, such as normal distribution, Pareto distribution, Poisson distribution with LAMDA value of 2,4, and 6, and Chi-square distribution with freedom of 6.


### Prerequisites
* libdpdk (Intel's DPDK package*, DPDK-19.04 best) 
* libnuma

### Included directories

```bash
./    
|__dist/	Distribution file,include normal distributon,pareto distribution,chi_square Distribution.etc
|__tools/   The tool for generate distributed files
```

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

### Setup L2shaping

#### Compile
```bash
$ cd <path to L2shaping>
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
### Release Note
###  V1.0 (August 24, 2022)
* **Rate control updated**

   In the current version, L2shaping is mainly used for mixing invalid packets of variable length for rate control.

* **Features added**

   * **Packet interval distribution control** 

   * **Delay simulation** 

   * **Out-of-order Simulation**  

   * **Statistical distribution support**


### Roadmap

+ Drop simulation support
+ Detailed configuration documents
+ 40Gbps & 100Gbps support

## Acknowledgments

L2shaping is a traffic shaping tool based on DPDK development. The version 1.0 is able to support 10Gps traffic input&output. The work was supported by the Strategic Priority Research Program of the Chinese Academy of Sciences under Grant No. XDA06010401, the National Key Research and Development Program of China under Grant No. 2016YFB1000203 & 2017YFB1001602. We appreciated project leaders, Prof. Xu Zhiwei, Prof. Chen Mingyu and Prof. Bao Yungang, for their selfless support and guidance.


## Contacts
Issue board is the preferred way to report bugs and ask questions about QStack or contact Zhang Wenli (zhangwl at ict.ac.cn).