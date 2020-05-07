// Driver.cpp
// CSCE 463-500
// Luke Grammer
// 10/22/19

#include "pch.h"

#define _CRTDBG_MAP_ALLOC  
#include <stdlib.h>  
#include <crtdbg.h> // libraries to check for memory leaks

#pragma comment(lib, "ws2_32.lib")

using namespace std;

int main(INT argc, CHAR** argv)
{
	// debug flag to check for memory leaks
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF); 

	// ************* VALIDATE ARGUMENTS ************** //
	if (argc != 8)
	{
		(argc < 8) ? printf("too few arguments") : printf("too many arguments");
		printf("\nusage: Driver.exe <destination server> <buffer size> <sender window size> <RTT propogation delay> <loss probability (forward)> <loss probability (return)> <bottleneck link speed>\n");
		return INVALID_ARGUMENTS;
	}

	// ************ INITIALIZE VARIABLES ************* //
	CHAR* destination     = argv[1];
	DWORD dwordBufSize   = (DWORD) 1 << atoi(argv[2]); // Can't use UINT64 because allocation with new uses unsigned 32 bit int
	DWORD senderWindow    = atoi(argv[3]);
	FLOAT RTT             = (FLOAT) atof(argv[4]);
	FLOAT fLossProb       = (FLOAT) atof(argv[5]);
	FLOAT rLossProb       = (FLOAT) atof(argv[6]);
	DWORD bottleneckSpeed = atoi(argv[7]);

	FLOAT lossProb[2] = { fLossProb, rLossProb };
	chrono::time_point<chrono::high_resolution_clock> startTime, stopTime;

	printf("Main:   sender W = %d, RTT = %.3f sec, loss %g / %g, link %d Mbps\n" , senderWindow, RTT, lossProb[0], lossProb[1], bottleneckSpeed);
	printf("Main:   initializing DWORD array with 2^%d elements... ", atoi(argv[2]));
	startTime = chrono::high_resolution_clock::now();

	DWORD* dwordBuf = new DWORD[dwordBufSize];
	for (DWORD i = 0; i < dwordBufSize; i++)
		dwordBuf[i] = i;

	stopTime = chrono::high_resolution_clock::now();
	printf("done in %lld ms\n",
		chrono::duration_cast<chrono::milliseconds>
		(stopTime - startTime).count());

	SenderSocket ss;
	INT status = -1;

	STRUCT LinkProperties lp;
	lp.RTT = RTT;
	lp.speed = (FLOAT) bottleneckSpeed * 1000000;
	lp.pLoss[0] = lossProb[0];
	lp.pLoss[1] = lossProb[1];

	// ********** OPEN CONNECTION TO SERVER ********** //
	startTime = chrono::high_resolution_clock::now();
	if ((status = ss.Open(destination, MAGIC_PORT, senderWindow, &lp)) != STATUS_OK)
	{
		printf("Main:   connect failed with status %d\n", status);
		delete[] dwordBuf;
		return status;
	}
	stopTime = chrono::high_resolution_clock::now();

	printf("Main:   connected to %s in %0.3f sec, pkt size %d bytes\n", destination, 
		chrono::duration_cast<chrono::milliseconds>
		(stopTime - startTime).count() / 1000.0, MAX_PKT_SIZE);
	
	startTime = chrono::high_resolution_clock::now();
	/*/ ************* SEND DATA TO SERVER ************* //
	UINT64 charBufSize = dwordBufSize << 2;
	CHAR* charBuf = (CHAR*)dwordBuf;
	UINT64 offset = 0;
	
	while (offset < charBufSize)
	{
		// decide the size of the next chunk
		UINT64 bytes = min(charBufSize - offset, (MAX_PKT_SIZE - sizeof(SenderDataHeader)));
		// send chunk into socket
		if ((status = ss.Send(charBuf + offset, bytes)) != STATUS_OK)
		{
			printf("Main:   connect failed with status %d\n", status);
			return status;
		}

		offset += bytes;
	}
	*/
	stopTime = chrono::high_resolution_clock::now();

	// ********** CLOSE CONNECTION TO SERVER ********* //
	if ((status = ss.Close()) != STATUS_OK)
	{
		printf("Main:   connect failed with status %d\n", status);
		delete[] dwordBuf;
		return status;
	}

	printf("Main:   transfer finished in %0.3f sec\n",
		chrono::duration_cast<chrono::milliseconds>
		(stopTime - startTime).count() / 1000.0);

	delete[] dwordBuf;
	return 0;
}
