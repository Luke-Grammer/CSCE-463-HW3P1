// SenderSocket.cpp
// CSCE 463-500
// Luke Grammer
// 10/22/19

#include "pch.h"

using namespace std;

/* Constructor initializes WinSock and sets up a UDP socket for RDP. 
 * In addition, the server also starts a timer for the life of the 
 * SenderSocket object. calls exit() if socket creation is unsuccessful. */
SenderSocket::SenderSocket()
{
	STRUCT WSAData wsaData;
	WORD wVerRequested;

	//initialize WinSock
	wVerRequested = MAKEWORD(2, 2);
	if (WSAStartup(wVerRequested, &wsaData) != 0) {
		printf("\tWSAStartup error %d\n", WSAGetLastError());
		WSACleanup();
		exit(EXIT_FAILURE);
	}

	// open a UDP socket
	sock = socket(AF_INET, SOCK_DGRAM, NULL);
	if (sock == INVALID_SOCKET)
	{
		printf("\tsocket() generated error %d\n", WSAGetLastError());
		WSACleanup();
		exit(EXIT_FAILURE);
	}

	// Bind socket to local machine
	struct sockaddr_in local;
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = INADDR_ANY;
	local.sin_port = NULL;

	if (bind(sock, (struct sockaddr*) &local, sizeof(local)) == SOCKET_ERROR)
	{
		printf("bind() generated error %d\n", WSAGetLastError());
		WSACleanup();
		exit(EXIT_FAILURE);
	}

	// Set up address for local DNS server
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;

	totalTime = chrono::high_resolution_clock::now();
}

/* Basic destructor for SenderSocket cleans up socket and WinSock. */
SenderSocket::~SenderSocket()
{
	closesocket(sock);
	WSACleanup();
}

/* GetServerInfo does a forward lookup on the destination host string if necessary
 * and populates it's internal server information with the result. Called in Open().
 * Returns code 0 to indicate success or 3 if the target hostname does not have an 
 * entry in DNS. */
WORD SenderSocket::GetServerInfo(CONST CHAR* destination, WORD port)
{
	STRUCT hostent* remote;
	DWORD destinationIP = inet_addr(destination);

	// host is a valid IP, do not do a DNS lookup
	if (destinationIP != INADDR_NONE)
		server.sin_addr.S_un.S_addr = destinationIP;
	else
	{
		if ((remote = gethostbyname(destination)) == NULL)
		{
			// failure in gethostbyname
			stopTime = std::chrono::high_resolution_clock::now();
			printf("[%2.3f] --> ", chrono::duration_cast<chrono::milliseconds>(stopTime - totalTime).count() / 1000.0);
			printf("target %s is invalid\n", destination);
			return INVALID_NAME;
		}
		// take the first IP address and copy into sin_addr
		else
		{
			destinationIP = *(u_long*)remote->h_addr;
			memcpy((char*) & (server.sin_addr), remote->h_addr, remote->h_length);
		}
	}
	server.sin_port = htons(port);
	serverAddr.s_addr = destinationIP;

	return STATUS_OK;
}

/* Open() calls GetServerInfo() to populate internal server information and then creates a handshake
 * packet and attempts to send it to the corresponding server. If un-acknowledged, it will retransmit
 * this packet up to MAX_SYN_ATTEMPS times (by default 3). Open() will set the retransmission
 * timeout for future communication with the server to a constant scale of the handshake RTT.
 * Returns 0 to indicate success or a positive number to indicate failure. */
WORD SenderSocket::Open(CONST CHAR* destination, WORD port, DWORD senderWindow, STRUCT LinkProperties* lp)
{
	startTime = chrono::high_resolution_clock::now();
	WORD result = -1;
	
	if (connected)
		return ALREADY_CONNECTED;

	if ((result = GetServerInfo(destination, port)) != STATUS_OK)
		return result;

	// create handshake packet
	SenderSynHeader handshake;
	handshake.sdh.flags.SYN = 1;
	handshake.sdh.seq = sequenceNumber;
	handshake.lp = *lp;
	handshake.lp.bufferSize = senderWindow + MAX_DATA_ATTEMPTS;

	// attempt to send SYN and receive ACK MAX_SYN_ATTEMPTS times
	CHAR* buf = new CHAR[MAX_PKT_SIZE];
	if ((result = TrySendN((CHAR*)& handshake, (INT) sizeof(handshake), buf, MAX_SYN_ATTEMPTS, true)) == STATUS_OK)
	{
		RTO = 3 * (DWORD)chrono::duration_cast<chrono::milliseconds>(stopTime - startTime).count();
		printf("SYN-ACK %d window %d; setting initial RTO to %.3f\n", ((ReceiverHeader*)buf)->ackSeq, ((ReceiverHeader*)buf)->recvWnd, (RTO / 1000.0));
		connected = true;
	}

	delete[] buf;
	return result;
}

/* Attempts to send a SYN or FIN message to the connected server and receive the 
 * corresponding ACK. If un-acknowledged, it will retransmit this packet up to 
 * 'attempts' times. Returns 0 to indicate success or a positive number to indicate 
 * failure. */
