// iocp-client.cpp: 콘솔 응용 프로그램의 진입점을 정의합니다.
//

#include "stdafx.h"
// winsock2 사용을 위해 아래 코멘트 추가
#pragma comment(lib, "ws2_32.lib")
// 멀티바이트 집합 사용시 define
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <WinSock2.h>
#include <iostream>
#include <sstream>
#include <map>
#include "CommonClass.h"

using namespace std;

#define	MAX_BUFFER		4096
#define SERVER_PORT		8000
#define SERVER_IP		"127.0.0.1"
#define MAX_CLIENTS		100

struct stSOCKETINFO
{
	WSAOVERLAPPED	overlapped;
	WSABUF			dataBuf;
	SOCKET			socket;
	char			messageBuffer[MAX_BUFFER];
	int				recvBytes;
	int				sendBytes;
};

int main()
{
	WSADATA wsaData;
	// 윈속 버전을 2.2로 초기화
	int nRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (nRet != 0) {
		std::cout << "Error : " << WSAGetLastError() << std::endl;
		return false;
	}

	// TCP 소켓 생성
	SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (clientSocket == INVALID_SOCKET) {
		std::cout << "Error : " << WSAGetLastError() << std::endl;
		return false;
	}

	std::cout << "socket initialize success." << std::endl;

	// 접속할 서버 정보를 저장할 구조체
	SOCKADDR_IN stServerAddr;

	char	szOutMsg[MAX_BUFFER];
	char	sz_socketbuf_[MAX_BUFFER];
	stServerAddr.sin_family = AF_INET;
	// 접속할 서버 포트 및 IP
	stServerAddr.sin_port = htons(SERVER_PORT);
	stServerAddr.sin_addr.s_addr = inet_addr(SERVER_IP);

	nRet = connect(clientSocket, (sockaddr*)&stServerAddr, sizeof(sockaddr));
	if (nRet == SOCKET_ERROR) {
		std::cout << "Error : " << WSAGetLastError() << std::endl;
		return false;
	}

	std::cout << "Connection success..." << std::endl;

	stringstream SendStream;
	SendStream << EPacketType::SIGNUP << endl;
	SendStream << "tes2t" << endl;
	SendStream << "test" << endl;

	int nSend = send(clientSocket, (CHAR*)SendStream.str().c_str(), SendStream.str().length(), 0);

	int nRecv = recv(clientSocket, sz_socketbuf_, MAX_BUFFER, 0);

	stringstream RecvStream;
	bool result;
	RecvStream << sz_socketbuf_;
	RecvStream >> result;
	printf_s("%d\n", result);

	closesocket(clientSocket);
	WSACleanup();
	std::cout << "Client has been terminated..." << std::endl;

    return 0;
}

