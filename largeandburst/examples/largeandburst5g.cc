
#include "ns3/core-module.h"
#include "ns3/config-store.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/applications-module.h"
#include "ns3/mobility-module.h"
#include "ns3/log.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/mmwave-helper.h"
#include "ns3/nr-point-to-point-epc-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/config-store-module.h"
#include "ns3/mmwave-mac-scheduler-tdma-rr.h"
#include "ns3/largeandburst-helper.h"
using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("example");

int 
main (int argc, char *argv[])
{
  bool udpFullBuffer = false;
  int32_t fixedMcs = -1;
  uint16_t gNbNum = 1;
  uint16_t ueNumPergNb = 2;
  bool cellScan = false;
  double beamSearchAngleStep = 10.0;
  uint16_t numerologyBwp1 = 4;
  double frequencyBwp1 = 28e9;
  double bandwidthBwp1 = 100e6;
  uint16_t numerologyBwp2 = 2;
  double frequencyBwp2 = 28e9;
  double bandwidthBwp2 = 100e6;
  uint32_t udpPacketSizeUll = 100;
  uint32_t udpPacketSizeBe = 1252;
  uint32_t lambdaUll = 10000;
  uint32_t lambdaBe = 1000;
  bool singleBwp = false;
  std::string simTag = "default";
  std::string outputDir = "./";
  double totalTxPower = 4;
  bool logging = true;

  double simTime = 1; // seconds


  CommandLine cmd;

  cmd.AddValue ("simTime", "Simulation time", simTime);
  cmd.AddValue ("udpFullBuffer",
                "Whether to set the full buffer traffic; if this parameter is "
                "set then the udpInterval parameter will be neglected.",
                udpFullBuffer);
  cmd.AddValue ("fixedMcs",
                "The MCS that will be used in this example, -1 for auto",
                fixedMcs);
  cmd.AddValue ("gNbNum",
                "The number of gNbs in multiple-ue topology",
                gNbNum);
  cmd.AddValue ("ueNumPergNb",
                "The number of UE per gNb in multiple-ue topology",
                ueNumPergNb);
  cmd.AddValue ("cellScan",
                "Use beam search method to determine beamforming vector,"
                " the default is long-term covariance matrix method"
                " true to use cell scanning method, false to use the default"
                " power method.",
                cellScan);
  cmd.AddValue ("beamSearchAngleStep",
                "Beam search angle step for beam search method",
                beamSearchAngleStep);
  cmd.AddValue ("numerologyBwp1",
                "The numerology to be used in bandwidth part 1",
                numerologyBwp1);
  cmd.AddValue ("frequencyBwp1",
                "The system frequency to be used in bandwidth part 1",
                frequencyBwp1);
  cmd.AddValue ("bandwidthBwp1",
                "The system bandwidth to be used in bandwidth part 1",
                bandwidthBwp1);
  cmd.AddValue ("numerologyBwp2",
                "The numerology to be used in bandwidth part 2",
                numerologyBwp2);
  cmd.AddValue ("frequencyBwp2",
                "The system frequency to be used in bandwidth part 2",
                frequencyBwp2);
  cmd.AddValue ("bandwidthBwp2",
                "The system bandwidth to be used in bandwidth part 2",
                bandwidthBwp2);
  cmd.AddValue ("packetSizeUll",
                "packet size in bytes to be used by ultra low latency traffic",
                udpPacketSizeUll);
  cmd.AddValue ("packetSizeBe",
                "packet size in bytes to be used by best effort traffic",
                udpPacketSizeBe);
  cmd.AddValue ("lambdaUll",
                "Number of UDP packets in one second for ultra low latency traffic",
                lambdaUll);
  cmd.AddValue ("lambdaBe",
                "Number of UDP packets in one second for best effor traffic",
                lambdaBe);
  cmd.AddValue ("singleBwp",
                "Simulate with single BWP, BWP1 configuration will be used",
                singleBwp);
  cmd.AddValue ("simTag",
                "tag to be appended to output filenames to distinguish simulation campaigns",
                simTag);
  cmd.AddValue ("outputDir",
                "directory where to store simulation results",
                outputDir);
  cmd.AddValue ("totalTxPower",
                "total tx power that will be proportionally assigned to"
                " bandwidth parts depending on each BWP bandwidth ",
                totalTxPower);
  cmd.AddValue ("logging",
                "Enable logging",
                logging);


  cmd.Parse (argc, argv);
  NS_ABORT_IF (frequencyBwp1 < 6e9 || frequencyBwp1 > 100e9);
  NS_ABORT_IF (frequencyBwp2 < 6e9 || frequencyBwp2 > 100e9);


  LogComponentEnable("PacketSink",LOG_LEVEL_INFO);
  LogComponentEnable ("example",LOG_LEVEL_INFO);  
  Config::SetDefault ("ns3::MmWave3gppPropagationLossModel::ChannelCondition",
                      StringValue("l"));
  Config::SetDefault ("ns3::MmWave3gppPropagationLossModel::Scenario",
                      StringValue("UMi-StreetCanyon")); // with antenna height of 10 m
  Config::SetDefault ("ns3::MmWave3gppPropagationLossModel::Shadowing",
                      BooleanValue(false));

  Config::SetDefault ("ns3::MmWave3gppChannel::CellScan",
                      BooleanValue(cellScan));
  Config::SetDefault ("ns3::MmWave3gppChannel::BeamSearchAngleStep",
                      DoubleValue(beamSearchAngleStep));


  Config::SetDefault ("ns3::LteRlcUm::MaxTxBufferSize",
                      UintegerValue(999999999));

  Config::SetDefault("ns3::PointToPointEpcHelper::S1uLinkDelay", TimeValue (MilliSeconds(0)));


  if(singleBwp)
    {
      Config::SetDefault ("ns3::MmWaveHelper::NumberOfComponentCarriers", UintegerValue (1));
    }
  else
    {
      Config::SetDefault ("ns3::MmWaveHelper::NumberOfComponentCarriers", UintegerValue (2));
    }

  Config::SetDefault ("ns3::BwpManagerAlgorithmStatic::NGBR_LOW_LAT_EMBB", UintegerValue (0));
  Config::SetDefault ("ns3::BwpManagerAlgorithmStatic::GBR_CONV_VOICE", UintegerValue (1));

  // create base stations and mobile terminals
  NodeContainer gNbNodes;
  NodeContainer ueNodes;
  MobilityHelper mobility;

  double gNbHeight = 10;
  double ueHeight = 1.5;

  gNbNodes.Create (gNbNum);
  ueNodes.Create (ueNumPergNb * gNbNum);

  Ptr<ListPositionAllocator> apPositionAlloc = CreateObject<ListPositionAllocator> ();
  Ptr<ListPositionAllocator> staPositionAlloc = CreateObject<ListPositionAllocator> ();
  int32_t yValue = 0.0;

  for (uint32_t i = 1; i <= gNbNodes.GetN(); ++i)
    {
      // 2.0, -2.0, 6.0, -6.0, 10.0, -10.0, ....
      if (i % 2 != 0)
        {
          yValue = static_cast<int>(i) * 30;
        }
      else
        {
          yValue = -yValue;
        }

      apPositionAlloc->Add (Vector (0.0, yValue, gNbHeight));


      // 1.0, -1.0, 3.0, -3.0, 5.0, -5.0, ...
      double xValue = 0.0;
      for (uint32_t j = 1; j <= ueNumPergNb; ++j)
        {
          if (j % 2 != 0)
            {
              xValue = j;
            }
          else
            {
              xValue = -xValue;
            }

          if (yValue > 0)
            {
              staPositionAlloc->Add (Vector (xValue, 10, ueHeight));
            }
          else
            {
              staPositionAlloc->Add (Vector (xValue, -10, ueHeight));
            }
        }
    }

  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.SetPositionAllocator (apPositionAlloc);
  mobility.Install (gNbNodes);

  mobility.SetPositionAllocator (staPositionAlloc);
  mobility.Install (ueNodes);

  // setup the mmWave simulation
  Ptr<MmWaveHelper> mmWaveHelper = CreateObject<MmWaveHelper> ();
  mmWaveHelper->SetAttribute ("PathlossModel", StringValue ("ns3::MmWave3gppPropagationLossModel"));
  mmWaveHelper->SetAttribute ("ChannelModel", StringValue ("ns3::MmWave3gppChannel"));

  Ptr<MmWavePhyMacCommon> phyMacCommonBwp1 = CreateObject<MmWavePhyMacCommon>();
  phyMacCommonBwp1->SetCentreFrequency(frequencyBwp1);
  phyMacCommonBwp1->SetBandwidth (bandwidthBwp1);
  phyMacCommonBwp1->SetNumerology(numerologyBwp1);
  phyMacCommonBwp1->SetAttribute ("MacSchedulerType", TypeIdValue (MmWaveMacSchedulerTdmaRR::GetTypeId ()));
  phyMacCommonBwp1->SetCcId (0);

  BandwidthPartRepresentation repr1 (0, phyMacCommonBwp1, nullptr, nullptr, nullptr);
  mmWaveHelper->AddBandwidthPart (0, repr1);

  // if not single BWP simulation add second BWP configuration
  if (!singleBwp)
    {
      Ptr<MmWavePhyMacCommon> phyMacCommonBwp2 = CreateObject<MmWavePhyMacCommon>();
      phyMacCommonBwp2->SetCentreFrequency(frequencyBwp2);
      phyMacCommonBwp2->SetBandwidth (bandwidthBwp2);
      phyMacCommonBwp2->SetNumerology(numerologyBwp2);
      phyMacCommonBwp2->SetCcId(1);
      BandwidthPartRepresentation repr2 (1, phyMacCommonBwp2, nullptr, nullptr, nullptr);
      mmWaveHelper->AddBandwidthPart(1, repr2);
    }

  Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper> ();
  mmWaveHelper->SetEpcHelper (epcHelper);
  mmWaveHelper->Initialize();

  // install mmWave net devices
  NetDeviceContainer enbNetDev = mmWaveHelper->InstallEnbDevice (gNbNodes);
  NetDeviceContainer ueNetDev = mmWaveHelper->InstallUeDevice (ueNodes);

  double x = pow(10, totalTxPower/10);

  double totalBandwidth = 0;

  if (singleBwp)
    {
      totalBandwidth = bandwidthBwp1;
    }
  else
    {
      totalBandwidth = bandwidthBwp1 + bandwidthBwp2;
    }


  for (uint32_t j = 0; j < enbNetDev.GetN(); ++j)
    {
      ObjectMapValue objectMapValue;
      Ptr<MmWaveEnbNetDevice> netDevice = DynamicCast<MmWaveEnbNetDevice>(enbNetDev.Get(j));
      netDevice->GetAttribute("ComponentCarrierMap", objectMapValue);
      for (uint32_t i = 0; i < objectMapValue.GetN(); i++)
        {
          Ptr<ComponentCarrierGnb> bandwidthPart = DynamicCast<ComponentCarrierGnb>(objectMapValue.Get(i));
          if (i==0)
            {
              bandwidthPart->GetPhy()->SetTxPower(10*log10((bandwidthBwp1/totalBandwidth)*x));
              std::cout<<"\n txPower1 = "<<10*log10((bandwidthBwp1/totalBandwidth)*x)<<std::endl;
            }
          else if (i==1)
            {
              bandwidthPart->GetPhy()->SetTxPower(10*log10((bandwidthBwp2/totalBandwidth)*x));
              std::cout<<"\n txPower2 = "<<10*log10((bandwidthBwp2/totalBandwidth)*x)<<std::endl;
            }
          else
            {
              std::cout<<"\n Please extend power assignment for additional bandwidht parts...";
            }
        }
      //std::map<uint8_t, Ptr<ComponentCarrierGnb> > ccMap = objectMapValue.GetN()
    }

  // create the internet and install the IP stack on the UEs
  // get SGW/PGW and create a single RemoteHost
  Ptr<Node> pgw = epcHelper->GetPgwNode ();
  NodeContainer remoteHostContainer;
  remoteHostContainer.Create (1);
  Ptr<Node> remoteHost = remoteHostContainer.Get (0);
  InternetStackHelper internet;
  internet.Install (remoteHostContainer);

  // connect a remoteHost to pgw. Setup routing too
  PointToPointHelper p2ph;
  p2ph.SetDeviceAttribute ("DataRate", DataRateValue (DataRate ("100Gb/s")));
  p2ph.SetDeviceAttribute ("Mtu", UintegerValue (2500));
  p2ph.SetChannelAttribute ("Delay", TimeValue (Seconds (0.000)));
  NetDeviceContainer internetDevices = p2ph.Install (pgw, remoteHost);
  Ipv4AddressHelper ipv4h;
  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  ipv4h.SetBase ("1.0.0.0", "255.0.0.0");
  Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign (internetDevices);
  Ptr<Ipv4StaticRouting> remoteHostStaticRouting = ipv4RoutingHelper.GetStaticRouting (remoteHost->GetObject<Ipv4> ());
  remoteHostStaticRouting->AddNetworkRouteTo (Ipv4Address ("7.0.0.0"), Ipv4Mask ("255.0.0.0"), 1);
  internet.Install (ueNodes);
  Ipv4InterfaceContainer ueIpIface;
  ueIpIface = epcHelper->AssignUeIpv4Address (NetDeviceContainer (ueNetDev));

  // Set the default gateway for the UEs
  for (uint32_t j = 0; j < ueNodes.GetN(); ++j)
    {
      Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting (ueNodes.Get(j)->GetObject<Ipv4> ());
      ueStaticRouting->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress (), 1);
    }

  uint16_t sinkport = 8080;
  Address sinkAddress(InetSocketAddress(Ipv4Address::GetAny(),sinkport));
  PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory",sinkAddress);
  ApplicationContainer sinkApps = packetSinkHelper.Install (remoteHost);
  sinkApps.Start (Seconds (0.));
  sinkApps.Stop (Seconds (10.));

  BurstHelper burstHelper(1040,4,2);
  LargeHelper largeHelper(2000000,1040);
  ApplicationContainer clientApps;
  for(uint32_t i = 0; i < ueNodes.GetN(); i++){
    Address serverAddress(InetSocketAddress(internetIpIfaces.GetAddress(1),sinkport));
    burstHelper.SetAddress(serverAddress);
    largeHelper.SetAddress(serverAddress);
    clientApps.Add(burstHelper.Install(ueNodes.Get(i)));
    clientApps.Add(largeHelper.Install(ueNodes.Get(i)));
  }

  clientApps.Start(Seconds(1.));
  clientApps.Stop(Seconds(10.));
  // attach UEs to the closest eNB
  mmWaveHelper->AttachToClosestEnb (ueNetDev, enbNetDev);

  NS_LOG_INFO("Check PCAP files largeandburst-example-*-*.pcap");
  p2ph.EnablePcapAll("largeandburst-example");
  Simulator::Stop (Seconds (10));
  Simulator::Run ();

  Simulator::Destroy ();
  return 0;
}


