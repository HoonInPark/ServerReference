#include "stdafx.h"
#include "MainIocp.h"
#include <process.h>
#include <sstream>
#include <algorithm>
#include <string>

// static ���� �ʱ�ȭ
float				MainIocp::HitPoint = 0.1f;
map<int, SOCKET>	MainIocp::SessionSocket;
cCharactersInfo		MainIocp::CharactersInfo;
DBConnector			MainIocp::Conn;
CRITICAL_SECTION	MainIocp::csPlayers;
MonsterSet			MainIocp::MonstersInfo;

map<int, string>	MainIocp::PacketMap;

unsigned int WINAPI CallWorkerThread(LPVOID p)
{
	MainIocp* pOverlappedEvent = (MainIocp*)p;
	pOverlappedEvent->WorkerThread();
	return 0;
}

/* LPVOID == void*
* �� ������ ������ ��� �ڷ����� ������ ������ �Ű������� ���� �� �ִ� �����̴�.
* �׷��� ����� ���� ����ȯ�� ���� ���� ������ ������ ��ȯ�ؼ� ����Ѵ�.
* C#�� object�� ����ó�� ����Ѵ�.
* 
* p���� this�� ���޵Ǿ���.
* ���� Callback�Լ��� CallMonsterThread�� MainIocp ��ü �ּҰ� �Ű������� ���޵Ǿ���.
*/
unsigned int WINAPI CallMonsterThread(LPVOID p)
{
	MainIocp* pOverlappedEvent = (MainIocp*)p;
	pOverlappedEvent->MonsterManagementThread();
	return 0;
}

/*
* MainIocp�� �θ�Ŭ������ IocpBase�̹Ƿ�
* MainIocp�����ڰ� ȣ��Ǳ� ���� IocpBase��ü�� �켱 ��������� �ȴ�.
* �׷��Ƿ� MainIocp�����ں��� IocpBase�����ڰ� ���� ȣ��ȴ�.
*/
MainIocp::MainIocp()
{
	/*�Ӱ迵�� ����ȭ
	�Ӱ迵�� : ������ ����(��Ƽ������ �۵��� ���� �ְ�� �� �ִ� ����)*/
	/*ũ��Ƽ�� ������ �̷��� �ʱ�ȭ�� �ؾ� ����� �� �ִ�.*/
	InitializeCriticalSection(&csPlayers);

	// DB ����(DB������ TCP/IP�� �����Ѵ�)
	if (Conn.Connect(DB_ADDRESS, DB_ID, DB_PW, DB_SCHEMA, DB_PORT))
	{
		printf_s("[INFO] DB ���� ����\n");
	}
	else {
		printf_s("[ERROR] DB ���� ����\n");
	}

	// ��Ŷ �Լ� �����Ϳ� �Լ� ����
	/*��ɾ�� enum ��, ������ �Ǿ� �����Ƿ�
	�迭�� ������ġ�� �Լ������� ������ ó���� �Լ��� �����Ѵ�.
	�׷��� ���߿� �ش� ��ɾ� �϶� �ش� �Լ��� ȣ��ȴ�.
	
	������ ������ ��Ŷ�� ��ɿ� ���� ó���ϴ� �Լ��� ����ü �迭�� �Լ������Ϳ� ����
	*/
	fnProcess[EPacketType::SIGNUP].funcProcessPacket = SignUp;
	fnProcess[EPacketType::LOGIN].funcProcessPacket = Login;
	fnProcess[EPacketType::ENROLL_PLAYER].funcProcessPacket = EnrollCharacter;
	fnProcess[EPacketType::SEND_PLAYER].funcProcessPacket = SyncCharacters;
	fnProcess[EPacketType::HIT_PLAYER].funcProcessPacket = HitCharacter;
	fnProcess[EPacketType::CHAT].funcProcessPacket = BroadcastChat;
	fnProcess[EPacketType::LOGOUT_PLAYER].funcProcessPacket = LogoutCharacter;
	fnProcess[EPacketType::HIT_MONSTER].funcProcessPacket = HitMonster;

	PacketMap[EPacketType::SIGNUP] = "SIGNUP";
	PacketMap[EPacketType::LOGIN] = "LOGIN";
	PacketMap[EPacketType::ENROLL_PLAYER] = "ENROLL_PLAYER";
	PacketMap[EPacketType::SEND_PLAYER] = "SEND_PLAYER";
	PacketMap[EPacketType::HIT_PLAYER] = "HIT_PLAYER";
	PacketMap[EPacketType::CHAT] = "CHAT";
	PacketMap[EPacketType::LOGOUT_PLAYER] = "LOGOUT_PLAYER";
	PacketMap[EPacketType::HIT_MONSTER] = "HIT_MONSTER";
	
}


