#PCE
Path Computation Element for Cisco routers using Dijkstra's shortest path algorithm.

##Description
This software is a Traffic Engineering application which takes as input the network and installs a LSP (Label Switched Path).

The remote login session for the routers configuration is via Telnet and the application will behave like PCE (Path Computation Element) and send commands to the routers.

The path must comply with the constraints of total and used capacity, then is used a search algorithm of the least cost path.

The program can be operated in three different modes:
- real;
- simulation with GNS3;
- demo.

The project is implemented in C++ with the help of some Bash script and XML.

##Network topology
The topology of the network was created through the implementation class *Topology* where the *n* attribute indicates the number of routers (nodes) on the network and the attribute *adjMatrix* corresponds to a square matrix of adjacencies, where each cell is the link between the node *i* and the node *j* of the network; each of them also contains key information for the description of the link (capacity, used bandwidth, source and destination addresses and interfaces. The attribute *loopbackArray* corresponds to a vector of loopback addresses of the nodes in the network.

For the realization of the project we were created two applications: Save and Load Topology. The first is only used to create the equivalent in XML network topology. The second is used initially to import network topology.

##Dijkstra algorithm
The paths are calculated using Dijkstra's shortest path algorithm. If a path between source and destination requires a capability that the links calculated by the algorithm are not able to provide, the path must be changed in order to meet the demands (so, bounded algorithm). The cost of a path is measured in number of hops.

The total capacity of the link and of them used is stored in files containing network topologies. Every time you run the function that creates the LSP, the capacity use of the links will be updated.

It is also implemented a version of Dijkstra that does not take into account the constraints related to the capacity; it is used in demo mode to compare the LSPs in case of Traffic Engineering Application or not.

##Execution
The program can run in three modes:
- GNS3: The topology introduced is considered not real but virtual, emulated program in GNS3. To communicate with the network is necessary to enable and configure the interface tapX. Necessary operations are performed by the script called *config_tap.sh expect*.
- Real: The topology is considered real. There is no additional preliminary steps and the rest of the operations are performed directly on the physical routers.
- Demo: The topology is considered for demonstration purposes only. Program execution does not act directly on any physical element or emulated, but only shows the user the output generated by the program.

After the choice of method of execution is shown a menu that allows the user to perform certain actions:
- *PrintAdjMatrix*: print screen contents of the adjacency matrix, representing the topology of the network.
- *ConfigureNet*: configures the network to be ready to receive commands for installing the LSP. Why this should happen at each router must be enabled CEF(Cisco Express Forwarding) both globally and at the level of each interface. Also it must also be enabled MPLS. All necessary operations are performed through the script called *cef.sh*.
- *InstallLSP*: allows installation of a tunnel LSP. The user is prompted the index of the ingress router of the tunnel and the index of the exit router of the tunnel, in addition to the capacity of the same. Right now the program acts as a PCE, running the Dijkstra's algorithm on the adjacency matrix and finds a valid path. Necessary operations are performed through the script called *lsp.sh*.
- *Exit*: exit the program.

###Build load topology and save topology
```
gcc load_topology.cc config_topology.cpp -lpdel -lexpat -lpthread -lstdc++
gcc save_topology.cc config_topology.cpp -lpdel -lexpat -lpthread -lstdc++
```
###Required libraries
```
libxml2
libxml++
libxml++-dev
libstdc++
expat
libexpat1-dev
libssl
```