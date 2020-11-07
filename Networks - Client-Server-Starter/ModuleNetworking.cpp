#include "Networks.h"
#include "ModuleNetworking.h"
#include <list>

static uint8 NumModulesUsingWinsock = 0;



void ModuleNetworking::reportError(const char* inOperationDesc)
{
	LPVOID lpMsgBuf;
	DWORD errorNum = WSAGetLastError();

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		errorNum,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf,
		0, NULL);

	ELOG("Error %s: %d- %s", inOperationDesc, errorNum, lpMsgBuf);
}

void ModuleNetworking::disconnect()
{
	for (SOCKET socket : sockets)
	{
		shutdown(socket, 2);
		closesocket(socket);
	}

	sockets.clear();
}

bool ModuleNetworking::init()
{
	if (NumModulesUsingWinsock == 0)
	{
		NumModulesUsingWinsock++;

		WORD version = MAKEWORD(2, 2);
		WSADATA data;
		if (WSAStartup(version, &data) != 0)
		{
			reportError("ModuleNetworking::init() - WSAStartup");
			return false;
		}
	}

	return true;
}

bool ModuleNetworking::preUpdate()
{
	if (sockets.empty()) return true;

	// NOTE(jesus): You can use this temporary buffer to store data from recv()
	const uint32 incomingDataBufferSize = Kilobytes(1);
	byte incomingDataBuffer[incomingDataBufferSize];

	// TODO(jesus): select those sockets that have a read operation available
	std::list<SOCKET> offline_sockets;

	// New socket set
	fd_set readfds;
	FD_ZERO(&readfds);

	// Fill the set
	for (auto s : sockets) {
		FD_SET(s, &readfds);
	}

	// Timeout (return immediately)
	TIMEVAL timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	// Select (check for readability)
	int val = select(0, &readfds, nullptr, nullptr, &timeout);
	if (val == SOCKET_ERROR) {
		ELOG("Error selecting sockets");
		return false;
	}
	// TODO(jesus): for those sockets selected, check wheter or not they are
	// a listen socket or a standard socket and perform the corresponding
	// operation (accept() an incoming connection or recv() incoming data,
	// respectively).
	for (int i = 0; i < readfds.fd_count; i++)
	{
		SOCKET curr = readfds.fd_array[i];

		// On accept() success, communicate the new connected socket to the
		// subclass (use the callback onSocketConnected()), and add the new
		// connected socket to the managed list of sockets.
		if (isListenSocket(curr))
		{
			sockaddr_in bindAddr;
			int fromlen = sizeof(bindAddr);

			SOCKET client = accept(curr, (sockaddr*)&bindAddr, &fromlen);

			if (client == INVALID_SOCKET)
			{
				reportError("Socket error: %ls");

				continue;
			}

			onSocketConnected(client, bindAddr);
			sockets.push_back(client);

		}
	

	// On recv() success, communicate the incoming data received to the
	// subclass (use the callback onSocketReceivedData()).
		else { // Is a client socket
				// Recv stuff
			val = recv(curr, (char*)incomingDataBuffer, incomingDataBufferSize, 0);

			if (val == SOCKET_ERROR || val == 0) {
				reportError("Disconnection");
				offline_sockets.push_back(curr);
			}
			else
			{
				onSocketReceivedData(curr, incomingDataBuffer);
			}
		}
	}
	// TODO(jesus): handle disconnections. Remember that a socket has been
	// disconnected from its remote end either when recv() returned 0,
	// or when it generated some errors such as ECONNRESET.
	// Communicate detected disconnections to the subclass using the callback
	// onSocketDisconnected().

	// TODO(jesus): Finally, remove all disconnected sockets from the list
	// of managed sockets.
	for (auto curr : offline_sockets)
	{
		onSocketDisconnected(curr);
		std::vector<SOCKET>::iterator itr = std::find(sockets.begin(), sockets.end(), curr);
		sockets.erase(itr);
	}

	return true;
}

bool ModuleNetworking::cleanUp()
{
	disconnect();

	NumModulesUsingWinsock--;
	if (NumModulesUsingWinsock == 0)
	{

		if (WSACleanup() != 0)
		{
			reportError("ModuleNetworking::cleanUp() - WSACleanup");
			return false;
		}
	}

	return true;
}

void ModuleNetworking::addSocket(SOCKET socket)
{
	sockets.push_back(socket);
}
