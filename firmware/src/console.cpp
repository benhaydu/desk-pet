#include <Arduino.h>
#include "console.h"

static char lineBuf[128];
static size_t lineLen = 0;

bool readLine(char *out, size_t outSize)
{
    while (Serial.available() > 0)
    {
        char c = Serial.read();

        if (c == '\r')
        {
            // ignore, Windows sends \r\n and we only care about the \n
        }
        else if (c == '\n')
        {
            // blank lines are just dropped
            if (lineLen > 0)
            {
                lineBuf[lineLen] = '\0';
                snprintf(out, outSize, "%s", lineBuf);
                lineLen = 0;
                return true;
            }
        }
        else if (lineLen < sizeof(lineBuf) - 1)
        {
            // keep one spot free for the '\0', anything past that gets dropped
            lineBuf[lineLen] = c;
            lineLen++;
        }
    }
    return false; // no complete line yet
}