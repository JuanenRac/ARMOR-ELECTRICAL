// ARMOR-ELECTRICAL - host tests of the switching rules: a stand-in for two contactors, scripted scenarios, and a hundred thousand random steps that check what the
// controller guarantees. No real contactor has ever been driven.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include <cstdint>
#include <cstdio>

#include "../core/interlock.hpp"

static int failures = 0;
static int checks = 0;
#define CHECK(condition)                                                              \
  do {                                                                                \
    ++checks;                                                                         \
    if (!(condition)) {                                                               \
      ++failures;                                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);                \
    }                                                                                 \
  } while (0)

using namespace armor::electrical;

// A contactor: it follows its coil after a delay; it can be stuck closed or stuck open (a weld, a broken spring, a burnt coil).
struct Contactor {
  bool actual = false;
  bool stuck_closed = false, stuck_open = false;
  std::uint64_t close_ms = 60, open_ms = 40;
  bool last_coil = false;
  std::uint64_t changed_at = 0;
  void step(bool coil, std::uint64_t now) {
    if (coil != last_coil) { last_coil = coil; changed_at = now; }
    if (coil && !actual && !stuck_open && now - changed_at >= close_ms) actual = true;
    if (!coil && actual && !stuck_closed && now - changed_at >= open_ms) actual = false;
  }
};

