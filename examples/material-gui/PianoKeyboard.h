#pragma once
#include <algorithm>
#include <functional>

#include <TinyMaterialDesign.h>

/**
 * @brief Material styled piano keyboard widget for TinyMaterialDesign.
 *
 * The gestures of TinyMaterialDesign report a tap only when the finger is
 * released, which is too late for a piano. So the keyboard is played from the
 * raw touch points (setTouches(), once per loop): a key sounds as soon as it is
 * touched, sliding over the keys plays a glissando and a second finger is
 * supported if the touch driver reports one. The velocity depends on where the
 * key is hit: softer at the top, louder at the front.
 *
 * Notes played from somewhere else (e.g. a midi input) can be shown with
 * markNote().
 */
template <typename RGB_T = tinymd::TINYMD_DEFAULT_RGB_T>
class PianoKeyboard : public tinymd::Widget<RGB_T> {
 public:
  static constexpr int kMaxTouches = 2;

  PianoKeyboard(tinymd::Bounds bounds, int whiteKeys = 10) : white_keys(whiteKeys) {
    this->bounds = bounds;
  }

  /// Called with (note, velocity) for a note on and (note, 0) for a note off
  std::function<void(uint8_t note, uint8_t velocity)> onKey;

  /// Lowest note: a C (or any other white key)
  void setFirstNote(int note) {
    releaseAll();
    first_note = note;
    dirty = true;
  }
  int firstNote() const { return first_note; }
  int lastNote() const { return noteOfWhite(white_keys - 1); }

  /// Shows a note which is played from somewhere else
  void markNote(uint8_t note, bool on) {
    uint32_t& word = marked[(note >> 5) & 3];
    uint32_t bit = 1u << (note & 31);
    bool was = word & bit;
    if (on) word |= bit; else word &= ~bit;
    if (was != on && note >= first_note && note <= lastNote()) dirty = true;
  }

  void clearMarks() {
    for (auto& w : marked) w = 0;
    dirty = true;
  }

  /// Raw touch points of this loop (screen coordinates): plays the keys
  void setTouches(const tinygpu::Point* points, int count) {
    int notes[kMaxTouches];
    uint8_t velocities[kMaxTouches];
    int n = 0;
    if (this->visible) {
      for (int j = 0; j < count && n < kMaxTouches; j++) {
        uint8_t velocity;
        int note = noteAt(points[j].x, points[j].y, velocity);
        if (note >= 0 && !contains(notes, n, note)) {
          notes[n] = note;
          velocities[n++] = velocity;
        }
      }
    }
    // released keys
    for (int j = 0; j < held_count; j++) {
      if (!contains(notes, n, held[j])) {
        if (onKey) onKey(held[j], 0);
        dirty = true;
      }
    }
    // new keys
    for (int j = 0; j < n; j++) {
      if (!contains(held, held_count, notes[j])) {
        if (onKey) onKey(notes[j], velocities[j]);
        dirty = true;
      }
    }
    for (int j = 0; j < n; j++) held[j] = notes[j];
    held_count = n;
  }

  /// Releases all keys which are pressed on the screen
  void releaseAll() {
    setTouches(nullptr, 0);
  }

