#!/usr/bin/env python3
"""Host-side mirror of alarm.c's per-slot scheduling rules.

There is no host C compiler on the dev PC, so the candidate-eligibility and
next-ring logic of alarm_task()/publish_next() is re-implemented here 1:1 and
exercised against the edge cases the design review called out. Keep this in
step with alarm.c when the rules change.

Run: python src/bedside_clock/tools/test_alarm_sched.py
"""
import datetime as dt
import sys

ALARM_COUNT = 3
DAYS_MASK, DAYS_ONCE, DAYS_ALL, DAYS_WEEK, DAYS_WEND = 0x7F, 0x80, 0x7F, 0x3E, 0x41
CATCHUP_S = 5 * 60
DAY = 86400
TZ = dt.timezone(dt.timedelta(hours=10))   # fixed offset, like the device


def days_match(days, wday):
    return (days & DAYS_MASK & (1 << wday)) != 0


def local(epoch):
    return dt.datetime.fromtimestamp(epoch, TZ)


def tm_wday(d):
    return (d.weekday() + 1) % 7            # python Mon=0 -> tm_wday Sun=0


def sched_today(now, hh, mm):
    d = local(now).replace(hour=hh, minute=mm, second=0, microsecond=0)
    return int(d.timestamp())


class Slot:
    def __init__(self, enabled, hh, mm, days):
        self.enabled, self.hh, self.mm, self.days = enabled, hh, mm, days


class Sched:
    """State of alarm_task across ticks (one instance = one boot)."""

    def __init__(self, slots, now):
        self.slots = slots
        self.snooze = [0] * ALARM_COUNT
        self.last_fired = [0] * ALARM_COUNT
        self.armed_since = [now - CATCHUP_S] * ALARM_COUNT   # first-tick rule
        self.last_cfg = [(s.enabled, s.hh, s.mm, s.days) for s in slots]
        self.next = None
        self.fired = []          # (idx, mask, at, from_snooze)

    def tick(self, now):
        # re-arm changed slots
        for i, s in enumerate(self.slots):
            cfg = (s.enabled, s.hh, s.mm, s.days)
            if cfg != self.last_cfg[i]:
                self.armed_since[i] = now
                self.last_cfg[i] = cfg
            if now < self.armed_since[i]:
                self.armed_since[i] = now
            if not s.enabled:
                self.snooze[i] = 0

        wday = tm_wday(local(now))
        sched = [sched_today(now, s.hh, s.mm) for s in self.slots]

        fire_idx, fire_at, fire_snooze = -1, 0, False
        elig = [0] * ALARM_COUNT
        for i, s in enumerate(self.slots):
            if self.snooze[i] and now >= self.snooze[i] and (fire_idx < 0 or self.snooze[i] < fire_at):
                fire_idx, fire_at, fire_snooze = i, self.snooze[i], True
            if not s.enabled:
                continue
            for d in (0, 1):
                if elig[i]:
                    break
                cand = sched[i] - d * DAY
                if now < cand or now - cand >= CATCHUP_S:
                    continue
                if not days_match(s.days, (wday + 7 - d) % 7):
                    continue
                if cand != self.last_fired[i] and cand >= self.armed_since[i]:
                    elig[i] = cand
            if elig[i] and (fire_idx < 0 or elig[i] < fire_at or (elig[i] == fire_at and fire_snooze)):
                fire_idx, fire_at, fire_snooze = i, elig[i], False

        self.next = self.publish_next(now, wday, sched)

        if fire_idx < 0:
            return None
        if fire_snooze:
            self.snooze[fire_idx] = 0
            mask = 1 << fire_idx
        else:
            mask = 0
            for i in range(ALARM_COUNT):
                if elig[i] == fire_at:
                    self.last_fired[i] = fire_at
                    mask |= 1 << i
        ev = (fire_idx, mask, fire_at, fire_snooze)
        self.fired.append(ev)
        return ev

    def publish_next(self, now, wday, sched):
        best = None
        for i in range(ALARM_COUNT):
            if self.snooze[i] and self.snooze[i] > now and (best is None or self.snooze[i] < best[1]):
                best = (i, self.snooze[i], True)
        for i, s in enumerate(self.slots):
            if not s.enabled:
                continue
            for d in range(0, 8):
                cand = sched[i] + d * DAY
                if cand <= now or not days_match(s.days, (wday + d) % 7):
                    continue
                if best is None or cand < best[1]:
                    best = (i, cand, False)
                break
        return best

    # ring outcomes (alarm_fire's tail)
    def end_snooze(self, idx, now, minutes=9):
        self.snooze[idx] = now + minutes * 60

    def end_dismiss(self, mask):
        for j in range(ALARM_COUNT):
            if mask & (1 << j):
                self.snooze[j] = 0
                if self.slots[j].days & DAYS_ONCE and self.slots[j].enabled:
                    self.slots[j].enabled = 0


