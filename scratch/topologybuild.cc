#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"

using namespace ns3;

int main(int argc, char *argv[]){
	//Command Line Variables
	std::string speed = "10Mbps";

	CommandLine cmd;
	cmd.AddValue("DefaultRate","Default data rate to be used on network devices",speed);
	cmd.Parse(argc,argv);

	std::cout << "speed " << speed << std::endl;

	//configuring default Attributes
	DataRate rate(speed);
	Time delay("2ms");

	Config::SetDefault("ns3::PointToPointNetDevice::DataRate",DataRateValue(rate));
	Config::SetDefault("ns3::PointToPointChannel::Delay",TimeValue(delay));

	Config::SetDefault("ns3::CsmaChannel::DataRate",DataRateValue(rate));
	Config::SetDefault("ns3::CsmaChannel::Delay",TimeValue(delay));

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
	csma.SetChannelAttribute("DataRate",StringValue("100Mbps"));
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

	//Configuring attributes for subnet2
	DataRate rate_subnet2("50Mbps");
	Time delay_subnet2("4ms");

	NetDeviceContainer subnet2Devices;
	subnet2Devices = p2p.Install(subnet2);
	address.SetBase("10.1.2.0","255.255.255.0");
	Ipv4InterfaceContainer subnet2Interfaces = address.Assign(subnet2Devices);

	Config::Set("/NodeList/1/DeviceList/2/$ns3::PointToPointNetDevice/DataRate",DataRateValue(rate_subnet2));
	Config::Set("/NodeList/2/DeviceList/1/$ns3::PointToPointNetDevice/DataRate",DataRateValue(rate_subnet2));
	Config::Set("/ChannelList/1/$ns3::PointToPointChannel/Delay",TimeValue(delay_subnet2));

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

	//Configure net Device on subnet 4
	Ptr<NetDevice> deviceA = subnet4Devices.Get(0);
	Ptr<NetDevice> deviceB = subnet4Devices.Get(1);

	NetDevice* deviceA_ptr = PeekPointer(deviceA);
	NetDevice* deviceB_ptr = PeekPointer(deviceB);

	PointToPointNetDevice* p2pDeviceA = dynamic_cast<PointToPointNetDevice*>(deviceA_ptr);
	PointToPointNetDevice* p2pDeviceB = dynamic_cast<PointToPointNetDevice*>(deviceB_ptr);

	DataRate rate_subnet4("100Mbps");

	p2pDeviceA->SetAttribute("DataRate",DataRateValue(rate_subnet4));
	p2pDeviceB->SetAttribute("DataRate",DataRateValue(rate_subnet4));

	//Configuring the channel on subnet4
	Ptr<Channel> channel = ChannelList::GetChannel(3);
	Channel* channel_ptr = PeekPointer(channel);

	Time delay_subnet4("5ms");

	PointToPointChannel* p2pchannel = dynamic_cast<PointToPointChannel*>(channel_ptr);
	p2pchannel->SetAttribute("Delay",TimeValue(delay_subnet4));
	address.SetBase("10.1.4.0","255.255.255.0");
	Ipv4InterfaceContainer subnet4Interfaces = address.Assign(subnet4Devices);


	//create node container belong to subnet5
	NodeContainer subnet5;
	subnet5.Add(routers.Get(2));
	subnet5.Add(branch);

	NetDeviceContainer subnet5Devices = csma.Install(subnet5);
	address.SetBase("10.1.5.0","255.255.255.0");
	Ipv4InterfaceContainer subnet5Interfaces = address.Assign(subnet5Devices);

	//Adding Applications

	Ipv4Address FS_Address(subnet5Interfaces.GetAddress(1));
	uint16_t FS_port = 4500;

	UdpEchoClientHelper WS1Echo(FS_Address,FS_port);
	ApplicationContainer WS1EchoApp = WS1Echo.Install(hosts.Get(0));
	WS1EchoApp.Start(Seconds(1.0));
	WS1EchoApp.Stop(Seconds(2.0));

	UdpEchoServerHelper FS(FS_port);
	ApplicationContainer FS_App = FS.Install(subnet5.Get(1));
	FS_App.Start(Seconds(1.0));
	FS_App.Stop(Seconds(10.0));

	Ipv4Address WS2_Address(subnet5Interfaces.GetAddress(2));
	uint16_t WS2_port = 7250;

	InetSocketAddress WS2_Socket_Address(WS2_Address,WS2_port);

	OnOffHelper WS1_OnOff("ns3::UdpSocketFactory",WS2_Socket_Address);
	ApplicationContainer WS1_OnOffApp = WS1_OnOff.Install(hosts.Get(0));
	WS1_OnOffApp.Start(Seconds(1.0));
	WS1_OnOffApp.Stop(Seconds(10.0));

	PacketSinkHelper WS2Sink("ns3::UdpSocketFactory",WS2_Socket_Address);
	ApplicationContainer WS2_SinkApp = WS2Sink.Install(branch.Get(1));
	WS2_SinkApp.Start(Seconds(1.0));
	WS2_SinkApp.Stop(Seconds(10.0));

	Ipv4Address Printer_Address(subnet5Interfaces.GetAddress(3));
	uint16_t Printer_port = 2650;
	UdpClientHelper WS2_Udp(Printer_Address,Printer_port);
	ApplicationContainer WS2_UdpApp = WS2_Udp.Install(branch.Get(1));
	WS2_UdpApp.Start(Seconds(1.0));
	WS2_UdpApp.Stop(Seconds(10.0));

	UdpServerHelper Printer(Printer_port);
	ApplicationContainer PrinterApp = Printer.Install(branch.Get(2));
	PrinterApp.Start(Seconds(1.0));
	PrinterApp.Stop(Seconds(10.0));

	//run simulator

	Simulator::Run();
	Simulator::Destroy();

	return 0;
}