  void draw(tinygpu::ISurface<RGB_T>& target) override {
    using tinymd::toPx;
    const auto& colors = this->theme().colors;
    const tinymd::Bounds& b = this->bounds;
    const int32_t black_h = b.h * 58 / 100;
    target.fillRect(toPx(b.x), toPx(b.y), toPx(b.w), toPx(b.h), colors.outline);
    // the keys keep their natural look in the light and in the dark theme:
    // white keys with the lighter, black keys with the darker of surface and
    // onSurface
    RGB_T white = colors.surface, black = colors.onSurface;
    if (brightness(white) < brightness(black)) std::swap(white, black);
    const RGB_T label_color = tinymd::blend(white, black, 0.6f);
    // white keys
    tinygpu::IFont<RGB_T>& font = *this->theme().typography.label;
    for (int k = 0; k < white_keys; k++) {
      int32_t x0 = whiteX(k), x1 = whiteX(k + 1);
      int note = noteOfWhite(k);
      RGB_T color = white;
      if (isPressed(note)) color = tinymd::blend(white, colors.primary, 0.55f);
      else if (isMarked(note)) color = tinymd::blend(white, colors.secondary, 0.35f);
      target.fillRoundRect(toPx(x0 + 1), toPx(b.y), toPx(x1 - x0 - 1), toPx(b.h - 1), 3, color);
      if (note % 12 == 0) {  // label of the C keys, e.g. "C4"
        char label[6];
        snprintf(label, sizeof(label), "C%d", note / 12 - 1);
        int32_t tw = font.measureTextWidth(label);
        font.drawText(target, static_cast<int16_t>((x0 + x1 - tw) / 2),
                      static_cast<int16_t>(b.bottom() - font.getHeight(1) - 4), label,
                      label_color, color, false);
      }
    }
    // black keys
    for (int k = 0; k < white_keys - 1; k++) {
      int note = noteOfWhite(k);
      if (!hasBlackAfter(note)) continue;
      int32_t x0, x1;
      blackRange(k, x0, x1);
      RGB_T color = black;
      if (isPressed(note + 1)) color = colors.primary;
      else if (isMarked(note + 1)) color = colors.secondary;
      target.fillRoundRect(toPx(x0), toPx(b.y), toPx(x1 - x0), toPx(black_h), 2, color);
    }
  }

  /// The keyboard is played with setTouches(): consume the gestures
  bool onGesture(const tinygpu::GestureEvent& event) override {
    return this->bounds.contains(event.point.x, event.point.y);
  }

  bool update(uint32_t) override {
    bool result = dirty;
    dirty = false;
    return result;
  }

 protected:
  /// Perceived brightness (0..255) of a color
  static int brightness(RGB_T c) {
    return (c.getRed() * 299 + c.getGreen() * 587 + c.getBlue() * 114) / 1000;
  }

  int white_keys;
  int first_note = 48;  // C3
  int held[kMaxTouches];
  int held_count = 0;
  uint32_t marked[4] = {0, 0, 0, 0};
  bool dirty = true;

  static bool contains(const int* values, int count, int value) {
    for (int j = 0; j < count; j++)
      if (values[j] == value) return true;
    return false;
  }

  static bool isWhite(int note) {
    int n = note % 12;
    return n == 0 || n == 2 || n == 4 || n == 5 || n == 7 || n == 9 || n == 11;
  }

  static bool hasBlackAfter(int note) {
    int n = note % 12;
    return n == 0 || n == 2 || n == 5 || n == 7 || n == 9;
  }

  bool isPressed(int note) const { return contains(held, held_count, note); }

  bool isMarked(int note) const { return note >= 0 && note < 128 && (marked[note >> 5] & (1u << (note & 31))); }

  int noteOfWhite(int k) const {
    int note = first_note;
    while (!isWhite(note)) note++;
    for (int j = 0; j < k; j++) {
      note++;
      while (!isWhite(note)) note++;
    }
    return note;
  }

  int32_t whiteX(int k) const { return this->bounds.x + this->bounds.w * k / white_keys; }

  /// x range of the black key after the white key k
  void blackRange(int k, int32_t& x0, int32_t& x1) const {
    int32_t w = this->bounds.w / white_keys;
    int32_t center = whiteX(k + 1);
    x0 = center - w * 3 / 10;
    x1 = center + w * 3 / 10;
  }

  /// Note at a screen position (-1: none) and the velocity
  int noteAt(int32_t x, int32_t y, uint8_t& velocity) const {
    const tinymd::Bounds& b = this->bounds;
    if (!b.contains(x, y)) return -1;
    const int32_t black_h = b.h * 58 / 100;
    int32_t rel = y - b.y;
    if (rel < black_h) {
      for (int k = 0; k < white_keys - 1; k++) {
        if (!hasBlackAfter(noteOfWhite(k))) continue;
        int32_t x0, x1;
        blackRange(k, x0, x1);
        if (x >= x0 && x < x1) {
          velocity = static_cast<uint8_t>(50 + 77 * rel / black_h);
          return noteOfWhite(k) + 1;
        }
      }
    }
    int k = (x - b.x) * white_keys / b.w;
    if (k < 0) k = 0;
    if (k >= white_keys) k = white_keys - 1;
    velocity = static_cast<uint8_t>(50 + 77 * rel / b.h);
    return noteOfWhite(k);
  }
};
