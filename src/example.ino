#include "arduinos.h"


struct arduinos_semaphore_t sem;


int f1(void* arg)
{
	Serial.print("f1 arg = ");
	Serial.print((int)arg);
	Serial.print(" cid = ");
	Serial.println((int)arduinos_self());
		
	arduinos_delay(10000);
	arduinos_semaphore_signal(&sem);

	arduinos_delay(10000);
	Serial.println("should never be here");
	return 0;
}

int f2(void* arg)
{
	int ret;
	Serial.print("f1 arg = ");
	Serial.print((int)arg);
	Serial.print(" cid = ");
	Serial.println((int)arduinos_self());

	arduinos_delay(1000);

	// wait for f1 to signal
	arduinos_semaphore_wait(&sem);

	Serial.println("killing f1");

	arduinos_kill((cid)arg);
	arduinos_join((cid)arg, &ret);

	Serial.println("f1 is dead");
	while (1);
	return 0;
}


void setup()
{
	cid x;
	Serial.begin(9600);
	Serial.println("starting");
	arduinos_setup();
	arduinos_semaphore_init(&sem, 0);
	Serial.println("creating f1");
	x = arduinos_create(f1, (void*)8);
	Serial.println("creating f2");
	arduinos_create(f2, (void*)x);

}

void loop()
{
	Serial.println("loop");
	arduinos_loop();
}



