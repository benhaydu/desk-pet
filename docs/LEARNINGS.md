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

### Wi-Fi and time

- **Wi-Fi as a state machine.** Connecting runs in steps checked every pass through `loop()`, not a blocking `do`/`while`, so the eyes keep moving while it connects or retries.
- **Keep my own state instead of trusting `WiFi.status()` everywhere.** Reading the status in several places raced with the library changing it. `isOnline()` now returns my state machine's state, and only the network module talks to the Wi-Fi library.
- **`network.h` collided with the ESP32 core's `Network.h`.** Windows filenames aren't case-sensitive, so `#include "network.h"` found the core's file instead of mine. Renamed it to `net.h`.
- **Time zones.** NTP gives UTC. `configTzTime` sets the zone when Wi-Fi connects, but before that the clock showed UTC, so I also call `setenv("TZ", ...)` and `tzset()` at boot. The chip's RTC keeps the time across a reset, so the clock is right straight away after a restart.

### HTTP and JSON

- **HTTPS with `WiFiClientSecure` and `setInsecure()`.** The connection is encrypted, but the board doesn't check who it's talking to. Fine on my home network for now; the proper fix is giving it the server's root certificate with `setCACert`.
- **ArduinoJson v7.** `JsonDocument`, `deserializeJson`, then `.is<T>()` to check a field's type before trusting it. `doc["x"] | fallback` gives a default when a field is missing, which is how the board copes with older server versions.
- **Pointers into a `JsonDocument` die with it.** `const char *reply = doc["reply"]` points into the document's memory, so it has to be copied (with `snprintf`) before the document goes out of scope.
- **`%d` with a float is undefined behaviour.** The format string has to match the type exactly; `%.1f` for floats.

### Two cores and FreeRTOS

- **Networking in its own task.** `xTaskCreatePinnedToCore` runs the weather fetch on core 0 while `loop()` and the eyes run on core 1. A slow request can no longer freeze the face. Inside a task, `vTaskDelay(pdMS_TO_TICKS(ms))` sleeps without blocking anything else.
- **A mutex protects data both cores touch.** Take it, copy the values into locals, give it back, then work with the copies. Holding the lock only for the copy keeps the other core waiting as little as possible.
- **Bugs I hit with the weather task:**
  - The mutex was never created, so the first `xSemaphoreTake` used a null handle.
  - `weatherValid()` called itself while holding the lock, which is a deadlock: the task waits forever for a lock it already holds.
  - A getter returned the shared global instead of the copy.
  - `weatherVersion++` was inside a getter, so reading the value changed it.
  - The retry delays were in seconds where the function wanted milliseconds, so they were 1000 times too short.
  - The timeout comparison was backwards.
- **A version counter tells `loop()` something changed.** The task bumps it with each new reading, and `loop()` only redraws when the number differs from the last one it saw. That's cheaper and simpler than a flag that someone has to remember to reset.

### Performance, looking back

- **The frame rate was really 16 fps, not 30.** `lastFrameTime = millis()` was set at the *end* of the frame, so drawing time was added to every interval. Setting it at the start fixed it.
- **80 MHz SPI showed artifacts on this panel.** Pushing was faster, but the picture glitched, so it stays at 40 MHz. Faster isn't worth it if it looks broken.

### Design

- **I built a status strip (time and temperature) and then removed it.** Always-on information took screen space from the face and wasn't something I looked at. Time and weather are better on demand.

---

## Phase 4: The brain

### Architecture

- **The board never holds the Anthropic API key.** It sends the question to my own server, which calls Claude and replies with a small JSON object. Keeping the brain on the server means prompts and tools change without reflashing the board, and that part is in Python.
- **FastAPI and Pydantic.** A request or response is a Pydantic model, so FastAPI validates the JSON coming in and builds it going out. `Literal["happy", "sad", ...]` restricts a field to the exact expression names the firmware knows.
- **The server setup:** a `venv` for the Python packages, `requirements.txt` listing them, and the API key in `.env` (gitignored) loaded with `python-dotenv`. `.env.example` with fake values is committed so the repo explains what's needed.

### Structured output from Claude

- **`client.messages.parse(output_format=AskResponse)`** makes Claude return JSON that matches my Pydantic model, so I never parse free text.
- **`parsed_output` can be `None`.** The model's thinking counts toward `max_tokens`, and at a low limit it ran out before writing the answer. I raised the limit to 2000 and added a `None` check that logs `stop_reason` and returns a 502 instead of crashing with a 500.
- **The board's font is plain ASCII.** `clean_for_screen` swaps curly quotes, dashes and ellipses for ASCII *first*, then drops anything else (emoji, accents), then strips URLs and collapses spaces. The order matters: drop first and the quotes vanish instead of being swapped.

