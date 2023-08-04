#pragma once

// 멀티바이트 집합 사용시 define
#define _WINSOCK_DEPRECATED_NO_WARNINGS

// winsock2 사용을 위해 아래 코멘트 추가
#pragma comment(lib, "ws2_32.lib")
#include <WinSock2.h>
#include <map>
#include <vector>
#include <iostream>
#include "CommonClass.h"

using namespace std;

#define	MAX_BUFFER		4096
#define SERVER_PORT		8000
#define MAX_CLIENTS		100

// IOCP 소켓 구조체
/*일부러 구조체의 첫번째 멤버변수로 WSAOVERLAPPED	overlapped;를 준것이다.
구조체 전체의 시작주소와 첫번째 멤버변수의 시작주소는 같다.
WSARecv의 WSAOVERLAPPED	overlapped;를 등록해야 하는 곳에 SocketInfo->overlapped로
등록하면 데이터 수신시 overlapped정보가 함께 넘어오는데 이 주소를 통해
우리는 역으로 stSOCKETINFO 전체 정보를 얻을 수 있게 된다.
*/
struct stSOCKETINFO
{
	WSAOVERLAPPED	overlapped;
	WSABUF			dataBuf;
	SOCKET			socket;
	char			messageBuffer[MAX_BUFFER];
	int				recvBytes;
	int				sendBytes;
};

// 패킷 처리 함수 포인터
struct FuncProcess
{
	/*함수포인터 선언
	리턴형은 void, 매개변수는 2개 (stringstream & RecvStream, stSOCKETINFO * pSocket)
	이렇게 생긴 함수는 funcProcessPacket에 저장할 수 있다.
	*/
	void(*funcProcessPacket)(stringstream & RecvStream, stSOCKETINFO * pSocket);
	FuncProcess()
	{
		funcProcessPacket = nullptr;
	}
};

class IocpBase
{
public:
	IocpBase();
	virtual ~IocpBase();

	// 소켓 등록 및 서버 정보 설정
	bool Initialize();
	// 서버 시작
	virtual void StartServer();
	// 작업 스레드 생성
	virtual bool CreateWorkerThread();	
	// 작업 스레드
	virtual void WorkerThread();
	// 클라이언트에게 송신
	virtual void Send(stSOCKETINFO * pSocket);
	// 클라이언트 수신 대기
	virtual void Recv(stSOCKETINFO * pSocket);		

protected:
	stSOCKETINFO * SocketInfo;		// 소켓 정보
	SOCKET			ListenSocket;	// 서버 리슨 소켓
	HANDLE			hIOCP;			// IOCP 객체 핸들
	bool			bAccept;		// 요청 동작 플래그
	bool			bWorkerThread;	// 작업 스레드 동작 플래그
	HANDLE *		hWorkerHandle;	// 작업 스레드 핸들		
	int				nThreadCnt;	
};
