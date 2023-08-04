#include "stdafx.h"
#include "MainIocp.h"
#include <process.h>
#include <sstream>
#include <algorithm>
#include <string>

// static 변수 초기화
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
* 이 포인터 변수는 모든 자료형의 포인터 변수를 매개변수로 받을 수 있는 인자이다.
* 그래서 사용할 때는 형변환을 통해 원래 포인터 변수로 변환해서 사용한다.
* C#의 object의 역할처럼 사용한다.
* 
* p에는 this가 전달되었다.
* 전역 Callback함수는 CallMonsterThread에 MainIocp 객체 주소가 매개변수로 전달되었다.
*/
unsigned int WINAPI CallMonsterThread(LPVOID p)
{
	MainIocp* pOverlappedEvent = (MainIocp*)p;
	pOverlappedEvent->MonsterManagementThread();
	return 0;
}

/*
* MainIocp의 부모클래스는 IocpBase이므로
* MainIocp생성자가 호출되기 전에 IocpBase객체가 우선 만들어지게 된다.
* 그러므로 MainIocp생성자보다 IocpBase생성자가 먼저 호출된다.
*/
MainIocp::MainIocp()
{
	/*임계영역 동기화
	임계영역 : 주의할 영역(멀티스레드 작동시 값이 왜곡될 수 있는 구간)*/
	/*크리티컬 섹션은 이렇게 초기화를 해야 사용할 수 있다.*/
	InitializeCriticalSection(&csPlayers);

	// DB 접속(DB서버에 TCP/IP로 접속한다)
	if (Conn.Connect(DB_ADDRESS, DB_ID, DB_PW, DB_SCHEMA, DB_PORT))
	{
		printf_s("[INFO] DB 접속 성공\n");
	}
	else {
		printf_s("[ERROR] DB 접속 실패\n");
	}

	// 패킷 함수 포인터에 함수 지정
	/*명령어는 enum 즉, 정수로 되어 있으므로
	배열의 정수위치의 함수포인터 변수에 처리할 함수를 저장한다.
	그러면 나중에 해당 명령어 일때 해당 함수가 호출된다.
	
	서버로 들어오는 패킷의 명령에 따라 처리하는 함수를 구조체 배열의 함수포인터에 연결
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

	// DB 연결 종료
	Conn.Close();
}

void MainIocp::StartServer()
{
	// 게임 레벨상의 몬스터들을 관리하는 스레드를 별도로 생성
	CreateMonsterManagementThread();	// 대략 0.5초마다 주기적으로 몬스터 정보 전송

	// IOCP 모델 초기화
	IocpBase::StartServer();
}

/*IOCP에서 사용하는 스레드풀을 생성하는 함수
스레드들은 O/S에 의해 완료된 통신을 Completion Port객체를 통해서
IOCP 큐에서 꺼내서 처리하는 역할을 한다.
*/
bool MainIocp::CreateWorkerThread()
{
	unsigned int threadId;
	// 시스템 정보 가져옴
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	printf_s("[INFO] CPU 갯수 : %d\n", sysInfo.dwNumberOfProcessors);
	// 적절한 작업 스레드의 갯수는 (CPU * 2) + 1
	/*MSDN에서 IOCP의 권장 스레드 갯수를
	CPU * 2 + 1 이 좋다고 권장했다.
	스레드가 지나치게 많으면 Context Switching에 의해 오히려 성능이 저하
	스레드가 너무 적으면 동시에 일하는 Thread가 너무 적어서 성능의 최대치를 사용못함.
	*/
	/*현재 이 PC는 OctaCore이므로 8 * 2 = 16개의 스레드가 생성된다.
	* 가장 좋은 스레드의 갯수는 성능 테스트를 통해서 결정해야 한다.
	*/
	nThreadCnt = sysInfo.dwNumberOfProcessors * 2;

	// thread handler 선언
	// 스레드 생성 갯수만큼 스레드 핸들 저장 배열 생성
	hWorkerHandle = new HANDLE[nThreadCnt];
	// thread를 nThreadCnt갯수만큼 생성
	for (int i = 0; i < nThreadCnt; i++)
	{
		// 일시정지된 스레드를 생성
		// CallWorkerThread의 매개변수로 MainIocp의 객체주소인 this를 전달
		hWorkerHandle[i] = (HANDLE *)_beginthreadex(
			NULL, 0, &CallWorkerThread, this, CREATE_SUSPENDED, &threadId
		);
		if (hWorkerHandle[i] == NULL)
		{
			printf_s("[ERROR] Worker Thread 생성 실패\n");
			return false;
		}
		// 정상적으로 생성되었다면 스레드를 동작
		ResumeThread(hWorkerHandle[i]);
	}
	printf_s("[INFO] Worker Thread 시작...\n");
	return true;
}

/*Send는 IOCP를 통해 결과를 확인하는 방식이 아니라
그냥 동기방식으로 전송하고 있다.

그러므로 stSOCKETINFO 구조체를 사용할 필요는 없지만
아마도 수신시 stSOCKETINFO 구조체를 사용했기 때문에
형식을 맞춰준 것 같다.
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
		printf_s("[ERROR] WSASend 실패 : ", WSAGetLastError());
	}


}

void MainIocp::CreateMonsterManagementThread()
{
	unsigned int threadId;
	/* C/C++에서 스레드를 생성하는 함수는 _beginthreadex이다.
	* 이 함수를 호출하면 Windows는 스레드를 생성하고
	* 이 스레드를 제어하기 위해 Handle과 Id를 리턴한다.
	* MonsterHandle이 핸들이고
	* threadId가 Id이다.
	* 프로세스 내에서는 핸들로 스레드를 제어하고
	* 프로세스를 넘나들 때는 threadId를 사용한다.
	* 
	* 스레드가 생성되면 CallMonsterThread 함수를 스레드가 진행시킨다.
	* 그런데 여기서는 CREATE_SUSPENDED 옵션을 줘서
	* 스레드가 생성되었지만 바로 동작하지 않고 일시정지 상태이다.
	* 
	* 아래의 this는 스레드에 의해 제어되는 CallMonsterThread의 매개변수로 전달된다.
	*/
	MonsterHandle = (HANDLE *)_beginthreadex(
		NULL, 0, &CallMonsterThread, this, CREATE_SUSPENDED, &threadId
	);
	if (MonsterHandle == NULL)
	{
		printf_s("[ERROR] Monster Thread 생성 실패\n");
		return;
	}
	/* CREATE_SUSPENDED에 의해 일시정지된 스레드를 동작시킨다.
	*/
	ResumeThread(MonsterHandle);

	printf_s("[INFO] Monster Thread 시작...\n");
}