### Getting the board to reach the server

- **Windows Firewall blocked the board and my phone.** The PC could reach its own server, nothing else could. It needed `--host 0.0.0.0` (listen on the network, not just localhost), the network profile set to Private, and a firewall rule for the port, which has to be created from an admin PowerShell.

### The board side

- **A queue passes questions to the ask task, a mutex guards the answer.** `xQueueSend(..., 0)` never waits, so `loop()` can't block; if the queue is full, Steve's busy. The task sleeps in `xQueueReceive` until a question arrives, posts it, and writes the answer under the lock with a version bump.
- **ArduinoJson builds the request body** so a question containing quotes gets escaped properly.
- **`secrets.h` caused a "multiple definition" link error** once a second file included it. `const char *` makes the characters const but not the pointer, so the variable itself was defined twice. `const char* const` makes it a true constant, one per file.
- **Typing in the serial monitor.** `readLine` builds the line one character at a time without blocking. Backspace arrives as a real character (`'\b'` or 127), so it has to remove the last character, or commands like `/mini` never match with `strcmp`.

### C strings, properly this time

- **Arrays can't be assigned with `=`.** Copy a string with `snprintf(dest, sizeof(dest), "%s", src)`, which also never overflows.
- **`strcmp(a, b) == 0`** means equal. `==` on two `char*` compares addresses.
- **`%.*s` prints part of a string:** `printf("%.*s", len, text + start)`. `text + start` is pointer arithmetic, "the string starting `start` characters in".
- **Structs *can* be copied with `=`,** arrays inside them included. That's what made the `Answer` struct easy to pass around later.

### Mini Steve and the talking sequence

- **Switching faces during a blink hides the swap.** `setFaceMode` only records the request; the actual change happens when the eyes are fully closed, and then the whole screen is cleared once (`needsFullRedraw`) to remove the old face.
- **`faceIsMini()` checks `!switchPending` too.** Otherwise text could start drawing just as a switch back to the big face was about to happen.
- **A talking state machine** waited for mini Steve, showed the text, then brought him back. It worked, but reading scrolling text off a 2.8" screen turned out to be the wrong idea (see Phase 5).

### Deploying to Render

- **Lock the door before opening it.** The server requires an `X-Steve-Key` header before it's public, or anyone who finds the URL could spend my Anthropic credits.
  - `secrets.compare_digest` instead of `==`, so a rejection takes the same time however much of the key was right.
  - `Header(default="")` means a missing header reaches my check and gets the same **401** as a wrong key. Without the default, FastAPI rejects it first with a 422, and "missing" and "wrong" look different from outside.
  - A **guard clause** rejects bad requests at the top, so the normal path isn't nested inside an `if`.
  - An empty `STEVE_KEY` would let a request with no header match `""`, so the server refuses to start without a real key.
  - A wrong key that fell off the end of the function returned `None`, which became a 500. Every path needs an explicit return or raise.
- **Environment variables instead of `.env` in production.** Render has no `.env` file, so `load_dotenv()` does nothing there and `os.environ` reads the dashboard's variables. Same code, different config. The start command uses `$PORT`, because Render picks the port.
- **Render's free tier sleeps after 15 minutes**, and waking takes 30 to 60 seconds. The board's timeout is 60 seconds, so the first question after a quiet stretch sometimes fails with status -11 (`HTTPC_ERROR_READ_TIMEOUT`). I decided a clear "Brain offline" is better than waiting longer.
- **Scrambled serial output** came from two tasks printing at the same moment. `Serial.printf` isn't locked, so their characters interleave. Harmless, just confusing.
- **Git habits.** Run `git status` on its own and read it before `git add`, and keep server and firmware changes in separate commits. Pasting all the commands at once runs the check after it's too late to matter. Also, "ahead of origin/main by 1 commit" means Render can't see it yet.

---

## Phase 5: Glanceable replies

### What Steve is for

- **A chatbot on a 2.8" screen is the wrong product.** My phone and PC are always on the desk and better at long answers. Steve's strengths are being always visible, having a face, and showing things without being asked. So answers became a face plus a few big words, and general questions became a minor feature.

### Headlines