MainIocp::~MainIocp()
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

	// DB ���� ����
	Conn.Close();
}

void MainIocp::StartServer()
{
	// ���� �������� ���͵��� �����ϴ� �����带 ������ ����
	CreateMonsterManagementThread();	// �뷫 0.5�ʸ��� �ֱ������� ���� ���� ����

	// IOCP �� �ʱ�ȭ
	IocpBase::StartServer();
}

/*IOCP���� ����ϴ� ������Ǯ�� �����ϴ� �Լ�
��������� O/S�� ���� �Ϸ�� ����� Completion Port��ü�� ���ؼ�
IOCP ť���� ������ ó���ϴ� ������ �Ѵ�.
*/
bool MainIocp::CreateWorkerThread()
{
	unsigned int threadId;
	// �ý��� ���� ������
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	printf_s("[INFO] CPU ���� : %d\n", sysInfo.dwNumberOfProcessors);
	// ������ �۾� �������� ������ (CPU * 2) + 1
	/*MSDN���� IOCP�� ���� ������ ������
	CPU * 2 + 1 �� ���ٰ� �����ߴ�.
	�����尡 ����ġ�� ������ Context Switching�� ���� ������ ������ ����
	�����尡 �ʹ� ������ ���ÿ� ���ϴ� Thread�� �ʹ� ��� ������ �ִ�ġ�� ������.
	*/
	/*���� �� PC�� OctaCore�̹Ƿ� 8 * 2 = 16���� �����尡 �����ȴ�.
	* ���� ���� �������� ������ ���� �׽�Ʈ�� ���ؼ� �����ؾ� �Ѵ�.
	*/
	nThreadCnt = sysInfo.dwNumberOfProcessors * 2;

	// thread handler ����
	// ������ ���� ������ŭ ������ �ڵ� ���� �迭 ����
	hWorkerHandle = new HANDLE[nThreadCnt];
	// thread�� nThreadCnt������ŭ ����
	for (int i = 0; i < nThreadCnt; i++)
	{
		// �Ͻ������� �����带 ����
		// CallWorkerThread�� �Ű������� MainIocp�� ��ü�ּ��� this�� ����
		hWorkerHandle[i] = (HANDLE *)_beginthreadex(
			NULL, 0, &CallWorkerThread, this, CREATE_SUSPENDED, &threadId
		);
		if (hWorkerHandle[i] == NULL)
		{
			printf_s("[ERROR] Worker Thread ���� ����\n");
			return false;
		}
		// ���������� �����Ǿ��ٸ� �����带 ����
		ResumeThread(hWorkerHandle[i]);
	}
	printf_s("[INFO] Worker Thread ����...\n");
	return true;
}

/*Send�� IOCP�� ���� ����� Ȯ���ϴ� ����� �ƴ϶�
�׳� ���������� �����ϰ� �ִ�.

�׷��Ƿ� stSOCKETINFO ����ü�� ����� �ʿ�� ������
�Ƹ��� ���Ž� stSOCKETINFO ����ü�� ����߱� ������
������ ������ �� ����.
*/
void MainIocp::Send(stSOCKETINFO * pSocket)
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

void MainIocp::CreateMonsterManagementThread()
{
	unsigned int threadId;
	/* C/C++���� �����带 �����ϴ� �Լ��� _beginthreadex�̴�.
	* �� �Լ��� ȣ���ϸ� Windows�� �����带 �����ϰ�
	* �� �����带 �����ϱ� ���� Handle�� Id�� �����Ѵ�.
	* MonsterHandle�� �ڵ��̰�
	* threadId�� Id�̴�.
	* ���μ��� �������� �ڵ�� �����带 �����ϰ�
	* ���μ����� �ѳ��� ���� threadId�� ����Ѵ�.
	* 
	* �����尡 �����Ǹ� CallMonsterThread �Լ��� �����尡 �����Ų��.
	* �׷��� ���⼭�� CREATE_SUSPENDED �ɼ��� �༭
	* �����尡 �����Ǿ����� �ٷ� �������� �ʰ� �Ͻ����� �����̴�.
	* 
	* �Ʒ��� this�� �����忡 ���� ����Ǵ� CallMonsterThread�� �Ű������� ���޵ȴ�.
	*/
	MonsterHandle = (HANDLE *)_beginthreadex(
		NULL, 0, &CallMonsterThread, this, CREATE_SUSPENDED, &threadId
	);
	if (MonsterHandle == NULL)
	{
		printf_s("[ERROR] Monster Thread ���� ����\n");
		return;
	}
	/* CREATE_SUSPENDED�� ���� �Ͻ������� �����带 ���۽�Ų��.
	*/
	ResumeThread(MonsterHandle);

	printf_s("[INFO] Monster Thread ����...\n");
}

