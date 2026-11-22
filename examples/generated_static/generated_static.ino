/*
 * generated_static -- no parser, no filesystem, no heap.
 *
 * tipper_fis.h is committed output of:
 *   python3 tools/fis2h.py examples/generated_static/tipper.fis \
 *       --source examples/generated_static/tipper.fis \
 *       -o examples/generated_static/tipper_fis.h
 * CI regenerates it and fails on any diff, so edit the .fis, never the header.
 *
 * This is the example to use on AVR: everything lands in flash (.rodata).
 */
#include <fz.h>
#include "tipper_fis.h"

static fz_work_t work;

static void show(float service, float food) {
  const float in[2] = {service, food};
  float out[1];
  fz_eval_all(&fz_fis_tipper, in, out, &work);
  Serial.print(F("service="));
  Serial.print(service);
  Serial.print(F(" food="));
  Serial.print(food);
  Serial.print(F(" -> tip="));
  Serial.println(out[0], 3);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
  }
  Serial.print(F("static FIS: "));
  Serial.print(fz_fis_tipper.n_in);
  Serial.print(F(" inputs, "));
  Serial.print(fz_fis_tipper.n_rules);
  Serial.println(F(" rules"));

  show(0.0f, 0.0f);
  show(2.0f, 7.0f);
  show(5.0f, 5.0f);
  show(10.0f, 10.0f);
}

void loop() {}
