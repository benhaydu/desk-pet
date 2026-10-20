# Learnings

Notes on what I learned in each phase of the desk pet build. The point is to be able to explain each of these out loud, so if something here doesn't make sense to me anymore, that's a sign to go back and redo it.

---

## Phase 0: Hardware and tooling

- **Board package / toolchain / serial port.** The Arduino "board package" is the compiler and libraries for a specific chip (ESP32-S3 here, core 3.2.0). Upload flashes the compiled binary over USB, and the serial port is how the board talks back to my computer.
- **"Port busy" on upload** means something else has COM3 open. In my case it was the Freenove web flasher. Only one program can hold a serial port at a time.
- **The "device does not exist" error after a successful upload** is harmless. The ESP32-S3 uses native USB, so when it resets after flashing, the USB device disappears and comes back, and the uploader complains.
- **`printf("%s", someString)` with an Arduino `String` is undefined behaviour.** `%s` expects a C string (a `char*`), not a `String` object. It sometimes "works" because of how short strings are stored, which makes it worse. Use `.c_str()`.
- **Pull-up resistors and active-low buttons.** The BOOT button reads HIGH when not pressed (the internal pull-up holds it high) and LOW when pressed, because pressing connects it to ground.
- **Interrupts.** An ISR runs the instant the pin changes, interrupting whatever `loop()` was doing. Variables shared with an ISR need `volatile` (so the compiler re-reads them every time) and a critical section / lock around updates, because `count++` is actually read, add, write, and an interrupt can land in the middle.

## Phase 1: Foundations and repo

- **PlatformIO over Arduino IDE.** Library versions are pinned in `platformio.ini`, code is split into real `.cpp`/`.h` files, and builds are reproducible. I'm using the pioarduino platform so I get ESP32 core 3.2.0.
- **Build flags (`-D`)** define macros for the whole build, like `-DBOARD_HAS_PSRAM`. It's the same as putting `#define` at the top of every file.
- **Macro collisions.** I named a constant `PIN_RGB_LED` and got "expected unqualified-id before numeric constant". The ESP32 core already has `#define PIN_RGB_LED 48`, and the preprocessor replaced my variable name with `48` before the compiler saw it. Fix: prefix my own names (`PET_PIN_RGB_LED`).
- **Secrets.** `secrets.h` holds the Wi-Fi password and is in `.gitignore`. `secrets.example.h` with fake values is committed so someone cloning the repo knows what to create. Always check `git status` before committing.
- **`delay()` freezes everything.** Nothing else can happen while it waits. The replacement is checking elapsed time every pass through `loop()`:
  ```cpp
  if (millis() - lastTime >= INTERVAL) { ... lastTime = millis(); }
  ```
- **Edge detection and debouncing.** To count a button *press* (not "is it held"), compare this read to the last one and act only on HIGH → LOW. Buttons physically bounce for a few ms, so I also ignore changes within 50 ms of the last one.

## Phase 2: The eyes

### Graphics

- **Flicker comes from drawing directly to the screen.** The screen shows every intermediate step (clear to black, then draw the eye). Fix: draw the whole frame into a **sprite** (an off-screen buffer in PSRAM), then push it to the screen in one go. That's double buffering.
- **The half-eye bug.** My sprite was only clearing 3/4 of its width. I narrowed it down by ruling things out one at a time (wrong config, duplicate library, PSRAM) before finding it: I was calling `frame.fillScreen()`, which is inherited from the screen class and uses the screen's native 240 px width, not the sprite's 320. `frame.fillSprite()` is the right call.
- **Painter's algorithm.** Whatever is drawn last ends up on top. Expressions use this: draw the eye, then draw black eyelids over parts of it.
- **RGB565.** Colours are 16 bits: 5 red, 6 green, 5 blue (green gets the extra bit because our eyes are most sensitive to it). `tft.color565(r, g, b)` converts normal 0–255 values.

### Performance

- **Measure before optimising.** A full frame took ~41 ms, which is too slow for 30 fps (33 ms budget). Timing each step with `micros()` showed drawing was only ~5 ms and the **push** was ~36 ms.
- **Pushing only the region that changes.** I pushed just the box around the eyes instead of the whole screen. Push went from ~36 ms to 16.8 ms.
- **Push time is proportional to pixel count, about 0.53 µs per pixel.** This one model explained all three measurements:
  | Region | Pixels | Predicted | Measured |
  |---|---|---|---|
  | Full screen | 76,800 | ~41 ms | 41 ms |
  | 240 × 130 | 31,200 | — | 16.8 ms |
  | 290 × 150 | 43,500 | ~23.4 ms | 23.0 ms |

  I predicted the third one before measuring it, which is how I knew the model was right.
