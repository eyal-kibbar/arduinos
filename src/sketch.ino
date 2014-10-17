#include "arduinos.h"
#include "limits.h"

struct test_t {
	const char* name;
	int (*test_func)();
};

//-----------------------------------------------------------------------------
// SimpleJoin
//-----------------------------------------------------------------------------
static int SimpleJoin_func(int num)
{
	Serial.print(" num addr = ");
	Serial.println((int)&num);
	return num + 1;
}

static int SimpleJoin_test()
{
	cid context_id;
	int num = random(INT_MAX);
	int ret, rc;

	if ((context_id = arduinos_create((context_start_func)SimpleJoin_func, (void*)num)) < 0) {
		Serial.println(" error: unable to create new context");
		return context_id;
	}
	
	if ((rc = arduinos_join(context_id, &ret))) {
		Serial.println(" error: unable to join the created context");
		return rc;
	}

	if ((num + 1) != ret) {
		Serial.println(" error: got an unexpected value from context func");
		return -1;
	}

	return 0;
}


int arduinos_test_suite(void* arg)
{
	static const struct test_t tests[] = {
		{ "SimpleJoin", SimpleJoin_test },
	};
	int i, ret;

	for (i=0; i < sizeof tests / sizeof *tests; ++i) {

		Serial.print("Running ");
		Serial.println(tests[i].name);
		if ((ret = tests[i].test_func())) {
			Serial.print("TEST FAILED: ");
			Serial.println(ret);
		} else {
			Serial.println("PASSED");
		}

	}
}



void setup()
{
	Serial.begin(9600);
	int seed = analogRead(0);
	randomSeed(seed);
	Serial.print("Arduinos test suite, seed = ");
	Serial.println(seed);
	Serial.println();
	arduinos_setup();
	arduinos_create(arduinos_test_suite, NULL);
}

void loop()
{
	arduinos_loop();
}


