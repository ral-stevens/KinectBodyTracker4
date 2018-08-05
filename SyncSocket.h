#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdint>

#define SYNC_SOCKET_PORT     3464
#define PACKET_LENGTH_TIME   16

#pragma comment(lib, "Ws2_32.lib")

typedef INT64 OdroidTimestamp;

struct SportSolePacket
{
	uint8_t val;
	uint8_t Odroid_Timestamp[8];
	uint8_t Odroid_Trigger;
};

class SyncSocket
{
private:
	SOCKET             m_socketListen;
	WSAEVENT           m_hEventRecv;
	int                m_nPacketCount;
	int                m_nErrorCount;
	bool               m_bWs2Loaded; // indicates whether WSACleanup() is needed on exit
	char               m_pBuffer[PACKET_LENGTH_TIME];
public:
	INT64              m_tsWindows;
	OdroidTimestamp    m_tsOdroid;
	
public:
	SyncSocket();
	~SyncSocket();
	bool init();
	OdroidTimestamp receive(INT64 tsWindows, SportSolePacket * pPacket = NULL);
protected:
	bool checkSportSolePacket(uint8_t * buffer);
	void reconstructStructSportSolePacket(uint8_t * recvbuffer, SportSolePacket & dataPacket);
	void releaseResource();
};