WORD SenderSocket::TrySendN(CONST CHAR* message, INT messageSize, CHAR* response, WORD attempts, BOOLEAN syn)
{
	WORD result = -1;

	// Attempt to communicate with server N times
	for (USHORT i = 1; i <= attempts; i++)
	{
		// ************ SEND MESSAGE ************ //
		if ((result = InternalSend(message, messageSize)) != STATUS_OK)
			return result;

		stopTime = std::chrono::high_resolution_clock::now();
		printf("[%2.3f] --> ", chrono::duration_cast<chrono::milliseconds>(stopTime - totalTime).count() / 1000.0);

		if (syn)
			printf("SYN %d (attempt %d of %d, RTO % .3f) to % s\n", ((SenderDataHeader*)message)->seq, i, attempts, (RTO / 1000.0), inet_ntoa(serverAddr));
		else
			printf("FIN %d (attempt %d of %d, RTO % .3f)\n", ((SenderDataHeader*)message)->seq, i, attempts, (RTO / 1000.0));


		startTime = std::chrono::high_resolution_clock::now();

		// ********** RECEIVE RESPONSE ********** //
		if ((result = ReceiveACK(response)) != TIMEOUT)
		{
			if (result == STATUS_OK)
			{
				ReceiverHeader responseHeader = *(ReceiverHeader*)response;
				stopTime = std::chrono::high_resolution_clock::now();
				printf("[%2.3f] <-- ", chrono::duration_cast<chrono::milliseconds>(stopTime - totalTime).count() / 1000.0);
				if (responseHeader.flags.ACK && responseHeader.ackSeq == ((SenderDataHeader*)message)->seq)
					return STATUS_OK;
				else
				{
					if (!responseHeader.flags.ACK)
					{
						printf("ACK expected, not received\n");
						return MSG_NOT_ACK;
					}
					else
					{
						printf("unexpected sequence number returned\n");
						return INCORRECT_SEQ;
					}
				}
			}
			else
				return result;
		}
	}

	// all attempts timed out, return
	return TIMEOUT;
}

/* Attempts to send a single packet to the connected server. Disregards connectivity
 * check in order to send SYN messages to new servers. Returns 0 to indicate success 
 * or a positive number for failure. */
WORD SenderSocket::InternalSend(CONST CHAR* message, INT messageSize)
{
	WORD result = -1;

	// attempt to send a single packet to server
	result = sendto(sock, message, messageSize, NULL, (STRUCT sockaddr*) & server, sizeof(server));
	if (result == SOCKET_ERROR)
	{
		printf("[%2.3f] --> ", chrono::duration_cast<chrono::milliseconds>(stopTime - totalTime).count() / 1000.0);
		printf("failed sendto with %d\n", WSAGetLastError());
		return FAILED_SEND;
	}

	return STATUS_OK;
}

/* Attempts to send a single packet to the connected server. This is the externally facing 
 * Send() function and therefore requires a previously successful call to Open(). 
 * Returns 0 to indicate success or a positive number for failure. */
WORD SenderSocket::Send(CONST CHAR* message, INT messageSize)
{
	// send single packet to server with connectivity check
	if (!connected)
		return NOT_CONNECTED;

	return InternalSend(message, messageSize);
}

/* Attempts to receive a single acknowledgement packet from the connected server. 
 * Uses the current RTO and store the acknowledgement the 'response' buffer.
 * It is assumed that this buffer has already been allocated and is capacity 
 * of at least MAX_PKT_SIZE bytes. Returns 0 to indicate success or a positive 
 * number for failure. */
WORD SenderSocket::ReceiveACK(CHAR* response)
{
	WORD result = -1;

	// set timeout
	STRUCT timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = RTO * 1000;

	fd_set fd;
	FD_ZERO(&fd);
	FD_SET(sock, &fd);

	// create address struct for responder
	STRUCT sockaddr_in response_addr;
	INT response_size = sizeof(response_addr);

	result = select(0, &fd, NULL, NULL, &timeout);
	if (result > 0)
	{
		// attempt to get response from server
		result = recvfrom(sock, response, MAX_PKT_SIZE, NULL, (STRUCT sockaddr*) & response_addr, &response_size);
		if (result == SOCKET_ERROR)
		{
			printf("[%2.3f] <-- ", chrono::duration_cast<chrono::milliseconds>(stopTime - totalTime).count() / 1000.0);
			printf("failed recvfrom with %d\n", WSAGetLastError());
			return FAILED_RECV;
		}

		return STATUS_OK;
	}
	else if (result != 0)
	{
		printf("[%2.3f] <-- ", chrono::duration_cast<chrono::milliseconds>(stopTime - totalTime).count() / 1000.0);
		printf("failed select with %d\n", WSAGetLastError());
		return FAILED_RECV;
	}

	return TIMEOUT;
}

/* Closes connection to the current server. Sends a connection termination packet and waits 
 * for an acknowledgement using the RTO calculated in the call to Open(). Returns 0 to 
 * indicate success or a positive number for failure.*/
WORD SenderSocket::Close()
{
	startTime = chrono::high_resolution_clock::now();
	WORD result = -1;

	if (!connected)
		return NOT_CONNECTED;

	// create connection termination packet
	SenderDataHeader termination;
	termination.flags.FIN = 1;
	termination.seq = sequenceNumber;

	CHAR* buf = new CHAR[MAX_PKT_SIZE];
	// attempt to send SYN and receive ACK MAX_DATA_ATTEMPTS times
	if ((result = TrySendN((CHAR*)& termination, (INT) sizeof(termination), buf, MAX_DATA_ATTEMPTS, false)) == STATUS_OK)
	{
		printf("FIN-ACK %d window %d\n", ((ReceiverHeader*)buf)->ackSeq, ((ReceiverHeader*)buf)->recvWnd);
		connected = false;
	}

	delete[] buf;
	return result;
}