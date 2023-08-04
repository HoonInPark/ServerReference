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
#include "DBConnector.h"
#include "IocpBase.h"
#include "Monster.h"

using namespace std;

// DB 정보
#define DB_ADDRESS		"localhost"
#define	DB_PORT			3306
#define DB_ID			"MetaUnreal"
#define DB_PW			"123456"
#define DB_SCHEMA		"MetaUnreal"

class MainIocp : public IocpBase
{
public:
	MainIocp();
	virtual ~MainIocp();
	
	virtual void StartServer() override;
	// 작업 스레드 생성
	virtual bool CreateWorkerThread() override;
	// 작업 스레드
	virtual void WorkerThread() override;
	// 클라이언트에게 송신
	static void Send(stSOCKETINFO * pSocket);	

	// 몬스터 스레드
	void CreateMonsterManagementThread();
	void MonsterManagementThread();

	static map<int, string> PacketMap;

private:
	static cCharactersInfo	CharactersInfo;	// 접속한 클라이언트의 정보를 저장	
	static map<int, SOCKET> SessionSocket;	// 세션별 소켓 저장
	static float			HitPoint;		// 타격 데미지
	static DBConnector 		Conn;			// DB 커넥터

	/*
	* 여러 스레드가 1개의 변수를 동시에 접근하면 값이 왜곡될 위험이 있다.
	* 값의 왜곡을 막기 위해서는 1개 변수의 연산을 1개 스레드만 할 수 있도록
	* 해줘야 한다.
	* C#에서의 lock()이나 Monitor클래스의 역할과 같다.
	* C/C++에서는 멀티스레드 상황에서 1개 스레드만 진입하게 하려면
	* 크리티컬 섹션이나 뮤텍스를 사용한다.
	* - 크리티컬 섹션 : 임계영역을 동기화할 때 사용
	*                 유저모드 오브젝트이므로 1개 프로세스내의 
	*                 멀티스레드 임계영역 동기화 시 사용
	* - 뮤텍스 : 임계영역을 동기화할 때 사용
	*            커널모드 오브젝트이므로 여러 개 프로세스의
	*            멀티스레드 임계영역 동기화 시 사용
	* 
	* CRITICAL_SECTION 구조체의 변수를 사용해서 
	* EnterCriticalSection(&csPlayers);
	*	임계영역
	* LeaveCriticalSection(&csPlayers);
	* 
	* // {}영역에는 2개의 스레드가 동시진입 불가
	 lock(this)
	 {
		임계영역
	 }

	 Monitor.Enter(this);
		임계영역
	 Monitor.Exit(this);
	*/
	static CRITICAL_SECTION	csPlayers;		// CharactersInfo 임계영역

	FuncProcess				fnProcess[100];	// 패킷 처리 구조체
	HANDLE*					MonsterHandle;	// 몬스터 스레드 핸들러
	static MonsterSet		MonstersInfo;	// 몬스터 집합 정보

	// 회원가입
	static void SignUp(stringstream & RecvStream, stSOCKETINFO * pSocket);
	// DB에 로그인
	static void Login(stringstream & RecvStream, stSOCKETINFO * pSocket);
	// 캐릭터 초기 등록
	static void EnrollCharacter(stringstream & RecvStream, stSOCKETINFO * pSocket);
	// 캐릭터 위치 동기화
	static void SyncCharacters(stringstream & RecvStream, stSOCKETINFO * pSocket);
	// 캐릭터 로그아웃 처리
	static void LogoutCharacter(stringstream & RecvStream, stSOCKETINFO * pSocket);
	// 캐릭터 피격 처리
	static void HitCharacter(stringstream & RecvStream, stSOCKETINFO * pSocket);
	// 채팅 수신 후 클라이언트들에게 송신
	static void BroadcastChat(stringstream & RecvStream, stSOCKETINFO * pSocket);
	// 몬스터 피격 처리
	static void HitMonster(stringstream & RecvStream, stSOCKETINFO * pSocket);

	// 브로드캐스트 함수
	static void Broadcast(stringstream & SendStream);	
	// 다른 클라이언트들에게 새 플레이어 입장 정보 보냄
	static void BroadcastNewPlayer(cCharacter & player);
	// 캐릭터 정보를 버퍼에 기록
	static void WriteCharactersInfoToSocket(stSOCKETINFO * pSocket);		
	
	// 몬스터 정보 초기화
	void InitializeMonsterSet();
};
