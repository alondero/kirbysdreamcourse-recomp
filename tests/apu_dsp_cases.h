/* Shared synthetic register writes, applied at whole DSP sample boundaries.
 * Each adapter supplies start/write/step/read/finish. */
static void dsp_case(unsigned voice, unsigned pitch, unsigned filter,
                     int noise, int next_noise, unsigned change) {
  uint8_t ram[65536] = {0};
  unsigned base = voice << 4, mask = 1u << voice;
  ram[0x2001] = ram[0x2003] = 0x21;
  ram[0x2100] = 0xc3 | (filter << 2);
  for (unsigned i = 1; i <= 8; ++i) ram[0x2100 + i] = i * 17;
  test_dsp_start(ram);
  test_dsp_write(0x6c, 0x20 | (noise < 0 ? 0 : noise));
  test_dsp_write(0x0c, 127);
  test_dsp_write(0x1c, 127);
  test_dsp_write(base, 127);
  test_dsp_write(base + 1, 127);
  test_dsp_write(base + 2, pitch & 0xff);
  test_dsp_write(base + 3, pitch >> 8);
  test_dsp_write(base + 5, 0);
  test_dsp_write(base + 6, 0xe0);
  test_dsp_write(base + 7, 0x7f);
  test_dsp_write(0x5d, 0x20);
  test_dsp_write(0x3d, noise < 0 ? 0 : mask);
  test_dsp_write(0x7c, 0);
  for (unsigned i = 0; i < 256; ++i) {
    if (next_noise >= 0 && i == change) test_dsp_write(0x6c, 0x20 | next_noise);
    if (i == 10 || i == 100) test_dsp_write(0x4c, mask);
    if (i == 13 || i == 103) test_dsp_write(0x4c, 0);
    int16_t output[2];
    test_dsp_step(output);
    printf("dsp %u %u %u %d %d %u %u %d %d %u %u\n",
           voice, pitch, filter, noise, next_noise, change, i,
           output[0], output[1], test_dsp_read(0x7c), test_dsp_read(base + 8));
  }
  test_dsp_finish();
}

static void dsp_cases(void) {
  const unsigned pitches[] = {0, 0x08ab, 0x1000, 0x3fff};
  const int rates[] = {0, 10, 25, 31};
  const int transitions[][2] = {{0, 31}, {31, 0}, {25, 31}, {31, 25}, {10, 25}};
  const unsigned changes[] = {1, 10, 50, 100, 200};
  for (unsigned voice = 0; voice < 8; ++voice) {
    for (unsigned filter = 0; filter < 4; ++filter)
      for (unsigned p = 0; p < 4; ++p)
        dsp_case(voice, pitches[p], filter, -1, -1, 0);
    for (unsigned rate = 0; rate < 4; ++rate)
      dsp_case(voice, 0x1000, 0, rates[rate], -1, 0);
    for (unsigned transition = 0; transition < 5; ++transition)
      for (unsigned change = 0; change < 5; ++change)
        dsp_case(voice, 0x1000, 0, transitions[transition][0],
                 transitions[transition][1], changes[change]);
  }
}
