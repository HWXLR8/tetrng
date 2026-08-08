#!/usr/bin/env python3

import argparse


pieces = {
    0: 'I',
    1: 'Z',
    2: 'S',
    3: 'J',
    4: 'L',
    5: 'O',
    6: 'T'
}

def hash(x):
    m = 0x41C64E6D
    b = 12345
    y = m*x + b
    y &= 0xFFFFFFFF
    return y

def num2piece(x):
    x = x >> 10
    x = x & 0x7FFF
    x = x % 7
    return pieces[x]


def seed_search(goal):
    # goal will double as a history -- no sense to shuffling history around in the loop when it has a known state
    goal = 'SSZ' + goal

    for seed in range(0xFFFFFFFF):

        # first piece handled differently -- straight mod 7
        i = 3
        rng_state = hash(seed)
        piece = num2piece(rng_state)
        if piece != goal[i]:
            continue
        i += 1

        # other pieces get up to 6 rolls to avoid a 4-history
        while i < len(goal):
            for rolls in range(5): # despite the 5 there is a sneaky 6th roll, see the second hash
                rng_state = hash(rng_state)
                piece = num2piece(rng_state)
                history = goal[i-4:i]
                if piece not in history:
                    break
                rng_state = hash(rng_state) # rerolls hash an extra time, also doubles as a sneaky 6th roll in a loop of 5
                piece = num2piece(rng_state)
            if piece != goal[i]:
                break
            i += 1
        if i == len(goal):
            print("matching seed:", seed)
            break
    print("complete")


def main():
    parser = argparse.ArgumentParser(
        description="Search for an RNG seed that produces a Tetris piece sequence."
    )
    parser.add_argument("goal", help="piece sequence to search for")
    args = parser.parse_args()
    seed_search(args.goal)


if __name__ == "__main__":
    main()
