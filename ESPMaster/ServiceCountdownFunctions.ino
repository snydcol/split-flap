/**
 * Countdown-mode service.
 *
 * The configured Unix timestamp is converted into whole hours and then into a
 * whole number of days. Partial 24-hour periods round up, and expired
 * countdowns stop at zero.
 */

/**
 * Recalculates the countdown text when countdown mode is active.
 */
void checkCountdown() {
  if (deviceMode == DEVICE_MODE_COUNTDOWN) {
    long countdownInSeconds = atol(countdownToDateUnix.c_str());

    //Work out how long left
    long currentTimeSeconds = timezone.now();
    long differenceSeconds = countdownInSeconds - currentTimeSeconds;
    long hours = differenceSeconds / 60 / 60;

    //If there is any remainder, we want to actually still display the full day remaining 
    long days = ceil(hours / 24.0);

    //Make sure we don't go negative
    days = days > 0 ? days : 0;
    
    String daysText = String(days) + (days == 1 ? " day" : " days");
    if (daysText.length() > UNITS_AMOUNT) {
      SerialPrintln("Days Text was too long, cutting down to just the number...");
      daysText = "" + days;
    }

    if (inputText != daysText) {
      SerialPrintln("Setting Countdown Text to: " + daysText);
      inputText = daysText;
    }
  }
}