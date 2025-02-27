/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/* *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Michele Polese <michele.polese@gmail.com>
 */

#include <sys/stat.h>
#include <sys/types.h>
#include "ns3/mmwave-module.h"
#include "ns3/mmwave-helper.h"
#include "ns3/epc-helper.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/config-store.h"
#include "ns3/config-store-module.h"
#include "ns3/netanim-module.h"
#include "ns3/mmwave-point-to-point-epc-helper.h"
#include "ns3/flow-monitor-module.h"
//#include "ns3/gtk-config-store.h"
#include <ns3/buildings-helper.h>
#include <ns3/buildings-module.h>
#include <ns3/random-variable-stream.h>
#include <ns3/lte-ue-net-device.h>
#include "ns3/log.h"
#include "ns3/internet-apps-module.h"
#include "ns3/dash-helper.h"

#include <iostream>
#include <ctime>
#include <stdlib.h>
#include <list>


using namespace ns3;
using namespace mmwave;

/**
 * Sample simulation script for MC device. It instantiates a LTE and two MmWave eNodeB,
 * attaches one MC UE to both and starts a flow for the UE to and from a remote host.
 */

NS_LOG_COMPONENT_DEFINE ("multicell");


void
PrintPosition (Ptr<Node> node)
{
  Ptr<MobilityModel> model = node->GetObject<MobilityModel> ();
  NS_LOG_UNCOND ("Position +****************************** " << model->GetPosition () << " at time " << Simulator::Now ().GetSeconds ());
}

bool
AreOverlapping (Box a, Box b)
{
  return !((a.xMin > b.xMax) || (b.xMin > a.xMax) || (a.yMin > b.yMax) || (b.yMin > a.yMax) );
}


bool
OverlapWithAnyPrevious (Box box, std::list<Box> m_previousBlocks)
{
  for (std::list<Box>::iterator it = m_previousBlocks.begin (); it != m_previousBlocks.end (); ++it)
    {
      if (AreOverlapping (*it,box))
        {
          return true;
        }
    }
  return false;
}


std::pair<Box, std::list<Box> >
GenerateBuildingBounds (double xCoormin, double yCoormin, double xArea, double yArea, double maxBuildSize, std::list<Box> m_previousBlocks )
{

  Ptr<UniformRandomVariable> xMinBuilding = CreateObject<UniformRandomVariable> ();
  xMinBuilding->SetAttribute ("Min",DoubleValue (xCoormin));
  xMinBuilding->SetAttribute ("Max",DoubleValue (xArea));

  NS_LOG_UNCOND ("min " << xCoormin << " max " << xArea);

  Ptr<UniformRandomVariable> yMinBuilding = CreateObject<UniformRandomVariable> ();
  yMinBuilding->SetAttribute ("Min",DoubleValue (yCoormin));
  yMinBuilding->SetAttribute ("Max",DoubleValue (yArea));

  NS_LOG_UNCOND ("min " << yCoormin << " max " << yArea);

  Box box;
  uint32_t attempt = 0;
  do
    {
      NS_ASSERT_MSG (attempt < 100, "Too many failed attempts to position non-overlapping buildings. Maybe area too small or too many buildings?");
      box.xMin = xMinBuilding->GetValue ();

      Ptr<UniformRandomVariable> xMaxBuilding = CreateObject<UniformRandomVariable> ();
      xMaxBuilding->SetAttribute ("Min",DoubleValue (box.xMin));
      xMaxBuilding->SetAttribute ("Max",DoubleValue (box.xMin + maxBuildSize));
      box.xMax = xMaxBuilding->GetValue ();

      box.yMin = yMinBuilding->GetValue ();

      Ptr<UniformRandomVariable> yMaxBuilding = CreateObject<UniformRandomVariable> ();
      yMaxBuilding->SetAttribute ("Min",DoubleValue (box.yMin));
      yMaxBuilding->SetAttribute ("Max",DoubleValue (box.yMin + maxBuildSize));
      box.yMax = yMaxBuilding->GetValue ();

      ++attempt;
    }
  while (OverlapWithAnyPrevious (box, m_previousBlocks));


  NS_LOG_UNCOND ("Building in coordinates (" << box.xMin << " , " << box.yMin << ") and ("  << box.xMax << " , " << box.yMax <<
                 ") accepted after " << attempt << " attempts");
  m_previousBlocks.push_back (box);
  std::pair<Box, std::list<Box> > pairReturn = std::make_pair (box,m_previousBlocks);
  return pairReturn;

}

static ns3::GlobalValue g_interPckInterval ("interPckInterval", "Interarrival time of UDP packets (us)",
                                            ns3::UintegerValue (20), ns3::MakeUintegerChecker<uint32_t> ());
static ns3::GlobalValue g_bufferSize ("bufferSize", "RLC tx buffer size (MB)",
                                      ns3::UintegerValue (20), ns3::MakeUintegerChecker<uint32_t> ());
static ns3::GlobalValue g_x2Latency ("x2Latency", "Latency on X2 interface (us)",
                                     ns3::DoubleValue (500), ns3::MakeDoubleChecker<double> ());
static ns3::GlobalValue g_mmeLatency ("mmeLatency", "Latency on MME interface (us)",
                                      ns3::DoubleValue (10000), ns3::MakeDoubleChecker<double> ());
static ns3::GlobalValue g_rlcAmEnabled ("rlcAmEnabled", "If true, use RLC AM, else use RLC UM",
                                        ns3::BooleanValue (true), ns3::MakeBooleanChecker ());
static ns3::GlobalValue g_runNumber ("runNumber", "Run number for rng",
                                     ns3::UintegerValue (10), ns3::MakeUintegerChecker<uint32_t> ());
static ns3::GlobalValue g_outPath ("outPath",
                                   "The path of output log files",
                                   ns3::StringValue ("./"), ns3::MakeStringChecker ());
static ns3::GlobalValue g_noiseAndFilter ("noiseAndFilter", "If true, use noisy SINR samples, filtered. If false, just use the SINR measure",
                                          ns3::BooleanValue (false), ns3::MakeBooleanChecker ());
static ns3::GlobalValue g_handoverMode ("handoverMode",
                                        "Handover mode",
                                        ns3::UintegerValue (3), ns3::MakeUintegerChecker<uint8_t> ());
static ns3::GlobalValue g_reportTablePeriodicity ("reportTablePeriodicity", "Periodicity of RTs",
                                                  ns3::UintegerValue (1600), ns3::MakeUintegerChecker<uint32_t> ());
