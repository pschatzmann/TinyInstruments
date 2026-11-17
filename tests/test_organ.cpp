#include "TinyInstruments.h"
#include "TestHelper.h"

// Tonewheel organ (synthesized) and rotary speaker

static const int RATE = 44100;

struct Render {
  SampleSynthesizer synth;
  std::vector<float> left, right;
  explicit Render(const Instrument& inst, ChannelEffect* effect = nullptr) {
    synth.begin(RATE);
    synth.setInstrument(0, inst);
    synth.setEffect(0, effect);
  }
  /// renders the indicated time and returns the left channel of it
  std::vector<float> play(float seconds) {
    size_t n = seconds * RATE;
    std::vector<float> l(n), r(n);
    synth.renderFloat(l.data(), r.data(), n);
    left = l;
    right = r;
    return l;
  }
};

int main() {
  // pitch of the drawbars: 8' = key, 16' = octave below, 4' octave above,
  // 2 2/3' = fifth above the octave (equal tempered)
  struct {
    const char* drawbars;
    double semitones;
  } pitches[] = {{"008000000", 0}, {"800000000", -12}, {"000800000", 12}, {"000080000", 19}};
  for (auto& p : pitches) {
    TonewheelOrgan organ(p.drawbars);
    organ.setKeyClick(0.0f);
    Instrument inst = OrganInstrument(organ);
    Render r(inst);
    r.synth.noteOn(0, 57, 100);  // A3
    r.play(0.2f);
    auto x = r.play(0.4f);
    double expected = 220.0 * pow(2.0, p.semitones / 12.0);
    double freq = estimateFrequency(x, RATE, expected * 0.8, expected * 1.25);
    printf("drawbars %s: %.2f Hz (expected %.2f)\n", p.drawbars, freq, expected);
    CHECK_NEAR(freq, expected, expected * 0.003);
  }

  // foldback: the 1' drawbar of the highest key stays below 5.9 kHz
  {
    TonewheelOrgan organ("000000008");
    organ.setKeyClick(0.0f);
    Instrument inst = OrganInstrument(organ);
    Render r(inst);
    r.synth.noteOn(0, 96, 100);  // C7: 1' would be 16.7 kHz
    r.play(0.1f);
    auto x = r.play(0.3f);
    double expected = 2093.0 * 8 / 4;  // folded back two octaves
    double freq = estimateFrequency(x, RATE, expected * 0.8, expected * 1.25);
    printf("foldback C7 1': %.1f Hz (expected %.1f)\n", freq, expected);
    CHECK_NEAR(freq, expected, expected * 0.01);
  }

  // sustain: constant level while the key is held, fast release
  {
    TonewheelOrgan organ("888000000");
    Instrument inst = OrganInstrument(organ);
    Render r(inst);
    r.synth.noteOn(0, 60, 100);
    double early = rms(r.play(0.3f), RATE / 10);
    r.play(2.0f);
    double late = rms(r.play(0.3f));
    printf("sustain: %.1f dB after 2 s\n", 20 * log10(late / early));
    CHECK_NEAR(20 * log10(late / early), 0.0, 0.5);
    CHECK(early > 0.05 && early < 0.5);
    r.synth.noteOff(0, 60);
    r.play(0.1f);
    double released = rms(r.play(0.1f));
    printf("release: %.1f dB after 0.1 s\n", 20 * log10(released / late + 1e-12));
    CHECK(released < late * 0.001);
    // not velocity sensitive
    r.synth.noteOn(0, 60, 20);
    r.play(0.1f);
    double soft = rms(r.play(0.3f));
    CHECK_NEAR(20 * log10(soft / late), 0.0, 0.5);
  }

  // percussion: decays, single triggered (not when another key is held)
  {
    TonewheelOrgan organ("008000000");
    organ.setPercussion(true, true, true, false);
    organ.setKeyClick(0.0f);
    Instrument inst = OrganInstrument(organ);
    Render r(inst);
    r.synth.noteOn(0, 60, 100);
    double attack = rms(r.play(0.05f));
    r.play(1.0f);
    double held = rms(r.play(0.2f));
    printf("percussion: attack %+.1f dB over the sustain\n", 20 * log10(attack / held));
    CHECK(attack > held * 1.5);
    // legato: the second key has no percussion
    r.synth.noteOn(0, 64, 100);
    double second = rms(r.play(0.05f));
    r.play(1.0f);
    double both = rms(r.play(0.2f));
    printf("percussion legato: attack %+.1f dB over the sustain\n", 20 * log10(second / both));
    CHECK(second < both * 1.2);
  }

  // drawbars with midi control changes while the note is playing
  {
    TonewheelOrgan organ("008000000");
    organ.setKeyClick(0.0f);
    Instrument inst = OrganInstrument(organ);
    Render r(inst);
    r.synth.noteOn(0, 60, 100);
    double before = rms(r.play(0.3f), RATE / 10);
    r.synth.controlChange(0, 14, 0);    // 8' off
    r.synth.controlChange(0, 15, 127);  // 4' on
    r.play(0.05f);
    auto x = r.play(0.3f);
    double freq = estimateFrequency(x, RATE, 400, 650);
    printf("CC drawbars: %.1f Hz (4'), level %+.1f dB\n", freq, 20 * log10(rms(x) / before));
    CHECK_NEAR(freq, 523.25, 2.0);
    CHECK(organ.drawbar(2) == 0 && organ.drawbar(3) == 8);
  }

  // rotary speaker: stereo, amplitude modulation at the rotor speed, speed
  // change with acceleration
  {
    TonewheelOrgan organ("008000000");
    organ.setKeyClick(0.0f);
    Instrument inst = OrganInstrument(organ);
    RotarySpeaker leslie;
    Render r(inst, &leslie);
    r.synth.noteOn(0, 96, 100);  // C7: only the horn
    auto modulation = [&](float seconds) {
      // envelope in 20 ms frames, rate of the modulation from its zero crossings
      auto x = r.play(seconds);
      size_t frame = RATE / 50;
      std::vector<double> env;
      for (size_t i = 0; i + frame <= x.size(); i += frame) env.push_back(rms(x, i, i + frame));
      double mean = 0;
      for (double e : env) mean += e;
      mean /= env.size();
      int crossings = 0;
      double lo = 1e9, hi = 0;
      for (size_t i = 1; i < env.size(); i++) {
        if ((env[i - 1] - mean) * (env[i] - mean) < 0) crossings++;
        lo = std::min(lo, env[i]);
        hi = std::max(hi, env[i]);
      }
      double depth = 20 * log10(hi / lo);
      return std::make_pair(crossings / 2.0 / seconds, depth);
    };
    r.play(1.0f);
    auto slow = modulation(4.0f);
    printf("leslie slow: %.2f Hz (horn %.2f Hz), depth %.1f dB\n", slow.first, leslie.hornSpeed(), slow.second);
    CHECK_NEAR(slow.first, 0.8, 0.3);
    CHECK(slow.second > 3.0);
    double diff = 0;
    for (size_t i = 0; i < r.left.size(); i++) diff += fabs(r.left[i] - r.right[i]);
    CHECK(diff / r.left.size() > 0.01);  // stereo
    r.synth.controlChange(0, 1, 127);    // fast
    r.play(0.3f);
    float horn_mid = leslie.hornSpeed(), drum_mid = leslie.drumSpeed();
    printf("leslie after 0.3 s: horn %.2f Hz, drum %.2f Hz\n", horn_mid, drum_mid);
    CHECK(horn_mid > 1.5f && horn_mid < 6.5f);
    CHECK(drum_mid < horn_mid);  // the drum is slower
    r.play(4.0f);
    auto fast = modulation(2.0f);
    printf("leslie fast: %.2f Hz (horn %.2f Hz), depth %.1f dB\n", fast.first, leslie.hornSpeed(), fast.second);
    CHECK_NEAR(fast.first, 6.8, 1.0);
    // drive: more harmonics -> no change in pitch, louder overtones
    leslie.setDrive(1.0f);
    auto driven = r.play(0.5f);
    CHECK(std::isfinite(rms(driven)) && rms(driven) > 0.01);
  }

  // organ and piano on different channels share the synthesizer
  {
    TonewheelOrgan organ;
    Instrument inst = OrganInstrument(organ);
    SampleSynthesizer synth;
    synth.begin(RATE);
    synth.setInstrument(-1, inst);
    for (int n = 48; n < 58; n++) synth.noteOn(0, n, 100);  // 10 notes
    std::vector<float> l(RATE), r(RATE);
    synth.renderFloat(l.data(), r.data(), RATE);
    double level = rms(l);
    printf("10 notes: rms %.3f, %d notes\n", level, synth.activeNotes());
    CHECK(synth.activeNotes() == 10);
    CHECK(level < 0.9);
  }
  return testResult("test_organ");
}
