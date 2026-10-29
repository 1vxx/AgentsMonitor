#pragma once

#include <stdint.h>

// The pets, as pixel art: 12 columns, up to 11 rows, drawn bottom-aligned so
// a taller one (the rabbit's ears) rises above the rest. Each character is a
// pixel: '.' is empty, 'E' an eye (left dark, a dot of the face's black),
// anything else a colour from the animal's inks. Eyes are two pixels tall;
// closing one fills its upper pixel with `lid`, smiling its lower one.
namespace sprites {

constexpr int W = 12, BASE_ROWS = 10, MAX_ROWS = 11;

struct Ink {
  char key;
  uint32_t rgb;
};

struct Animal {
  const char *name;  // in capitals, for "CAT NEEDS YOU"
  char lid;
  Ink inks[5];
  const char *rows[MAX_ROWS];  // nullptr after the last
};

constexpr int COUNT = 8;
const Animal ANIMALS[COUNT] = {
    {"CAT",
     'B',
     {{'B', 0xffa858}, {'L', 0xffd6c4}, {'N', 0xff7896}, {'C', 0xd85870}},
     {"B..........B", "BB........BB", "BLB......BLB", "BBBBBBBBBBBB", "BBBBBBBBBBBB", "BBEBBBBBBEBB", "BBEBBBBBBEBB",
      "CBBBBNNBBBBC", "BBBBLLLLBBBB", ".BBBBBBBBBB."}},
    {"DOG",
     'B',
     {{'B', 0xe2b274}, {'D', 0x96623a}, {'L', 0xfceacc}, {'N', 0x3a2418}},
     {"..BBBBBBBB..", ".BBBBBBBBBB.", "DDBBBBBBBBDD", "DDBEBBBBEBDD", "DDBEBBBBEBDD", "DDBBBBBBBBDD", "D.BBLLLLBB.D",
      "..BLLNNLLB..", "..BLLLLLLB..", "...BBBBBB..."}},
    {"BEAR",
     'B',
     {{'B', 0xc48654}, {'L', 0xeeccA4}, {'N', 0x4a2c1c}, {'C', 0xd06070}},
     {".BB......BB.", "BLBB....BBLB", "BBBBBBBBBBBB", "BBBBBBBBBBBB", "BBEBBBBBBEBB", "BBEBBBBBBEBB", "BBBBLLLLBBBB",
      "CBBLLNNLLBBC", "BBBBLLLLBBBB", ".BBBBBBBBBB."}},
    {"BUNNY",
     'B',
     {{'B', 0xececf6}, {'L', 0xffa8be}, {'N', 0xff7896}, {'C', 0xe88aa0}},
     {"..BB....BB..", "..BL....LB..", "..BL....LB..", "..BB....BB..", ".BBBBBBBBBB.", "BBBBBBBBBBBB", "BBEBBBBBBEBB",
      "BBEBBBBBBEBB", "CBBBBNNBBBBC", "BBBBBLLBBBBB", ".BBBBBBBBBB."}},
    {"PENGUIN",
     'L',
     {{'B', 0x6a82c8}, {'L', 0xeef0fc}, {'N', 0xffaa3c}, {'Y', 0xffaa3c}},
     {"...BBBBBB...", "..BBBBBBBB..", ".BBLLBBLLBB.", ".BLELLLLELB.", ".BLELLLLELB.", ".BLLLNNLLLB.", "BBLLLLLLLLBB",
      "BBLLLLLLLLBB", ".BLLLLLLLLB.", "..YY....YY.."}},
    {"FROG",
     'L',
     {{'B', 0x76d676}, {'L', 0xe8fae8}, {'C', 0xd8707c}, {'M', 0x2e6a32}},
     {".LLL....LLL.", "LELL....LELL", "LELL....LELL", "BLLLBBBBLLLB", "BBBBBBBBBBBB", "BBBBBBBBBBBB", "BCBBBBBBBBCB",
      "BBMMMMMMMMBB", "BBBBBBBBBBBB", ".BBBBBBBBBB."}},
    {"FOX",
     'B',
     {{'B', 0xff8a3c}, {'L', 0xfff4ea}, {'D', 0x8a4428}, {'N', 0x2a1a14}},
     {"B..........B", "BB........BB", "BDB......BDB", "BBBBBBBBBBBB", "BBEBBBBBBEBB", "BBEBBBBBBEBB", "LLBBBBBBBBLL",
      ".LLLLNNLLLL.", "..LLLLLLLL..", "....LLLL...."}},
    {"OWL",
     'L',
     {{'B', 0xa8885e}, {'L', 0xf4ead2}, {'N', 0xffb43c}, {'M', 0xd2b88c}},
     {"B..........B", "BB.BBBBBB.BB", "BBBBBBBBBBBB", "BLLLBBBBLLLB", "LLELLBBLLELL", "LLELLNNLLELL", "BLLLBNNBLLLB",
      "BBBBBBBBBBBB", "BBMBMBBMBMBB", ".BBBBBBBBBB."}},
};

// Small marks drawn beside a pet, one cell a pixel at size 2 (two at size 4)
const char *const HEART[] = {".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."};
const char *const PUFF[] = {".##..", "####.", ".####", "..##."};

}  // namespace sprites