- **Timing mistakes I made:** subtracting in the wrong order (`start - now` with unsigned numbers wraps to a huge value), `"text" + micros()` (that's pointer arithmetic, not string joining), and printing *inside* the timed section (serial is slow and skews the result).

### Timing and state machines

- **State machines with `enum` + `switch`.** The blink is four states (OPEN, CLOSING, CLOSED, OPENING). Each frame I only handle the current state and decide whether to move to the next one. Every `case` ends with `break;`, or it falls through into the next case.
- **Separate update from draw.** Update functions change the numbers (eye height, position, lid sizes). Drawing just draws whatever the numbers say. Order each frame: update → clear → draw → push.
- **The 49-day `millis()` rollover.** `millis()` is a 32-bit unsigned number and wraps back to 0 after ~49.7 days. `millis() >= targetTime` breaks across the wrap. `millis() - startTime >= duration` doesn't, because unsigned subtraction wraps too and still gives the right elapsed time. Since the pet runs 24/7, every timer uses the subtraction form.
- **Independent systems.** Blink, gaze, and expression each have their own small state machine and timers. Right now they don't know about each other, which is why sometimes he blinks, glances, and morphs at the same time. A behaviour layer will coordinate that later.

### Animation

- **Step-based vs time-based.** The blink moves a fixed number of pixels per frame, so its speed depends on the frame rate. Glances and expression morphs are time-based instead: work out how far through the animation we are and position from that, so frame rate doesn't matter.
- **Lerp (linear interpolation):**
  ```
  t = elapsed / duration          // 0.0 → 1.0
  value = start + (end - start) * t
  ```
- **Integer division trap.** `90 / 180` is `0` in integer math. You need `elapsed / (float)duration` so at least one side is a float.
- **Easing.** Plain lerp starts and stops instantly, which looks mechanical. Smoothstep, `t * t * (3 - 2 * t)`, starts slow, speeds up, and slows down at the end. Same 0 and 1 endpoints, different curve.
- **Minimum glance distance.** Random targets were sometimes 2 px away and looked like nothing happened. I re-roll with a `do`/`while` until the target is far enough. I compare squared distance (`dx*dx + dy*dy < MIN*MIN`) to skip the square root.
- **Don't compare floats with `==`.** Rounding means "0" might be 0.0000001. Check "close enough" with a threshold instead.

### Expressions

- **An expression is three numbers:** how far the top lid drops on the inner side, on the outer side, and how far the bottom lid rises. Angry, sad, sleepy, happy, and suspicious are all just different values of the same struct.
- **A four-sided lid is two triangles,** but they have to be split along the same diagonal. My first version had both triangles share the top edge, which left a gap in the middle that only showed up on sleepy (both drops equal).
- **Mirroring.** The inner side is the right side of the left eye and the left side of the right eye. Pick the drops once with `if`/`else`, then draw once, instead of duplicating the drawing code.
- **Anti-aliased edges.** `fillSmoothRoundRect` blends its edges, so a lid that stops exactly at the eye edge leaves a coloured sliver. The lids overshoot by 2 px.
- **Morphing between expressions** is the same lerp as glances, applied to each field of the struct. `setExpression()` starts from the *current* face, not the old target, so changing mid-morph doesn't jump.

### C++ things I learned along the way

- **Local variables start as garbage; globals start at 0.** Reading a local before setting it is undefined behaviour.
- **Scope.** A variable only exists until the `}` of the block it was declared in. My `do`/`while` failed because I declared `dx` inside the `do { }` and then used it in the `while (...)` after the brace.
- **A loop condition has to depend on something the loop changes,** or it's an infinite loop. I did this once by moving the `dx`/`dy` calculation out of the loop.
- **Variables inside a `case` need braces** if another case comes after, or you get "jump to case label crosses initialization".
- **`static` means two different things.** Inside a function it keeps the value between calls (my frame counter). At file level it makes something private to that file.
- **`const` vs `constexpr`.** `constexpr` has to be worked out at compile time, so `TFT_CYAN` works but `tft.color565(...)` doesn't (it's a normal function that runs at runtime).
- **Case sensitivity.** `UpdateGaze` and `updateGaze` are different names. The prototype had a capital U and the compiler couldn't find the function.
- **Copy-paste bugs compile fine.** I copied the glance timing into `updateExpression` and left it using the glance's variables. Every name existed, so no error, just wrong behaviour.

### Splitting into files

- **Each `.cpp` compiles on its own, `#include` is copy-paste, and the linker joins everything at the end.**
- **Declarations vs definitions.** You can declare something many times but define it once in the whole program. So headers hold declarations, `.cpp` files hold definitions.
- **`extern TFT_eSprite frame;`** in `eyes.cpp` says "this exists somewhere else, the linker will find it". It's a prototype for a variable.
- **Why `const char*` in `secrets.h` would break** once two files include it: the `const` applies to the characters, not the pointer, so the variable itself isn't const and gets defined twice. `const char* const` fixes it.
- **The eyes module has a small public API** (`eyesInit`, `updateEyes`, `drawEyes`, `setExpression`) in `eyes.h`, and everything else in `eyes.cpp` is `static`. `main.cpp` just runs the frame loop.

---

## Phase 3: Connectivity

_(to do)_