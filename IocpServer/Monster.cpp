#include "stdafx.h"
#include "Monster.h"
#include <thread>


Monster::Monster()
	:X(0), Y(0), Z(0),
	Health(0), MovePoint(5), HitPoint(0.1f),
	TraceRange(700), HitRange(180),
	bIsAttacking(false),
	bIsTracking(false)
{
	// 고유 id 할당
}


Monster::~Monster()
{
}

void Monster::MoveTo(const cCharacter & target)
{	
	if (target.X > X)
		X += MovePoint;
	if (target.X < X)
		X -= MovePoint;
	if (target.Y > Y)
		Y += MovePoint;
	if (target.Y < Y)
		Y -= MovePoint;	
	if (target.Z > Z)
		Z += MovePoint;
	if (target.Z < Z)
		Z -= MovePoint;
}

void Monster::HitPlayer(cCharacter & target)
{
	std::thread t([&]() {
		// 1초에 한번씩 때리도록		
		bIsAttacking = true;
		printf_s("때림\n");
		target.HealthValue -= HitPoint;
		std::this_thread::sleep_for(1s); // 1초동안 멈춤
		bIsAttacking = false;		
	});

	/*Main Thread와 분리한다.
	원래 스레드는 특정 스레드에서 다른 스레드를 만들면 부모-자식 관계가 형성된다.
	이때 부모 스레드가 종료하면 자식 스레드도 종료하게 된다.
	그런데, 만약 부모 스레드가 종료해도 자식 스레드는 계속 동작하게 하려면
	부모로부터 분리되도록 해야 한다.
	C#에서는 분리할 때 IsBackground = false를 해주고
	C++의 std::thread는 t.detach()를 사용해서 부모 스레드의 실행여부와 관계없이 
	별도로 동작하도록 한다.
	*/
	t.detach();
}

void Monster::Damaged(float damage)
{	
	Health -= damage;
	printf_s("맞음 남은 체력 : %f\n", Health);
}

bool Monster::IsAlive()
{
	if (Health <= 0)
		return false;

	return true;
}

bool Monster::IsAttacking()
{
	return bIsAttacking;
}

bool Monster::IsPlayerInTraceRange(const cCharacter & target)
{
	if (abs(target.X - X) < TraceRange && abs(target.Y - Y) < TraceRange)
		return true;

	return false;
}

bool Monster::IsPlayerInHitRange(const cCharacter & target)
{
	if (abs(target.X - X) < HitRange && abs(target.Y - Y) < HitRange)
		return true;

	return false;
}

void Monster::SetLocation(float x, float y, float z)
{
	X = x;
	Y = y;
	Z = z;
}