/*�����忡 ���� ���Ǵ� �Ʒ� �Լ���
���Ϳ� �÷��̾�� ��ȣ�ۿ�����(��Ʈ����, ��������, ��ġ����)�� �����ϰ�
0.5�ʸ��� �ݺ������� ���ӵ� ���� Ŭ���̾�Ʈ�� 
���͵��� ���ŵ� ������ ������ ������ �Ѵ�*/
void MainIocp::MonsterManagementThread()
{
	// ���� �ʱ�ȭ
	InitializeMonsterSet();
	int count = 0;	
	// ���� ����
	while (true)  // ���� ���� �ݺ�
	{
		// C 11ǥ�ؿ��� �迭�̳� map���� ��ü���� ������� ���� ������ for��
		// auto�� C#������ var�� ���� �ǹ�(�����Ϳ� ���� Ÿ���� �����ȴ�)
		for (auto & kvp : MonstersInfo.monsters)
		{
			/* 
			* map<int, Monster> monsters;
			* ==> key, value
			* == MonstersInfo.monsters[mFields.Id] = mFields; // �����
			* kvp.first == key		// Ű���� ���� ��
			* kvp.second == value	// value���� ���� ��
			*/
			auto & monster = kvp.second;	// map��ü�� value��

			/*ó�� ������ ����� ���� ������ �÷��̾ �����Ƿ� �Ʒ� for���� ������� �ʰ�
			�÷��̾ ���ӵǾ��� �� ���� CharactersInfp.players�� ��ϵǹǷ�
			�� ������ �Ʒ� for���� �����ϰ� �ȴ�.
			*/
			for (auto & player : CharactersInfo.players)
			{
				// �÷��̾ ���Ͱ� �׾����� �� ����
				if (!player.second.IsAlive || !monster.IsAlive())
					continue;

				// ���Ͱ� �÷��̾ ������ �������� �Ǵ�
				if (monster.IsPlayerInHitRange(player.second) && !monster.bIsAttacking)
				{
					monster.HitPlayer(player.second);
					continue;
				}

				// ���Ͱ� �÷��̾ ������ �������� �Ǵ�
				if (monster.IsPlayerInTraceRange(player.second) && !monster.bIsAttacking)
				{
					monster.MoveTo(player.second);
					continue;
				}
			}
		}

		count++;
		// 0.5�ʸ��� Ŭ���̾�Ʈ���� ���͵��� ������ �ֱ������� ����
		if (count > 15)   // 0.033 * 15 = 0.495 (Windows�� ����Ÿ��os�� �ƴϹǷ� �뷫 0.5��)
		{			
			/*��Ŷ����
			stringstream��ü�� char�� �����Ѵ�.
			��ɾ� + ������(int, char, short, ����ü...)	

			* tcpƯ���� ����Ѵٸ� ��ü���� + ��ɾ� + ������
			* �̷��� �����ؼ� 1�� ��Ŷ�� ���̸� �� �� �ֵ��� �ؾ� �Ѵ�.
			* �׷��� ���� ���� ������ 
			* �Ƹ��� Localȯ�濡�� �׽�Ʈ�� �����Ƿ� 
			* ��Ŷ�� �и�/���յǴ� ���� ���� ������ ���̹Ƿ�
			* ��Ŷ ó���� �� �Ǿ��� ���̴�.
			*/
			/* stringstream�� ���ο� char���� �迭�� �ִ�.
			* << �����ڸ� ���� �����͸� char�� �����Ѵ�.
			*/
			stringstream SendStream;	// ��Ŷ�� ������� ������ ��Ʈ�� ��ü
			SendStream << EPacketType::SYNC_MONSTER << endl;	// ���(����)
			SendStream << MonstersInfo << endl;	// ��ɿ� ���� ������

			count = 0;
			Broadcast(SendStream);		// ��� ����� ���� Ŭ���̾�Ʈ�� ����
		}
		
		Sleep(33);  // 0.033��
	}
}

