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
#include "DBConnector.h"
#include "IocpBase.h"
#include "Monster.h"

using namespace std;

// DB ����
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
	// �۾� ������ ����
	virtual bool CreateWorkerThread() override;
	// �۾� ������
	virtual void WorkerThread() override;
	// Ŭ���̾�Ʈ���� �۽�
	static void Send(stSOCKETINFO * pSocket);	

	// ���� ������
	void CreateMonsterManagementThread();
	void MonsterManagementThread();

	static map<int, string> PacketMap;

private:
	static cCharactersInfo	CharactersInfo;	// ������ Ŭ���̾�Ʈ�� ������ ����	
	static map<int, SOCKET> SessionSocket;	// ���Ǻ� ���� ����
	static float			HitPoint;		// Ÿ�� ������
	static DBConnector 		Conn;			// DB Ŀ����

	/*
	* ���� �����尡 1���� ������ ���ÿ� �����ϸ� ���� �ְ�� ������ �ִ�.
	* ���� �ְ��� ���� ���ؼ��� 1�� ������ ������ 1�� �����常 �� �� �ֵ���
	* ����� �Ѵ�.
	* C#������ lock()�̳� MonitorŬ������ ���Ұ� ����.
	* C/C++������ ��Ƽ������ ��Ȳ���� 1�� �����常 �����ϰ� �Ϸ���
	* ũ��Ƽ�� �����̳� ���ؽ��� ����Ѵ�.
	* - ũ��Ƽ�� ���� : �Ӱ迵���� ����ȭ�� �� ���
	*                 ������� ������Ʈ�̹Ƿ� 1�� ���μ������� 
	*                 ��Ƽ������ �Ӱ迵�� ����ȭ �� ���
	* - ���ؽ� : �Ӱ迵���� ����ȭ�� �� ���
	*            Ŀ�θ�� ������Ʈ�̹Ƿ� ���� �� ���μ�����
	*            ��Ƽ������ �Ӱ迵�� ����ȭ �� ���
	* 
	* CRITICAL_SECTION ����ü�� ������ ����ؼ� 
	* EnterCriticalSection(&csPlayers);
	*	�Ӱ迵��
	* LeaveCriticalSection(&csPlayers);
	* 
	* // {}�������� 2���� �����尡 �������� �Ұ�
	 lock(this)
	 {
		�Ӱ迵��
	 }

	 Monitor.Enter(this);
		�Ӱ迵��
	 Monitor.Exit(this);
	*/
	static CRITICAL_SECTION	csPlayers;		// CharactersInfo �Ӱ迵��

	FuncProcess				fnProcess[100];	// ��Ŷ ó�� ����ü
	HANDLE*					MonsterHandle;	// ���� ������ �ڵ鷯
	static MonsterSet		MonstersInfo;	// ���� ���� ����

	// ȸ������
	static void SignUp(stringstream & RecvStream, stSOCKETINFO * pSocket);
	// DB�� �α���
	static void Login(stringstream & RecvStream, stSOCKETINFO * pSocket);
	// ĳ���� �ʱ� ���
	static void EnrollCharacter(stringstream & RecvStream, stSOCKETINFO * pSocket);
	// ĳ���� ��ġ ����ȭ
	static void SyncCharacters(stringstream & RecvStream, stSOCKETINFO * pSocket);
	// ĳ���� �α׾ƿ� ó��
	static void LogoutCharacter(stringstream & RecvStream, stSOCKETINFO * pSocket);
	// ĳ���� �ǰ� ó��
	static void HitCharacter(stringstream & RecvStream, stSOCKETINFO * pSocket);
	// ä�� ���� �� Ŭ���̾�Ʈ�鿡�� �۽�
	static void BroadcastChat(stringstream & RecvStream, stSOCKETINFO * pSocket);
	// ���� �ǰ� ó��
	static void HitMonster(stringstream & RecvStream, stSOCKETINFO * pSocket);

	// ��ε�ĳ��Ʈ �Լ�
	static void Broadcast(stringstream & SendStream);	
	// �ٸ� Ŭ���̾�Ʈ�鿡�� �� �÷��̾� ���� ���� ����
	static void BroadcastNewPlayer(cCharacter & player);
	// ĳ���� ������ ���ۿ� ���
	static void WriteCharactersInfoToSocket(stSOCKETINFO * pSocket);		
	
	// ���� ���� �ʱ�ȭ
	void InitializeMonsterSet();
};