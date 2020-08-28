/*
 * dash-video-player.cc
 *
 *  Created on: 09-Apr-2020
 *      Author: abhijit
 */

#include "dash-video-player.h"
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include "ns3/nlohmann_json.h"

using json = nlohmann::json;
namespace ns3 {


NS_LOG_COMPONENT_DEFINE("DashVideoPlayer");
NS_OBJECT_ENSURE_REGISTERED(DashVideoPlayer);

TypeId DashVideoPlayer::GetTypeId(void) {
	static TypeId tid = TypeId("ns3::DashVideoPlayer")
			.SetParent<Application>()
			.SetGroupName("Applications")
			.AddConstructor<DashVideoPlayer>()
			.AddAttribute("RemoteAddress",
					"Server address",
					AddressValue(),
					MakeAddressAccessor(&DashVideoPlayer::m_serverAddress),
					MakeAddressChecker())
			.AddAttribute("RemotePort",
					"Port on which we listen for incoming packets.",
					UintegerValue(9),
					MakeUintegerAccessor(&DashVideoPlayer::m_serverPort),
					MakeUintegerChecker<uint16_t>())
			.AddAttribute ("VideoFilePath",
					"The relative path (from ns-3.x directory) to the file containing the segment sizes in bytes",
					StringValue ("vid.txt"),
					MakeStringAccessor (&DashVideoPlayer::m_videoFilePath),
					MakeStringChecker ())
			.AddAttribute("OnStartCB",
					"Callback for start and stop",
					CallbackValue(MakeNullCallback<void>()),
					MakeCallbackAccessor(&DashVideoPlayer::m_onStartClient),
					MakeCallbackChecker())
			.AddAttribute("OnStopCB",
					"Callback for start and stop",
					CallbackValue(MakeNullCallback<void>()),
					MakeCallbackAccessor(&DashVideoPlayer::m_onStopClient),
					MakeCallbackChecker())
			.AddAttribute("TracePath",
					"File Path to store trace",
					StringValue(),
					MakeStringAccessor(&DashVideoPlayer::m_tracePath),
					MakeStringChecker());
	return tid;
}

DashVideoPlayer::DashVideoPlayer(): m_running(0), m_serverPort(0) {

}

DashVideoPlayer::~DashVideoPlayer() {
}

void DashVideoPlayer::StopApplication() {
	NS_LOG_FUNCTION(this);
	if(!m_running) return;

	if(m_httpDownloader != 0){
		m_httpDownloader->StopConnection();
	}
}

void DashVideoPlayer::EndApplication() {
	NS_LOG_FUNCTION(this);
	if(!m_running) return;
	m_running = false;

	if(!m_onStopClient.IsNull()) {
		m_onStopClient();
	}
}

void DashVideoPlayer::StartApplication() {
	NS_LOG_FUNCTION(this);
	NS_ASSERT(!m_running);
	m_running = true;

	if(!m_onStartClient.IsNull()) {
		m_onStartClient();
	}

	ReadInVideoInfo();
	StartDash();
}

int DashVideoPlayer::ReadInVideoInfo() {
	NS_LOG_FUNCTION(this);
	std::ifstream myfile;
	myfile.open(m_videoFilePath);
	NS_ASSERT(myfile);
	if (!myfile) {
		return -1;
	}
	std::string temp;
	std::getline(myfile, temp);
	if (temp.empty()) {
		return -1;
	}
	std::istringstream buffer(temp);
	std::vector<uint64_t> line((std::istream_iterator<uint64_t>(buffer)),
			std::istream_iterator<uint64_t>());
	m_videoData.m_segmentDuration = line.front();
	//read bitrates
	std::getline(myfile, temp);
	if (temp.empty()) {
		return -1;
	}
	buffer = std::istringstream(temp);
	std::vector<double> linef((std::istream_iterator<double>(buffer)),
			std::istream_iterator<double>());
	m_videoData.m_averageBitrate = linef;
	//read bitrates
	uint16_t numsegs = 0;
	while (std::getline(myfile, temp)) {
		if (temp.empty()) {
			break;
		}
		std::istringstream buffer(temp);
		std::vector<uint64_t> line((std::istream_iterator<uint64_t>(buffer)),
				std::istream_iterator<uint64_t>());
		m_videoData.m_segmentSizes.push_back(line);
		NS_ASSERT(numsegs == 0 || numsegs == line.size());
		numsegs = line.size();
	}
	m_videoData.m_numSegments = numsegs;
	NS_ASSERT_MSG(!m_videoData.m_segmentSizes.empty(),
			"No segment sizes read from file.");
	return 1;
}

/****************************************
 *             DASH functions
 ****************************************/


void DashVideoPlayer::StartDash() {
	NS_LOG_FUNCTION(this);
	if(!m_running) return;
	const clen_t expected = 1232; //Arbit

	NS_ASSERT(m_playback.m_state == DASH_PLAYER_STATE_UNINITIALIZED);

	m_httpDownloader = Create<HttpClientBasic>();
	m_httpDownloader->SetCollectionCB(MakeCallback(&DashVideoPlayer::DownloadedCB, this).Bind(Ptr<Object>()), GetNode());
	m_httpDownloader->InitConnection(m_serverAddress, m_serverPort, "/mpd");

	m_httpDownloader->AddReqHeader("X-Require-Length", std::to_string(expected));
	//initialisation
	m_lastChunkStartTime = Simulator::Now();	//time when download started
	m_cookie = "";
	m_lastChunkSize = std::to_string(expected);
	m_lastQuality = "0";
	m_totalRebuffer = Time(0);
	m_currentRebuffer = Time(0);
	m_httpDownloader->Connect();
	m_playback.m_state = DASH_PLAYER_STATE_MPD_DOWNLOADING;
}



void DashVideoPlayer::Downloaded() {
	NS_LOG_FUNCTION(this);
	HttpTrace trace = m_httpDownloader->GetTrace();
	m_httpTrace.push_back(trace);
	if(!m_running) return;

	switch (m_playback.m_state) {
		case DASH_PLAYER_STATE_MPD_DOWNLOADING:
			m_playback.m_state = DASH_PLAYER_STATE_MPD_DOWNLOADED;
			DownloadNextSegment(); //no need to go through
			break;
		case DASH_PLAYER_STATE_SEGMENT_DOWNLOADING:
			m_playback.m_state = DASH_PLAYER_STATE_IDLE;
			DashController();
			break;
		default:
			NS_ASSERT(false && "Invalid State");
	}
}

void DashVideoPlayer::DownloadedCB(Ptr<Object> obj) {
	NS_LOG_FUNCTION(this);

	m_lastChunkFinishTime = Simulator::Now();
	Simulator::ScheduleNow(&DashVideoPlayer::Downloaded, this);
}

#define MIN_BUFFER_LENGTH 30 //sec
#define NS_IN_SEC 1000000
void DashVideoPlayer::DashController() {
	NS_LOG_FUNCTION(this);

	AdjustVideoMetrices();

	if(m_playback.m_curSegmentNum < m_videoData.m_numSegments - 1) {
		Time delay = std::max(m_playback.m_bufferUpto - Time(std::to_string(MIN_BUFFER_LENGTH) + "s"), Time(0));
		Simulator::Schedule(delay, &DashVideoPlayer::DownloadNextSegment, this);
	}
	else{
		Simulator::Schedule(m_playback.m_bufferUpto, &DashVideoPlayer::FinishedPlayback, this);
	}
}

std::string DashVideoPlayer::CreateRequestString( std::string cookie, std::string nextChunkId, std::string lastQuality, std::string buffer, std::string lastRequest, std::string rebufferTime, std::string lastChunkFinishTime, std::string lastChunkStartTime, std::string lastChunkSize) {
	json jsonBody;
	jsonBody["bitrateArray"] = m_videoData.m_averageBitrate;

	if(cookie=="")
		jsonBody["cookie"] = nullptr;
	else
		jsonBody["cookie"] = cookie;
//	std::cout<<"cookie="<<cookie<<", nextChunkId="<<nextChunkId<<", lastQuality="<<lastQuality<<", buffer="<<buffer<<", lastRequest="<<lastRequest<<", rebufferTime="<<rebufferTime<<", lastChunkFinishTime="<<lastChunkFinishTime<<", lastChunkStartTime="<<lastChunkStartTime<<", lastChunkSize="<<lastChunkSize<<"\n";
	jsonBody["nextChunkId"] = stoul(nextChunkId);
	// std::cout<<"okay\n"<<lastQuality<<"\n"<<buffer<<"\n";
	jsonBody["lastquality"] = stoul(lastQuality);
	// std::cout<<"okay\n";
	jsonBody["buffer"] = stod(buffer);  //in seconds

	jsonBody["lastRequest"] = stoul(lastRequest);
	jsonBody["rebufferTime"] = stod(rebufferTime);
	// std::cout<<"okay\n";
	jsonBody["lastChunkFinishTime"] = stoul(lastChunkFinishTime);
	jsonBody["lastChunkStartTime"] = stoul(lastChunkStartTime);
	// std::cout<<"okay"<<lastChunkSize<<"\n";
	jsonBody["lastChunkSize"] = stoul(lastChunkSize);
	// std::cout<<"okay\n";
	std::string jsonString = jsonBody.dump(4);
	int contentLength = jsonString.length();
	std::string requestString = "POST / HTTP/1.1\r\nUser-Agent: Mozilla/4.0 (compatible; MSIE5.01; Windows NT)\r\nHost: www.tutorialspoint.com\r\nContent-Type: text/xml; charset=utf-8\r\nContent-Length: " + std::to_string(contentLength) + "\r\nAccept-Language: en-us\r\nAccept-Encoding: gzip, deflate\r\nConnection: Keep-Alive\r\n\r\n"+jsonString;
	// std::cout<<"jstring="<<requestString<<"\n";
	return requestString;
}

//Adds header in response
int
DashVideoPlayer::Abr (std::string cookie, std::string segmentNum, std::string lastQuality, std::string buffer,
                      std::string lastRequest, std::string rebufferTime, std::string lastChunkFinishTime,
                      std::string lastChunkStartTime, std::string lastChunkSize)
{
  const int PORT = 8333;
  int sock = 0, valread;
  struct sockaddr_in serv_addr;

  std::string to_send = CreateRequestString (cookie, segmentNum, lastQuality, buffer, lastRequest, rebufferTime,
                                             lastChunkFinishTime, lastChunkStartTime, lastChunkSize);

  // std::cout<< "POST / HTTP/1.1\r\nUser-Agent: Mozilla/4.0 (compatible; MSIE5.01; Windows NT)\r\nHost: www.tutorialspoint.com\r\nContent-Type: text/xml; charset=utf-8\r\nContent-Length: 260\r\nAccept-Language: en-us\r\nAccept-Encoding: gzip, deflate\r\nConnection: Keep-Alive\r\n\r\n{\n\"bitrateArray\":[400000,600000,1000000,1500000,2500000,4000000],\n\"cookie\":null,\n\"nextChunkId\":1,\n\"lastquality\":1,\n\"buffer\":8,\n\"lastRequest\":0,\n\"rebufferTime\":0,\n\"lastChunkFinishTime\":1594907984144,\n\"lastChunkStartTime\":1594907984101,\n\"lastChunkSize\":1366834\n}"<<"\n";
  const char *AbrRequest = to_send.c_str ();

  printf ("\n####################AbrRequest#######################\n%s\n", AbrRequest);

  char buff[1024] =
      { 0 };
  if ((sock = socket (AF_INET, SOCK_STREAM, 0)) < 0)
    {
      printf ("\n Socket creation error \n");
      return -1;
    }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons (PORT);

  // Convert IPv4 and IPv6 addresses from text to binary form
  if (inet_pton (AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
    {
      printf ("\nInvalid address/ Address not supported \n");
      return -1;
    }

  if (connect (sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0)
    {
      printf ("\nConnection Failed \n");
      return -1;
    }
  // else
  // 	printf("connected\n");
  send (sock, AbrRequest, strlen (AbrRequest), 0);
  // printf("Hello message sent\n");
  // bool firstTime = true;
  std::string entireContent;
  while (1)
    {
      // firstTime=false;
      valread = read (sock, buff, 1024);
      if (valread <= 0)
        break;
      buff[valread] = '\0';
      entireContent += buff;
      // printf("%d %s####\n",valread,buff );
    }
  //std::cout<< "entire response<<<<" << entireContent << "<<<<<<<<<\n\n\n\n" ;
  // for(unsigned int i=0 ; i < entireContent.length();i++) {
  // 	std::cout<<int(entireContent[i])<<entireContent[i]<<"\n";
  // }
  std::string delimiter = "\r\n\r\n";
  std::string responseBody = entireContent.substr (entireContent.find (delimiter) + 4, std::string::npos);
  // std::cout<< "substr = "<<responseBody<<"\n";
  json jsonResponse = json::parse (responseBody);
  for (auto it = jsonResponse.begin (); it != jsonResponse.end (); ++it)
    {
      // AddHeader(it.key(),it.value());
      std::cout << (it.key ()) << " " << (it.value ()) << "\n";
    }
  m_lastQuality = std::to_string (int (jsonResponse["quality"]));
  m_cookie = jsonResponse["cookie"];
  // std::cout<<"\n";
  // std::cout<<"Exiting ABR-------------------------\n";
  return int (jsonResponse["quality"]);
}

void DashVideoPlayer::DownloadNextSegment() {
	NS_LOG_FUNCTION(this);
	if(!m_running) return;
	m_playback.m_curSegmentNum += 1;
	if(m_playback.m_curSegmentNum == m_videoData.m_numSegments) {
		m_playback.m_state = DASH_PLAYER_STATE_FINISHED;
		return;
	}

	std::string segmentNum = std::to_string(m_playback.m_curSegmentNum);
	std::string lastChunkFinishTime = std::to_string(m_lastChunkFinishTime.GetNanoSeconds());
	std::string lastChunkStartTime = std::to_string(m_lastChunkStartTime.GetNanoSeconds());
	std::string buffer = std::to_string(m_playback.m_bufferUpto.GetSeconds());
	// std::to_string(0);
	std::string lastChunkSize = m_lastChunkSize;
	std::string cookie = m_cookie;
	std::string lastQuality = m_lastQuality;
	std::string rebufferTime = std::to_string(m_totalRebuffer.GetSeconds());
	std::string lastRequest = std::to_string(m_playback.m_curSegmentNum-1);
	int nextQualityNum = Abr( cookie, segmentNum, lastQuality, buffer, lastRequest, rebufferTime, lastChunkFinishTime, lastChunkStartTime, lastChunkSize);//updates m_lq,m_cookie

	/****************************************
	 * We will call abrController from here *
	 ****************************************/
	auto nextSegmentLength = m_videoData.m_segmentSizes.at(
			nextQualityNum).at(m_playback.m_curSegmentNum);

	std::string url = "/seg-" + std::to_string(m_playback.m_curSegmentNum)
			+ "-" + std::to_string(m_playback.m_nextQualityNum);
	m_httpDownloader->InitConnection(m_serverAddress, m_serverPort, url);
	m_httpDownloader->AddReqHeader("X-Require-Length", std::to_string(nextSegmentLength));

	m_lastChunkStartTime = Simulator::Now();
	m_lastChunkSize = std::to_string(nextSegmentLength);
	m_httpDownloader->Connect();
	m_playback.m_state = DASH_PLAYER_STATE_SEGMENT_DOWNLOADING;
}

void DashVideoPlayer::FinishedPlayback() {
	NS_LOG_FUNCTION(this);
	m_playback.m_state = DASH_PLAYER_STATE_FINISHED;
	EndApplication();
//	StopApplication();
//	std::cout<<"playback finished"<<std::endl;
}

void DashVideoPlayer::AdjustVideoMetrices() {
	NS_LOG_FUNCTION(this);
	Time timeSpent(0);
	Time curBufUpto(0);
	Time stall(0);
	Time curPlaybackTime(0);
	if(m_playback.m_curSegmentNum > 0) {
		timeSpent = Simulator::Now() - m_lastIncident;
		curBufUpto = std::max(m_playback.m_bufferUpto - timeSpent, Time(0));
		auto bufChanged = m_playback.m_bufferUpto - curBufUpto;
		stall = timeSpent - bufChanged;
		std::cout<<"\t\t\t\t\t\t"<<stall<<"=stall\n";
		m_currentRebuffer = stall;
		m_totalRebuffer += stall;
		curPlaybackTime = m_playback.m_playbackTime + bufChanged;
	}
	m_lastIncident = Simulator::Now();

	m_playback.m_playbackTime = curPlaybackTime;
	m_playback.m_bufferUpto = curBufUpto + Time(std::to_string(m_videoData.m_segmentDuration) + "us");
}

void DashVideoPlayer::LogTrace() {
	NS_LOG_FUNCTION(this);
	if(m_tracePath.empty()) return;

	std::ofstream outFile;
	std::string fpath = m_tracePath + "-" + std::to_string(GetNode()->GetId()) + ".json";
	outFile.open(fpath.c_str(), std::ofstream::out | std::ofstream::trunc);
	if (!outFile.is_open()) {
		std::cerr << "Can't open file " << m_tracePath << std::endl;
		return;
	}
	std::string separator = "";
	outFile << "[" << std::endl;
	for(auto it : m_httpTrace) {
		outFile << separator;
		it.StoreInFile(outFile);
		outFile << std::endl;
		separator = ",";
	}
	outFile << "]" << std::endl;
}

void DashVideoPlayer::LogABR() {
}

} /* namespace ns3 */