- **Claude returns a `headline` (30 characters at most) next to the full reply.** The screen only shows the headline.
- **Limits go in the description, not `Field(max_length=30)`.** If structured output can't guarantee a length limit and Claude goes one character over, parsing fails and the whole answer is lost. The description guides Claude, and `fit_headline` enforces it on the server, cutting at the last space before the limit.
- **Clean first, then fit.** Cleaning can make text longer (an ellipsis becomes three dots), so fitting first could still end up over the limit.
- **Slices exclude their end,** like `range`: `s[:30]` is 30 characters, and `s[0, 30]` with a comma is a tuple index and a `TypeError`.
- **The degree sign isn't in the board's font,** so the prompt says to write `10C`. Claude copies the style of the examples it's given, so the examples have to be screen-safe too.
- **A FastAPI endpoint's return type filters the response.** I left the endpoint typed as the old model and the new fields were silently dropped: no error anywhere, the board just never got them.

### Drawing the headline (`headline.cpp`)

- **Auto-fit:** try FreeSansBold 18pt, then 12pt, then 9pt, and use the biggest one whose `textWidth()` fits the strip. Measure with the real font rather than counting characters, because "W" is much wider than "i".
- **`MC_DATUM`** makes `drawString`'s `x, y` the centre of the text, so centring works whatever font got picked.
- **Push after clearing, even when nothing is drawn.** Hiding a headline means pushing a black strip, otherwise the old text stays on the screen.
- **A `dirty` flag** means the strip is only redrawn and pushed when it changes, not every frame.
- **`font` was already taken.** TFT_eSPI pulls in a global array called `font`, and my variable with the same name caused four errors. Only the first one was the real problem; the rest followed from it. Renamed mine to `chosenFont`.
- **A parameter can hide a global.** A parameter called `text` hid my `static char text[40]`, so `snprintf` tried to write into a read-only `const char *`, and `sizeof(text)` was the size of a pointer (4), not 40.
- **Prototypes have to match definitions exactly.** When `storeAnswer` and `storeFallback` changed parameters but their prototypes didn't, C++ treated them as different functions with the same name (overloading). The calls compiled, then the linker couldn't find the old version.
- **The `Answer` struct** replaced passing four buffers and four sizes around. `askLatest(Answer &out)` takes a **reference**: the caller's own variable, used like a normal variable, never null.

### The thinking face

