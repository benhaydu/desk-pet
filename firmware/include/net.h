#pragma once

void networkInit();      // once in setup()
void updateNetwork();    // call every loop
bool isOnline();         // so other code can ask "are we connected?"