void MainIocp::InitializeMonsterSet()
{
	// ���� 4������ �ʱ� ��ġ, Health, Id �������� ������ �ʱ�ȭ �Ѵ�.
	// �� ������ ���ӿ� ������ ����
	Monster mFields;

	mFields.X = -5746;
	mFields.Y = 3736;
	mFields.Z = 7362;
	mFields.Health = 100.0f;
	mFields.Id = 1;
	mFields.MovePoint = 10.f;
	MonstersInfo.monsters[mFields.Id] = mFields;	// key=mFields.Id, value=mFields

	mFields.X = -5136;
	mFields.Y = 1026;
	mFields.Z = 7712;
	mFields.Id = 2;
	MonstersInfo.monsters[mFields.Id] = mFields;

	mFields.X = -3266;
	mFields.Y = 286;
	mFields.Z = 8232;
	mFields.Id = 3;
	MonstersInfo.monsters[mFields.Id] = mFields;

	mFields.X = -156;
	mFields.Y = 326;
	mFields.Z = 8352;
	mFields.Id = 4;
	MonstersInfo.monsters[mFields.Id] = mFields;
}

/*IOCP�� ������Ǯ���� ȣ���ϴ� �Լ�
���⿡�� ������ ���ŵ� �Ϸ� ��Ŷ�� ó���Ѵ�.

������ ���� ���� ��������� ��� �� �Լ��� �۾��� �����Ѵ�.
*/
void MainIocp::WorkerThread()
{
	// �Լ� ȣ�� ���� ����
	BOOL	bResult;
	int		nResult;
	// Overlapped I/O �۾����� ���۵� ������ ũ��
	DWORD	recvBytes;
	DWORD	sendBytes;
	// Completion Key�� ���� ������ ����
	stSOCKETINFO *	pCompletionKey;
	// I/O �۾��� ���� ��û�� Overlapped ����ü�� ���� ������	
	stSOCKETINFO *	pSocketInfo;
	DWORD	dwFlags = 0;


	while (bWorkerThread)
	{
		/**
		 * �� �Լ��� ���� ��������� WaitingThread Queue �� �����·� ���� ��
		 * �Ϸ�� Overlapped I/O �۾��� �߻��ϸ� IOCP Queue ���� �Ϸ�� �۾��� ������
		 * ��ó���� ��
		 */
		/*
		//(DWORD)SocketInfo => (PULONG_PTR)&pCompletionKey�� ���� ���� �� �ִ�.
		hIOCP = CreateIoCompletionPort(
			(HANDLE)clientSocket, hIOCP, (DWORD)SocketInfo, 0
		);

		//&(SocketInfo->overlapped) => (LPOVERLAPPED *)&pSocketInfo
		nResult = WSARecv(
			SocketInfo->socket,
			&SocketInfo->dataBuf,
			1,
			&recvBytes,
			&flags,
			&(SocketInfo->overlapped),
			NULL
		);
		*/
		bResult = GetQueuedCompletionStatus(hIOCP,
			&recvBytes,				// ������ ���ŵ� ����Ʈ
			(PULONG_PTR)&pCompletionKey,	// completion key
			(LPOVERLAPPED *)&pSocketInfo,			// overlapped I/O ��ü
			INFINITE				// ����� �ð�
		);
		
		// ����� false�̰� ����byte�� 0�ΰ�� => ���� ������
		if (!bResult && recvBytes == 0)
		{
			printf_s("[INFO] socket(%d) ���� ����\n", pSocketInfo->socket);
			closesocket(pSocketInfo->socket);
			free(pSocketInfo);
			continue;
		}

		pSocketInfo->dataBuf.len = recvBytes;

		// ����byte�� 0�ΰ�� => ���� ������
		if (recvBytes == 0)
		{
			printf_s("[INFO] socket(%d) ���� close\n", pSocketInfo->socket);
			closesocket(pSocketInfo->socket);
			free(pSocketInfo);
			continue;
		}

		/*���� ���ŵǾ����ϱ� ��Ŷ ó��
		
		UDP�� 100byte���� => 100byte����
		      1byte���� => 1byte����
			  ���� Ƚ���� ���� Ƚ���� �����ϴ�.
			  �ٸ� �߰��� ��Ŷ �ս��� ���� �� �ִ�.
		TCP�� 100byte���� => 100byte����
		                  => 99byte����, 1byte����
						  => 80byte����, 20byte����
						  => 30byte����, 57byte����, 13byte����
			  �̷��� ���� Ƚ���� ���� Ƚ���� �޶��� �� �ִ�.
			  (Ư�� �����ڰ� ����, ������ Ŭ���̾�Ʈ�� ������ �Ÿ��� �ְ�,
			   �����Ͱ� ũ��, ������ Ƚ���� ����� �� ����)

		�� ����� ������ TCP�� ����ߴµ� ��ó�� 1���� ��Ŷ�� �ϼ����ִ� �ڵ尡 ����.
		1�� ������ �� �޴� ��ó�� �������� ó���ߴ�.
		*/
		try
		{
			/*���� �����͸� ������Ŷ���� Ȯ���ϴ� ������ ����.
			������Ŷ�� �ƴ� ��� ������Ŷ���� ������ִ� �ڵ尡 ���� �Ѵ�.

			��, ����� �ַ� �׽�Ʈ ȯ���� Local�̹Ƿ� ū ������ ���� �ʴ´�.
			*/

			// ��Ŷ ����
			int PacketType;
			// Ŭ���̾�Ʈ ���� ������ȭ
			stringstream RecvStream;

			// ���ŵ����� �迭 -> stringStream��ü�� �Է�(���� �� ���ϰ� ��������)
			RecvStream << pSocketInfo->dataBuf.buf;
			// Ŭ���̾�Ʈ�� ���� ������� �����͸� ���� �� �ִ�.
			// ��� + ������
			// ����� ���±� ������ RecvStream���� �����͸� �����ִ�.
			// �����ʹ� ����ü �迭 �Լ������� ������ ����� �Լ��� ���� ó���Ѵ�.
			RecvStream >> PacketType;	// ���

			// ��Ŷ ��� ���� -> ��Ŷ ���ڿ�
			printf_s("[Packet Command] %s\n", PacketMap[PacketType].c_str());

			/*
			* C/C++���� enum���� int�� ��ȣȣȯ�� �ȴ�.
			* PacketType�� ����ִ� ���� ���� �Լ������Ͱ� �Լ��� �ּҸ� �����س������Ƿ�
			* �ش� ��Ŷó�� �Լ��� ȣ��ǰ� �ȴ�.
			* PacketType�� EPacketType::SIGNUP�̸� SignUp�Լ��� ȣ��ȴ�.
			* 
				fnProcess[EPacketType::SIGNUP].funcProcessPacket = SignUp;
				fnProcess[EPacketType::LOGIN].funcProcessPacket = Login;
				fnProcess[EPacketType::ENROLL_PLAYER].funcProcessPacket = EnrollCharacter;
				fnProcess[EPacketType::SEND_PLAYER].funcProcessPacket = SyncCharacters;
				fnProcess[EPacketType::HIT_PLAYER].funcProcessPacket = HitCharacter;
				fnProcess[EPacketType::CHAT].funcProcessPacket = BroadcastChat;
				fnProcess[EPacketType::LOGOUT_PLAYER].funcProcessPacket = LogoutCharacter;
				fnProcess[EPacketType::HIT_MONSTER].funcProcessPacket = HitMonster;
			*/
			// ��Ŷ ó��
			if (fnProcess[PacketType].funcProcessPacket != nullptr)
			{
				fnProcess[PacketType].funcProcessPacket(RecvStream, pSocketInfo);
			}
			else
			{
				printf_s("[ERROR] ���� ���� ���� ��Ŷ : %d\n", PacketType);
			}
		}
		catch (const std::exception& e)
		{
			printf_s("[ERROR] �� �� ���� ���� �߻� : %s\n", e.what());
		}

		/*������ �������Ƿ� �ٽ� IOCP�� Ŭ���̾�Ʈ�κ��� �����ϱ� ���� 
		�̸� �񵿱�/Overlapped I/O�� ȣ���ϸ� �ȴ�.
		IOCPť�� ����� ó���� WSARecv�� �� �� ����� �����Ƿ� �߰� ����� �� �ʿ�� ����.
		*/
		// Ŭ���̾�Ʈ ���
		Recv(pSocketInfo);
	}
}

