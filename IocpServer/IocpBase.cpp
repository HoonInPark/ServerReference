#include "stdafx.h"
#include "IocpBase.h"
#include <process.h>
#include <sstream>
#include <algorithm>
#include <string>

IocpBase::IocpBase()
{
	// ��� ���� �ʱ�ȭ
	bWorkerThread = true;   // WorkerThread while �ݺ�
	bAccept = true;			// MainThread while �ݺ�
}


IocpBase::~IocpBase()
{
	// winsock �� ����� ������
	WSACleanup();
	// �� ����� ��ü�� ����
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

/* C/C++���� winsock�� ����ϱ� ���� ǥ������ ����
* C#���� �ƹ����� �� ���� �ڵ尡 �ʿ��ϴ�.
* ������ winsock�� ����� ���� �Ʒ�ó�� �ʱ�ȭ�Ѵ�.
*/
bool IocpBase::Initialize()
{
	/* ��â�� �¶��� ���Ӽ����� Unix ���� ����� ������� �����߾���. 
	* �׷��� 2000��� �ʹݿ� WinSock 2.2������ ���Դ�.
	* �� �� Unix/Linux���� ������ �ξ� �������Ƿ� ���� ��κ��� ���� ������ �̶�
	* Windows�� ���ߵǰ� �Ǿ���.
	* (�񵿱����, EventSelect, Overlapped I/O, IOCP)
	* WinSock 2.2 ���Ĵ� ������ �ʾҴ�.
	* ���ķ� Linux�� ������ ������ ���� ���� ���԰�
	* ������ 5:5�� 6(Windows):4(Linux) ���� ������ ���� ������ ��ȴ�.
	*/
	WSADATA wsaData;
	int nResult;
	// winsock 2.2 �������� �ʱ�ȭ
	nResult = WSAStartup(MAKEWORD(2, 2), &wsaData);

	if (nResult != 0)
	{
		printf_s("[ERROR] winsock �ʱ�ȭ ����\n");
		return false;
	}

	/* Overlapped I/O�� ������ �񵿱� ���� ����
		AF_INET : Ipv4
		SOCK_STREAM : TCP
		WSA_FLAG_OVERLAPPED : ��ø I/O ���
	*/
	ListenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	if (ListenSocket == INVALID_SOCKET)
	{
		printf_s("[ERROR] ���� ���� ����\n");
		return false;
	}

	// C#�� IPEndPoint�� �ּ� �����ϴ� �Ͱ� ����.
	// ���� ���� ����
	SOCKADDR_IN serverAddr;
	serverAddr.sin_family = PF_INET;			// IPv4
	serverAddr.sin_port = htons(SERVER_PORT);	// ��Ʈ��ȣ 8000
	serverAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY); // �� ���μ����� �����ϴ� ������ IP

	// ���� ����
	// boost bind �� �������� ���� ::bind ���
	// ���Ͽ� �ּҸ� �ο�
	nResult = ::bind(ListenSocket, (struct sockaddr*)&serverAddr, sizeof(SOCKADDR_IN));
	
	if (nResult == SOCKET_ERROR)
	{
		printf_s("[ERROR] bind ����\n");
		closesocket(ListenSocket);
		WSACleanup();
		return false;
	}

	// ������ Ŭ���̾�Ʈ�� ������ �ޱ� ���� �ϴ� �غ�
	// ���� ��⿭ ����
	// ���ÿ� Ŭ���̾�Ʈ�� Connect�õ��� ���� �� 1���� ó���ϰ� 1���� ��� ����ϴ� ť�� ũ��
	// SOMAXCONN : �ִ�ġ
	nResult = listen(ListenSocket, SOMAXCONN);
	if (nResult == SOCKET_ERROR)
	{
		printf_s("[ERROR] listen ����\n");
		closesocket(ListenSocket);
		WSACleanup();
		return false;
	}

	return true;
}
/* IOCP : Window Socket���߿� ���� ������ �پ�ٰ� �˷��� ��
*        C/C++ ���� ������ �� ���� ����ϴ� ��찡 ��κ���.
* (Input Output Completion Port)
* 
* IOCP = �񵿱� ���� + Overlapped I/O + ������ Ǯ
*/
void IocpBase::StartServer()
{
	int nResult;
	// Ŭ���̾�Ʈ ����
	SOCKADDR_IN clientAddr;
	int addrLen = sizeof(SOCKADDR_IN);
	SOCKET clientSocket;
	DWORD recvBytes;
	DWORD flags;

	// Completion Port ��ü ����(OS�� ��� �Ϸ� ť�� ������ ������ ���� �̺�Ʈ ��ȣ��)
	hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

	/* CreateWorkerThread()�� IocpBase�� MainIocpŬ���� ��� ������ �ִ�.
	* �׸��� virtual �Լ��̴�.
	* ���� StartServer()�� MainIocp��ü�̹Ƿ� 
	* virtual�� Ư���� Ŭ������ ������ �ʰ� ��ü�� �Լ��� ���󰣴�.
	* �׷��Ƿ� MainIocp::CreateWorkerThread()�� ȣ��ȴ�.
	*/
	// Worker Thread ����
	/*�Ϸ�� ��ŵ����͸� ������ ó���ϴ� ������Ǯ�� �����Ѵ�*/
	if (!CreateWorkerThread()) return;	

	/*IOCP����� �Ϸ�Ǿ��� �� ����� 2���� �غ�
	1)hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	  - CP �ڵ� (��ȣ��) ����
	2)CreateWorkerThread()
	  - ���� ������ �����带 �ټ� �����Ѵ�.
	  - hIOCP�� GetQueuedCompletionStatus(hIOCP, ...)�� OS�� ����� �Ϸ�ť�� ��Ŷ ������
	   ���� �� ���� �� ��������� ��� ���¿� �� �ִ�.
	*/

	printf_s("[INFO] ���� ����...\n");

	// Ŭ���̾�Ʈ ������ ����
	// IocpBase�� �����ڿ��� bAccept=true�� ó�������Ƿ�
	// ���ѷ����� �ݺ��ϸ鼭 Ŭ���̾�Ʈ�� ����ó���� �Ѵ�.
	// Main ������� ���⼭ ���ϵ��� �ʰ� ��� �ݺ��ϰ� �ȴ�.
	while (bAccept)
	{
		/*Ŭ���̾�Ʈ ����� Kernel���� Ŭ���̾����� ����� Socket ������Ʈ(����ü)�� �����ǰ�
		Application���� clientSocket�� �ڵ������� ��Ƽ� �����Ѵ�.
		Ŭ���̾�Ʈ�� �ּҴ� clientAddr����ü�� ����ȴ�.
		*/
		clientSocket = WSAAccept(
			ListenSocket, (struct sockaddr *)&clientAddr, &addrLen, NULL, NULL
		);

		if (clientSocket == INVALID_SOCKET)
		{
			printf_s("[ERROR] Accept ����\n");
			return;
		}

		// Ŭ���̾�Ʈ�� ������ �� ���� ȭ�鿡 ���� �α� ���
		char ipbuf[30];
		memset(ipbuf, 0, sizeof(ipbuf));
		strncpy_s(ipbuf, inet_ntoa(clientAddr.sin_addr), sizeof(ipbuf));
		printf_s("Connected Client : %lld, %s, %hu\n", 
					clientSocket, ipbuf, clientAddr.sin_port);

		/*���������� Ŭ���̾�Ʈ�� ����Ǿ����Ƿ�
		Ŭ���̾�Ʈ�� ������ �����͸� IOCP������� �����ϱ� ����
		���ŵǾ��� �� �ش� Ŭ���̾�Ʈ�� ���õ� ������ �����ϴ� ����ü��
		�ش� ����ü�� O/S�� hIOCP�� �Բ� ����Ѵ�.

		�̷��� ����س����� WorkerThread�� GetQueuedCompletionStatus()�� ���ؼ�
		�Ϸ�� ���� ������ ����� ����ü ������ ���� �� �ִ�.
		�� ����ü�� ���ؼ� � Ŭ���̾�Ʈ�� �����Ͱ� ���ŵǾ����� �Ǵ��� �� �ִ�.
		*/
		SocketInfo = new stSOCKETINFO();
		SocketInfo->socket = clientSocket;
		SocketInfo->recvBytes = 0;
		SocketInfo->sendBytes = 0;
		SocketInfo->dataBuf.len = MAX_BUFFER;
		SocketInfo->dataBuf.buf = SocketInfo->messageBuffer;
		flags = 0;

		/* 
		���⼭ CreateIoCompletionPort�� ������ ȣ���� �Ͱ� �ٸ� �ǹ̷� ���ȴ�.
		ù��° CreateIoCompletionPort�� ȣ���� hIOCP ��ȣ�� �ڵ� ����
		���� CreateIoCompletionPort�� ȣ���� ������� - hIOCP - SocketInfo�� ���

		[O/S�� ����� Ŭ���̾�Ʈ�� IOCP����� ���]
		hIOCP ��ȣ�⸦ ���� clientSocket�� ����� IOCP�� ����ϰ�
		������ �Ϸ�� IOCPť�� ���� �����͸� �����Ѵ�.
		���� �Բ� ���� �Ѱ��ִ� SocketInfo�� �Բ� �޴´�.*/

		hIOCP = CreateIoCompletionPort(
			(HANDLE)clientSocket, hIOCP, (DWORD)SocketInfo, 0
		);

		/*�񵿱� �����̱� ������ ���� WSA_IO_PENDING�� �Ͼ
		 ���� ��Ȥ ȣ���ϴ� �߿� ������ �Ǵ� ��쵵 ���� �� �ִ�.
		*/
		// ��ø ������ �����ϰ� �Ϸ�� ����� �Լ��� �Ѱ���
		nResult = WSARecv(
			SocketInfo->socket,
			&SocketInfo->dataBuf,
			1,
			&recvBytes,
			&flags,
			&(SocketInfo->overlapped),
			NULL
		);

		/*WSAGetLastError()�� 2���� �뵵
		1) ���� App�� �߻��� �ֽ� ���� ���� ���� Ȯ��
		2) ���� App�� �߻��� ���� �ֱ��� ���� ���� Ȯ��
		*/

		/*if (nResult != SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
		���⼭ SOCKET_ERROR�� �ƴϰ� WSA_IO_PENDING�� �ƴ� ����
		 �񵿱� ���������� �Լ� ȣ���ϴ� �߿� �����Ͱ� ���ŵ� ����̹Ƿ�
		 �̷� ���� ������ �ƴϴ�.
		*/
		if (nResult == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
		{
			printf_s("[ERROR] IO Pending ���� : %d", WSAGetLastError());
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
		printf_s("[ERROR] WSASend ���� : ", WSAGetLastError());
	}


}

void IocpBase::Recv(stSOCKETINFO * pSocket)
{
	int nResult;
	// DWORD	sendBytes;
	DWORD	dwFlags = 0;

	// stSOCKETINFO ������ �ʱ�ȭ
	ZeroMemory(&(pSocket->overlapped), sizeof(OVERLAPPED));
	ZeroMemory(pSocket->messageBuffer, MAX_BUFFER);
	pSocket->dataBuf.len = MAX_BUFFER;
	pSocket->dataBuf.buf = pSocket->messageBuffer;
	pSocket->recvBytes = 0;
	pSocket->sendBytes = 0;

	dwFlags = 0;

	// Ŭ���̾�Ʈ�κ��� �ٽ� ������ �ޱ� ���� WSARecv �� ȣ������
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
		printf_s("[ERROR] WSARecv ���� : ", WSAGetLastError());
	}
}

void IocpBase::WorkerThread()
{
	
}
