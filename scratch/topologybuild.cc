#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"

using namespace ns3;

int main(int argc, char *argv[]){
	//creating network nodes
	NodeContainer hosts;
	NodeContainer routers;
	NodeContainer branch;

	hosts.Create(1);
	routers.Create(4);
	branch.Create(3);
	//install stack on each node
	InternetStackHelper stack;
	stack.Install(hosts);
	stack.Install(routers);
	stack.Install(branch);

	//create csma helper
	CsmaHelper csma;

	//create point to point helper
	PointToPointHelper p2p;

	//create address helper
	Ipv4AddressHelper address;

	//configuring subnets
	//create a node container to hold the nodes that belong to subnet1 and workstation1 and router 1
	NodeContainer subnet1;
	subnet1.Add(hosts.Get(0));
	subnet1.Add(routers.Get(0));

	//create a device container to hold net devices installed on each node
	NetDeviceContainer subnet1Devices = p2p.Install(subnet1);

	//configure the subnet address and mask
	address.SetBase("10.1.1.0","255.255.255.0");

	//create an interface container to hold the ipv4 created and assign ip address to each interface
	Ipv4InterfaceContainer subnet1Interfaces = address.Assign(subnet1Devices);

	NodeContainer subnet2;
	subnet2.Add(routers.Get(0));
	subnet2.Add(routers.Get(1));

	NetDeviceContainer subnet2Devices;
	subnet2Devices = p2p.Install(subnet2);
	address.SetBase("10.1.2.0","255.255.255.0");
	Ipv4InterfaceContainer subnet2Interfaces = address.Assign(subnet2Devices);

	NodeContainer subnet3;
	subnet3.Add(routers.Get(1));
	subnet3.Add(routers.Get(2));

	NetDeviceContainer subnet3Devices = p2p.Install(subnet3);
	address.SetBase("10.1.3.0","255.255.255.0");
	Ipv4InterfaceContainer subnet3Interfaces = address.Assign(subnet3Devices);

	NodeContainer subnet4;
	subnet4.Add(routers.Get(1));
	subnet4.Add(routers.Get(3));

	NetDeviceContainer subnet4Devices = p2p.Install(subnet4);
	address.SetBase("10.1.4.0","255.255.255.0");
	Ipv4InterfaceContainer subnet4Interfaces = address.Assign(subnet4Devices);

	//create node container belong to subnet5
	NodeContainer subnet5;
	subnet5.Add(routers.Get(2));
	subnet5.Add(branch);

	NetDeviceContainer subnet5Devices = csma.Install(subnet5);
	address.SetBase("10.1.5.0","255.255.255.0");
	Ipv4InterfaceContainer subnet5Interfaces = address.Assign(subnet5Devices);

	//run simulator

	Simulator::Run();
	Simulator::Destroy();

	return 0;
}
