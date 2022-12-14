//#include "Arduino.h"
#include <atmel_start.h>

#include "FT8_commands.h"
#include "FT8.h"  //default values for draw and background color can be found in FT8.h
#include "FT8_config.h"

class MessageLog {
  private:
  
  char** messages;
  int currentCharIndex;
  int currentLine;
  int x, y;
  int font;
  int widthChar, numberOfLines;
  int lineSpacing;

  public:
  MessageLog(int x, int y, int font, int numberOfLines, int widthChar, int lineSpacing);
  ~MessageLog();
  void newLine(const char* text);
  void addText(const char* text); // add text to the same line
  void addNumber(float num); // add number to the same line
  void addNumberPrecision(float num, int precision); // add number to the same line with given precision
  void display_messageLog();
};

// change from class into structure, then finish the definition where all the vars end, remove public, google constructor, as a first argument you have to add an instance of the structure, go into every one of the funcitons and 