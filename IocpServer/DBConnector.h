#pragma once
#include "mysql.h"
#include <string>

using namespace std;

//속성 > 링커 > 입력 > 추가종속성에서 등록을 안해주면 아래처럼 코드로 추가해도 된다.
#pragma comment(lib, "libmysql.lib")

class DBConnector
{
public:
	DBConnector();
	~DBConnector();

	// DB 에 연결
	bool Connect(
		const string&	Server,
		const string&	User,
		const string&	Password,
		const string&	Database,
		const int&		Port
	);
	// DB 연결 종료
	void Close();
	
	// 유저 계정을 찾음
	bool SearchAccount(const string& Id, const string& Password);
	// 유저 계정을 등록
	bool SignUpAccount(const string& Id, const string& Password);

private:
	MYSQL * Conn;		// 커넥터
	MYSQL_RES * Res;	// 결과값
	MYSQL_ROW Row;		// 결과 row
};