- **Lid shapes always read as an emotion.** Inner lids down read as angry, an even droop as tired, and a raised bottom lid as happy, even a small one. My first thinking faces kept looking like one of those.
- **Motion carries state better than shape.** Other projects (FluxGarage RoboEyes, Anki's Vector) show states like confused or curious mostly through movement. I mocked up five options before building one, and picked short eyes sweeping side to side.
- **One `thinkAmount` from 0 to 1 drives both effects.** It eases in and out over a quarter of a second, and both the eye height and the gaze blend by it, so he slides into thinking instead of snapping. Each fade starts from the current value, so flipping it halfway through carries on smoothly.
- **An early `return` in `updateGaze`** lets thinking take over the gaze without touching the normal glance state machine.

---

## Phase 6 so far: the weather tool

### Tool use

- **Claude can't fetch anything itself.** I describe a `get_weather` tool (a name, a description, and a JSON schema for its input). When a question needs it, Claude replies with `stop_reason == "tool_use"` and the input it wants. My server runs the function and sends the result back, and Claude answers from real data.
- **The descriptions decide when the tool gets used.** "Even if the word 'weather' isn't used" is what makes "do I need an umbrella?" trigger it, and the location's description sets Victoria as the default.
- **The loop:** a `messages` list that grows; Claude's turn appended exactly as it came; one `tool_result` per `tool_use`, tagged with its id, `json.dumps`'d, and marked `is_error` on failure. A cap on rounds stops a confused model from looping forever, and `parsed_output` is `None` on tool rounds, so the check moved after the loop.
- **Every `tool_use` needs a result,** even for a tool the server doesn't know, or the next call fails.
- **Claude does language, code does facts.** Claude writes the reply and picks the mood, and the server picks the scene from the weather data with a plain dictionary. That's why there are two response models: `AskResponse` (what Claude fills in) and `SteveResponse` (what the board gets).

### The weather module

- **"What's the day like" isn't the daily weather code.** Open-Meteo's daily code is the *worst* weather at any point, so a sunny day with a 15-minute shower came back as rain. I count the hourly conditions between 5 am and midnight, grouped into categories, and the most common one wins. The rainy hours go to Claude separately, so he can still mention the shower.
- **`data["x"]` vs `data.get("x")`.** Square brackets raise a `KeyError` when a required field is missing, so the error gets caught. `.get()` quietly returns `None`, and the `KeyError` in my `except` could never happen.
- **Bugs that compiled and ran:**
  - `...` left in as a placeholder is a real Python value (`Ellipsis`), which only failed later at `json.dumps`.
  - `hour <= WAKING_START < WAKING_END` is a chained comparison: it means `hour <= 5 and 5 < 24`, so it checked the wrong thing entirely.
  - `code <- 77` is read as `code < -77`, which no code ever is, so snow never matched.
  - Comparing against `datetime.now()` used the *server's* clock, and Render runs on UTC. The location's own date from the API is the right one.
  - Python has no block scope: a `for` loop variable called `code` was the same variable as the `code` above it, so the function returned the last hour's weather instead of the day's.
- **Open-Meteo returned 429 on Render.** Free cloud servers share outgoing IP addresses, and Open-Meteo limits by IP, so other people's traffic used up my limit. I switched to WeatherAPI.com, where the limit belongs to my own key. Because the weather logic lives in its own module, nothing else on the server had to change.
- **Read secrets when they're used, not at import.** `main.py` imports `weather.py` *before* calling `load_dotenv()`, so a key read at the top of the file would always be empty.
- **Don't log errors that contain secrets.** WeatherAPI takes the key in the URL, and httpx's error messages include the URL, so printing the exception would have put my key in Render's logs.

### The morning briefing (server)

- **The server decides the structure, Claude writes the words.** `_skeleton` builds every card in a fixed order with the facts for each, and Claude only fills in titles and lines. The worst a bad run can do is an awkward line, never a missing or shuffled briefing.
- **Only give Claude the facts you want it to use.** The weather card got today and tomorrow, and Claude wrote about tomorrow's rain under a cloudy scene for today. Sending only `today` fixed it; a prompt rule would only have asked nicely.
- **Feeds can be wrong without failing.** CBC's tech feed came back with moose and mice stories. The fallback chain only catches empty feeds, so the fix was putting a source that's always about tech first. Reading the output is the only way to catch that kind of bug.
- **`max_tokens` has to cover the thinking, and thinking varies run to run.** 3000 fit locally and ran out on Render with the same model. I raised it to 6000 and log `usage.output_tokens` on every run, before the `None` check so failures log too.
- **`ThreadPoolExecutor` runs the three sources at once,** and `.result()` waits for each and hands back its value.

### The morning briefing (board)

- **One network task, two kinds of request.** The queue item got an `enum class RequestKind`, so the task can fetch a briefing or ask a question but only ever holds one TLS connection (about 40 KB) at a time.
- **Every request ends in exactly one version bump,** even a failed one (stored with `count = 0`), so whoever is waiting always finds out.
- **`Card &card = b.cards[b.count]`** fills the array slot through a reference. Without the `&` it fills a copy that gets thrown away. A skipped card doesn't bump `count`, so the next card just overwrites the slot.
- **`sizeof(card.lines[0])`, not `sizeof(card.lines)`.** In a 2D array the second is the whole block, and `snprintf` would think it could write three lines' worth into one.
- **`String(SERVER_URL) + "/ask"`** because adding two `const char *` would be adding two addresses, which doesn't compile.
- **`__has_include` and a gitignored header** let my own team images show on my board while the public repo builds without them and falls back to drawn icons. `static_assert` checks the image size matches the slot at compile time.
- **A table of function pointers** maps each card id to its colour and icon function, so a new topic is one line.
- **Easing makes motion feel intentional.** Panels slide in with ease-out (fast, then settling) and out with ease-in (slow, then gone), and in is a bit slower than out because the eye needs to land on new content.
- **A viewport clips the slide** so text passing under mini Steve's corner can't draw over him.
- **Drawing an image one pixel at a time** avoids `pushImage`'s byte-swap setting (wrong colours if you get it wrong) and makes black transparent for free. 1,600 pixels per frame, only during a slide, costs nothing noticeable.
- **The player acts on taps and stops at safe points.** `skipping` and `stopping` are flags the `Hold` step reads, never actions taken immediately, so a tap during a slide or a blink can't cut it in half.
- **A function defined in a header has to be `inline`** (and `constexpr` implies it), or every file that includes it defines it again and the linker refuses.
- **Windows blocked `pip.exe`** with an Application Control policy because the launcher isn't signed. `python -m pip` runs the same code inside the signed `python.exe`.