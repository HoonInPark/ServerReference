#pragma once

// ��Ƽ����Ʈ ���� ���� define
#define _WINSOCK_DEPRECATED_NO_WARNINGS

// winsock2 ����� ���� �Ʒ� �ڸ�Ʈ �߰�
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

// IOCP ���� ����ü
/*�Ϻη� ����ü�� ù��° ��������� WSAOVERLAPPED	overlapped;�� �ذ��̴�.
����ü ��ü�� �����ּҿ� ù��° ��������� �����ּҴ� ����.
WSARecv�� WSAOVERLAPPED	overlapped;�� ����ؾ� �ϴ� ���� SocketInfo->overlapped��
����ϸ� ������ ���Ž� overlapped������ �Բ� �Ѿ���µ� �� �ּҸ� ����
�츮�� ������ stSOCKETINFO ��ü ������ ���� �� �ְ� �ȴ�.
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

// ��Ŷ ó�� �Լ� ������
struct FuncProcess
{
	/*�Լ������� ����
	�������� void, �Ű������� 2�� (stringstream & RecvStream, stSOCKETINFO * pSocket)
	�̷��� ���� �Լ��� funcProcessPacket�� ������ �� �ִ�.
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

	// ���� ��� �� ���� ���� ����
	bool Initialize();
	// ���� ����
	virtual void StartServer();
	// �۾� ������ ����
	virtual bool CreateWorkerThread();	
	// �۾� ������
	virtual void WorkerThread();
	// Ŭ���̾�Ʈ���� �۽�
	virtual void Send(stSOCKETINFO * pSocket);
	// Ŭ���̾�Ʈ ���� ���
	virtual void Recv(stSOCKETINFO * pSocket);		

protected:
	stSOCKETINFO * SocketInfo;		// ���� ����
	SOCKET			ListenSocket;	// ���� ���� ����
	HANDLE			hIOCP;			// IOCP ��ü �ڵ�
	bool			bAccept;		// ��û ���� �÷���
	bool			bWorkerThread;	// �۾� ������ ���� �÷���
	HANDLE *		hWorkerHandle;	// �۾� ������ �ڵ�		
	int				nThreadCnt;	
};
