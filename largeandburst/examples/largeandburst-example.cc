/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"
#include "ns3/largeandburst-helper.h"

using namespace ns3;


int 
main (int argc, char *argv[])
{
  bool verbose = true;

  CommandLine cmd;
  cmd.AddValue ("verbose", "Tell application to log if true", verbose);

  cmd.Parse (argc,argv);

  LogComponentEnable("PacketSink",LOG_LEVEL_INFO);

  NodeContainer nodes;
  nodes.Create(2);
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate",StringValue("5Mbps"));
  p2p.SetChannelAttribute("Delay",StringValue("2ms"));
  NetDeviceContainer devices;
  devices = p2p.Install(nodes);
  InternetStackHelper stack;
  stack.Install(nodes);

  Ipv4AddressHelper address;
  address.SetBase("10.1.1.0","255.255.255.0");
  Ipv4InterfaceContainer interfaces = address.Assign(devices);

  uint16_t sinkport = 8080;
  Address sinkAddress(InetSocketAddress(interfaces.GetAddress(1),sinkport));
  PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), sinkport));
  ApplicationContainer sinkApps = packetSinkHelper.Install (nodes.Get (1));
  sinkApps.Start (Seconds (0.));
  sinkApps.Stop (Seconds (60.));
  
  BurstHelper burstdev(sinkAddress,1040,4,2);
  ApplicationContainer burstapp = burstdev.Install(nodes.Get(0));
  burstapp.Start(Seconds(1.));
  burstapp.Stop(Seconds(20.));

  LargeHelper ltdev(sinkAddress,2000000, 1040);
  ApplicationContainer ltapp = ltdev.Install(nodes.Get(0));
  ltapp.Start(Seconds(1.));
  ltapp.Stop(Seconds(20.));
  p2p.EnablePcapAll("largeandburst-example");
  Simulator::Stop (Seconds (80));
  Simulator::Run ();
  Simulator::Destroy ();
}


