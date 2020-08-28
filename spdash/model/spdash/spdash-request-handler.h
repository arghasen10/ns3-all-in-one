/*
 * dash-request-handler.h
 *
 *  Created on: 08-Apr-2020
 *      Author: abhijit
 */

#ifndef SRC_SPDASH_MODEL_SPDASH_SPDASH_REQUEST_HANDLER_H_
#define SRC_SPDASH_MODEL_SPDASH_SPDASH_REQUEST_HANDLER_H_

#include "ns3/http-server-base-request-handler.h"
#include "spdash-common.h"

namespace ns3 {

class SpDashRequestHandler: public HttpServerBaseRequestHandler {
public:
	static TypeId GetTypeId(void);
	SpDashRequestHandler();
	virtual ~SpDashRequestHandler();
protected:
	virtual void ReadyToSend(uint32_t);
	virtual void SocketClosed();
	virtual void RequestHeaderReceived();
private:
	clen_t m_toSent;
	clen_t m_sent;

	// new
	std::string m_videoFilePath;
	VideoData m_videoData;
	// int m_lastChunkSize;
	int ReadInVideoInfo();
	int Abr(std::string cookie, std::string nextChunkId, std::string lastQuality, std::string buffer, std::string lastRequest, std::string rebufferTime, std::string lastChunkFinishTime, std::string lastChunkStartTime, std::string lastChunkSize);
	std::string CreateRequestString( std::string cookie, std::string nextChunkId, std::string lastQuality, std::string buffer, std::string lastRequest, std::string rebufferTime, std::string lastChunkFinishTime, std::string lastChunkStartTime, std::string lastChunkSize);

};

} /* namespace ns3 */

#endif /* SRC_SPDASH_MODEL_DASH_DASH_REQUEST_HANDLER_H_ */