/// <summary>
/// Ŭ���̾�Ʈ�� ȸ������ ��û�� ó���ϴ� �Լ�
/// </summary>
/// <param name="RecvStream">����� ������ �� �� ������</param>
/// <param name="pSocket">���� ����</param>
void MainIocp::SignUp(stringstream & RecvStream, stSOCKETINFO * pSocket)
{
	string Id;
	string Pw;

	// Ŭ���̾�Ʈ�� ���� ���ڿ��� ������� ������.
	RecvStream >> Id;
	RecvStream >> Pw;

	printf_s("[INFO] ȸ������ �õ� {%s}/{%s}\n", Id.c_str(), Pw.c_str());

	// Ŭ���̾�Ʈ�� ���� ����
	stringstream SendStream;
	SendStream << EPacketType::SIGNUP << endl;
	SendStream << Conn.SignUpAccount(Id, Pw) << endl;

	// streamstream��ü�� char�迭�� ��ȯ�ؼ�, char�迭�� �����ּ�(char*)�� ���̸� ����
	// pSocket->messageBuffer�� ����
	CopyMemory(pSocket->messageBuffer, (CHAR*)SendStream.str().c_str(), SendStream.str().length());
	pSocket->dataBuf.buf = pSocket->messageBuffer;
	pSocket->dataBuf.len = SendStream.str().length();

	// �� ��Ŷ�� ������ Ŭ���̾�Ʈ���� ���� ��Ŷ ����
	Send(pSocket);
}

