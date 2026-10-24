/**
 * Text formatting helpers used before messages are sent to the flap units.
 *
 * The physical display has exactly UNITS_AMOUNT positions. These helpers
 * pad messages with spaces so callers can apply the selected alignment before
 * the characters are translated to flap positions.
 */

/**
 * Centers a message across the available display positions and pads it.
 */
String centerString(String message) {
  //Add the left-hand padding first so the remaining space can be added right.
  int leftSpaceAmount = (UNITS_AMOUNT -  message.length()) / 2;
  for (int spaceIndex = 0; spaceIndex < leftSpaceAmount; spaceIndex++) {
    message = " " + message;
  }

  //Pad the right-hand side until the message fills the display.
  for(int spaceIndex = message.length(); spaceIndex < UNITS_AMOUNT; spaceIndex++) {
    message = message + " ";
  }

  message = cleanString(message);
  
  return message;
}

/**
 * Creates a display-width string containing the same character in every slot.
 */
String createRepeatingString(char character) {
  String newMessage = "";
  for (int unitIndex = 0; unitIndex < UNITS_AMOUNT; unitIndex++) {
    newMessage.concat(character);
  }

  return newMessage;  
}

/**
 * Right-aligns a message and pads the unused positions with spaces.
 */
String rightString(String message) {
  int rightSpaceAmount = (UNITS_AMOUNT - message.length());
  for (int spaceIndex = 0; spaceIndex < rightSpaceAmount; spaceIndex++) {
    message = " " + message;
  }

  message = cleanString(message);

  return message;
}

/**
 * Left-aligns a message and pads the unused positions with spaces.
 */
String leftString(String message) {
  int leftSpaceAmount = (UNITS_AMOUNT - message.length());
  for (int spaceIndex = 0; spaceIndex < leftSpaceAmount; spaceIndex++) {
    message = message + " ";
  }

  message = cleanString(message);

  return message;
}

/**
 * Normalizes a message to uppercase for the configured flap alphabet.
 */
String cleanString(String message) {
  message.toUpperCase();
  
  return message;
}

/**
 * Converts the UI's 1-100 speed value to the unit's configured speed range.
 */
int convertSpeed(String flapSpeed) {
  int flapSpeedInt;
  flapSpeedInt = flapSpeed.toInt();
  flapSpeedInt = map(flapSpeedInt, 1, 100, MIN_SPEED, MAX_SPEED);
  
  return flapSpeedInt;
}

/**
 * Normalizes a sentence and wraps it into display-width lines.
 *
 * Explicit "\\n" sequences become line breaks. Words are kept together where
 * possible, long words are split at the display width, and a hyphen is added
 * when a word must continue on the next line.
 */
LList<String> processSentenceToLines(String sentence) {    
  SerialPrintln("Processing Sentence to lines");
  
  //Remove whitespace around the complete sentence.
  sentence.trim();
  
  //The web form sends a literal "\n" sequence for a requested line break.
  sentence.replace("\\n", "\n");

  //Do not create an empty first line.
  if (sentence.startsWith("\n")) {
    sentence = sentence.substring(1);
  }

  //Collapse repeated line breaks to one line break.
  while(sentence.indexOf("\n\n") != -1) 
  {
    sentence.replace("\n\n", "\n");
  }

  //Remove spaces immediately before a line break.
  while(sentence.indexOf(" \n") != -1) 
  {
    sentence.replace(" \n", "\n");
  }

  //Avoid creating extra display columns from repeated spaces.
  while(sentence.indexOf("  ") != -1) 
  {
    sentence.replace("  ", " ");
  }
    
  //Split the sentence into words while retaining embedded line breaks.
  LList<String> words; 
  while (sentence.length() > 0)
  {
    //Find the next word boundary.
    int indexOfSpace = sentence.indexOf(' ');

    //If there is no remaining space, the rest is the final word.
    if (indexOfSpace == -1)
    {
      words.add(sentence);
      break;
    }
    else
    {
      words.add(sentence.substring(0, indexOfSpace));
      sentence = sentence.substring(indexOfSpace + 1);
    }
  }

  //Build display-width lines from the normalized words.
  LList<String> lines;
  String inProgressLine;
  while(words.size() != 0) 
  {
    //Always process the first word; it is removed after processing below.
    String wordItem = words[0];

    //A newline terminates the current line, so only measure the first segment.
    int indexOfFirstNewline = wordItem.indexOf('\n');

    //A word beginning with a newline should start a new line immediately.
    if (indexOfFirstNewline != 0) 
    {
      //Check whether the next word segment fits on the current line.
      int wordLength = indexOfFirstNewline == -1 ? wordItem.length() : indexOfFirstNewline;

      //Flush the current line before adding a word that would exceed it.
      if (inProgressLine != "" && inProgressLine.length() + wordLength + 1 > UNITS_AMOUNT)
      {
          lines.add(inProgressLine);
          inProgressLine = "";
      }

      //Insert the separator only between words already on this line.
      if (inProgressLine.length() > 0)
      {
        inProgressLine = inProgressLine + " ";
      }
    }

    //Copy the word into the current line, honoring explicit line breaks.
    int currentWordLength = wordItem.length();
    for(int letterIndex = 0; letterIndex < currentWordLength; letterIndex++)
    {
      char wordItemLetter = wordItem[letterIndex];

      if (wordItemLetter == '\n')
      {
        lines.add(inProgressLine);
        inProgressLine = "";
      }
      else
      {
        //Split an overlong word and mark the continuation with a hyphen.
        if (inProgressLine.length() == UNITS_AMOUNT - 1 && letterIndex < currentWordLength - 1)
        {
          inProgressLine = inProgressLine + "-";
          lines.add(inProgressLine);
          inProgressLine = "";
        }

        //Add the character and flush a full display-width line.
        inProgressLine = inProgressLine + wordItemLetter;
        if (inProgressLine.length() == UNITS_AMOUNT)
        {  
          lines.add(inProgressLine);
          inProgressLine = "";
        }
      }
    }
    
    //Remove the word now that every character has been handled.
    words.remove(0);
    
    //Do not lose a final partial line.
    if (words.size() == 0 && inProgressLine != "")
    {
      lines.add(inProgressLine);
      inProgressLine = "";
    }
  }

  return lines;
}

/**
 * Checks whether strtol can consume the complete string as a base-10 value.
 */
bool isNumber(String str) {
  char* endPtr;

  //10 specifies base 10 (decimal)
  strtol(str.c_str(), &endPtr, 10);  

  //Check if the conversion reached the end of the string
  return (*endPtr == '\0');
}