struct Plant {
  Contactor a, b;
  TransferController controller;
  std::uint64_t now = 0;
  std::uint64_t both_actual_closed_ticks = 0;
  explicit Plant(TransferConfig config = {true, 2000, 1000, 10'000}) : controller(config) {}
  Feedback feedback() const { return {a.actual, b.actual}; }
  void run(std::uint64_t ms) {
    for (std::uint64_t end = now + ms; now < end; now += 10) {
      controller.tick(feedback(), now);
      const Coils c = controller.coils();
      a.step(c.a, now); b.step(c.b, now);
      if (a.actual && b.actual) ++both_actual_closed_ticks;
    }
  }
};

static void test_disabled() {
  Plant plant(TransferConfig{false, 2000, 1000, 10'000});
  plant.controller.arm(plant.now);
  CHECK(plant.controller.request(Source::kA, plant.now) == Refusal::kDisabled);
  plant.run(5000);
  CHECK(!plant.controller.coils().a && !plant.controller.coils().b && plant.controller.selected() == Source::kNone && !plant.a.actual);
  CHECK(plant.controller.request(Source::kNone, plant.now) == Refusal::kNone);   // opening is always accepted
}

static void test_arming() {
  Plant plant;
  plant.run(100);
  CHECK(plant.controller.request(Source::kA, plant.now) == Refusal::kNotArmed);     // a request that was not armed first
  plant.run(3000);
  CHECK(plant.controller.selected() == Source::kNone && !plant.a.actual);
  plant.controller.arm(plant.now);
  CHECK(plant.controller.armed(plant.now));
  plant.run(11'000);                                                                 // the arm has run out
  CHECK(!plant.controller.armed(plant.now) && plant.controller.request(Source::kA, plant.now) == Refusal::kNotArmed);
  plant.controller.arm(plant.now);
  CHECK(plant.controller.request(Source::kA, plant.now) == Refusal::kNone);
  CHECK(!plant.controller.armed(plant.now));                                         // one arm, one request
  CHECK(plant.controller.request(Source::kB, plant.now) == Refusal::kNotArmed);
}

static void test_transfer() {
  Plant plant;
  plant.run(100);
  plant.controller.arm(plant.now);
  CHECK(plant.controller.request(Source::kA, plant.now) == Refusal::kNone);
  plant.run(1500);
  CHECK(!plant.a.actual && !plant.controller.coils().a);                              // the dead time has not passed: nothing is closed yet
  plant.run(1500);
  CHECK(plant.a.actual && plant.controller.selected() == Source::kA && plant.controller.fault() == Fault::kNone);
  // to the other source: A opens first, then a dead time, then B
  plant.controller.arm(plant.now);
  CHECK(plant.controller.request(Source::kB, plant.now) == Refusal::kNone);
  plant.run(10);
  CHECK(!plant.controller.coils().a && !plant.controller.coils().b);                  // A is told to open at once
  plant.run(500);
  CHECK(!plant.a.actual && !plant.b.actual && plant.controller.selected() == Source::kNone);
  plant.run(2500);
  CHECK(plant.b.actual && !plant.a.actual && plant.controller.selected() == Source::kB);
  CHECK(plant.both_actual_closed_ticks == 0);
  // opening is immediate and needs no arm
  CHECK(plant.controller.request(Source::kNone, plant.now) == Refusal::kNone);
  plant.run(10);
  CHECK(!plant.controller.coils().a && !plant.controller.coils().b);
  plant.run(500);
  CHECK(!plant.b.actual && plant.controller.selected() == Source::kNone);
}

static void test_faults() {
  // a contactor that will not open (welded): the fault comes, everything is told to open, and nothing else can close
  Plant weld;
  weld.run(100);
  weld.controller.arm(weld.now); weld.controller.request(Source::kA, weld.now);
  weld.run(3500);
  CHECK(weld.a.actual);
  weld.a.stuck_closed = true;
  weld.controller.request(Source::kNone, weld.now);
  weld.run(1500);
  CHECK(weld.controller.fault() == Fault::kDidNotOpen && !weld.controller.coils().a && !weld.controller.coils().b);
  weld.controller.arm(weld.now);
  CHECK(weld.controller.request(Source::kB, weld.now) == Refusal::kFault);
  CHECK(!weld.controller.acknowledge(weld.feedback(), weld.now) && weld.controller.fault() == Fault::kDidNotOpen);   // not while it is still closed
  weld.run(10'000);
  CHECK(weld.controller.fault() == Fault::kDidNotOpen && !weld.b.actual);                                            // it never clears itself
  weld.a.stuck_closed = false;
  weld.run(200);
  CHECK(!weld.a.actual);
  CHECK(weld.controller.acknowledge(weld.feedback(), weld.now) && weld.controller.fault() == Fault::kNone);
  // and after that it can be used again, with the dead time counted from the acknowledgement
  weld.controller.arm(weld.now);
  CHECK(weld.controller.request(Source::kB, weld.now) == Refusal::kNone);
  weld.run(1000);
  CHECK(!weld.b.actual);
  weld.run(2000);
  CHECK(weld.b.actual && weld.controller.selected() == Source::kB);

  // a contactor that will not close
  Plant stuck;
  stuck.b.stuck_open = true;
  stuck.run(100);
  stuck.controller.arm(stuck.now); stuck.controller.request(Source::kB, stuck.now);
  stuck.run(4500);
  CHECK(stuck.controller.fault() == Fault::kDidNotClose && !stuck.controller.coils().b && stuck.controller.selected() == Source::kNone);

  // both auxiliary contacts closed: never accepted
  Plant both;
  both.run(100);
  both.a.actual = true; both.b.actual = true; both.a.stuck_closed = true; both.b.stuck_closed = true;
  both.run(50);
  CHECK(both.controller.fault() == Fault::kBothClosed && !both.controller.coils().a && !both.controller.coils().b);

  // a contactor that falls out on its own
  Plant falls;
  falls.run(100);
  falls.controller.arm(falls.now); falls.controller.request(Source::kA, falls.now);
  falls.run(3500);
  CHECK(falls.a.actual);
  falls.a.actual = false; falls.a.stuck_open = true;
  falls.run(100);
  CHECK(falls.controller.fault() == Fault::kDidNotClose && !falls.controller.coils().a);

  // something closed that nobody asked for
  Plant ghost;
  ghost.run(100);
  ghost.a.actual = true; ghost.a.stuck_closed = true;
  ghost.run(1500);
  CHECK(ghost.controller.fault() == Fault::kDidNotOpen);
}

// ---- random steps ------------------------------------------------------------------------------------------------------------------------

struct Random {
  std::uint64_t state;
  explicit Random(std::uint64_t seed) : state(seed) {}
  std::uint32_t next() { state = state * 6364136223846793005ULL + 1442695040888963407ULL; return static_cast<std::uint32_t>(state >> 33); }
  bool chance(unsigned percent) { return next() % 100 < percent; }
};

static void test_random() {
  for (std::uint64_t seed = 1; seed <= 20; ++seed) {
    Random random(seed);
    const bool allowed = seed % 5 != 0;                                            // every fifth run has the switching off
    Plant plant(TransferConfig{allowed, 2000, 1000, 5000});
    plant.a.close_ms = 20 + random.next() % 200; plant.b.close_ms = 20 + random.next() % 200;
    plant.a.open_ms = 10 + random.next() % 100; plant.b.open_ms = 10 + random.next() % 100;
    bool both_open_seen_since_valid = false;
    std::uint64_t open_since = 0;
    Coils previous;
    bool was_fault = false;
    for (int step = 0; step < 5000; ++step) {
      // the outside world: requests, arms, acknowledgements, and things going wrong
      if (random.chance(2)) plant.controller.arm(plant.now);
      if (random.chance(3)) {
        const Source wanted = static_cast<Source>(random.next() % 3);
        const Refusal refusal = plant.controller.request(wanted, plant.now);
        if (wanted != Source::kNone && refusal == Refusal::kNone && !allowed) CHECK(false);     // never accepted with the switching off
      }
      if (random.chance(1)) plant.controller.acknowledge(plant.feedback(), plant.now);
      if (random.chance(1)) plant.a.stuck_closed = !plant.a.stuck_closed;
      if (random.chance(1)) plant.b.stuck_open = !plant.b.stuck_open;
      if (random.chance(1)) plant.b.stuck_closed = !plant.b.stuck_closed;
      if (random.chance(1)) plant.a.stuck_open = !plant.a.stuck_open;
      if (random.chance(1)) plant.a.actual = !plant.a.actual && !plant.a.stuck_open;             // a contactor that moves by itself
      plant.controller.tick(plant.feedback(), plant.now);
      const Coils coils = plant.controller.coils();
      // 1. never two sources at once
      CHECK(!(coils.a && coils.b));
      // 2. with the switching off, nothing is ever energised
      if (!allowed) CHECK(!coils.a && !coils.b);
      // 3. a fault means every coil open, and it does not clear on its own
      if (plant.controller.fault() != Fault::kNone) { CHECK(!coils.a && !coils.b); was_fault = true; }
      else if (was_fault) was_fault = false;   // only `acknowledge` (called above) can have cleared it
      // 4. a coil is energised only after both contactors were confirmed open for the whole dead time
      const Feedback fb = plant.feedback();
      if (!fb.a_closed && !fb.b_closed) { if (!both_open_seen_since_valid) { both_open_seen_since_valid = true; open_since = plant.now; } }
      else both_open_seen_since_valid = false;
      if ((coils.a && !previous.a) || (coils.b && !previous.b)) {
        CHECK(both_open_seen_since_valid && plant.now - open_since >= 2000);
        CHECK(allowed && plant.controller.fault() == Fault::kNone);
      }
      // 5. a source that is not the one that was chosen is never energised
      if (coils.a) CHECK(plant.controller.wanted() == Source::kA);
      if (coils.b) CHECK(plant.controller.wanted() == Source::kB);
      previous = coils;
      plant.a.step(coils.a, plant.now); plant.b.step(coils.b, plant.now);
      plant.now += 10;
    }
  }
  // a request to open drops every coil at the very next tick, whatever was going on
  Random random(99);
  for (int run = 0; run < 200; ++run) {
    Plant plant;
    plant.run(100);
    for (int step = 0; step < 300; ++step) {
      if (random.chance(20)) plant.controller.arm(plant.now);
      if (random.chance(15)) plant.controller.request(static_cast<Source>(1 + random.next() % 2), plant.now);
      plant.run(10);
    }
    plant.controller.request(Source::kNone, plant.now);
    plant.controller.tick(plant.feedback(), plant.now);
    CHECK(!plant.controller.coils().a && !plant.controller.coils().b);
  }
}

int main() {
  test_disabled();
  test_arming();
  test_transfer();
  test_faults();
  test_random();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