/*스레드에 의해 사용되는 아래 함수는
몬스터와 플레이어간의 상호작용정보(히트가능, 추적가능, 위치정보)를 갱신하고
0.5초마다 반복적으로 접속된 게임 클라이언트에 
몬스터들의 갱신된 정보를 보내는 역할을 한다*/
void MainIocp::MonsterManagementThread()
{
	// 몬스터 초기화
	InitializeMonsterSet();
	int count = 0;	
	// 로직 시작
	while (true)  // 무한 루프 반복
	{
		// C 11표준에서 배열이나 map같은 객체에서 순서대로 값을 꺼내는 for문
		// auto는 C#에서의 var와 같은 의미(데이터에 따라 타입이 결정된다)
		for (auto & kvp : MonstersInfo.monsters)
		{
			/* 
			* map<int, Monster> monsters;
			* ==> key, value
			* == MonstersInfo.monsters[mFields.Id] = mFields; // 저장시
			* kvp.first == key		// 키값을 얻을 때
			* kvp.second == value	// value값을 얻을 때
			*/
			auto & monster = kvp.second;	// map객체의 value값

			/*처음 서버가 실행될 때는 접속한 플레이어가 없으므로 아래 for문은 실행되지 않고
			플레이어가 접속되었을 때 부터 CharactersInfp.players에 등록되므로
			그 때부터 아래 for문이 동작하게 된다.
			*/
			for (auto & player : CharactersInfo.players)
			{
				// 플레이어나 몬스터가 죽어있을 땐 무시
				if (!player.second.IsAlive || !monster.IsAlive())
					continue;

				// 몬스터가 플레이어를 공격할 범위인지 판단
				if (monster.IsPlayerInHitRange(player.second) && !monster.bIsAttacking)
				{
					monster.HitPlayer(player.second);
					continue;
				}

				// 몬스터가 플레이어를 추적할 범위인지 판단
				if (monster.IsPlayerInTraceRange(player.second) && !monster.bIsAttacking)
				{
					monster.MoveTo(player.second);
					continue;
				}
			}
		}

		count++;
		// 0.5초마다 클라이언트에게 몬스터들의 정보를 주기적으로 전송
		if (count > 15)   // 0.033 * 15 = 0.495 (Windows는 리얼타임os가 아니므로 대략 0.5초)
		{			
			/*패킷구조
			stringstream객체에 char로 저장한다.
			명령어 + 데이터(int, char, short, 구조체...)	

			* tcp특성을 고려한다면 전체길이 + 명령어 + 데이터
			* 이렇게 설계해서 1개 패킷의 길이를 알 수 있도록 해야 한다.
			* 그렇게 하지 않은 이유는 
			* 아마도 Local환경에서 테스트를 했으므로 
			* 패킷이 분리/결합되는 일이 거의 없었을 것이므로
			* 패킷 처리가 잘 되었을 것이다.
			*/
			/* stringstream은 내부에 char버퍼 배열이 있다.
			* << 연산자를 통해 데이터를 char로 저장한다.
			*/
			stringstream SendStream;	// 패킷을 순서대로 저장할 스트림 객체
			SendStream << EPacketType::SYNC_MONSTER << endl;	// 명령(정수)
			SendStream << MonstersInfo << endl;	// 명령에 따른 데이터

			count = 0;
			Broadcast(SendStream);		// 모든 연결된 게임 클라이언트에 전달
		}
		
		Sleep(33);  // 0.033초
	}
}

