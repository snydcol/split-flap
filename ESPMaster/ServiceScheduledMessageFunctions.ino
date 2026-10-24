/**
 * Scheduled-message service.
 *
 * Messages are kept in memory while the device is running and serialized to
 * scheduled-messages.txt whenever a change is persisted. A timestamp is the
 * message key: adding another message for the same timestamp replaces the
 * existing one.
 */

/**
 * Adds or replaces a scheduled message and immediately persists the list.
 */
void addAndPersistScheduledMessage(String scheduledText, long scheduledDateTimeUnix, bool showIndefinitely) {
  addScheduledMessage(scheduledText, scheduledDateTimeUnix, showIndefinitely);
  writeScheduledMessagesToFile();
}

/**
 * Adds a future message, replacing any existing message at the same timestamp.
 *
 * Timestamps in the past are deliberately ignored so stale entries loaded
 * after a reboot cannot be re-added to the queue.
 */
void addScheduledMessage(String scheduledText, long scheduledDateTimeUnix, bool showIndefinitely) {
  SerialPrintln("Processing New Scheduled Message");
  SerialPrintln("-- Message: " + scheduledText);
  SerialPrint("-- Unix: ");
  SerialPrintln(scheduledDateTimeUnix);
  SerialPrint("-- Show Indefinitely: ");
  SerialPrintln(showIndefinitely ? "Yes" : "No");

  //Find the existing scheduled message and update it if one exists
  for(int scheduledMessageIndex = 0; scheduledMessageIndex < scheduledMessages.size(); scheduledMessageIndex++) {
    ScheduledMessage scheduledMessage = scheduledMessages[scheduledMessageIndex];

    if (scheduledDateTimeUnix == scheduledMessage.ScheduledDateTimeUnix) {
      SerialPrintln("Removing Existing Scheduled Message due to be shown, it will be replaced");
      scheduledMessages.remove(scheduledMessageIndex);
      break;
    }
  }

  //Add the new scheduled message with the new value
  if (scheduledDateTimeUnix > timezone.now()) {
    SerialPrintln("Adding new Scheduled Message");
    scheduledMessages.add({scheduledText, scheduledDateTimeUnix, showIndefinitely});
  }
  else {
    SerialPrintln("Not adding Scheduled Message as it is in the past");
  }
}

/**
 * Removes and persists the message identified by its scheduled timestamp.
 *
 * Returns true when a matching message was found.
 */
bool removeScheduledMessage(long scheduledDateTimeUnix) {
  //Find the existing scheduled message and delete it
  for(int scheduledMessageIndex = 0; scheduledMessageIndex < scheduledMessages.size(); scheduledMessageIndex++) {
    ScheduledMessage scheduledMessage = scheduledMessages[scheduledMessageIndex];

    if (scheduledDateTimeUnix == scheduledMessage.ScheduledDateTimeUnix) {
      SerialPrintln("Deleting Scheduled Message due to be shown: " + scheduledMessage.Message);
      scheduledMessages.remove(scheduledMessageIndex);
      
      writeScheduledMessagesToFile();

      return true;
    }
  }
  
  return false;
}

/**
 * Displays the first message whose scheduled time has passed.
 *
 * Indefinite messages switch the device to text mode and remain on screen.
 * Temporary messages use scheduledMessageDisplayTimeMillis and then allow the
 * normal mode loop to resume.
 */
void checkScheduledMessages() {   
  //Find the first message whose scheduled time has passed and show it now.
  unsigned long currentTimeUnix = timezone.now();
  for(int scheduledMessageIndex = 0; scheduledMessageIndex < scheduledMessages.size(); scheduledMessageIndex++) {
    ScheduledMessage scheduledMessage = scheduledMessages[scheduledMessageIndex];

    if (currentTimeUnix > scheduledMessage.ScheduledDateTimeUnix) {
      SerialPrintln("Scheduled Message due to be shown: " + scheduledMessage.Message);
      SerialPrint("Scheduled Message to be Shown Indefinitely: ");
      SerialPrintln(scheduledMessage.ShowIndefinitely ? "Yes" : "No");

      if (scheduledMessage.ShowIndefinitely) {
        deviceMode = DEVICE_MODE_TEXT;
        inputText = scheduledMessage.Message;
        showText(scheduledMessage.Message);
      }
      else {
        showText(scheduledMessage.Message, scheduledMessageDisplayTimeMillis);
      }      

      scheduledMessages.remove(scheduledMessageIndex);
      writeScheduledMessagesToFile();
      break;
    }
  }
}

/**
 * Parses the persisted JSON array and loads future messages into memory.
 *
 * Invalid JSON or a non-array value clears the stored schedule so the device
 * can recover on the next boot.
 */
void readScheduledMessagesFromJson(String scheduledMessagesJson) {
  if (scheduledMessagesJson != "") {    
    int addedScheduledMessageCount = 0;

    JsonDocument jsonDocument;
    DeserializationError deserialisationError = deserializeJson(jsonDocument, scheduledMessagesJson);

    if (!deserialisationError) {
      if (jsonDocument.is<JsonArray>()) {
        for (JsonVariant value : jsonDocument.as<JsonArray>()) {
          long scheduledDateTimeUnix = value["scheduledDateTimeUnix"];
          String message = value["message"];
          bool showIndefinitely = value["showIndefinitely"];

          addScheduledMessage(message, scheduledDateTimeUnix, showIndefinitely);
          addedScheduledMessageCount++;
        }
        
        //If there is a difference in scheduled messages, re-write the messages
        if (addedScheduledMessageCount != scheduledMessages.size()) {
          SerialPrintln("Read message count and scheduled message count differ, writing updated messages");
          writeScheduledMessagesToFile();
        }
      } 
      else {
        SerialPrintln("Invalid JSON Array found, scrapping and starting again");
        writeEmptyScheduledMessagesToFile();
      }
    }
    else {
      SerialPrintln("Invalid JSON found, scrapping and starting again");
      writeEmptyScheduledMessagesToFile();
    }
  }
}

/**
 * Serializes the in-memory schedule to the LittleFS JSON file.
 */
void writeScheduledMessagesToFile() {
  if (scheduledMessages.size()) {
    JsonDocument scheduledMessagesDocument;
    for(int scheduledMessageIndex = 0; scheduledMessageIndex < scheduledMessages.size(); scheduledMessageIndex++) {
      ScheduledMessage scheduledMessage = scheduledMessages[scheduledMessageIndex];
      
      scheduledMessagesDocument[scheduledMessageIndex]["scheduledDateTimeUnix"] = scheduledMessage.ScheduledDateTimeUnix;
      scheduledMessagesDocument[scheduledMessageIndex]["message"] = scheduledMessage.Message;
      scheduledMessagesDocument[scheduledMessageIndex]["showIndefinitely"] = scheduledMessage.ShowIndefinitely;
    }

    String scheduledMessagesJson;
    serializeJson(scheduledMessagesDocument, scheduledMessagesJson);
    writeFile(LittleFS, scheduledMessagesPath, scheduledMessagesJson.c_str());
  }
  else {
    SerialPrintln("No Scheduled Messages Left - Writing Empty to File");
    writeEmptyScheduledMessagesToFile();
  }
}

/**
 * Replaces the schedule file with an empty JSON array.
 */
void writeEmptyScheduledMessagesToFile() {
  writeFile(LittleFS, scheduledMessagesPath, "[]");
}
