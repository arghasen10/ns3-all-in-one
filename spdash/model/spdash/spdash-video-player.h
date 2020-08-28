/*
 * dash-video-player.h
 *
 *  Created on: 09-Apr-2020
 *      Author: abhijit
 */

#ifndef SRC_SPDASH_MODEL_SPDASH_SPDASH_VIDEO_PLAYER_H_
#define SRC_SPDASH_MODEL_SPDASH_SPDASH_VIDEO_PLAYER_H_

#include "spdash-common.h"
#include "ns3/application.h"

#include "ns3/http-client-basic.h"
#include<fstream>

namespace ns3 {


class SpDashVideoPlayer:public Application {
public:
	static TypeId GetTypeId(void);
	SpDashVideoPlayer();
	virtual ~SpDashVideoPlayer();

private:
	virtual void StopApplication();
	virtual void StartApplication();
	virtual void EndApplication();

	int ReadInVideoInfo ();
	void FinishedPlayback ();


/********************************
 *   DASH state functions
 ********************************/
	void StartDash(); //Should be called only once.
	void Downloaded();
	void DownloadedCB(Ptr<Object> obj);
	void DashController();
	void DownloadNextSegment();
	void AdjustVideoMetrices();

	void LogTrace();

	void LogABR();

/********************************
 *    DASH state variable
 ********************************/
	std::string m_clientId;
	bool m_running;
	Address m_serverAddress;
	uint16_t m_serverPort;
	std::string m_videoFilePath;
	VideoData m_videoData;
	SpDashPlaybackStatus m_playback;
	Ptr<HttpClientBasic> m_httpDownloader;
	std::vector<HttpTrace> m_httpTrace;
	std::string m_tracePath;
	Time m_lastIncident;
	Time m_lastChunkStartTime;
	Time m_lastChunkFinishTime;
	std::string m_lastQuality;
	std::string m_lastChunkSize;
	std::string m_cookie;
	Time m_totalRebuffer;
	Time m_currentRebuffer;
	std::ofstream m_file;
	std::string m_allLogFile;

	Callback<void> m_onStartClient;
	Callback<void> m_onStopClient;
};

} /* namespace ns3 */

#endif /* SRC_SPDASH_MODEL_DASH_DASH_VIDEO_PLAYER_H_ */
