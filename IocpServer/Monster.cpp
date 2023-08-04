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
	// ���� id �Ҵ�
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
		// 1�ʿ� �ѹ��� ��������		
		bIsAttacking = true;
		printf_s("����\n");
		target.HealthValue -= HitPoint;
		std::this_thread::sleep_for(1s); // 1�ʵ��� ����
		bIsAttacking = false;		
	});

	/*Main Thread�� �и��Ѵ�.
	���� ������� Ư�� �����忡�� �ٸ� �����带 ����� �θ�-�ڽ� ���谡 �����ȴ�.
	�̶� �θ� �����尡 �����ϸ� �ڽ� �����嵵 �����ϰ� �ȴ�.
	�׷���, ���� �θ� �����尡 �����ص� �ڽ� ������� ��� �����ϰ� �Ϸ���
	�θ�κ��� �и��ǵ��� �ؾ� �Ѵ�.
	C#������ �и��� �� IsBackground = false�� ���ְ�
	C++�� std::thread�� t.detach()�� ����ؼ� �θ� �������� ���࿩�ο� ������� 
	������ �����ϵ��� �Ѵ�.
	*/
	t.detach();
}

void Monster::Damaged(float damage)
{	
	Health -= damage;
	printf_s("���� ���� ü�� : %f\n", Health);
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