static ns3::GlobalValue g_outageThreshold ("outageTh", "Outage threshold",
                                           ns3::DoubleValue (-5), ns3::MakeDoubleChecker<double> ());
static ns3::GlobalValue g_lteUplink ("lteUplink", "If true, always use LTE for uplink signalling",
                                     ns3::BooleanValue (false), ns3::MakeBooleanChecker ());

class DemoTcpServer : public Application
{
private:
  Ptr<Socket> m_socket;
  uint16_t m_port;
public:
  DemoTcpServer (uint16_t port) :
      m_port (port)
  {
  }
  ~DemoTcpServer ()
  {
    m_socket = 0;
  }
  void
  StartApplication (void)
  {
    if (m_socket != 0)
      return;

    TypeId tid = TypeId::LookupByName ("ns3::TcpSocketFactory");
    m_socket = Socket::CreateSocket (GetNode (), tid);
    InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), m_port);
    m_socket->Bind (local);
    m_socket->Listen ();

    // Accept connection requests from remote hosts.
    m_socket->SetAcceptCallback (MakeNullCallback<bool, Ptr<Socket>, const Address&> (),
                                 MakeCallback (&DemoTcpServer::HandleAccept, this));
  }
  void
  StopApplication ()
  {

    if (m_socket != 0)
      {
        m_socket->Close ();
        m_socket->SetAcceptCallback (MakeNullCallback<bool, Ptr<Socket>, const Address&> (),
                                     MakeNullCallback<void, Ptr<Socket>, const Address&> ());
        m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
        m_socket->SetSendCallback (MakeNullCallback<void, Ptr<Socket>, uint32_t> ());
        m_socket = 0;
      }
    std::cout << "Application started at: " << GetNode ()->GetId () << std::endl;
  }

  void
  HandleAccept (Ptr<Socket> s, const Address &from)
  {
    if (m_socket == 0)
      return;

    std::cout << "new connection rcved " << GetNode ()->GetId () << std::endl;
    s->SetRecvCallback (MakeCallback (&DemoTcpServer::HandleRecv, this));
    s->SetSendCallback (MakeCallback (&DemoTcpServer::HandleSend, this));
    HandleSend (s, s->GetTxAvailable ());
  }

  void
  HandleRecv (Ptr<Socket> socket)
  {
    if (m_socket == 0)
      return;
    uint8_t buf[2048];
    uint16_t len = 2048;
    len = socket->Recv (buf, len, 0);
//    std::cerr << "recved at server " << len << "\r";
  }

  void
  HandleSend (Ptr<Socket> socket, uint32_t txSpace)
  {
    if (m_socket == 0)
      return;
    uint8_t buf[2048];
    uint16_t len = 2048;
    len = socket->Send (buf, std::min (txSpace, (uint32_t) len), 0);
//    std::cerr << "sent at server " << len << "\r";
  }
};

class DemoTcpClient : public Application
{
private:
  Ptr<Socket> m_socket;
  Address m_peerAddress; //!< Remote peer address
  uint16_t m_peerPort; //!< Remote peer port

public:
  DemoTcpClient (Address peerAddress, uint16_t peerPort) :
      m_peerAddress (peerAddress), m_peerPort (peerPort)
  {
  }
  ~DemoTcpClient ()
  {
    m_socket = 0;
  }
  void
  StartApplication ()
  {
    TypeId tid = TypeId::LookupByName ("ns3::TcpSocketFactory");
    m_socket = Socket::CreateSocket (m_node, tid);

    m_socket->SetRecvCallback (MakeCallback (&DemoTcpClient::HandleRecv, this));
    m_socket->SetSendCallback (MakeCallback (&DemoTcpClient::HandleSend, this));

    m_socket->SetConnectCallback (MakeCallback (&DemoTcpClient::Success, this),
                                  MakeCallback (&DemoTcpClient::Error, this));

    m_socket->SetCloseCallbacks (MakeCallback (&DemoTcpClient::Success, this),
                                 MakeCallback (&DemoTcpClient::Error, this));
    auto ret = m_socket->Connect (InetSocketAddress (Ipv4Address::ConvertFrom (m_peerAddress), m_peerPort));
    std::cout << "Application started at: " << GetNode ()->GetId () << " connected with: " << ret << std::endl;
  }

  void
  Success (Ptr<Socket> s)
  {
    std::cout << "Connection successfull in node:" << GetNode ()->GetId () << std::endl;
  }

  void
  Error (Ptr<Socket> s)
  {
    std::cout << "Connection Error in node:" << GetNode ()->GetId () << std::endl;
  }

  void
  HandleRecv (Ptr<Socket> socket)
  {
    uint8_t buf[2048];
    uint16_t len = 2048;
    len = socket->Recv (buf, len, 0);
//    std::cerr << "recved at client " << len << "\r";
  }

  void
  HandleSend (Ptr<Socket> socket, uint32_t txSpace)
  {
    uint8_t buf[2048];
    uint16_t len = 2048;
    len = socket->Send (buf, std::min (txSpace, (uint32_t) len), 0);
//    std::cerr << "sent at client " << len << "\r";
  }
};

