#!/usr/bin/env python3
"""Write a deterministic input script for the headless runners (tests/gens_headless_trace.h).

Each line is "P1 P2" as hexadecimal pressed masks. The script waits two seconds, then presses
menu-style buttons in short bursts separated by pauses, so menus and game modes are visited.
"""

import argparse
import random


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frames", type=int)
    parser.add_argument("seed", type=int)
    parser.add_argument("output")
    args = parser.parse_args()
    rng = random.Random(args.seed)
    lines = ["0 0"] * args.frames
    buttons = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024]
    frame = 120
    while frame < args.frames:
        kind = rng.random()
        if kind < 0.35:
            pressed = rng.choice([1, 2, 2, 2])
        elif kind < 0.55:
            pressed = rng.choice([16, 64, 128])
        elif kind < 0.7:
            pressed = 32
        else:
            pressed = rng.choice(buttons) | (rng.choice(buttons) if rng.random() < 0.3 else 0)
        hold = rng.randint(2, 12)
        for i in range(frame, min(args.frames, frame + hold)):
            lines[i] = "%x 0" % pressed
        frame += hold + rng.randint(20, 240)
    with open(args.output, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()