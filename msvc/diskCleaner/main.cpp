#include "diskCleaner.h"
#include <chrono>
#include <thread>
#include "logger.h";
#include "kvIni.h"


void sleep_ms(unsigned int milliseconds) {
	std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

int main(int argc, char** argv)
{
	diskCleaner.run();

	while (1) {
		sleep_ms(1000);
	}
}