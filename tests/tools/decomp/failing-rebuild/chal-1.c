#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define serial_print(...) \
  do {                    \
    printf(__VA_ARGS__);  \
  } while (0)
#define serial_println(...) \
  do {                      \
    printf(__VA_ARGS__);    \
    printf("\n");           \
  } while (0)
bool brake_state = false;
bool need_to_flash = false;
bool previous_brake_state = false;
void brake_on() { serial_println("%s called", __FUNCTION__); }
void brake_off() { serial_println("%s called", __FUNCTION__); }
void rx_message_routine(unsigned char buf[]) {
  int16_t speed_value = (((int16_t)buf[3]) << 8) + buf[2];
  uint8_t brake_switch = (buf[4] & 0b00001100) >> 2;
  serial_print("%s", " Speed = ");
  serial_print("%d", speed_value / 256);
  serial_print("%s", ", brake =");
  serial_print("%d", brake_switch);
  serial_println("%s", "]");
  if (brake_switch) {
    brake_state = true;
    brake_on();
    if (speed_value > 0 &&
        previous_brake_state !=
            brake_state) {  // speed > 0 and brakes were off last
      need_to_flash = true;
      serial_println("%s", "Flashing=true");
    }
  } else {
    brake_state = false;
    need_to_flash = false;
    brake_off();
  }
  previous_brake_state = brake_state;
}
int main(int argc, const char *argv[]) {
  // default input
  unsigned char buf[8] = {0, 1, 2, 3, 4, 5, 6, 7};
  // optional input
  for (int i = 1; i < argc && i < sizeof(buf) / sizeof(buf[0]); i++) {
    buf[i] = (unsigned char)atoi(argv[i]);
  }
  rx_message_routine(buf);
  return 0;
}