void
storeFlowMonitor (Ptr<ns3::FlowMonitor> monitor,
                  FlowMonitorHelper &flowmonHelper)
{
  // Print per-flow statistics
  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (
      flowmonHelper.GetClassifier ());
  FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats ();

  double averageFlowThroughput = 0.0;
  double averageFlowDelay = 0.0;

  std::ofstream outFile;
  std::string filename = "multicellStat/default";
  outFile.open (filename.c_str (), std::ofstream::out | std::ofstream::trunc);
  if (!outFile.is_open ())
    {
      std::cerr << "Can't open file " << filename << std::endl;
      return;
    }

  outFile.setf (std::ios_base::fixed);

  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i =
      stats.begin (); i != stats.end (); ++i)
    {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
      std::stringstream protoStream;
      protoStream << (uint16_t) t.protocol;
      if (t.protocol == 6)
        {
          protoStream.str ("TCP");
        }
      if (t.protocol == 17)
        {
          protoStream.str ("UDP");
        }
      double txDuration = (i->second.timeLastTxPacket.GetSeconds ()
          - i->second.timeFirstTxPacket.GetSeconds ());
      outFile << "Flow " << i->first << " (" << t.sourceAddress << ":"
          << t.sourcePort << " -> " << t.destinationAddress << ":"
          << t.destinationPort << ") proto " << protoStream.str () << "\n";
      outFile << "  Tx Packets: " << i->second.txPackets << "\n";
      outFile << "  Tx Bytes:   " << i->second.txBytes << "\n";
      outFile << "  TxOffered:  "
          << i->second.txBytes * 8.0 / txDuration / 1000 / 1000 << " Mbps\n";
      outFile << "  Rx Bytes:   " << i->second.rxBytes << "\n";
      if (i->second.rxPackets > 0)
        {
          // Measure the duration of the flow from receiver's perspective
          //double rxDuration = i->second.timeLastRxPacket.GetSeconds () - i->second.timeFirstTxPacket.GetSeconds ();
          double rxDuration = (i->second.timeLastRxPacket.GetSeconds ()
              - i->second.timeFirstRxPacket.GetSeconds ());

          averageFlowThroughput += i->second.rxBytes * 8.0 / rxDuration / 1000
              / 1000;
          averageFlowDelay += 1000 * i->second.delaySum.GetSeconds ()
              / i->second.rxPackets;

          outFile << "  Throughput: "
              << i->second.rxBytes * 8.0 / rxDuration / 1000 / 1000
              << " Mbps\n";
          outFile << "  Mean delay:  "
              << 1000 * i->second.delaySum.GetSeconds () / i->second.rxPackets
              << " ms\n";
          //outFile << "  Mean upt:  " << i->second.uptSum / i->second.rxPackets / 1000/1000 << " Mbps \n";
          outFile << "  Mean jitter:  "
              << 1000 * i->second.jitterSum.GetSeconds () / i->second.rxPackets
              << " ms\n";
        }
      else
        {
          outFile << "  Throughput:  0 Mbps\n";
          outFile << "  Mean delay:  0 ms\n";
          outFile << "  Mean jitter: 0 ms\n";
        }
      outFile << "  Rx Packets: " << i->second.rxPackets << "\n";
    }

  outFile << "\n\n  Mean flow throughput: "
      << averageFlowThroughput / stats.size () << "\n";
  outFile << "  Mean flow delay: " << averageFlowDelay / stats.size () << "\n";

  outFile.close ();
}

void
NotifyConnectionEstablishedUe (std::string context,
                               uint64_t imsi,
                               uint16_t cellid,
                               uint16_t rnti)
{
  std::fstream fileout;
  std::string filename = "handover_tcp.csv";
  fileout.open(filename,std::ios::out | std::ios::app);
  fileout << Simulator::Now().GetSeconds() <<",";
  fileout << "ConnectionEstablishedUe" <<",";
  fileout << imsi << ",";
  fileout << cellid <<",";
  fileout << rnti <<",";
  fileout << "NA" << std::endl;
  std::cout << Simulator::Now ().GetSeconds () << " " << context
            << " UE IMSI " << imsi
            << ": connected to CellId " << cellid
            << " with RNTI " << rnti
            << std::endl;
}

void
NotifyHandoverStartUe (std::string context,
                       uint64_t imsi,
                       uint16_t cellid,
                       uint16_t rnti,
                       uint16_t targetCellId)
{
  std::fstream fileout;
  std::string filename = "handover_tcp.csv";
  fileout.open(filename,std::ios::out | std::ios::app);
  fileout << Simulator::Now().GetSeconds() <<",";
  fileout << "HandoverStartUe" <<",";
  fileout << imsi << ",";
  fileout << cellid <<",";
  fileout << rnti <<",";
  fileout << targetCellId << std::endl;

  std::cout << Simulator::Now ().GetSeconds () << " " << context
            << " UE IMSI " << imsi
            << ": previously connected to CellId " << cellid
            << " with RNTI " << rnti
            << ", doing handover to CellId " << targetCellId
            << std::endl;
}

void
NotifyHandoverEndOkUe (std::string context,
                       uint64_t imsi,
                       uint16_t cellid,
                       uint16_t rnti)
{
  std::fstream fileout;
  std::string filename = "handover_tcp.csv";
  fileout.open(filename,std::ios::out | std::ios::app);
  fileout << Simulator::Now().GetSeconds() <<",";
  fileout << "HandoverEndOkUe" <<",";
  fileout << imsi << ",";
  fileout << cellid <<",";
  fileout << rnti <<",";
  fileout << "NA" << std::endl;

  std::cout << Simulator::Now ().GetSeconds () << " " << context
            << " UE IMSI " << imsi
            << ": successful handover to CellId " << cellid
            << " with RNTI " << rnti
            << std::endl;
}

void
NotifyConnectionEstablishedEnb (std::string context,
                                uint64_t imsi,
                                uint16_t cellid,
                                uint16_t rnti)
{
  std::fstream fileout;
  std::string filename = "handover_tcp.csv";
  fileout.open(filename,std::ios::out | std::ios::app);
  fileout << Simulator::Now().GetSeconds() <<",";
  fileout << "ConnectionEstablishedEnb" <<",";
  fileout << imsi << ",";
  fileout << cellid <<",";
  fileout << rnti <<",";
  fileout << "NA" << std::endl;

  std::cout << Simulator::Now ().GetSeconds () << " " << context
            << " eNB CellId " << cellid
            << ": successful connection of UE with IMSI " << imsi
            << " RNTI " << rnti
            << std::endl;
}

void
NotifyHandoverStartEnb (std::string context,
                        uint64_t imsi,
                        uint16_t cellid,
                        uint16_t rnti,
                        uint16_t targetCellId)
{
  std::fstream fileout;
  std::string filename = "handover_tcp.csv";
  fileout.open(filename,std::ios::out | std::ios::app);
  fileout << Simulator::Now().GetSeconds() <<",";
  fileout << "HandoverStartEnb" <<",";
  fileout << imsi << ",";
  fileout << cellid <<",";
  fileout << rnti <<",";
  fileout << targetCellId << std::endl;

  std::cout << Simulator::Now ().GetSeconds () << " " << context
            << " eNB CellId " << cellid
            << ": start handover of UE with IMSI " << imsi
            << " RNTI " << rnti
            << " to CellId " << targetCellId
            << std::endl;
}

void
NotifyHandoverEndOkEnb (std::string context,
                        uint64_t imsi,
                        uint16_t cellid,
                        uint16_t rnti)
{
  std::fstream fileout;
  std::string filename = "handover_tcp.csv";
  fileout.open(filename,std::ios::out | std::ios::app);
  fileout << Simulator::Now().GetSeconds() <<",";
  fileout << "HandoverEndOkEnb" <<",";
  fileout << imsi << ",";
  fileout << cellid <<",";
  fileout << rnti <<",";
  fileout << "NA" << std::endl;

  std::cout << Simulator::Now ().GetSeconds () << " " << context
            << " eNB CellId " << cellid
            << ": completed handover of UE with IMSI " << imsi
            << " RNTI " << rnti
            << std::endl;
}