def at(y, m, d, hh, mm, ss=0):
    return int(dt.datetime(y, m, d, hh, mm, ss, tzinfo=TZ).timestamp())


FAILS = 0


def check(name, cond, detail=""):
    global FAILS
    print(("PASS " if cond else "FAIL ") + name + (("  " + detail) if detail and not cond else ""))
    if not cond:
        FAILS += 1


def main():
    # 2026-09-21 is a Monday
    MON = (2026, 9, 21)
    TUE = (2026, 9, 22)
    bit = lambda wd: 1 << wd   # tm_wday: Sun0 Mon1 Tue2 ... Sat6

    # 1. midnight crossing: 23:59 Monday-only alarm, clock stepped to Tue 00:01
    s = Sched([Slot(1, 23, 59, bit(1)), Slot(0, 7, 0, DAYS_ALL), Slot(0, 8, 0, DAYS_ALL)], at(*MON, 23, 50))
    s.tick(at(*MON, 23, 50))
    ev = s.tick(at(*TUE, 0, 1))
    check("midnight: yesterday's candidate on its own weekday fires", ev is not None and ev[0] == 0 and ev[2] == at(*MON, 23, 59))
    # same, but Tuesday-only: Monday 23:59 must NOT ring
    s = Sched([Slot(1, 23, 59, bit(2)), Slot(0, 7, 0, DAYS_ALL), Slot(0, 8, 0, DAYS_ALL)], at(*MON, 23, 50))
    s.tick(at(*MON, 23, 50))
    check("midnight: masked-out weekday does not fire", s.tick(at(*TUE, 0, 1)) is None)

    # 2. single-day mask, today's time passed -> next is +7 days
    s = Sched([Slot(1, 7, 0, bit(1)), Slot(0, 7, 30, DAYS_ALL), Slot(0, 8, 0, DAYS_ALL)], at(*MON, 9, 0))
    s.tick(at(*MON, 9, 0))
    check("next: single-day mask rolls to next week", s.next == (0, at(2026, 9, 28, 7, 0), False), str(s.next))

    # 3. two alarms at one instant -> one ring, both marked
    s = Sched([Slot(1, 7, 0, DAYS_ALL), Slot(1, 7, 0, DAYS_ALL), Slot(0, 8, 0, DAYS_ALL)], at(*MON, 6, 59))
    s.tick(at(*MON, 6, 59))
    ev = s.tick(at(*MON, 7, 0))
    check("same instant: one ring with both slots in the mask", ev == (0, 0b011, at(*MON, 7, 0), False), str(ev))
    s.end_dismiss(ev[1])
    check("same instant: nothing rings again in the window", s.tick(at(*MON, 7, 1)) is None)

    # 4. A1 snoozed to 07:09, A2 at 07:05 -> next is A2; A1's snooze survives A2's dismiss
    s = Sched([Slot(1, 7, 0, DAYS_ALL), Slot(1, 7, 5, DAYS_ALL), Slot(0, 8, 0, DAYS_ALL)], at(*MON, 6, 59))
    s.tick(at(*MON, 6, 59))
    ev = s.tick(at(*MON, 7, 0))
    check("A1 rings at 07:00", ev is not None and ev[0] == 0)
    s.end_snooze(0, at(*MON, 7, 0, 20))
    s.tick(at(*MON, 7, 1))
    check("next while snoozed: earlier scheduled A2 beats A1's snooze", s.next == (1, at(*MON, 7, 5), False), str(s.next))
    ev = s.tick(at(*MON, 7, 5))
    check("A2 rings at 07:05 with only A2 in the mask", ev == (1, 0b010, at(*MON, 7, 5), False), str(ev))
    s.end_dismiss(ev[1])
    s.tick(at(*MON, 7, 6))
    check("A1's snooze survives A2's dismiss", s.snooze[0] == at(*MON, 7, 9, 20) and s.next == (0, at(*MON, 7, 9, 20), True), str(s.next))
    ev = s.tick(at(*MON, 7, 9, 20))
    check("A1 snooze re-ring fires", ev == (0, 0b001, at(*MON, 7, 9, 20), True), str(ev))

    # 5. snooze vs scheduled at the same instant: scheduled wins the index, no double ring
    s = Sched([Slot(1, 7, 0, DAYS_ALL), Slot(1, 7, 9, DAYS_ALL), Slot(0, 8, 0, DAYS_ALL)], at(*MON, 6, 59))
    s.tick(at(*MON, 6, 59))
    s.tick(at(*MON, 7, 0))
    s.end_snooze(0, at(*MON, 7, 0))          # snooze until 07:09:00
    ev = s.tick(at(*MON, 7, 9))
    check("tie: scheduled slot rings first", ev == (1, 0b010, at(*MON, 7, 9), False), str(ev))
    s.end_dismiss(ev[1])
    ev = s.tick(at(*MON, 7, 9, 30))
    check("tie: the snooze still rings afterwards", ev == (0, 0b001, at(*MON, 7, 9), True), str(ev))

    # 6. once-mode: rings, snooze keeps it, dismiss switches it off
    s = Sched([Slot(1, 7, 0, DAYS_ALL | DAYS_ONCE), Slot(0, 7, 30, DAYS_ALL), Slot(0, 8, 0, DAYS_ALL)], at(*MON, 6, 59))
    s.tick(at(*MON, 6, 59))
    ev = s.tick(at(*MON, 7, 0))
    check("once: rings", ev is not None)
    s.end_snooze(0, at(*MON, 7, 0))
    check("once: still enabled after snooze", s.slots[0].enabled == 1)
    ev = s.tick(at(*MON, 7, 9))
    check("once: snooze re-ring fires", ev is not None and ev[3])
    s.end_dismiss(ev[1])
    s.tick(at(*MON, 7, 10))
    check("once: switched off after dismiss, nothing scheduled", s.slots[0].enabled == 0 and s.next is None, str(s.next))

    # 7. set alarm to 07:00 at 07:02 -> armed after it: no ring, next is tomorrow
    s = Sched([Slot(0, 7, 0, DAYS_ALL), Slot(0, 7, 30, DAYS_ALL), Slot(0, 8, 0, DAYS_ALL)], at(*MON, 6, 0))
    s.tick(at(*MON, 6, 0))
    s.slots[0].enabled = 1
    check("armed after the minute: no ring", s.tick(at(*MON, 7, 2)) is None)
    check("armed after the minute: next is tomorrow", s.next == (0, at(*TUE, 7, 0), False), str(s.next))

    # 8. reboot 30 s after the minute (first-tick arming from now - CATCHUP): rings unless already rang
    s = Sched([Slot(1, 7, 0, DAYS_ALL), Slot(0, 7, 30, DAYS_ALL), Slot(0, 8, 0, DAYS_ALL)], at(*MON, 7, 0, 30))
    check("reboot inside the window: rings", s.tick(at(*MON, 7, 0, 30)) is not None)
    s = Sched([Slot(1, 7, 0, DAYS_ALL), Slot(0, 7, 30, DAYS_ALL), Slot(0, 8, 0, DAYS_ALL)], at(*MON, 7, 0, 30))
    s.last_fired[0] = at(*MON, 7, 0)      # persisted fired-at
    check("reboot inside the window after it rang: silent", s.tick(at(*MON, 7, 0, 30)) is None)

    # 9. weekend mask on a Monday: next is Saturday
    s = Sched([Slot(1, 9, 0, DAYS_WEND), Slot(0, 7, 30, DAYS_ALL), Slot(0, 8, 0, DAYS_ALL)], at(*MON, 6, 0))
    s.tick(at(*MON, 6, 0))
    check("weekends: next is Saturday", s.next == (0, at(2026, 9, 26, 9, 0), False), str(s.next))
    # weekdays mask on a Friday evening: next is Monday
    s = Sched([Slot(1, 7, 0, DAYS_WEEK), Slot(0, 7, 30, DAYS_ALL), Slot(0, 8, 0, DAYS_ALL)], at(2026, 9, 25, 20, 0))
    s.tick(at(2026, 9, 25, 20, 0))
    check("weekdays: Friday night -> Monday", s.next == (0, at(2026, 9, 28, 7, 0), False), str(s.next))

    print()
    print("ALL PASS" if FAILS == 0 else f"{FAILS} FAILED")
    return 0 if FAILS == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
