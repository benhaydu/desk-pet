#pragma once
#include "ask.h"  // NowPlaying
#include "eyes.h" // Rect

// the Now Playing screen: the album cover top left, Steve in headphones on the right bobbing
// along, the song and a progress bar underneath. It shows itself while something is playing,
// and steps aside for answers, timers, the paper and sleep
void musicInit();                          // once in setup(): the cover's memory
void musicResult(const NowPlaying &n);     // a /now-playing check came back
void musicArtReady();                      // artVersion() changed
void updateMusic();                        // once per frame, before working out the push region
void drawMusic(Rect pushed);               // once per frame, after drawFace(), with the region being pushed
bool musicActive();                        // on screen, or just leaving it
bool musicOwnsFace();                      // Steve has settled into place: this view's region covers him too
Rect musicRegion();                        // what needs pushing this frame
bool musicTakeExtra(Rect &out);            // after the main push: a changed part outside that region
bool musicTakeClear(Rect &out);            // true once after it goes away, with the area to wipe
void musicSetAside(bool aside);            // true while Steve needs his whole face: answers, the paper, sleep

// controlling Spotify, like the touch on the side of earbuds: one tap pauses or plays, two skip.
// Taps only count while the Now Playing screen is up
bool musicTap();                           // a tap. True if the music screen used it
void musicControl(const char *action);     // "play", "pause" or "next" straight away (the serial commands)
void musicPlayerResult();                  // playerVersion() changed
bool musicTakeRefresh();                   // true once after a command: check what's playing again soon