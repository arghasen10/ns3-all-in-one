/*
 * basic-http-client.cc
 *
 *  Created on: 06-Apr-2020
 *      Author: abhijit
 */

#include "http-client-basic.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/tcp-socket.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/uinteger.h"

namespace ns3 {
NS_LOG_COMPONENT_DEFINE("HttpClientBasic");
NS_OBJECT_ENSURE_REGISTERED(HttpClientBasic);

#define DOWNLOADSIZE_EXP 34567

TypeId HttpClientBasic::GetTypeId(void) {
	static TypeId tid = TypeId("ns3::HttpClientBasic")
				.SetParent<Application>()
				.SetGroupName("Applications")
				.AddConstructor<HttpClientBasic>();
	return tid;
}

HttpClientBasic::HttpClientBasic(): m_peerPort(0) {
	m_method = "GET";
	m_path = "/";
}

HttpClientBasic::~HttpClientBasic() {
}



void HttpClientBasic::RecvResponseData(uint8_t *data, uint32_t len) {
}

void HttpClientBasic::RecvResponseHeader() {
}


void HttpClientBasic::InitConnection(Address peerAddress, uint16_t peerPort, std::string path) {
	NS_LOG_FUNCTION(this);
	m_peerAddress = peerAddress;
	m_peerPort = peerPort;
	m_path = path;
	InitConnection();
}

void HttpClientBasic::InitConnection(std::string path) {
	NS_LOG_FUNCTION(this);

	if(m_socket != 0){
		m_socket = 0;
	}

	if(path.length() != 0) {
		m_path = path;
	}

	m_request = Create<HttpRequest>(m_method, m_path, "http1.1");
	m_response = Create<HttpResponse>();

}

void HttpClientBasic::Connect() {
	m_trace = HttpTrace();
	TypeId tid = TypeId::LookupByName("ns3::TcpSocketFactory");
	m_socket = Socket::CreateSocket(m_node, tid);

	m_socket->SetConnectCallback(
			MakeCallback(&HttpClientBasic::EvConnectionSucceeded, this),
			MakeCallback(&HttpClientBasic::EvConnectionFailed, this));
	m_socket->SetRecvCallback(MakeCallback(&HttpClientBasic::EvHandleRecv, this));
	m_socket->SetSendCallback(MakeCallback(&HttpClientBasic::EvHandleSend, this));
	m_socket->SetCloseCallbacks(MakeCallback(&HttpClientBasic::EvSocketClosed, this),
			MakeCallback(&HttpClientBasic::EvErrorClosed, this));

	if (Ipv4Address::IsMatchingType(m_peerAddress) == true) {
		m_socket->Connect(
				InetSocketAddress(Ipv4Address::ConvertFrom(m_peerAddress),
						m_peerPort));
	} else if (Ipv6Address::IsMatchingType(m_peerAddress) == true) {
		m_socket->Connect(
				Inet6SocketAddress(Ipv6Address::ConvertFrom(m_peerAddress),
						m_peerPort));
	}
}

void HttpClientBasic::StopConnection() {
	if(m_socket) {
		m_socket->SetConnectCallback(
				MakeNullCallback<void, Ptr<Socket> >(),
				MakeNullCallback<void, Ptr<Socket> >());
		m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket> >());
		m_socket->SetSendCallback(MakeNullCallback<void, Ptr<Socket>, uint32_t >());
		m_socket->SetCloseCallbacks(
				MakeNullCallback<void, Ptr<Socket> >(),
				MakeNullCallback<void, Ptr<Socket> >());
		m_socket->Close();
	}
	EndConnection();
}



void HttpClientBasic::SetCollectionCB(Callback<void> onConnectionClosed, Ptr<Node> node) {
	m_onConnectionClosed = onConnectionClosed;
//	m_collectionBlob = blob;
	m_node = node;
}

void HttpClientBasic::AddReqHeader(std::string name, std::string value) {
	m_request->AddHeader(name, value);
}

void HttpClientBasic::EndConnection() {
//	std::cout << m_trace.m_reqSentAt << " " << m_trace.m_firstByteAt << " " << m_trace.m_lastByteAt << std::endl;
	if(!m_onConnectionClosed.IsNull()){
//		Simulator::ScheduleNow(m_onConnectionClosed, m_collectionBlob);
		m_onConnectionClosed();
	}
}


