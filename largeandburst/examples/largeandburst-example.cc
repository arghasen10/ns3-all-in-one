/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"
#include "ns3/largeandburst-helper.h"

using namespace ns3;
NS_LOG_COMPONENT_DEFINE("largeandburst-example");

int 
main (int argc, char *argv[])
{
  bool verbose = true;
  uint32_t nNodes;
  ns3::CommandLine cmd;
  cmd.AddValue ("verbose", "Tell application to log if true", verbose);
  cmd.AddValue ("nNodes","Number of Client Nodes",nNodes);
  cmd.Parse (argc,argv);

  LogComponentEnable("largeandburst-example",LOG_LEVEL_INFO);
  LogComponentEnable("PacketSink",LOG_LEVEL_INFO);

  NodeContainer nodes;
  nodes.Create(nNodes+1);
  NS_LOG_INFO("Total nodes created = " << nodes.GetN() << " including one server node");
  NodeContainer clients;

  InternetStackHelper stack;
  stack.Install(nodes);

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate",StringValue("5Mbps"));
  p2p.SetChannelAttribute("Delay",StringValue("2ms"));

  //Needed for getting ipv4 address of corresponding p2p server node
  std::vector<Ipv4InterfaceContainer> InterfaceList(nNodes);

  //Adding p2pchannel, netdevice, Ipv4interface inside for loop

  for(uint32_t i = 0; i < nNodes; i++){
    clients.Add(nodes.Get(i));
    NodeContainer p2pNodes;
    p2pNodes.Add(nodes.Get(i));
    p2pNodes.Add(nodes.Get(nNodes));
    NetDeviceContainer p2pdevices;
    p2pdevices = p2p.Install(p2pNodes);
    std::string ipaddress = "10.1."+std::to_string(i+1)+".0";
    Ipv4AddressHelper address;
    address.SetBase(ipaddress.c_str(),"255.255.255.0");
    InterfaceList[i] = address.Assign(p2pdevices);
  }

  //Global static Routing
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();  

  //Packet Sink at node(nNode)
  uint16_t sinkport = 8080;
  Address sinkAddress(InetSocketAddress(Ipv4Address::GetAny(),sinkport));
  PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory",sinkAddress);
  ApplicationContainer sinkApps = packetSinkHelper.Install (nodes.Get (nNodes));
  sinkApps.Start (Seconds (0.));
  sinkApps.Stop (Seconds (60.));

  //Client Applications
  BurstHelper burstHelper(1040,4,2);
  LargeHelper largeHelper(2000000,1040);
  ApplicationContainer clientApps;
  for(uint32_t i = 0; i < nNodes; i++){
    Address serverAddress(InetSocketAddress(InterfaceList[i].GetAddress(1),sinkport));
    burstHelper.SetAddress(serverAddress);
    largeHelper.SetAddress(serverAddress);
    clientApps.Add(burstHelper.Install(clients.Get(i)));
    clientApps.Add(largeHelper.Install(clients.Get(i)));
  }

  clientApps.Start(Seconds(1.));
  clientApps.Stop(Seconds(60.));

  NS_LOG_INFO("Check PCAP files largeandburst-example-*-*.pcap");
  p2p.EnablePcapAll("largeandburst-example");
  Simulator::Stop (Seconds (80));
  Simulator::Run ();
  Simulator::Destroy ();
}