/// <summary>
/// Ŭ���̾�Ʈ�� �α��� ��û�� ó���ϴ� �Լ�
/// </summary>
/// <param name="RecvStream">����� ������ �� �� ������</param>
/// <param name="pSocket">���� ����</param>
void MainIocp::Login(stringstream & RecvStream, stSOCKETINFO * pSocket)
{
	string Id;
	string Pw;

	RecvStream >> Id;
	RecvStream >> Pw;

	printf_s("[INFO] �α��� �õ� {%s}/{%s}\n", Id.c_str(), Pw.c_str());

	stringstream SendStream;
	SendStream << EPacketType::LOGIN << endl;
	SendStream << Conn.SearchAccount(Id, Pw) << endl;

	CopyMemory(pSocket->messageBuffer, (CHAR*)SendStream.str().c_str(), SendStream.str().length());
	pSocket->dataBuf.buf = pSocket->messageBuffer;
	pSocket->dataBuf.len = SendStream.str().length();

	Send(pSocket);
}

/// <summary>
/// Ŭ���̾�Ʈ�� ElevenRuins������ �����ϰ� ���� �÷��̾ �����ϰ� ��� ��û�� ó���ϴ� �Լ�
/// 1) CharactersInfo�� ���� ������ �÷��̾� ĳ���� ���� ���
/// 2) SessionSocket�� ���� ���� ���
/// �� 2�� ��� sessionId�� key������ �Ѵ�.
/// </summary>
/// <param name="RecvStream">����� ������ �� �� ������</param>
/// <param name="pSocket">���� ����</param>
void MainIocp::EnrollCharacter(stringstream & RecvStream, stSOCKETINFO * pSocket)
{
	cCharacter info;
	RecvStream >> info;

	printf_s("[INFO][%d]ĳ���� ��� - X : [%f], Y : [%f], Z : [%f], Yaw : [%f], Alive : [%d], Health : [%f]\n",
		info.SessionId, info.X, info.Y, info.Z, info.Yaw, info.IsAlive, info.HealthValue);

	// CharactersInfo.players�� �ٸ� �����忡�� ���ÿ� ����� �� �ְ� �ȴ�.
	// �׶� ���� �� �����尡 ���ÿ� ���� �����Ϸ��� �õ��� ��
	// ���� �ְ��� �߻��� �� �����Ƿ�, 1���� �����常 ������ ����ϴ� �Ӱ迵���� �����Ѵ�.
	// �Ӱ迵��(ex. ȭ���)
	EnterCriticalSection(&csPlayers);

	// ��ü �÷��̾� �����߿� �� sessionId�� �ش��ϴ� �÷��̾� ���� ��ü �ּ�
	/*info.SessionId�� players(map)��ü�� key�� �Է��ϸ� cCharacter��ü�� �����ȴ�.
	�� ������ cCharacter��ü�� �ּҸ� �����Ͽ� pinfo�� �����Ѵ�.
	*/
	//1) 1��° ���
	cCharacter* pinfo = &CharactersInfo.players[info.SessionId];

	/*CharactersInfo�� �����ü�� players�� cCharacter��ü�� Ŭ���̾�Ʈ�κ��� ������ ĳ���� ������ �����Ѵ�.*/
	// ĳ������ ��ġ�� ����						
	pinfo->SessionId = info.SessionId;
	pinfo->X = info.X;
	pinfo->Y = info.Y;
	pinfo->Z = info.Z;

	// ĳ������ ȸ������ ����
	pinfo->Yaw = info.Yaw;
	pinfo->Pitch = info.Pitch;
	pinfo->Roll = info.Roll;

	// ĳ������ �ӵ��� ����
	pinfo->VX = info.VX;
	pinfo->VY = info.VY;
	pinfo->VZ = info.VZ;

	// ĳ���� �Ӽ�
	pinfo->IsAlive = info.IsAlive;
	pinfo->HealthValue = info.HealthValue;
	pinfo->IsAttacking = info.IsAttacking;

	LeaveCriticalSection(&csPlayers);

	// �÷��̾ ElevenRuins�ʵ忡 �����ؼ� ENROLL_PLAYER�� ������ ������
	// ���� ������ �����ϴ� map��ü�� sessionId�� �������� ������ ����Ѵ�.
	/*���������� �� �ܰ迡�� Ŭ���̾�Ʈ�� �������� ����Ǿ��ٰ� �����*/
	// 2) 2��° ���
	SessionSocket[info.SessionId] = pSocket->socket;

	printf_s("[INFO] Ŭ���̾�Ʈ �� : %d\n", SessionSocket.size());

	//Send(pSocket);

	// ����� ��� Ŭ���̾�Ʈ���� ���ο� �÷��̾� ������ ������
	BroadcastNewPlayer(info);
}

