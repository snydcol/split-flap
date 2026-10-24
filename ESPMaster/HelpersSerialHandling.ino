/**
 * Centralized serial logging helpers.
 *
 * Keeping the SERIAL_ENABLE check in this file means the rest of the sketch
 * can log useful diagnostics without repeating preprocessor guards at every
 * call site. When serial logging is disabled, these wrappers compile to no-op
 * functions and leave the I2C communication path available to the ESP-01.
 */

/**
 * Writes a value without adding a line ending when serial logging is enabled.
 */
template <typename T>
void SerialPrint(T value) {
#if SERIAL_ENABLE == true
    Serial.print(value);
#endif
}

/**
 * Writes a printf-style message and one value when serial logging is enabled.
 *
 * The format string must use the placeholders supported by the ESP8266
 * Serial.printf implementation.
 */
template <typename T>
void SerialPrintf(const char* message, T value) {
#if SERIAL_ENABLE == true
    Serial.printf(message, value);
#endif
}

/**
 * Writes a value followed by a line ending when serial logging is enabled.
 */
template <typename T>
void SerialPrintln(T value) {
#if SERIAL_ENABLE == true
    Serial.println(value);
#endif
}
