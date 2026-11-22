/*
 * tipper_mamdani -- runtime .fis parsing (Mamdani, 2 in / 1 out / 3 rules).
 *
 * The FIS text lives in flash; the parser fills caller-owned pools (no malloc).
 * On AVR parts under 4 KB SRAM fz.h disables the parser, so the sketch says so
 * instead of failing to build. Use generated_static/ or ESP32/STM32 for those.
 *
 * Serial monitor at 115200: prints the tip for a few (service, food) pairs.
 */
#include <fz.h>

#if !FZ_HAS_PARSER

void setup() {
  Serial.begin(115200);
  Serial.println(F("fz: parser disabled on this target (see FZ_ARDUINO_SAFE)"));
}
void loop() {}

#else

/* Plain RAM string: the parser reads it directly, and PROGMEM would need a
 * _P variant the library does not expose. On 2 KB AVR the parser is off anyway. */
static const char FIS_TEXT[] =
    "[System]\n"
    "Name='tipper'\n"
    "Type='mamdani'\n"
    "Version=2.0\n"
    "NumInputs=2\n"
    "NumOutputs=1\n"
    "NumRules=3\n"
    "AndMethod='min'\n"
    "OrMethod='max'\n"
    "ImpMethod='min'\n"
    "AggMethod='max'\n"
    "DefuzzMethod='centroid'\n"
    "[Input1]\n"
    "Name='service'\n"
    "Range=[0 10]\n"
    "NumMFs=3\n"
    "MF1='poor':'gaussmf',[1.5 0]\n"
    "MF2='good':'gaussmf',[1.5 5]\n"
    "MF3='excellent':'gaussmf',[1.5 10]\n"
    "[Input2]\n"
    "Name='food'\n"
    "Range=[0 10]\n"
    "NumMFs=2\n"
    "MF1='rancid':'gaussmf',[1.5 0]\n"
    "MF2='delicious':'gaussmf',[1.5 10]\n"
    "[Output1]\n"
    "Name='tip'\n"
    "Range=[0 30]\n"
    "NumMFs=3\n"
    "MF1='cheap':'trimf',[0 5 10]\n"
    "MF2='average':'trimf',[10 15 20]\n"
    "MF3='generous':'trimf',[20 25 30]\n"
    "[Rules]\n"
    "1 1, 1 (1) : 1\n"
    "2 2, 2 (1) : 1\n"
    "3 2, 3 (1) : 1\n";

/* Caller-owned pools -- the parser never allocates. This FIS needs 8 MFs,
 * 2 input vars, 1 output var, 3 rules and 15 index slots; the rest is headroom
 * so small edits to FIS_TEXT do not trip FZ_ERR_POOL. */
static fz_mf_t mf_pool[16];
static fz_var_t in_pool[4];
static fz_var_t out_pool[4];
static fz_rule_t rule_pool[8];
static int16_t idx_pool[32];
static fz_fis_t fis;
static fz_work_t work;

static void show(float service, float food) {
  const float in[2] = {service, food};
  float out[1];
  fz_eval_all(&fis, in, out, &work);
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

  int rc = fz_fis_parse_text(FIS_TEXT, sizeof FIS_TEXT, &fis,
                             mf_pool, 16,
                             in_pool, 4,
                             out_pool, 4,
                             rule_pool, 8,
                             idx_pool, 32);
  if (rc != FZ_OK) {
    /* FZ_ERR_POOL means the FIS outgrew the pools above: enlarge them. */
    Serial.print(F("fz_fis_parse_text failed: "));
    Serial.println(rc);
    return;
  }
  Serial.print(F("FIS: "));
  Serial.print(fis.n_in);
  Serial.print(F(" inputs, "));
  Serial.print(fis.n_out);
  Serial.print(F(" output(s), "));
  Serial.print(fis.n_rules);
  Serial.println(F(" rules"));

  show(0.0f, 0.0f);
  show(5.0f, 5.0f);
  show(10.0f, 10.0f);
}

void loop() {}

#endif /* FZ_HAS_PARSER */