void MainIocp::InitializeMonsterSet()
{
	// 몬스터 4마리의 초기 위치, Health, Id 정보들을 서버에 초기화 한다.
	// 이 정보를 게임에 전달할 예정
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

/*IOCP의 스레드풀에서 호출하는 함수
여기에서 서버로 수신된 완료 패킷을 처리한다.

생성된 여러 개의 스레드들은 모두 이 함수의 작업을 진행한다.
*/
void MainIocp::WorkerThread()
{
	// 함수 호출 성공 여부
	BOOL	bResult;
	int		nResult;
	// Overlapped I/O 작업에서 전송된 데이터 크기
	DWORD	recvBytes;
	DWORD	sendBytes;
	// Completion Key를 받을 포인터 변수
	stSOCKETINFO *	pCompletionKey;
	// I/O 작업을 위해 요청한 Overlapped 구조체를 받을 포인터	
	stSOCKETINFO *	pSocketInfo;
	DWORD	dwFlags = 0;


	while (bWorkerThread)
	{
		/**
		 * 이 함수로 인해 쓰레드들은 WaitingThread Queue 에 대기상태로 들어가게 됨
		 * 완료된 Overlapped I/O 작업이 발생하면 IOCP Queue 에서 완료된 작업을 가져와
		 * 뒷처리를 함
		 */
		/*
		//(DWORD)SocketInfo => (PULONG_PTR)&pCompletionKey를 통해 받을 수 있다.
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
			&recvBytes,				// 실제로 수신된 바이트
			(PULONG_PTR)&pCompletionKey,	// completion key
			(LPOVERLAPPED *)&pSocketInfo,			// overlapped I/O 객체
			INFINITE				// 대기할 시간
		);
		
		// 결과가 false이고 수신byte가 0인경우 => 연결 끊어짐
		if (!bResult && recvBytes == 0)
		{
			printf_s("[INFO] socket(%d) 접속 끊김\n", pSocketInfo->socket);
			closesocket(pSocketInfo->socket);
			free(pSocketInfo);
			continue;
		}

		pSocketInfo->dataBuf.len = recvBytes;

		// 수신byte가 0인경우 => 연결 끊어짐
		if (recvBytes == 0)
		{
			printf_s("[INFO] socket(%d) 접속 close\n", pSocketInfo->socket);
			closesocket(pSocketInfo->socket);
			free(pSocketInfo);
			continue;
		}

		/*정상 수신되었으니까 패킷 처리
		
		UDP는 100byte전송 => 100byte수신
		      1byte전송 => 1byte전송
			  보낸 횟수와 수신 횟수가 동일하다.
			  다만 중간에 패킷 손실이 있을 수 있다.
		TCP는 100byte전송 => 100byte수신
		                  => 99byte수신, 1byte수신
						  => 80byte수신, 20byte수신
						  => 30byte수신, 57byte수신, 13byte수신
			  이렇게 보낸 횟수와 수신 횟수가 달라질 수 있다.
			  (특히 접속자가 많고, 서버와 클라이언트의 물리적 거리가 멀고,
			   데이터가 크고, 보내는 횟수가 빈번할 때 마다)

		이 사람의 서버는 TCP를 사용했는데 위처럼 1개의 패킷을 완성해주는 코드가 없다.
		1번 보내면 잘 받는 것처럼 믿음으로 처리했다.
		*/
		try
		{
			/*꺼낸 데이터를 정상패킷인지 확인하는 과정이 없다.
			정상패킷이 아닌 경우 정상패킷으로 만들어주는 코드가 들어가야 한다.

			단, 현재는 주로 테스트 환경이 Local이므로 큰 문제가 되지 않는다.
			*/

			// 패킷 종류
			int PacketType;
			// 클라이언트 정보 역직렬화
			stringstream RecvStream;

			// 수신데이터 배열 -> stringStream객체에 입력(꺼낼 때 편리하게 꺼내려고)
			RecvStream << pSocketInfo->dataBuf.buf;
			// 클라이언트가 보낸 순서대로 데이터를 꺼낼 수 있다.
			// 명령 + 데이터
			// 명령을 꺼냈기 때문에 RecvStream에는 데이터만 남아있다.
			// 데이터는 구조체 배열 함수포인터 변수와 연결된 함수를 통해 처리한다.
			RecvStream >> PacketType;	// 명령

			// 패킷 명령 숫자 -> 패킷 문자열
			printf_s("[Packet Command] %s\n", PacketMap[PacketType].c_str());

			/*
			* C/C++에서 enum형은 int와 상호호환이 된다.
			* PacketType에 들어있는 값에 따라 함수포인터가 함수의 주소를 저장해놓았으므로
			* 해당 패킷처리 함수가 호출되게 된다.
			* PacketType이 EPacketType::SIGNUP이면 SignUp함수가 호출된다.
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
			// 패킷 처리
			if (fnProcess[PacketType].funcProcessPacket != nullptr)
			{
				fnProcess[PacketType].funcProcessPacket(RecvStream, pSocketInfo);
			}
			else
			{
				printf_s("[ERROR] 정의 되지 않은 패킷 : %d\n", PacketType);
			}
		}
		catch (const std::exception& e)
		{
			printf_s("[ERROR] 알 수 없는 예외 발생 : %s\n", e.what());
		}

		/*수신이 끝났으므로 다시 IOCP로 클라이언트로부터 수신하기 위해 
		미리 비동기/Overlapped I/O로 호출하면 된다.
		IOCP큐에 등록은 처음에 WSARecv를 할 때 등록을 했으므로 추가 등록을 할 필요는 없다.
		*/
		// 클라이언트 대기
		Recv(pSocketInfo);
	}
}

/// <summary>
/// 클라이언트가 회원가입 요청시 처리하는 함수
/// </summary>
/// <param name="RecvStream">명령을 꺼내고 난 후 데이터</param>
/// <param name="pSocket">소켓 정보</param>
void MainIocp::SignUp(stringstream & RecvStream, stSOCKETINFO * pSocket)
{
	string Id;
	string Pw;

	// 클라이언트가 보낸 문자열을 순서대로 꺼낸다.
	RecvStream >> Id;
	RecvStream >> Pw;

	printf_s("[INFO] 회원가입 시도 {%s}/{%s}\n", Id.c_str(), Pw.c_str());

	// 클라이언트에 보낼 응답
	stringstream SendStream;
	SendStream << EPacketType::SIGNUP << endl;
	SendStream << Conn.SignUpAccount(Id, Pw) << endl;

	// streamstream객체를 char배열로 변환해서, char배열의 시작주소(char*)와 길이를 통해
	// pSocket->messageBuffer에 복사
	CopyMemory(pSocket->messageBuffer, (CHAR*)SendStream.str().c_str(), SendStream.str().length());
	pSocket->dataBuf.buf = pSocket->messageBuffer;
	pSocket->dataBuf.len = SendStream.str().length();

	// 이 패킷을 보내온 클라이언트한테 응답 패킷 전송
	Send(pSocket);
}

/// <summary>
/// 클라이언트가 로그인 요청시 처리하는 함수
/// </summary>
/// <param name="RecvStream">명령을 꺼내고 난 후 데이터</param>
/// <param name="pSocket">소켓 정보</param>
void MainIocp::Login(stringstream & RecvStream, stSOCKETINFO * pSocket)
{
	string Id;
	string Pw;

	RecvStream >> Id;
	RecvStream >> Pw;

	printf_s("[INFO] 로그인 시도 {%s}/{%s}\n", Id.c_str(), Pw.c_str());

	stringstream SendStream;
	SendStream << EPacketType::LOGIN << endl;
	SendStream << Conn.SearchAccount(Id, Pw) << endl;

	CopyMemory(pSocket->messageBuffer, (CHAR*)SendStream.str().c_str(), SendStream.str().length());
	pSocket->dataBuf.buf = pSocket->messageBuffer;
	pSocket->dataBuf.len = SendStream.str().length();

	Send(pSocket);
}

/// <summary>
/// 클라이언트가 ElevenRuins레벨에 진입하고 로컬 플레이어를 생성하고 등록 요청시 처리하는 함수
/// 1) CharactersInfo에 새로 진입한 플레이어 캐릭터 정보 등록
/// 2) SessionSocket에 소켓 정보 등록
/// 위 2개 모두 sessionId를 key값으로 한다.
/// </summary>
/// <param name="RecvStream">명령을 꺼내고 난 후 데이터</param>
/// <param name="pSocket">소켓 정보</param>
void MainIocp::EnrollCharacter(stringstream & RecvStream, stSOCKETINFO * pSocket)
{
	cCharacter info;
	RecvStream >> info;

	printf_s("[INFO][%d]캐릭터 등록 - X : [%f], Y : [%f], Z : [%f], Yaw : [%f], Alive : [%d], Health : [%f]\n",
		info.SessionId, info.X, info.Y, info.Z, info.Yaw, info.IsAlive, info.HealthValue);

	// CharactersInfo.players를 다른 스레드에서 동시에 사용할 수 있게 된다.
	// 그때 여러 개 스레드가 동시에 값을 변경하려고 시도할 때
	// 값의 왜곡이 발생할 수 있으므로, 1개의 스레드만 진입을 허용하는 임계영역을 설정한다.
	// 임계영역(ex. 화장실)
	EnterCriticalSection(&csPlayers);

	// 전체 플레이어 정보중에 내 sessionId에 해당하는 플레이어 정보 객체 주소
	/*info.SessionId를 players(map)객체에 key로 입력하면 cCharacter객체가 생성된다.
	이 생성된 cCharacter객체의 주소를 리턴하여 pinfo에 저장한다.
	*/
	//1) 1번째 등록
	cCharacter* pinfo = &CharactersInfo.players[info.SessionId];

	/*CharactersInfo의 멤버객체인 players의 cCharacter객체에 클라이언트로부터 수신한 캐릭터 정보를 저장한다.*/
	// 캐릭터의 위치를 저장						
	pinfo->SessionId = info.SessionId;
	pinfo->X = info.X;
	pinfo->Y = info.Y;
	pinfo->Z = info.Z;

	// 캐릭터의 회전값을 저장
	pinfo->Yaw = info.Yaw;
	pinfo->Pitch = info.Pitch;
	pinfo->Roll = info.Roll;

	// 캐릭터의 속도를 저장
	pinfo->VX = info.VX;
	pinfo->VY = info.VY;
	pinfo->VZ = info.VZ;

	// 캐릭터 속성
	pinfo->IsAlive = info.IsAlive;
	pinfo->HealthValue = info.HealthValue;
	pinfo->IsAttacking = info.IsAttacking;

	LeaveCriticalSection(&csPlayers);

	// 플레이어가 ElevenRuins필드에 진입해서 ENROLL_PLAYER를 서버에 보내면
	// 소켓 연결을 관리하는 map객체에 sessionId를 기준으로 소켓을 등록한다.
	/*서버에서는 이 단계에서 클라이언트가 세션으로 연결되었다고 등록함*/
	// 2) 2번째 등록
	SessionSocket[info.SessionId] = pSocket->socket;

	printf_s("[INFO] 클라이언트 수 : %d\n", SessionSocket.size());

	//Send(pSocket);

	// 연결된 모든 클라이언트한테 새로운 플레이어 정보를 전달함
	BroadcastNewPlayer(info);
}

/// <summary>
/// 클라이언트마다 보내오는 각각의 로컬 플레이어 Transform정보를 수신한다.
/// </summary>
/// <param name="RecvStream"></param>
/// <param name="pSocket"></param>
void MainIocp::SyncCharacters(stringstream& RecvStream, stSOCKETINFO* pSocket)
{
	cCharacter info;
	RecvStream >> info;

	// 	 	printf_s("[INFO][%d]정보 수신 - %d\n",
	// 	 		info.SessionId, info.IsAttacking);	
	EnterCriticalSection(&csPlayers);

	// 전체 플레이어들의 정보중에 내 정보를 찾아서 업데이트
	cCharacter * pinfo = &CharactersInfo.players[info.SessionId];

	// 캐릭터의 위치를 저장						
	pinfo->SessionId = info.SessionId;
	pinfo->X = info.X;
	pinfo->Y = info.Y;
	pinfo->Z = info.Z;

	// 캐릭터의 회전값을 저장
	pinfo->Yaw = info.Yaw;
	pinfo->Pitch = info.Pitch;
	pinfo->Roll = info.Roll;

	// 캐릭터의 속도를 저장
	pinfo->VX = info.VX;
	pinfo->VY = info.VY;
	pinfo->VZ = info.VZ;

	pinfo->IsAttacking = info.IsAttacking;

	LeaveCriticalSection(&csPlayers);

	// 자신의 로컬 플레이어 정보를 보내준 클라이언트한테
	// 현재 서버가 가지고 있는 모든 플레이어 전체 정보를 보내준다.
	WriteCharactersInfoToSocket(pSocket);
	Send(pSocket);
}

void MainIocp::LogoutCharacter(stringstream& RecvStream, stSOCKETINFO* pSocket)
{
	int SessionId;
	RecvStream >> SessionId;
	printf_s("[INFO] (%d)로그아웃 요청 수신\n", SessionId);
	EnterCriticalSection(&csPlayers);
	CharactersInfo.players[SessionId].IsAlive = false;
	LeaveCriticalSection(&csPlayers);
	SessionSocket.erase(SessionId);
	printf_s("[INFO] 클라이언트 수 : %d\n", SessionSocket.size());
	WriteCharactersInfoToSocket(pSocket);
}

void MainIocp::HitCharacter(stringstream & RecvStream, stSOCKETINFO * pSocket)
{
	// 피격 처리된 세션 아이디
	int DamagedSessionId;
	RecvStream >> DamagedSessionId;
	printf_s("[INFO] %d 데미지 받음 \n", DamagedSessionId);
	EnterCriticalSection(&csPlayers);
	CharactersInfo.players[DamagedSessionId].HealthValue -= HitPoint;
	if (CharactersInfo.players[DamagedSessionId].HealthValue < 0)
	{
		// 캐릭터 사망처리
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
	// 몬스터 피격 처리
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

	// 다른 플레이어에게 브로드캐스트
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

	// SessionSocket에 등록된 모든 클라이언트한테 새로운 플레이어 객체 정보를 전달함
	Broadcast(SendStream);
}

void MainIocp::Broadcast(stringstream & SendStream)
{
	/*IOCP통신에서 사용할 구조체
	 소켓, 저장버퍼, 전송길이, 수신길이... 정보가 담겨있다.
	 나중에 IOCP큐에서 꺼냈을 때 누구로부터, 얼마만큼, 어떤 데이터가 들어있는지
	 확인할 수 있다.
	*/
	stSOCKETINFO* client = new stSOCKETINFO;

	/*SessionSocket : 로그인 후 클라이언트가 필드에 등장했을 때 서버에
	                  해당 클라이언트를 등록할 때 저장하는 map객체
	  동기화하기 위해 접속한 클라이언트 저장 객체
	*/
	// 전체 클라이언트한테
	for (const auto& kvp : SessionSocket)
	{
		client->socket = kvp.second;  // 클라이언트와 연결된 소켓(통신 주체)
		/* stringStream객체인 SendStream->string -> char* (배열의 시작주소) : 데이터시작
		* SendStream.str().length() : 데이터 길이
		* 정보를 client->messageBuffer에 복사(char배열을 복사한다)
		*/
		CopyMemory(client->messageBuffer, (CHAR*)SendStream.str().c_str(), SendStream.str().length());
		
		// 저장 버퍼의 시작주소와 길이 정보를 통신 구조체에 전달
		client->dataBuf.buf = client->messageBuffer;
		client->dataBuf.len = SendStream.str().length();

		Send(client);
	}
}

void MainIocp::WriteCharactersInfoToSocket(stSOCKETINFO * pSocket)
{
	stringstream SendStream;

	// 직렬화	
	SendStream << EPacketType::RECV_PLAYER << endl;
	SendStream << CharactersInfo << endl;		// 모든 플레이어들의 정보

	// !!! 중요 !!! data.buf 에다 직접 데이터를 쓰면 쓰레기값이 전달될 수 있음
	CopyMemory(pSocket->messageBuffer, (CHAR*)SendStream.str().c_str(), SendStream.str().length());
	pSocket->dataBuf.buf = pSocket->messageBuffer;
	pSocket->dataBuf.len = SendStream.str().length();
}