void printNodeTrace (Ptr<Node> node, uint32_t traceId)
{
  const std::string rrcStates[] =
    {
        "IDLE_START",
        "IDLE_CELL_SEARCH",
        "IDLE_WAIT_MIB_SIB1",
        "IDLE_WAIT_MIB",
        "IDLE_WAIT_SIB1",
        "IDLE_CAMPED_NORMALLY",
        "IDLE_WAIT_SIB2",
        "IDLE_RANDOM_ACCESS",
        "IDLE_CONNECTING",
        "CONNECTED_NORMALLY",
        "CONNECTED_HANDOVER",
        "CONNECTED_PHY_PROBLEM",
        "CONNECTED_REESTABLISHING",
        "NUM_STATES"
    };
    std::fstream fout;
    std::stringstream filename;
    filename << "multicelltcp";
    filename << node->GetId ();
    filename << ".csv";
    fout.open(filename.str(),std::ios::out | std::ios::app);
    fout <<traceId << "," << Simulator::Now().GetSeconds();
    fout << "," << std::to_string (node->GetId ());
    auto mModel = node->GetObject<MobilityModel> ();
    fout << "," << mModel->GetVelocity ().x << "," << mModel->GetVelocity ().y;
    fout << "," << mModel->GetPosition ().x << "," << mModel->GetPosition ().y;
    std::cout << "Current Time" << Simulator::Now().GetSeconds() << "at node" << node->GetId() << "TraceId" <<traceId <<std::endl;
    Ptr<McUeNetDevice> netDevice;

    for (uint32_t i = 0; i < node->GetNDevices (); i++)
    {
      std::cout << "\tNode: " << node->GetId () << ", Device " << i << ": "
        << node->GetDevice (i)->GetInstanceTypeId () << std::endl;
      auto nd = node->GetDevice (i);
      std::string nameofdevice = nd->GetInstanceTypeId ().GetName();
      if (nameofdevice == "ns3::McUeNetDevice")
      {
        std::cout<<"true"<<std::endl;
        netDevice = DynamicCast<McUeNetDevice> (nd);
        break;
      }
      else
      {
        std::cout<<"False"<<std::endl;
      }

    }
    std::cout << "CsgId: "<<netDevice->GetCsgId () <<std::endl;
    std::cout << "GetMmWaveEarfcn: "<<netDevice->GetMmWaveEarfcn () <<std::endl;
    std::cout << "GetLteDlEarfcn: "<<netDevice->GetLteDlEarfcn () <<std::endl;
    std::cout << "GetImsi: "<<netDevice->GetImsi () <<std::endl;

    fout << "," << std::to_string (netDevice->GetCsgId ());
    fout << "," << std::to_string (netDevice->GetMmWaveEarfcn ());
    fout << "," << std::to_string (netDevice->GetImsi ());
    Ptr<LteUeRrc> rrc = netDevice->GetMmWaveRrc ();

    std::cout << "GetState: "<<rrc->GetState () <<std::endl;
    std::cout << "rrcstate: "<<rrcStates[rrc->GetState ()] <<std::endl;
    std::cout << "GetCellId: "<<rrc->GetCellId () <<std::endl;
    std::cout << "GetDlBandwidth: "<<rrc->GetDlBandwidth () <<std::endl;

    fout << "," << std::to_string (rrc->GetState ());
    fout << "," << rrcStates[rrc->GetState ()];
    fout << "," << std::to_string (rrc->GetCellId ());
    fout << "," << std::to_string (rrc->GetDlBandwidth ());
    fout<<std::endl;

}