/// <summary>
/// Ŭ���̾�Ʈ���� �������� ������ ���� �÷��̾� Transform������ �����Ѵ�.
/// </summary>
/// <param name="RecvStream"></param>
/// <param name="pSocket"></param>
void MainIocp::SyncCharacters(stringstream& RecvStream, stSOCKETINFO* pSocket)
{
	cCharacter info;
	RecvStream >> info;

	// 	 	printf_s("[INFO][%d]���� ���� - %d\n",
	// 	 		info.SessionId, info.IsAttacking);	
	EnterCriticalSection(&csPlayers);

	// ��ü �÷��̾���� �����߿� �� ������ ã�Ƽ� ������Ʈ
	cCharacter * pinfo = &CharactersInfo.players[info.SessionId];

	// ĳ������ ��ġ�� ����						
	pinfo->SessionId = info.SessionId;
	pinfo->X = info.X;
	pinfo->Y = info.Y;
	pinfo->Z = info.Z;

	// ĳ������ ȸ������ ����
	pinfo->Yaw = info.Yaw;
	pinfo->Pitch = info.Pitch;
	pinfo->Roll = info.Roll;

	// ĳ������ �ӵ��� ����
	pinfo->VX = info.VX;
	pinfo->VY = info.VY;
	pinfo->VZ = info.VZ;

	pinfo->IsAttacking = info.IsAttacking;

	LeaveCriticalSection(&csPlayers);

	// �ڽ��� ���� �÷��̾� ������ ������ Ŭ���̾�Ʈ����
	// ���� ������ ������ �ִ� ��� �÷��̾� ��ü ������ �����ش�.
	WriteCharactersInfoToSocket(pSocket);
	Send(pSocket);
}

void MainIocp::LogoutCharacter(stringstream& RecvStream, stSOCKETINFO* pSocket)
{
	int SessionId;
	RecvStream >> SessionId;
	printf_s("[INFO] (%d)�α׾ƿ� ��û ����\n", SessionId);
	EnterCriticalSection(&csPlayers);
	CharactersInfo.players[SessionId].IsAlive = false;
	LeaveCriticalSection(&csPlayers);
	SessionSocket.erase(SessionId);
	printf_s("[INFO] Ŭ���̾�Ʈ �� : %d\n", SessionSocket.size());
	WriteCharactersInfoToSocket(pSocket);
}

void MainIocp::HitCharacter(stringstream & RecvStream, stSOCKETINFO * pSocket)
{
	// �ǰ� ó���� ���� ���̵�
	int DamagedSessionId;
	RecvStream >> DamagedSessionId;
	printf_s("[INFO] %d ������ ���� \n", DamagedSessionId);
	EnterCriticalSection(&csPlayers);
	CharactersInfo.players[DamagedSessionId].HealthValue -= HitPoint;
	if (CharactersInfo.players[DamagedSessionId].HealthValue < 0)
	{
		// ĳ���� ���ó��
		CharactersInfo.players[DamagedSessionId].IsAlive = false;
	}
	LeaveCriticalSection(&csPlayers);
	WriteCharactersInfoToSocket(pSocket);
	Send(pSocket);
}