const Ptr<HttpResponse>& HttpClientBasic::GetResponse() const {
	return m_response;
}

const HttpTrace& HttpClientBasic::GetTrace() const {
	return m_trace;
}



//===================
void HttpClientBasic::EvConnectionFailed(Ptr<Socket> socket) {
	NS_LOG_FUNCTION(this << socket);
	NS_LOG_LOGIC("Http Client connection failed");
	EndConnection();
}

void HttpClientBasic::EvConnectionSucceeded(Ptr<Socket> socket) {
	NS_LOG_FUNCTION(this << socket);
	EvHandleSend(socket, socket->GetTxAvailable());
}

void HttpClientBasic::EvErrorClosed(Ptr<Socket> socket) {
	NS_LOG_FUNCTION(this << socket);
	NS_LOG_LOGIC("Http Client connection error closed");
//	std::cout << "Error Closed" << std::endl;
}

void HttpClientBasic::EvHandleRecv(Ptr<Socket> socket) {
	NS_LOG_FUNCTION(this << socket);

	uint32_t rxAvailable = m_socket->GetRxAvailable();
	if(rxAvailable == 0) return;

	uint8_t *buf = new uint8_t[rxAvailable];
	uint32_t rlen = m_socket->Recv(buf, rxAvailable, 0);
	NS_ASSERT(rxAvailable == rlen);
	m_trace.ResponseRecv(rlen);

	if(!m_response->IsHeaderReceived()) {
		m_response->ParseHeader(buf, rlen);

		if(!m_response->IsHeaderReceived()) {
			goto cleanup;
		}
		RecvResponseHeader();
		rlen = m_response->ReadBody(buf, sizeof(buf));

	}
	if (rlen > 0)
		RecvResponseData(buf, rlen);
cleanup:
	delete[] buf;
}

void HttpClientBasic::EvHandleSend(Ptr<Socket> socket, uint32_t bufAvailable) {
	NS_LOG_FUNCTION(this << socket);

	m_trace.RequestSent();

	uint8_t buf[2048];
	uint32_t toSend = std::min(bufAvailable, (uint32_t)sizeof(buf));

	uint32_t len = m_request->ReadHeader(buf, toSend);
	if(len > 0) {
		uint32_t sent = m_socket->Send(buf, len, 0);
		NS_ASSERT(sent == len);
	}
}

void HttpClientBasic::EvSocketClosed(Ptr<Socket> socket) {
	NS_LOG_FUNCTION(this << socket);
	NS_LOG_LOGIC("Http Client connection closed");
//	std::cout << "Success Closed" << std::endl;
	EndConnection();
}

void HttpTrace::RequestSent() {
	m_reqSentAt = Simulator::Now();
}

void HttpTrace::ResponseRecv(uint32_t len) {

	if(m_firstByteAt.IsZero())
		m_firstByteAt = Simulator::Now();

	auto now = Simulator::Now();
	if(m_trace.size() > 1) {
		m_speed = (len*8 * 1.0e6) / (now.GetMicroSeconds() - m_lastByteAt.GetMicroSeconds());
	}
	m_lastByteAt = now;
	m_resLen += len;
	std::pair<Time, uint64_t> entry(Simulator::Now(), m_resLen);

	m_trace.push_back(entry);
}

void HttpTrace::StoreInFile(std::ostream &outFile) {
	outFile << "{" ;
	outFile << "\"reqSentAt\":" << m_reqSentAt.GetSeconds() << ",";
	outFile << "\"firstByteAt\":" << m_firstByteAt.GetSeconds() << ",";
	outFile << "\"lastByteAt\":" << m_lastByteAt.GetSeconds() << ",";
	outFile << "\"trace\": [";
	std::string separator = "";
	for(auto it: m_trace) {
		outFile << separator << "[";
		outFile << it.first.GetSeconds() << "," << it.second;
		outFile << "]";
		separator = ",";
	}
	outFile << "]";
	outFile << "}";
}
double HttpTrace::GetDownloadSpeed() const {
	return m_speed;
}

} /* namespace ns3 */
