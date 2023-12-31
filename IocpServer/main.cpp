// iocp-sample.cpp: 콘솔 응용 프로그램의 진입점을 정의합니다.
//

#include "stdafx.h"
#include "MainIocp.h"

int main()
{
	/*부모인 IocpBase() 생성자 호출 후
	자식인 MainIocp() 생성자 호출*/
	MainIocp iocp_server;

	/*
	IocpBase의 initialize()를 호출
	만약 IocpBase를 상속받은 MainIocp에도 Initialize()가 있었다면
	부모의 함수를 덮어썼으므로 MainIocp::Initialize()가 호출되었을 텐데
	없으므로 IocpBase::Initialize()를 호출하게 된다.
	*/
	if (iocp_server.Initialize())
	{
		/* StartServer()는 MainIocp의 StartServer()를 호출한다.
		* Main 스레드는 StartServer() 내부에서 무한루프를 반복하면서
		* 새로 접속하는 Client의 Accept처리와 IOCP통신을 하기 위한 등록을 해준다.
		*/
		/*MainIocp의 객체이므로 MainIocp::StartServer()가 호출됨*/
		iocp_server.StartServer();
	}
    return 0;
}

