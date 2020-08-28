/*
 * dash-video-player.cc
 *
 *  Created on: 09-Apr-2020
 *      Author: abhijit
 */

#include "spdash-video-player.h"

namespace ns3 {


NS_LOG_COMPONENT_DEFINE("SpDashVideoPlayer");
NS_OBJECT_ENSURE_REGISTERED(SpDashVideoPlayer);

TypeId SpDashVideoPlayer::GetTypeId(void) {
	static TypeId tid = TypeId("ns3::SpDashVideoPlayer")
			.SetParent<Application>()
			.SetGroupName("Applications")
			.AddConstructor<SpDashVideoPlayer>()
			.AddAttribute("RemoteAddress",
					"Server address",
					AddressValue(),
					MakeAddressAccessor(&SpDashVideoPlayer::m_serverAddress),
					MakeAddressChecker())
			.AddAttribute("RemotePort",
					"Port on which we listen for incoming packets.",
					UintegerValue(9),
					MakeUintegerAccessor(&SpDashVideoPlayer::m_serverPort),
					MakeUintegerChecker<uint16_t>())
			.AddAttribute ("VideoFilePath",
					"The relative path (from ns-3.x directory) to the file containing the segment sizes in bytes",
					StringValue ("vid.txt"),
					MakeStringAccessor (&SpDashVideoPlayer::m_videoFilePath),
					MakeStringChecker ())
			.AddAttribute("OnStartCB",
					"Callback for start and stop",
					CallbackValue(MakeNullCallback<void>()),
					MakeCallbackAccessor(&SpDashVideoPlayer::m_onStartClient),
					MakeCallbackChecker())
			.AddAttribute("OnStopCB",
					"Callback for start and stop",
					CallbackValue(MakeNullCallback<void>()),
					MakeCallbackAccessor(&SpDashVideoPlayer::m_onStopClient),
					MakeCallbackChecker())
			.AddAttribute("TracePath",
					"File Path to store trace",
					StringValue(),
					MakeStringAccessor(&SpDashVideoPlayer::m_tracePath),
					MakeStringChecker())
			.AddAttribute("ClientId",
					"Client Identifier",
					StringValue(),
					MakeStringAccessor(&SpDashVideoPlayer::m_clientId),
					MakeStringChecker())
			.AddAttribute("AllLogFile",
					"file path which stores logs of all clients",
					StringValue(),
					MakeStringAccessor(&SpDashVideoPlayer::m_allLogFile),
					MakeStringChecker());
	return tid;
}

SpDashVideoPlayer::SpDashVideoPlayer(): m_running(0), m_serverPort(0) {

}

SpDashVideoPlayer::~SpDashVideoPlayer() {
}

void SpDashVideoPlayer::StopApplication() {
	NS_LOG_FUNCTION(this);
	if(!m_running) return;

	if(m_httpDownloader != 0){
		m_httpDownloader->StopConnection();
	}
}

void SpDashVideoPlayer::EndApplication() {
	NS_LOG_FUNCTION(this);
	if(!m_running) return;
	m_running = false;

	if(!m_onStopClient.IsNull()) {
		m_onStopClient();
	}
}

void SpDashVideoPlayer::StartApplication() {
	NS_LOG_FUNCTION(this);
	NS_ASSERT(!m_running);
	m_running = true;

	if(!m_onStartClient.IsNull()) {
		m_onStartClient();
	}

	ReadInVideoInfo();
	StartDash();
}

int SpDashVideoPlayer::ReadInVideoInfo() {
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


void SpDashVideoPlayer::StartDash() {
	NS_LOG_FUNCTION(this);
	if(!m_running) return;
	const clen_t expected = 1232; //Arbit
	m_playback.m_curSegmentNum = 0;

	NS_ASSERT(m_playback.m_state == DASH_PLAYER_STATE_UNINITIALIZED);
	m_file.open("log_client_"+m_clientId+".csv");
	m_file <<"Time SegmentNumber ChunkSize StartTime FinishTime Buffer TotalRebuffer Quality CurrentRebuffer\n";
	// m_httpDownloader = Create<HttpClientBasic>();
	// m_httpDownloader->SetCollectionCB(MakeCallback(&SpDashVideoPlayer::DownloadedCB, this).Bind(Ptr<Object>()), GetNode()); //when is it called
	// m_httpDownloader->InitConnection(m_serverAddress, m_serverPort, "/mpd");

	// m_httpDownloader->AddReqHeader("X-Require-Segment-Num", std::to_string(m_playback.m_curSegmentNum));
	// // m_httpDownloader->AddReqHeader("X-Require-Length", std::to_string(expected));
	// m_httpDownloader->Connect();
	// m_playback.m_state = DASH_PLAYER_STATE_MPD_DOWNLOADING;


	m_httpDownloader = Create<HttpClientBasic>();
	m_httpDownloader->InitConnection(m_serverAddress, m_serverPort, "/mpd");
	m_httpDownloader->SetCollectionCB(MakeCallback(&SpDashVideoPlayer::DownloadedCB, this).Bind(Ptr<Object>()), GetNode());
	m_httpDownloader->AddReqHeader("videoPath",m_videoFilePath);
	m_httpDownloader->AddReqHeader("X-Require-Length", std::to_string(expected));
	// m_httpDownloader->AddReqHeader("X-Require-Length", std::to_string(expected));
	m_lastChunkStartTime = Simulator::Now();	//time when download started
	m_cookie = "";
	// std::cout<<"first req sent reqLen="<<expected<<"\n";
	m_httpDownloader->Connect();
	m_playback.m_state = DASH_PLAYER_STATE_MPD_DOWNLOADING;

	// m_httpDownloader->SetCollectionCB(MakeCallback(&SpDashVideoPlayer::DownloadedCB, this).Bind(Ptr<Object>()), GetNode()); //when is it called
	// m_httpDownloader->InitConnection(m_serverAddress, m_serverPort, "/mpd");

	// m_httpDownloader->AddReqHeader("X-Require-Segment-Num", std::to_string(m_playback.m_curSegmentNum));
	// // m_httpDownloader->AddReqHeader("X-Require-Length", std::to_string(expected));
	// m_httpDownloader->Connect();
	// m_playback.m_state = DASH_PLAYER_STATE_MPD_DOWNLOADING;

}



void SpDashVideoPlayer::Downloaded() {
	NS_LOG_FUNCTION(this);
	HttpTrace trace = m_httpDownloader->GetTrace();
	m_httpTrace.push_back(trace);
	if(!m_running) return;

	switch (m_playback.m_state) {
		case DASH_PLAYER_STATE_MPD_DOWNLOADING:						//is mpd really downloaded
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

void SpDashVideoPlayer::DownloadedCB(Ptr<Object> obj) {
	NS_LOG_FUNCTION(this);


	m_cookie = m_httpDownloader->GetResponse()->GetHeader("X-Cookie");
	m_lastQuality = m_httpDownloader->GetResponse()->GetHeader("X-LastQuality");
	m_lastChunkSize = m_httpDownloader->GetResponse()->GetHeader("X-LastChunkSize"); //check
	// std::cout<<m_cookie<<" "<<m_lastQuality<<" "<<m_lastChunkSize<<"\n";
	m_lastChunkFinishTime = Simulator::Now();
	Simulator::ScheduleNow(&SpDashVideoPlayer::Downloaded, this);
}

#define MIN_BUFFER_LENGTH 30 //sec
#define NS_IN_SEC 1000000
void SpDashVideoPlayer::DashController() {
	NS_LOG_FUNCTION(this);

	AdjustVideoMetrices();
	std::cout<<"\t\t\t\tClient side :: downloaded !!\tchunk id="<<m_playback.m_curSegmentNum<<", start time="<<m_lastChunkStartTime<<", finish time="<<m_lastChunkFinishTime<<", buffer length="<<m_playback.m_bufferUpto<<", total rebuffer="<<m_totalRebuffer<<", quality="<<m_lastQuality<<", current rebuffer="<<m_currentRebuffer<<"\n";
	m_file <<Simulator::Now()<<" "<<m_playback.m_curSegmentNum<<" "<<m_lastChunkSize<<" "<<m_lastChunkStartTime<<" "<<m_lastChunkFinishTime<<" "<<m_playback.m_bufferUpto<<" "<<m_totalRebuffer<<" "<<m_lastQuality<<" "<<m_currentRebuffer<<"\n";

	std::ofstream allClientLog;
	allClientLog.open(m_allLogFile, std::ofstream::out | std::ofstream::app);
	allClientLog<<Simulator::Now()<<" "<<m_clientId<<" "<<m_playback.m_curSegmentNum<<" "<<m_lastChunkSize<<" "<<m_lastChunkStartTime<<" "<<m_lastChunkFinishTime<<" "<<m_playback.m_bufferUpto<<" "<<m_totalRebuffer<<" "<<m_lastQuality<<" "<<m_currentRebuffer<<"\n";
	allClientLog.close();

	if(m_playback.m_curSegmentNum < m_videoData.m_numSegments - 1) {
		Time delay = std::max(m_playback.m_bufferUpto - Time(std::to_string(MIN_BUFFER_LENGTH) + "s"), Time(0));
		Simulator::Schedule(delay, &SpDashVideoPlayer::DownloadNextSegment, this);
	}
	else{
		Simulator::Schedule(m_playback.m_bufferUpto, &SpDashVideoPlayer::FinishedPlayback, this);
	}

}

void SpDashVideoPlayer::DownloadNextSegment() {
	NS_LOG_FUNCTION(this);
	if(!m_running) return;

	m_playback.m_curSegmentNum += 1;
	if(m_playback.m_curSegmentNum == m_videoData.m_numSegments) {
		m_playback.m_state = DASH_PLAYER_STATE_FINISHED;
		return;
	}
	/****************************************
	 * We will call abrController from here *
	 ****************************************/
	// auto nextSegmentLength = m_videoData.m_segmentSizes.at(
	// 		m_playback.m_nextQualityNum).at(m_playback.m_curSegmentNum);
	std::string url = "/seg-" + std::to_string(m_playback.m_curSegmentNum)
			+ "-" + std::to_string(m_playback.m_nextQualityNum);
	m_httpDownloader->InitConnection(m_serverAddress, m_serverPort, url);
	// m_httpDownloader->AddReqHeader("X-Require-Length", std::to_string(nextSegmentLength));
	m_httpDownloader->AddReqHeader("X-PathToVideo",m_videoFilePath);
	m_httpDownloader->AddReqHeader("X-Require-Segment-Num", std::to_string(m_playback.m_curSegmentNum));
	m_httpDownloader->AddReqHeader("X-LastChunkFinishTime", std::to_string(m_lastChunkFinishTime.GetNanoSeconds()));
	m_httpDownloader->AddReqHeader("X-LastChunkStartTime", std::to_string(m_lastChunkStartTime.GetNanoSeconds()));
	m_httpDownloader->AddReqHeader("X-BufferUpto", std::to_string(m_playback.m_bufferUpto.GetSeconds()));
	m_httpDownloader->AddReqHeader("X-Require-Length", std::to_string(0));
	m_httpDownloader->AddReqHeader("X-LastChunkSize",m_lastChunkSize);
	m_httpDownloader->AddReqHeader("X-Cookie",m_cookie);
	m_httpDownloader->AddReqHeader("X-LastQuality",m_lastQuality);
	m_httpDownloader->AddReqHeader("X-Rebuffer",std::to_string(m_totalRebuffer.GetSeconds()));

	m_lastChunkStartTime = Simulator::Now();	//time when download started
	m_httpDownloader->Connect();

	// std::cout<<"connect called"<<m_playback.m_curSegmentNum<<" segLen="<<nextSegmentLength<<"\n";
	// std::cout<<"ftme "<<m_lastChunkFinishTime<<" "<<m_lastChunkFinishTime.GetNanoSeconds()<<"\n";
	m_playback.m_state = DASH_PLAYER_STATE_SEGMENT_DOWNLOADING;
}

void SpDashVideoPlayer::FinishedPlayback() {
	NS_LOG_FUNCTION(this);
	m_playback.m_state = DASH_PLAYER_STATE_FINISHED;
	m_file.close();
	EndApplication();
//	StopApplication();
//	std::cout<<"playback finished"<<std::endl;
}

void SpDashVideoPlayer::AdjustVideoMetrices() {
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
		m_currentRebuffer = stall;
		m_totalRebuffer += stall;
		curPlaybackTime = m_playback.m_playbackTime + bufChanged;
	}
	m_lastIncident = Simulator::Now();

	m_playback.m_playbackTime = curPlaybackTime;
	m_playback.m_bufferUpto = curBufUpto + Time(std::to_string(m_videoData.m_segmentDuration) + "us");
}

void SpDashVideoPlayer::LogTrace() {
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

void SpDashVideoPlayer::LogABR() {
}

} /* namespace ns3 */

//doubts
// lastRequestId is not used by abr
// rebuffer rime - unit = seconds
// start and finish time - unit= ns