void MainIocp::BroadcastChat(stringstream& RecvStream, stSOCKETINFO* pSocket)
{
	stSOCKETINFO* client = new stSOCKETINFO;

	int SessionId;
	string Temp;
	string Chat;

	RecvStream >> SessionId;
	getline(RecvStream, Temp);
	Chat += to_string(SessionId) + "_:_";
	while (RecvStream >> Temp)
	{
		Chat += Temp + "_";
	}
	Chat += '\0';

	printf_s("[CHAT] %s\n", Chat.c_str());

	stringstream SendStream;
	SendStream << EPacketType::CHAT << endl;
	SendStream << Chat;

	Broadcast(SendStream);
}

void MainIocp::HitMonster(stringstream & RecvStream, stSOCKETINFO * pSocket)
{
	// ���� �ǰ� ó��
	int MonsterId;
	RecvStream >> MonsterId;
	MonstersInfo.monsters[MonsterId].Damaged(30.f);

	if (!MonstersInfo.monsters[MonsterId].IsAlive())
	{
		stringstream SendStream;
		SendStream << EPacketType::DESTROY_MONSTER << endl;
		SendStream << MonstersInfo.monsters[MonsterId] << endl;

		Broadcast(SendStream);

		MonstersInfo.monsters.erase(MonsterId);
	}

	// �ٸ� �÷��̾�� ��ε�ĳ��Ʈ
	/*stringstream SendStream;
	SendStream << EPacketType::HIT_MONSTER << endl;
	SendStream << MonstersInfo << endl;

	Broadcast(SendStream);*/
}

void MainIocp::BroadcastNewPlayer(cCharacter & player)
{
	stringstream SendStream;
	SendStream << EPacketType::ENTER_NEW_PLAYER << endl;
	SendStream << player << endl;

	// SessionSocket�� ��ϵ� ��� Ŭ���̾�Ʈ���� ���ο� �÷��̾� ��ü ������ ������
	Broadcast(SendStream);
}

void MainIocp::Broadcast(stringstream & SendStream)
{
	/*IOCP��ſ��� ����� ����ü
	 ����, �������, ���۱���, ���ű���... ������ ����ִ�.
	 ���߿� IOCPť���� ������ �� �����κ���, �󸶸�ŭ, � �����Ͱ� ����ִ���
	 Ȯ���� �� �ִ�.
	*/
	stSOCKETINFO* client = new stSOCKETINFO;

	/*SessionSocket : �α��� �� Ŭ���̾�Ʈ�� �ʵ忡 �������� �� ������
	                  �ش� Ŭ���̾�Ʈ�� ����� �� �����ϴ� map��ü
	  ����ȭ�ϱ� ���� ������ Ŭ���̾�Ʈ ���� ��ü
	*/
	// ��ü Ŭ���̾�Ʈ����
	for (const auto& kvp : SessionSocket)
	{
		client->socket = kvp.second;  // Ŭ���̾�Ʈ�� ����� ����(��� ��ü)
		/* stringStream��ü�� SendStream->string -> char* (�迭�� �����ּ�) : �����ͽ���
		* SendStream.str().length() : ������ ����
		* ������ client->messageBuffer�� ����(char�迭�� �����Ѵ�)
		*/
		CopyMemory(client->messageBuffer, (CHAR*)SendStream.str().c_str(), SendStream.str().length());
		
		// ���� ������ �����ּҿ� ���� ������ ��� ����ü�� ����
		client->dataBuf.buf = client->messageBuffer;
		client->dataBuf.len = SendStream.str().length();

		Send(client);
	}
}

void MainIocp::WriteCharactersInfoToSocket(stSOCKETINFO * pSocket)
{
	stringstream SendStream;

	// ����ȭ	
	SendStream << EPacketType::RECV_PLAYER << endl;
	SendStream << CharactersInfo << endl;		// ��� �÷��̾���� ����

	// !!! �߿� !!! data.buf ���� ���� �����͸� ���� �����Ⱚ�� ���޵� �� ����
	CopyMemory(pSocket->messageBuffer, (CHAR*)SendStream.str().c_str(), SendStream.str().length());
	pSocket->dataBuf.buf = pSocket->messageBuffer;
	pSocket->dataBuf.len = SendStream.str().length();
}

