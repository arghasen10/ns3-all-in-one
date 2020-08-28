/*
 * spdash-request-handler.cc
 *
 *  Created on: 08-Apr-2020
 *      Author: abhijit
 */

#include "spdash-request-handler.h"
#include <ns3/core-module.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include "ns3/nlohmann_json.h"

using json = nlohmann::json;

namespace ns3 {


template<typename T>
std::string ToString(T val) {
	std::stringstream stream;
	stream << val;
	return stream.str();
}

template<typename T>
std::string FromString(T val) {
	std::stringstream stream;
	stream << val;
	return stream.str();
}

NS_LOG_COMPONENT_DEFINE("SpDashRequestHandler");
NS_OBJECT_ENSURE_REGISTERED(SpDashRequestHandler);

TypeId SpDashRequestHandler::GetTypeId(void) {
	static TypeId tid = TypeId("ns3::SpDashRequestHandler")
			.SetParent<HttpServerBaseRequestHandler>()
			.SetGroupName("Applications")
			.AddConstructor<SpDashRequestHandler>();
	return tid;
}

SpDashRequestHandler::SpDashRequestHandler(): m_toSent(0), m_sent(0) {
	NS_LOG_FUNCTION(this);
	// m_videoPathMissing = true;
}

SpDashRequestHandler::~SpDashRequestHandler() {
	NS_LOG_FUNCTION(this);
}

void SpDashRequestHandler::ReadyToSend(uint32_t freeBufLen) {
	NS_LOG_FUNCTION(this);
	if(m_sent == m_toSent) {
		EndResponse();
		return;
	}

	NS_ASSERT(m_sent <= m_toSent);

	clen_t needToSent = std::min((clen_t)(m_toSent - m_sent), (clen_t)0xffffffff);
	uint8_t buf[1024];
	uint32_t tobeSend = std::min(freeBufLen, (uint32_t)sizeof(buf));
	tobeSend = std::min(tobeSend, (uint32_t)needToSent);
	uint32_t sent = Send(buf, tobeSend);
	NS_ASSERT(sent == tobeSend);
	m_sent += sent;
}

void SpDashRequestHandler::SocketClosed() {
	NS_LOG_FUNCTION(this);
}

int SpDashRequestHandler::ReadInVideoInfo() {
	NS_LOG_FUNCTION(this);
	// m_videoFilePath = "src/spdash/examples/vid.txt";	 //remove it
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

std::string SpDashRequestHandler::CreateRequestString( std::string cookie, std::string nextChunkId, std::string lastQuality, std::string buffer, std::string lastRequest, std::string rebufferTime, std::string lastChunkFinishTime, std::string lastChunkStartTime, std::string lastChunkSize) {
	json jsonBody;
	jsonBody["bitrateArray"] = m_videoData.m_averageBitrate;

	if(cookie=="")
		jsonBody["cookie"] = nullptr;
	else
		jsonBody["cookie"] = cookie;
	std::cout<<"cookie="<<cookie<<"; nextChunkId="<<nextChunkId<<"; lastQuality="<<lastQuality<<"; buffer="<<buffer<<"; lastRequest="<<lastRequest<<"; rebufferTime="<<rebufferTime<<"; lastChunkFinishTime="<<lastChunkFinishTime<<"; lastChunkStartTime="<<lastChunkStartTime<<"; lastChunkSize="<<lastChunkSize<<"\n";
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
int SpDashRequestHandler::Abr( std::string cookie, std::string segmentNum, std::string lastQuality, std::string buffer, std::string lastRequest, std::string rebufferTime, std::string lastChunkFinishTime, std::string lastChunkStartTime, std::string lastChunkSize) {
	const int PORT=8333;
	int sock = 0, valread;
    struct sockaddr_in serv_addr;

    std::string to_send = CreateRequestString( cookie, segmentNum, lastQuality, buffer, lastRequest, rebufferTime, lastChunkFinishTime, lastChunkStartTime, lastChunkSize);

    // std::cout<< "POST / HTTP/1.1\r\nUser-Agent: Mozilla/4.0 (compatible; MSIE5.01; Windows NT)\r\nHost: www.tutorialspoint.com\r\nContent-Type: text/xml; charset=utf-8\r\nContent-Length: 260\r\nAccept-Language: en-us\r\nAccept-Encoding: gzip, deflate\r\nConnection: Keep-Alive\r\n\r\n{\n\"bitrateArray\":[400000,600000,1000000,1500000,2500000,4000000],\n\"cookie\":null,\n\"nextChunkId\":1,\n\"lastquality\":1,\n\"buffer\":8,\n\"lastRequest\":0,\n\"rebufferTime\":0,\n\"lastChunkFinishTime\":1594907984144,\n\"lastChunkStartTime\":1594907984101,\n\"lastChunkSize\":1366834\n}"<<"\n";
    const char *AbrRequest = to_send.c_str();

    printf("\n####################AbrRequest#######################\n%s\n",AbrRequest);

    char buff[1024] = {0};
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if(inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr)<=0)
    {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        printf("\nConnection Failed \n");
        return -1;
    }
    // else
    // 	printf("connected\n");
    send(sock , AbrRequest , strlen(AbrRequest) , 0 );
    // printf("Hello message sent\n");
    // bool firstTime = true;
    std::string entireContent;
    while(1)
    {
    	// firstTime=false;
	    valread = read( sock , buff, 1024);
	    if(valread<=0)
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
	std::string responseBody = entireContent.substr(entireContent.find(delimiter)+4 , std::string::npos);
	// std::cout<< "substr = "<<responseBody<<"\n";
	json jsonResponse = json::parse(responseBody);
	for (auto it = jsonResponse.begin(); it != jsonResponse.end(); ++it)
	{
		// AddHeader(it.key(),it.value());
	    std::cout<<(it.key())<<" "<<(it.value())<<"\n";
	}
	AddHeader("X-LastQuality",std::to_string(int(jsonResponse["quality"])));
	AddHeader("X-Cookie",jsonResponse["cookie"]);
	// std::cout<<"\n";
	// std::cout<<"Exiting ABR-------------------------\n";
    return int(jsonResponse["quality"]);
}

//store info in server
void SpDashRequestHandler::RequestHeaderReceived() {
	NS_LOG_FUNCTION(this);
	// std::string responseLen = GetHeader("X-Require-Length");
	std::string responseLen = GetHeader("X-Require-Length");	//non zero for mpd
	std::cout<<"\n";
	if(stoul(responseLen)==0)	//if not mpd
	{
		m_videoFilePath = GetHeader("X-PathToVideo");
		std::string segmentNum = GetHeader("X-Require-Segment-Num");
		std::string lastChunkFinishTime = GetHeader("X-LastChunkFinishTime");
		std::string lastChunkStartTime =GetHeader("X-LastChunkStartTime");
		std::string buffer = (GetHeader)("X-BufferUpto");
		std::string lastChunkSize = GetHeader("X-LastChunkSize");
		std::string cookie = GetHeader("X-Cookie");
		std::string lastQuality = GetHeader("X-LastQuality");
		std::string rebufferTime = GetHeader("X-Rebuffer");
		ReadInVideoInfo();
		// if(type=="path")
		// {
		// 	m_videoPathMissing = false;
		// 	m_videoFilePath = GetHeader("videoPath");
		// 	std::cout<<"path::"<<m_videoFilePath<<"\n";
		// 	ReadInVideoInfo();
		// 	SetStatus(200, "OK");
		// 	EndHeader();
		// 	return;
		// }
		// std::string cookie = "";
		// std::string lastQuality = "0";
		// for(unsigned int i=0;i<m_videoData.m_averageBitrate.size();i++)
		// {
		// 	std::cout<<"    "<<m_videoData.m_averageBitrate[i]<<"\n";
		// }

		// std::cout<<"m_lastChunkSize = "<<m_lastChunkSize<<"\n";
		//last_request = segNum-1 //buff
		std::string lastRequest = std::to_string(stoi(segmentNum)-1);
		// std::string rebufferTime = "0";
		// std::string lastChunkSize = "1223";
		std::cout<<"header received in server"<<" nextsegmentNum="<<segmentNum<<" finish time="<<lastChunkFinishTime<<" start time="<<lastChunkStartTime<<" buffer="<<buffer<<" lastQuality="<<lastQuality<<"\n";
		int nextQualityNum = Abr( cookie, segmentNum, lastQuality, buffer, lastRequest, rebufferTime, lastChunkFinishTime, lastChunkStartTime, lastChunkSize);
		std::cout<<"nextQualityNum = "<<nextQualityNum<<"\n";
		if(nextQualityNum == -1) {
			std::cerr<<"can't connect to Abr server\n";
			return;
		}
		//nextqua
		// std::string nextQualityNumCookie = GetQualityFromAbr(m_videoData,cookie,segmentNum,lastQuality,lastRequest,lastChunkFinishTime,lastChunkStartTime,lastChunkSize);
		auto nextSegmentLength = m_videoData.m_segmentSizes.at(
				nextQualityNum).at(stoi(segmentNum));

		responseLen = std::to_string(nextSegmentLength);

		// m_lastChunkSize = nextSegmentLength;
	}
	else {
		AddHeader("X-LastQuality","0");
		AddHeader("X-Cookie","");
	}


	AddHeader("X-LastChunkSize",responseLen);
	// AddHeader("field", "response test");
	if(!responseLen.empty())
		m_toSent = std::stoul(responseLen);
	SetClen(m_toSent);
	SetStatus(200, "OK");
	EndHeader();
}

} /* namespace ns3 */
// in client side -> chunk id, d_start&finish, buffer length,rebuffer, quality, current rebuffer
// ns3 simulation - 1 server multiple clients (same video) different start time .... csv -> space separation, #header
// ttn csma cd ethernet ... later yanswifi t2 or t3
