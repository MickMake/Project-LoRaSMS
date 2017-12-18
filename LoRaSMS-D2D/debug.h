// #define DEBUG

#ifdef DEBUG
#define PRINTLN(fmt, ...)
#define PRINT(fmt, ...)
#else
#define PRINTLN(fmt, ...)	Serial.println(fmt, ##__VA_ARGS__)
#define PRINT(fmt, ...)		Serial.print(fmt, ##__VA_ARGS__)
#endif

