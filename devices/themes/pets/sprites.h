#pragma once

#include <stdint.h>

// The pets, as pixel art: 12 columns, up to 11 rows, drawn bottom-aligned so
// a taller one (the rabbit's ears) rises above the rest. Each character is a
// pixel: '.' is empty, 'E' an eye (left dark, a dot of the face's black,
// unless the animal gives E an ink and its eyes glow), anything else a colour
// from the animal's inks. Eyes are two pixels tall; closing one fills its
// upper pixel with `lid`, smiling its lower one. A `fine` animal is drawn at
// twice the detail, its pixels half the size, in the same room.
namespace sprites {

constexpr int W = 12, BASE_ROWS = 10, MAX_ROWS = 20;

struct Ink {
  char key;
  uint32_t rgb;
};

struct Animal {
  const char *name;  // in capitals, for "CAT NEEDS YOU"
  char lid;
  Ink inks[5];
  const char *rows[MAX_ROWS];  // nullptr after the last
  const char *flap[MAX_ROWS];  // a second frame with its wings down, if it has wings
  bool fine;                   // 24 columns by 20 rows, at half the size
};

constexpr int COUNT = 8;
const Animal ANIMALS[COUNT] = {
    {"CAT",
     'B',
     {{'B', 0xffa858}, {'L', 0xffd6c4}, {'N', 0xff7896}, {'C', 0xd85870}},
     {"B..........B", "BB........BB", "BLB......BLB", "BBBBBBBBBBBB", "BBBBBBBBBBBB", "BBEBBBBBBEBB", "BBEBBBBBBEBB",
      "CBBBBNNBBBBC", "BBBBLLLLBBBB", ".BBBBBBBBBB."}, {}, false},
    {"DOG",
     'B',
     {{'B', 0xe2b274}, {'D', 0x96623a}, {'L', 0xfceacc}, {'N', 0x3a2418}},
     {"..BBBBBBBB..", ".BBBBBBBBBB.", "DDBBBBBBBBDD", "DDBEBBBBEBDD", "DDBEBBBBEBDD", "DDBBBBBBBBDD", "D.BBLLLLBB.D",
      "..BLLNNLLB..", "..BLLLLLLB..", "...BBBBBB..."}, {}, false},
    {"BEAR",
     'B',
     {{'B', 0xc48654}, {'L', 0xeeccA4}, {'N', 0x4a2c1c}, {'C', 0xd06070}},
     {".BB......BB.", "BLBB....BBLB", "BBBBBBBBBBBB", "BBBBBBBBBBBB", "BBEBBBBBBEBB", "BBEBBBBBBEBB", "BBBBLLLLBBBB",
      "CBBLLNNLLBBC", "BBBBLLLLBBBB", ".BBBBBBBBBB."}, {}, false},
    {"BUNNY",
     'B',
     {{'B', 0xececf6}, {'L', 0xffa8be}, {'N', 0xff7896}, {'C', 0xe88aa0}},
     {"..BB....BB..", "..BL....LB..", "..BL....LB..", "..BB....BB..", ".BBBBBBBBBB.", "BBBBBBBBBBBB", "BBEBBBBBBEBB",
      "BBEBBBBBBEBB", "CBBBBNNBBBBC", "BBBBBLLBBBBB", ".BBBBBBBBBB."}, {}, false},
    {"PENGUIN",
     'L',
     {{'B', 0x6a82c8}, {'L', 0xeef0fc}, {'N', 0xffaa3c}, {'Y', 0xffaa3c}},
     {"...BBBBBB...", "..BBBBBBBB..", ".BBLLBBLLBB.", ".BLELLLLELB.", ".BLELLLLELB.", ".BLLLNNLLLB.", "BBLLLLLLLLBB",
      "BBLLLLLLLLBB", ".BLLLLLLLLB.", "..YY....YY.."}, {}, false},
    {"FROG",
     'L',
     {{'B', 0x76d676}, {'L', 0xe8fae8}, {'C', 0xd8707c}, {'M', 0x2e6a32}},
     {".LLL....LLL.", "LELL....LELL", "LELL....LELL", "BLLLBBBBLLLB", "BBBBBBBBBBBB", "BBBBBBBBBBBB", "BCBBBBBBBBCB",
      "BBMMMMMMMMBB", "BBBBBBBBBBBB", ".BBBBBBBBBB."}, {}, false},
    {"FOX",
     'B',
     {{'B', 0xff8a3c}, {'L', 0xfff4ea}, {'D', 0x8a4428}, {'N', 0x2a1a14}},
     {"B..........B", "BB........BB", "BDB......BDB", "BBBBBBBBBBBB", "BBEBBBBBBEBB", "BBEBBBBBBEBB", "LLBBBBBBBBLL",
      ".LLLLNNLLLL.", "..LLLLLLLL..", "....LLLL...."}, {}, false},
    {"OWL",
     'L',
     {{'B', 0xa8885e}, {'L', 0xf4ead2}, {'N', 0xffb43c}, {'M', 0xd2b88c}},
     {"B..........B", "BB.BBBBBB.BB", "BBBBBBBBBBBB", "BLLLBBBBLLLB", "LLELLBBLLELL", "LLELLNNLLELL", "BLLLBNNBLLLB",
      "BBBBBBBBBBBB", "BBMBMBBMBMBB", ".BBBBBBBBBB."}, {}, false},
};

// The legend: a pet's session that has done something rare becomes a dragon,
// side on, facing right: swept horn, fanned bat wing it beats down and up,
// claws and a spade-tipped tail, at twice the other pets' detail. Its
// colours come from one of four coats, picked at random when it is called.
const Animal DRAGON = {
    "DRAGON",
    'B',
    {},
    {".....R....R..H..........", ".R...RW...RWW.HH........", ".RWW.RWWW.RWWW.HBBB.....", "RWWWWRWWWWRWWWWSBBBBB...", ".RWWWRWWWWRWWWWSBBEBBB..", "..RWWWRWWWRWWWWSBBEBBBBB", "...RWWRWWWRWWMMDBBLLLLLT", ".....RWWRWWWRMMDBBLLLL..", ".......RWWRRMDDBBLLL....", "..........RRDDBBBLLL....", "........DDDDBBBBBLLL....", "......SDDDBBBBBBBLLL....", ".....DDDDBBBBBBBBLLL....", "S...DDDDBBBBBBBBBLLL....", "SS..DDDBBBBBBBBBBLL.....", "SD.DDDDBBBBDDBBBBLL.....", ".DDDDDDBBBD..DBBBBL.....", "......DBBBD...DBBB......", "......DBBD....DBBD......", ".....TTDBD...TTDBD......"},
    {".............H..........", "..............HH........", "...............HBBB.....", "...............SBBBBB...", "...............SBBEBBB..", "...............SBBEBBBBB", "....RRRRRRR....DBBLLLLLT", "...RWWWWWWWR...DBBLLLL..", "..RWWRWWWRWR.DDBBLLL....", ".RWWRWWWRWWRDDBBBLLL....", "RWWRWWWRWWRDBBBBBLLL....", "WW.WWWSWWRBBBBBBBLLL....", ".W..WDDWDBBBBBBBBLLL....", "S...DDDDBBBBBBBBBLLL....", "SS..DDDBBBBBBBBBBLL.....", "SD.DDDDBBBBDDBBBBLL.....", ".DDDDDDBBBD..DBBBBL.....", "......DBBBD...DBBB......", "......DBBD....DBBD......", ".....TTDBD...TTDBD......"},
    true,
};

struct Coat {
  const char *name;  // "COSMIC", for "COSMIC DRAGON"
  Ink inks[10];      // B body, D its shade, L belly, S spikes, H horn, W wing, M its shade, R wing bones, E eyes, T claws
  uint32_t fire[4];  // its fire, sparks and light, from the white-hot core out to the edge
};
constexpr int COATS = 4;
const Coat COAT[COATS] = {
    {"CRIMSON",
     {{'B', 0xd8392b}, {'D', 0x8c1d24}, {'L', 0xffb04a}, {'S', 0xff7a2a}, {'H', 0xf3e2c0},
      {'W', 0xa3242f}, {'M', 0x6a1424}, {'R', 0xff8a4a}, {'E', 0xffe04a}, {'T', 0xffffff}},
     {0xfff3c8, 0xffc94a, 0xff7a1a, 0xff3b1f}},
    {"COSMIC",
     {{'B', 0x6a4cf0}, {'D', 0x3a2aa0}, {'L', 0x8fe3ff}, {'S', 0xa58bff}, {'H', 0xe9ecff},
      {'W', 0x2b1f7a}, {'M', 0x1b1450}, {'R', 0x7fe0ff}, {'E', 0x7ffcff}, {'T', 0xffffff}},
     {0xffffff, 0x9fe8ff, 0x8f6bff, 0x4a32c8}},
    {"OBSIDIAN",
     {{'B', 0x565670}, {'D', 0x30303e}, {'L', 0xffcf5a}, {'S', 0xffcf5a}, {'H', 0xffe9b0},
      {'W', 0x26262f}, {'M', 0x18181f}, {'R', 0xffcf5a}, {'E', 0xff3a2a}, {'T', 0xffe9b0}},
     {0xfff3c8, 0xffcf5a, 0xff9a2a, 0xff3a2a}},
    {"CELESTIAL",
     {{'B', 0xeeeae0}, {'D', 0xbdb5a0}, {'L', 0xffd36a}, {'S', 0xffd36a}, {'H', 0xffd36a},
      {'W', 0x3a62c8}, {'M', 0x24408c}, {'R', 0xffd36a}, {'E', 0x44d4ff}, {'T', 0xffffff}},
     {0xffffff, 0xffe9a0, 0x6ac8ff, 0x3a62c8}},
};
constexpr int COSMIC = 1;  // whose wings have stars in them

// Small marks drawn beside a pet, one cell a pixel at size 2 (two at size 4)
const char *const HEART[] = {".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."};
const char *const PUFF[] = {".##..", "####.", ".####", "..##."};

}  // namespace sprites
