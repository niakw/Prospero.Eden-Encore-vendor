#include <stdint.h>

#include "gnmdriver.h"

int main(void) {
	uint32_t cmd[7] = {0};
	return sceGnmDrawIndexAuto(cmd, 7, 3, 0);
}
