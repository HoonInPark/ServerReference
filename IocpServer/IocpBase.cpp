#include "stdafx.h"
#include "IocpBase.h"
#include <process.h>
#include <sstream>
#include <algorithm>
#include <string>

IocpBase::IocpBase()
{
	// 멤버 변수 초기화
	bWorkerThread = true;   // WorkerThread while 반복
	bAccept = true;			// MainThread while 반복
}


IocpBase::~IocpBase()
{
	// winsock 의 사용을 끝낸다
	WSACleanup();
	// 다 사용한 객체를 삭제
	if (SocketInfo)
	{
		delete[] SocketInfo;
		SocketInfo = NULL;
	}

	if (hWorkerHandle)
	{
		delete[] hWorkerHandle;
		hWorkerHandle = NULL;
	}
}

/* C/C++에서 winsock을 사용하기 위한 표준적인 절차
* C#보다 아무래도 더 많은 코드가 필요하다.
* 누구나 winsock을 사용할 때는 아래처럼 초기화한다.
*/
bool IocpBase::Initialize()
{
	/* 초창기 온라인 게임서버는 Unix 서버 만들던 사람들이 개발했었다. 
	* 그러나 2000년대 초반에 WinSock 2.2버전이 나왔다.
	* 이 때 Unix/Linux보다 성능이 훨씬 좋았으므로 거의 대부분의 게임 서버는 이때
	* Windows로 개발되게 되었다.
	* (비동기소켓, EventSelect, Overlapped I/O, IOCP)
	* WinSock 2.2 이후는 나오지 않았다.
	* 이후로 Linux도 성능이 개선된 소켓 모델이 나왔고
	* 지금은 5:5나 6(Windows):4(Linux) 정도 비율로 게임 서버가 운영된다.
	*/
	WSADATA wsaData;
	int nResult;
	// winsock 2.2 버전으로 초기화
	nResult = WSAStartup(MAKEWORD(2, 2), &wsaData);

	if (nResult != 0)
	{
		printf_s("[ERROR] winsock 초기화 실패\n");
		return false;
	}

	/* Overlapped I/O가 가능한 비동기 소켓 생성
		AF_INET : Ipv4
		SOCK_STREAM : TCP
		WSA_FLAG_OVERLAPPED : 중첩 I/O 통신
	*/
	ListenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	if (ListenSocket == INVALID_SOCKET)
	{
		printf_s("[ERROR] 소켓 생성 실패\n");
		return false;
	}

	// C#의 IPEndPoint에 주소 설정하는 것과 같다.
	// 서버 정보 설정
	SOCKADDR_IN serverAddr;
	serverAddr.sin_family = PF_INET;			// IPv4
	serverAddr.sin_port = htons(SERVER_PORT);	// 포트번호 8000
	serverAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY); // 이 프로세스가 동작하는 서버의 IP

	// 소켓 설정
	// boost bind 와 구별짓기 위해 ::bind 사용
	// 소켓에 주소를 부여
	nResult = ::bind(ListenSocket, (struct sockaddr*)&serverAddr, sizeof(SOCKADDR_IN));
	
	if (nResult == SOCKET_ERROR)
	{
		printf_s("[ERROR] bind 실패\n");
		closesocket(ListenSocket);
		WSACleanup();
		return false;
	}

	// 서버가 클라이언트의 접속을 받기 위해 하는 준비
	// 수신 대기열 생성
	// 동시에 클라이언트가 Connect시도를 했을 때 1개는 처리하고 1개는 잠시 대기하는 큐의 크기
	// SOMAXCONN : 최대치
	nResult = listen(ListenSocket, SOMAXCONN);
	if (nResult == SOCKET_ERROR)
	{
		printf_s("[ERROR] listen 실패\n");
		closesocket(ListenSocket);
		WSACleanup();
		return false;
	}

	return true;
}
/* IOCP : Window Socket모델중에 가장 성능이 뛰어나다고 알려진 모델
*        C/C++ 소켓 서버는 이 모델을 사용하는 경우가 대부분임.
* (Input Output Completion Port)
* 
* IOCP = 비동기 소켓 + Overlapped I/O + 스레드 풀
*/
void IocpBase::StartServer()
{
	int nResult;
	// 클라이언트 정보
	SOCKADDR_IN clientAddr;
	int addrLen = sizeof(SOCKADDR_IN);
	SOCKET clientSocket;
	DWORD recvBytes;
	DWORD flags;

	// Completion Port 객체 생성(OS의 통신 완료 큐의 정보를 꺼내기 위한 이벤트 신호기)
	hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

	/* CreateWorkerThread()는 IocpBase와 MainIocp클래스 모두 가지고 있다.
	* 그리고 virtual 함수이다.
	* 현재 StartServer()는 MainIocp객체이므로 
	* virtual의 특성상 클래스를 따라가지 않고 객체의 함수를 따라간다.
	* 그러므로 MainIocp::CreateWorkerThread()가 호출된다.
	*/
	// Worker Thread 생성
	/*완료된 통신데이터를 꺼내서 처리하는 스레드풀을 시작한다*/
	if (!CreateWorkerThread()) return;	

	/*IOCP통신이 완료되었을 때 사용할 2가지 준비
	1)hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	  - CP 핸들 (신호기) 생성
	2)CreateWorkerThread()
	  - 적정 갯수의 스레드를 다수 생성한다.
	  - hIOCP를 GetQueuedCompletionStatus(hIOCP, ...)로 OS와 연결된 완료큐에 패킷 정보가
	   들어올 때 까지 각 스레드들은 대기 상태에 들어가 있다.
	*/

	printf_s("[INFO] 서버 시작...\n");

	// 클라이언트 접속을 받음
	// IocpBase의 생성자에서 bAccept=true를 처리했으므로
	// 무한루프를 반복하면서 클라이언트의 접속처리를 한다.
	// Main 스레드는 여기서 리턴되지 않고 계속 반복하게 된다.
	while (bAccept)
	{
		/*클라이언트 연결시 Kernel에는 클라이언투와 연결된 Socket 오브젝트(구조체)가 생성되고
		Application에는 clientSocket에 핸들정보를 담아서 리턴한다.
		클라이언트의 주소는 clientAddr구조체에 저장된다.
		*/
		clientSocket = WSAAccept(
			ListenSocket, (struct sockaddr *)&clientAddr, &addrLen, NULL, NULL
		);

		if (clientSocket == INVALID_SOCKET)
		{
			printf_s("[ERROR] Accept 실패\n");
			return;
		}

		// 클라이언트가 접속할 때 마다 화면에 접속 로그 출력
		char ipbuf[30];
		memset(ipbuf, 0, sizeof(ipbuf));
		strncpy_s(ipbuf, inet_ntoa(clientAddr.sin_addr), sizeof(ipbuf));
		printf_s("Connected Client : %lld, %s, %hu\n", 
					clientSocket, ipbuf, clientAddr.sin_port);

		/*정상적으로 클라이언트와 연결되었으므로
		클라이언트가 보내는 데이터를 IOCP방식으로 수신하기 위해
		수신되었을 때 해당 클라이언트와 관련된 정보를 저장하는 구조체와
		해당 구조체를 O/S에 hIOCP와 함께 등록한다.

		이렇게 등록해놓으면 WorkerThread의 GetQueuedCompletionStatus()를 통해서
		완료된 수신 정보에 등록한 구조체 정보를 얻을 수 있다.
		이 구조체를 통해서 어떤 클라이언트의 데이터가 수신되었는지 판단할 수 있다.
		*/
		SocketInfo = new stSOCKETINFO();
		SocketInfo->socket = clientSocket;
		SocketInfo->recvBytes = 0;
		SocketInfo->sendBytes = 0;
		SocketInfo->dataBuf.len = MAX_BUFFER;
		SocketInfo->dataBuf.buf = SocketInfo->messageBuffer;
		flags = 0;

		/* 
		여기서 CreateIoCompletionPort는 위에서 호출한 것과 다른 의미로 사용된다.
		첫번째 CreateIoCompletionPort의 호출은 hIOCP 신호기 핸들 생성
		현재 CreateIoCompletionPort의 호출은 연결소켓 - hIOCP - SocketInfo의 등록

		[O/S에 연결된 클라이언트의 IOCP통신을 등록]
		hIOCP 신호기를 통해 clientSocket의 통신을 IOCP를 사용하고
		데이터 완료시 IOCP큐를 통해 데이터를 수신한다.
		또한 함께 현재 넘겨주는 SocketInfo도 함께 받는다.*/

		hIOCP = CreateIoCompletionPort(
			(HANDLE)clientSocket, hIOCP, (DWORD)SocketInfo, 0
		);

		/*비동기 수신이기 때문에 거의 WSA_IO_PENDING이 일어남
		 물론 간혹 호출하는 중에 수신이 되는 경우도 있을 수 있다.
		*/
		// 중첩 소켓을 지정하고 완료시 실행될 함수를 넘겨줌
		nResult = WSARecv(
			SocketInfo->socket,
			&SocketInfo->dataBuf,
			1,
			&recvBytes,
			&flags,
			&(SocketInfo->overlapped),
			NULL
		);

		/*WSAGetLastError()의 2가지 용도
		1) 현재 App에 발생한 최신 소켓 상태 정보 확인
		2) 현재 App에 발생한 가장 최근의 에러 정보 확인
		*/

		/*if (nResult != SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
		여기서 SOCKET_ERROR가 아니고 WSA_IO_PENDING이 아닐 경우는
		 비동기 수신이지만 함수 호출하는 중에 데이터가 수신된 경우이므로
		 이럴 때는 에러가 아니다.
		*/
		if (nResult == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
		{
			printf_s("[ERROR] IO Pending 실패 : %d", WSAGetLastError());
			return;
		}
	}

}

bool IocpBase::CreateWorkerThread()
{
	return false;
}

void IocpBase::Send(stSOCKETINFO * pSocket)
{
	int nResult;
	DWORD	sendBytes;
	DWORD	dwFlags = 0;

	nResult = WSASend(
		pSocket->socket,
		&(pSocket->dataBuf),
		1,
		&sendBytes,
		dwFlags,
		NULL,
		NULL
	);

	if (nResult == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
	{
		printf_s("[ERROR] WSASend 실패 : ", WSAGetLastError());
	}


}

void IocpBase::Recv(stSOCKETINFO * pSocket)
{
	int nResult;
	// DWORD	sendBytes;
	DWORD	dwFlags = 0;

	// stSOCKETINFO 데이터 초기화
	ZeroMemory(&(pSocket->overlapped), sizeof(OVERLAPPED));
	ZeroMemory(pSocket->messageBuffer, MAX_BUFFER);
	pSocket->dataBuf.len = MAX_BUFFER;
	pSocket->dataBuf.buf = pSocket->messageBuffer;
	pSocket->recvBytes = 0;
	pSocket->sendBytes = 0;

	dwFlags = 0;

	// 클라이언트로부터 다시 응답을 받기 위해 WSARecv 를 호출해줌
	nResult = WSARecv(
		pSocket->socket,
		&(pSocket->dataBuf),
		1,
		(LPDWORD)&pSocket,
		&dwFlags,
		(LPWSAOVERLAPPED)&(pSocket->overlapped),
		NULL
	);

	if (nResult == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
	{
		printf_s("[ERROR] WSARecv 실패 : ", WSAGetLastError());
	}
}

void IocpBase::WorkerThread()
{
	
}