int
main (int argc, char *argv[])
{
  bool harqEnabled = true;
  bool fixedTti = false;
  unsigned symPerSf = 24;
  double sfPeriod = 100.0;

  std::list<Box>  m_previousBlocks;
  std::string outputDir = "multicellStat";
  std::string nodeTraceFile = "trace";
  double udpAppStartTime = 0.4; //seconds

  // Command line arguments
  CommandLine cmd;
  cmd.Parse (argc, argv);

  UintegerValue uintegerValue;
  BooleanValue booleanValue;
  StringValue stringValue;
  DoubleValue doubleValue;

  // Variables for the RT
  int windowForTransient = 150; // number of samples for the vector to use in the filter
  GlobalValue::GetValueByName ("reportTablePeriodicity", uintegerValue);
  int ReportTablePeriodicity = (int)uintegerValue.Get (); // in microseconds
  if (ReportTablePeriodicity == 1600)
    {
      windowForTransient = 150;
    }
  else if (ReportTablePeriodicity == 25600)
    {
      windowForTransient = 50;
    }
  else if (ReportTablePeriodicity == 12800)
    {
      windowForTransient = 100;
    }
  else
    {
      NS_ASSERT_MSG (false, "Unrecognized");
    }

  int vectorTransient = windowForTransient * ReportTablePeriodicity;

  // params for RT, filter, HO mode
  GlobalValue::GetValueByName ("noiseAndFilter", booleanValue);
  bool noiseAndFilter = booleanValue.Get ();
  GlobalValue::GetValueByName ("handoverMode", uintegerValue);
  uint8_t hoMode = uintegerValue.Get ();
  GlobalValue::GetValueByName ("outageTh", doubleValue);
  double outageTh = doubleValue.Get ();

  GlobalValue::GetValueByName ("rlcAmEnabled", booleanValue);
  bool rlcAmEnabled = booleanValue.Get ();
  GlobalValue::GetValueByName ("bufferSize", uintegerValue);
  uint32_t bufferSize = uintegerValue.Get ();
  GlobalValue::GetValueByName ("interPckInterval", uintegerValue);
  uint32_t interPacketInterval = uintegerValue.Get ();
  GlobalValue::GetValueByName ("x2Latency", doubleValue);
  double x2Latency = doubleValue.Get ();
  GlobalValue::GetValueByName ("mmeLatency", doubleValue);
  double mmeLatency = doubleValue.Get ();

  double simTime = 21.1;
  NS_LOG_UNCOND ("rlcAmEnabled " << rlcAmEnabled << " bufferSize " << bufferSize << " interPacketInterval " <<
                 interPacketInterval << " x2Latency " << x2Latency << " mmeLatency " << mmeLatency);

  // rng things
  GlobalValue::GetValueByName ("runNumber", uintegerValue);
  uint32_t runSet = uintegerValue.Get ();
  uint32_t seedSet = 5;
  RngSeedManager::SetSeed (seedSet);
  RngSeedManager::SetRun (runSet);
  char seedSetStr[21];
  char runSetStr[21];
  sprintf (seedSetStr, "%d", seedSet);
  sprintf (runSetStr, "%d", runSet);

  GlobalValue::GetValueByName ("outPath", stringValue);
  std::string path = stringValue.Get ();
  std::string mmWaveOutName = "MmWaveSwitchStats";
  std::string lteOutName = "LteSwitchStats";
  std::string dlRlcOutName = "DlRlcStats";
  std::string dlPdcpOutName = "DlPdcpStats";
  std::string ulRlcOutName = "UlRlcStats";
  std::string ulPdcpOutName = "UlPdcpStats";
  std::string  ueHandoverStartOutName =  "UeHandoverStartStats";
  std::string enbHandoverStartOutName = "EnbHandoverStartStats";
  std::string  ueHandoverEndOutName =  "UeHandoverEndStats";
  std::string enbHandoverEndOutName = "EnbHandoverEndStats";
  std::string cellIdInTimeOutName = "CellIdStats";
  std::string cellIdInTimeHandoverOutName = "CellIdStatsHandover";
  std::string mmWaveSinrOutputFilename = "MmWaveSinrTime";
  std::string x2statOutputFilename = "X2Stats";
  std::string udpSentFilename = "UdpSent";
  std::string udpReceivedFilename = "UdpReceived";
  std::string extension = ".txt";
  std::string version;
  version = "mc";
  Config::SetDefault ("ns3::MmWaveUeMac::UpdateUeSinrEstimatePeriod", DoubleValue (0));

  //get current time
  time_t rawtime;
  struct tm * timeinfo;
  char buffer[80];
  time (&rawtime);
  timeinfo = localtime (&rawtime);
  strftime (buffer,80,"%d_%m_%Y_%I_%M_%S",timeinfo);
  std::string time_str (buffer);

  Config::SetDefault ("ns3::MmWaveHelper::RlcAmEnabled", BooleanValue (rlcAmEnabled));
  Config::SetDefault ("ns3::MmWaveHelper::HarqEnabled", BooleanValue (harqEnabled));
  Config::SetDefault ("ns3::MmWaveFlexTtiMacScheduler::HarqEnabled", BooleanValue (harqEnabled));
  Config::SetDefault ("ns3::MmWaveFlexTtiMaxWeightMacScheduler::HarqEnabled", BooleanValue (harqEnabled));
  Config::SetDefault ("ns3::MmWaveFlexTtiMaxWeightMacScheduler::FixedTti", BooleanValue (fixedTti));
  Config::SetDefault ("ns3::MmWaveFlexTtiMaxWeightMacScheduler::SymPerSlot", UintegerValue (6));
  Config::SetDefault ("ns3::MmWavePhyMacCommon::ResourceBlockNum", UintegerValue (1));
  Config::SetDefault ("ns3::MmWavePhyMacCommon::ChunkPerRB", UintegerValue (72));
  Config::SetDefault ("ns3::MmWavePhyMacCommon::SymbolsPerSubframe", UintegerValue (symPerSf));
  Config::SetDefault ("ns3::MmWavePhyMacCommon::SubframePeriod", DoubleValue (sfPeriod));
  Config::SetDefault ("ns3::MmWavePhyMacCommon::TbDecodeLatency", UintegerValue (200.0));
  Config::SetDefault ("ns3::MmWavePhyMacCommon::NumHarqProcess", UintegerValue (100));
  Config::SetDefault ("ns3::MmWaveBeamforming::LongTermUpdatePeriod", TimeValue (MilliSeconds (100.0)));
  Config::SetDefault ("ns3::LteEnbRrc::SystemInformationPeriodicity", TimeValue (MilliSeconds (5.0)));
  Config::SetDefault ("ns3::LteRlcAm::ReportBufferStatusTimer", TimeValue (MicroSeconds (100.0)));
  Config::SetDefault ("ns3::LteRlcUmLowLat::ReportBufferStatusTimer", TimeValue (MicroSeconds (100.0)));
  Config::SetDefault ("ns3::LteEnbRrc::SrsPeriodicity", UintegerValue (320));
  Config::SetDefault ("ns3::LteEnbRrc::FirstSibTime", UintegerValue (2));
  Config::SetDefault ("ns3::MmWavePointToPointEpcHelper::X2LinkDelay", TimeValue (MicroSeconds (x2Latency)));
  Config::SetDefault ("ns3::MmWavePointToPointEpcHelper::X2LinkDataRate", DataRateValue (DataRate ("1000Gb/s")));
  Config::SetDefault ("ns3::MmWavePointToPointEpcHelper::X2LinkMtu",  UintegerValue (10000));
  Config::SetDefault ("ns3::MmWavePointToPointEpcHelper::S1uLinkDelay", TimeValue (MicroSeconds (1000)));
  Config::SetDefault ("ns3::MmWavePointToPointEpcHelper::S1apLinkDelay", TimeValue (MicroSeconds (mmeLatency)));
  Config::SetDefault ("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue (bufferSize * 1024 * 1024));
  Config::SetDefault ("ns3::LteRlcUmLowLat::MaxTxBufferSize", UintegerValue (bufferSize * 1024 * 1024));
  Config::SetDefault ("ns3::LteRlcAm::StatusProhibitTimer", TimeValue (MilliSeconds (10.0)));
  Config::SetDefault ("ns3::LteRlcAm::MaxTxBufferSize", UintegerValue (bufferSize * 1024 * 1024));

    // handover and RT related params
  switch (hoMode)
    {
    case 1:
      Config::SetDefault ("ns3::LteEnbRrc::SecondaryCellHandoverMode", EnumValue (LteEnbRrc::THRESHOLD));
      break;
    case 2:
      Config::SetDefault ("ns3::LteEnbRrc::SecondaryCellHandoverMode", EnumValue (LteEnbRrc::FIXED_TTT));
      break;
    case 3:
      Config::SetDefault ("ns3::LteEnbRrc::SecondaryCellHandoverMode", EnumValue (LteEnbRrc::DYNAMIC_TTT));
      break;
    }

  Config::SetDefault ("ns3::LteEnbRrc::FixedTttValue", UintegerValue (150));
  Config::SetDefault ("ns3::LteEnbRrc::CrtPeriod", IntegerValue (ReportTablePeriodicity));
  Config::SetDefault ("ns3::LteEnbRrc::OutageThreshold", DoubleValue (outageTh));
  Config::SetDefault ("ns3::MmWaveEnbPhy::UpdateSinrEstimatePeriod", IntegerValue (ReportTablePeriodicity));
  Config::SetDefault ("ns3::MmWaveEnbPhy::Transient", IntegerValue (vectorTransient));
  Config::SetDefault ("ns3::MmWaveEnbPhy::NoiseAndFilter", BooleanValue (noiseAndFilter));

  GlobalValue::GetValueByName ("lteUplink", booleanValue);
  bool lteUplink = booleanValue.Get ();

  Config::SetDefault ("ns3::McUePdcp::LteUplink", BooleanValue (lteUplink));
  std::cout << "Lte uplink " << lteUplink << "\n";

  // settings for the 3GPP the channel
  Config::SetDefault ("ns3::MmWave3gppPropagationLossModel::ChannelCondition", StringValue ("a"));
  Config::SetDefault ("ns3::MmWave3gppPropagationLossModel::Scenario", StringValue ("UMi-StreetCanyon"));
  Config::SetDefault ("ns3::MmWave3gppPropagationLossModel::OptionalNlos", BooleanValue (true));
  Config::SetDefault ("ns3::MmWave3gppPropagationLossModel::Shadowing", BooleanValue (true)); // enable or disable the shadowing effect
  Config::SetDefault ("ns3::MmWave3gppBuildingsPropagationLossModel::UpdateCondition", BooleanValue (true)); // enable or disable the LOS/NLOS update when the UE moves
  Config::SetDefault ("ns3::AntennaArrayModel::AntennaHorizontalSpacing", DoubleValue (0.5));
  Config::SetDefault ("ns3::AntennaArrayModel::AntennaVerticalSpacing", DoubleValue (0.5));
  Config::SetDefault ("ns3::MmWave3gppChannel::UpdatePeriod", TimeValue (MilliSeconds (100))); // interval after which the channel for a moving user is updated,
  
   // with spatial consistency procedure. If 0, spatial consistency is not used
  Config::SetDefault ("ns3::MmWave3gppChannel::DirectBeam", BooleanValue (true)); // Set true to perform the beam in the exact direction of receiver node.
  Config::SetDefault ("ns3::MmWave3gppChannel::Blockage", BooleanValue (true)); // use blockage or not
  Config::SetDefault ("ns3::MmWave3gppChannel::PortraitMode", BooleanValue (true)); // use blockage model with UT in portrait mode
  Config::SetDefault ("ns3::MmWave3gppChannel::NumNonselfBlocking", IntegerValue (4)); // number of non-self blocking obstacles

  // set the number of antennas in the devices
  Config::SetDefault ("ns3::McUeNetDevice::AntennaNum", UintegerValue(16));
  Config::SetDefault ("ns3::MmWaveEnbNetDevice::AntennaNum", UintegerValue(64));

  Ptr<MmWaveHelper> mmwaveHelper = CreateObject<MmWaveHelper> ();
  if (true)
    {
      mmwaveHelper->SetAttribute ("PathlossModel", StringValue ("ns3::MmWave3gppBuildingsPropagationLossModel"));
    }
  else
    {
      mmwaveHelper->SetAttribute ("PathlossModel", StringValue ("ns3::MmWave3gppPropagationLossModel"));
    }
  mmwaveHelper->SetAttribute ("ChannelModel", StringValue ("ns3::MmWave3gppChannel"));

  //Ptr<MmWaveHelper> mmwaveHelper = CreateObject<MmWaveHelper> ();
  //mmwaveHelper->SetSchedulerType ("ns3::MmWaveFlexTtiMaxWeightMacScheduler");
  Ptr<MmWavePointToPointEpcHelper> epcHelper = CreateObject<MmWavePointToPointEpcHelper> ();
  mmwaveHelper->SetEpcHelper (epcHelper);
  mmwaveHelper->SetHarqEnabled (harqEnabled);
//  mmwaveHelper->SetAttribute ("PathlossModel", StringValue ("ns3::BuildingsObstaclePropagationLossModel"));
  mmwaveHelper->Initialize ();


  // parse again so you can override default values from the command line
  cmd.Parse (argc, argv);

  // Get SGW/PGW and create a single RemoteHost
  Ptr<Node> mme = epcHelper->GetMmeNode ();
  Ptr<Node> pgw = epcHelper->GetPgwNode ();
  NodeContainer remoteHostContainer;
  remoteHostContainer.Create (1);
  Ptr<Node> remoteHost = remoteHostContainer.Get (0);
  InternetStackHelper internet;
  internet.Install (remoteHostContainer);

  std::cout << "pgw: " << pgw->GetId () << std::endl;
  std::cout << "mme: " << mme->GetId () << std::endl;
  std::cout << "remote: " << remoteHost->GetId () << std::endl;
  // Create the Internet by connecting remoteHost to pgw. Setup routing too
  PointToPointHelper p2ph;
  p2ph.SetDeviceAttribute ("DataRate", DataRateValue (DataRate ("100Gb/s")));
  p2ph.SetDeviceAttribute ("Mtu", UintegerValue (2500));
  p2ph.SetChannelAttribute ("Delay", TimeValue (Seconds (0.010)));
  NetDeviceContainer internetDevices = p2ph.Install (pgw, remoteHost);
  Ipv4AddressHelper ipv4h;
  ipv4h.SetBase ("1.0.0.0", "255.0.0.0");
  Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign (internetDevices);
  // interface 0 is localhost, 1 is the p2p device
  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  Ptr<Ipv4StaticRouting> remoteHostStaticRouting = ipv4RoutingHelper.GetStaticRouting (remoteHost->GetObject<Ipv4> ());
  remoteHostStaticRouting->AddNetworkRouteTo (Ipv4Address ("7.0.0.0"), Ipv4Mask ("255.0.0.0"), 1);

  // create LTE, mmWave eNB nodes and UE node
  double gNbHeight = 10;
  uint16_t gNbNum = 4;
  uint16_t ueNum = 10;
  NodeContainer ueNodes;
  NodeContainer mmWaveEnbNodes;
  NodeContainer lteEnbNodes;
  NodeContainer allEnbNodes;
  mmWaveEnbNodes.Create (gNbNum);
  lteEnbNodes.Create (1);
  ueNodes.Create (ueNum);
  allEnbNodes.Add (lteEnbNodes);
  allEnbNodes.Add (mmWaveEnbNodes);

  for (uint32_t i = 0; i < mmWaveEnbNodes.GetN(); i++)
    {
      std::cout << "gNb:" << i << " : " << mmWaveEnbNodes.Get (i)->GetId ()
          << std::endl;
    }
  for (uint32_t i = 0; i < ueNodes.GetN (); i++)
    {
      std::cout << "ue:" << i << " : " << ueNodes.Get (i)->GetId ()
          << std::endl;
    }
  for (uint32_t i = 0; i < lteEnbNodes.GetN (); i++)
    {
      std::cout << "LTE:" << i << " : " << lteEnbNodes.Get (i)->GetId ()
          << std::endl;
    }

 
  //Generate Buildings 
  std::vector<Ptr<Building> > buildingVector;

  double maxBuildingSize = 30;
  double  maxXaxis = 640, maxYaxis = 370, minxAxis = 340, minYaxis = 310;

  for(uint32_t buildingindex = 0; buildingindex < 8; buildingindex++)
  {
      Ptr < Building > building = Create<Building> ();

      std::pair<Box, std::list<Box> > pairBuildings = GenerateBuildingBounds (minxAxis, minYaxis, 
                maxXaxis, maxYaxis, maxBuildingSize, m_previousBlocks);
      m_previousBlocks = std::get<1> (pairBuildings);
      Box box = std::get<0> (pairBuildings);
      Ptr<UniformRandomVariable> randomBuildingZ = CreateObject<UniformRandomVariable> ();
      randomBuildingZ->SetAttribute ("Min",DoubleValue (1.6));
      randomBuildingZ->SetAttribute ("Max",DoubleValue (40));
      double buildingHeight = randomBuildingZ->GetValue ();

      building->SetBoundaries (Box (box.xMin, box.xMax,
                                    box.yMin,  box.yMax,
                                    0.0, buildingHeight));
      
      buildingVector.push_back (building);
  }
  maxXaxis = 640, maxYaxis = 720, minxAxis = 340, minYaxis = 650;
    for(uint32_t buildingindex = 0; buildingindex < 8; buildingindex++)
  {
      Ptr < Building > building = Create<Building> ();

      std::pair<Box, std::list<Box> > pairBuildings = GenerateBuildingBounds (340, 650, 
                640, 720, maxBuildingSize, m_previousBlocks);
      m_previousBlocks = std::get<1> (pairBuildings);
      Box box = std::get<0> (pairBuildings);
      Ptr<UniformRandomVariable> randomBuildingZ = CreateObject<UniformRandomVariable> ();
      randomBuildingZ->SetAttribute ("Min",DoubleValue (1.6));
      randomBuildingZ->SetAttribute ("Max",DoubleValue (40));
      double buildingHeight = randomBuildingZ->GetValue ();

      building->SetBoundaries (Box (box.xMin, box.xMax,
                                    box.yMin,  box.yMax,
                                    0.0, buildingHeight));
      buildingVector.push_back (building);
  }
  


  MobilityHelper gNbMobility, ueMobility, LteMobility;
  Ptr<ListPositionAllocator> ltepositionAloc = CreateObject<ListPositionAllocator> ();
    
  Ptr<ListPositionAllocator> apPositionAlloc = CreateObject<ListPositionAllocator> ();
  Ptr<ListPositionAllocator> staPositionAllocator = CreateObject<ListPositionAllocator> ();

  
  double x_random, y_random;
  for(uint32_t i = 0; i < ueNodes.GetN(); i ++)
  {
    x_random = (rand() % 1000) + 1;
    y_random = (rand() % 1000) + 1;
    staPositionAllocator->Add (Vector (x_random, y_random, 1.5));
  }

  ueMobility.SetPositionAllocator(staPositionAllocator);
  ueMobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
    "Bounds", RectangleValue (Rectangle (0, 1000, 0, 1000)),
    "Speed", StringValue("ns3::UniformRandomVariable[Min=20|Max=50]"));
  ueMobility.Install (ueNodes);
  BuildingsHelper::Install(ueNodes);

  apPositionAlloc->Add (Vector (333.0, 333.0,gNbHeight));
  apPositionAlloc->Add (Vector (666.0, 333.0, gNbHeight));
  apPositionAlloc->Add(Vector (333.0, 666.0, gNbHeight));
  apPositionAlloc->Add(Vector (666.0,666.0,gNbHeight));

  LteMobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  ltepositionAloc->Add(Vector(500.0, 500, 10.0));
  LteMobility.SetPositionAllocator (ltepositionAloc);
  LteMobility.Install (lteEnbNodes);

  gNbMobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  gNbMobility.SetPositionAllocator (apPositionAlloc);
  gNbMobility.Install (mmWaveEnbNodes);
  BuildingsHelper::Install(allEnbNodes);
  
  AnimationInterface::SetConstantPosition(mme,580,580);
  AnimationInterface::SetConstantPosition(pgw,640,580);
  AnimationInterface::SetConstantPosition(remoteHostContainer.Get(0),740,580);
  // Install mmWave, lte, mc Devices to the nodes
  NetDeviceContainer lteEnbDevs = mmwaveHelper->InstallLteEnbDevice (lteEnbNodes);
  NetDeviceContainer mmWaveEnbDevs = mmwaveHelper->InstallEnbDevice (mmWaveEnbNodes);
  NetDeviceContainer mcUeDevs;
  mcUeDevs = mmwaveHelper->InstallMcUeDevice (ueNodes);
  //ueDevs = mmwaveHelper->InstallUeDevice (ueNodes);

  for (uint32_t j = 0; j < ueNodes.GetN (); j++)
  {
    std::cout <<"Number of ue Net device after "<<j<<" iter :" << ueNodes.Get(j)->GetNDevices()<<std::endl;
  }
  // Install the IP stack on the UEs
  internet.Install (ueNodes);
  Ipv4InterfaceContainer ueIpIface;
  ueIpIface = epcHelper->AssignUeIpv4Address (NetDeviceContainer (mcUeDevs));
  // Assign IP address to UEs, and install applications
  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
    {
      Ptr<Node> ueNode = ueNodes.Get (u);
      // Set the default gateway for the UE
      Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting (ueNode->GetObject<Ipv4> ());
      ueStaticRouting->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress (), 1);
    }

  // Add X2 interfaces
  mmwaveHelper->AddX2Interface (lteEnbNodes, mmWaveEnbNodes);

  // Manual attachment
  mmwaveHelper->AttachToClosestEnb (mcUeDevs, mmWaveEnbDevs, lteEnbDevs);

  // Install and start applications on UEs and remote host

  for (uint32_t j = 0; j < ueNodes.GetN (); j++)
   {
     std::fstream fout;
     std::stringstream filename ;
     filename << "multicelltcp";
     filename << ueNodes.Get(j)->GetId ();
     filename << ".csv";
     fout.open(filename.str(),std::ios::out | std::ios::app);
     fout<<"#,Time,nodeId,velo_x,velo_y,pos_x,pos_y,Csgid,Earfcn,Imsi,rrcState,rrcState_str,rrcCellId,rrcDlBw";
     fout<<"\n";
   }
  //Handover store in file
  std::fstream fileout;
  std::string handoverfilename = "handover_tcp.csv";
  fileout.open(handoverfilename,std::ios::out | std::ios::app);
  fileout << "Time,Event,IMSI,CellId,RNTI,TargetCellId";
  fileout << std::endl; 
  uint16_t dlPort = 1234;
  ApplicationContainer clientApps, serverApps;

  // configure here UDP traffic

  Ptr<Application> sapp = CreateObject<DemoTcpServer> (dlPort);
  remoteHost->AddApplication (sapp);
  serverApps.Add (sapp);

  for (uint32_t j = 0; j < ueNodes.GetN (); j++)
  {
    std::cout <<"Ip Address of node "<<ueNodes.Get(j)->GetId()<<" "<<ueNodes.Get(j)->GetObject<Ipv4>()->GetAddress(1,0)<<std::endl;
  }
  std::cout<<"Ip Address of remoteHost "<<internetIpIfaces.GetAddress(1)<<std::endl;

  for (uint32_t j = 0; j < ueNodes.GetN (); j++)
    {

      Ptr<Application> capp = CreateObject<DemoTcpClient> (internetIpIfaces.GetAddress (1), dlPort);
      ueNodes.Get(j)->AddApplication (capp);
      clientApps.Add (capp);
      
      Ptr<EpcTft> tft = Create<EpcTft> ();
      EpcTft::PacketFilter dlpf;
      dlpf.localPortStart = dlPort;
      dlpf.localPortEnd = dlPort;
      dlPort++;
      tft->Add (dlpf);
      //SIGSEGV error
      // enum EpsBearer::Qci q;

      // q = EpsBearer::GBR_CONV_VOICE;
      
      // EpsBearer bearer (q);
      // mmwaveHelper->ActivateDataRadioBearer(mcUeDevs.Get(j), bearer);
     
    }
  // start UDP server and client apps
  
  serverApps.Start (Seconds (udpAppStartTime));
  clientApps.Start (Seconds (udpAppStartTime + 1));
  serverApps.Stop (Seconds (simTime));
  clientApps.Stop (Seconds (simTime));

  double numPrints = 10;
  for (int i = 0; i < numPrints; i++)
    {
      Simulator::Schedule (Seconds (i * simTime / numPrints), &PrintPosition, ueNodes.Get (0));
    }

  double numPrintsTrace = 1000;
  uint32_t traceId = 1;
  for (int i = 0; i < numPrintsTrace; i++)
    {
      for(uint32_t j=0; j<ueNodes.GetN (); j++)
      {
        Simulator::Schedule (Seconds (i * simTime / numPrintsTrace), &printNodeTrace, ueNodes.Get (j), traceId);
      }
      traceId++;
    }
  //TODO  SIGSEGV ERROR
  for (size_t i = 0; i < buildingVector.size(); i++)
  {
    MobilityBuildingInfo (buildingVector[i]);
  }

  //BuildingsHelper::MakeMobilityModelConsistent ();
  mmwaveHelper->EnableTraces ();
    // connect custom trace sinks for RRC connection establishment and handover notification
  Config::Connect ("/NodeList/*/DeviceList/*/LteEnbRrc/ConnectionEstablished",
                   MakeCallback (&NotifyConnectionEstablishedEnb));
  Config::Connect ("/NodeList/*/DeviceList/*/LteUeRrc/ConnectionEstablished",
                   MakeCallback (&NotifyConnectionEstablishedUe));
  Config::Connect ("/NodeList/*/DeviceList/*/LteEnbRrc/HandoverStart",
                   MakeCallback (&NotifyHandoverStartEnb));
  Config::Connect ("/NodeList/*/DeviceList/*/LteUeRrc/HandoverStart",
                   MakeCallback (&NotifyHandoverStartUe));
  Config::Connect ("/NodeList/*/DeviceList/*/LteEnbRrc/HandoverEndOk",
                   MakeCallback (&NotifyHandoverEndOkEnb));
  Config::Connect ("/NodeList/*/DeviceList/*/LteUeRrc/HandoverEndOk",
                   MakeCallback (&NotifyHandoverEndOkUe));
  Simulator::Stop (Seconds (simTime));
  AnimationInterface anim ("animation-two-enbs-grid-final-stats.xml");
  for (uint32_t i = 0; i < lteEnbNodes.GetN(); i++)
  {
    anim.UpdateNodeDescription(lteEnbNodes.Get(i), "LTE eNb");
    anim.UpdateNodeColor (lteEnbNodes.Get(i), 255, 0, 0);
  }
  for (uint32_t i =0; i < mmWaveEnbNodes.GetN(); i++)
  {
    anim.UpdateNodeDescription(mmWaveEnbNodes.Get(i), "gNb"+std::to_string(i));
    anim.UpdateNodeColor(mmWaveEnbNodes.Get(i),0,255,0);
  }
  for (uint32_t i=0; i< ueNodes.GetN(); i++)
  {
    anim.UpdateNodeDescription(ueNodes.Get(i), "UE"+std::to_string(i));
    anim.UpdateNodeColor (ueNodes.Get(i),0,0,255);
  }
  anim.UpdateNodeDescription(pgw,"PGW");
  anim.UpdateNodeDescription(mme,"MME");
  anim.UpdateNodeDescription(remoteHostContainer.Get(0),"Remote Host");
  p2ph.EnablePcapAll ("multicell-stat-tcp");
  
  Simulator::Run ();
  FlowMonitorHelper flowmonHelper;
  NodeContainer endpointNodes;
  endpointNodes.Add (remoteHost);
  endpointNodes.Add (ueNodes);

  Ptr<ns3::FlowMonitor> monitor = flowmonHelper.Install (endpointNodes);
  monitor->SetAttribute ("DelayBinWidth", DoubleValue (0.001));
  monitor->SetAttribute ("JitterBinWidth", DoubleValue (0.001));
  monitor->SetAttribute ("PacketSizeBinWidth", DoubleValue (20));

  storeFlowMonitor (monitor, flowmonHelper);
  Simulator::Destroy ();
  return 0;
}

