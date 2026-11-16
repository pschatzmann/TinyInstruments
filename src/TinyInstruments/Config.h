#pragma once

/// Max number of notes which can sound at the same time (a piano player
/// has 10 fingers). Notes held by the sustain pedal and release tails count
/// as well: if more notes are played, the quietest/oldest one is faded out.
#ifndef TI_MAX_VOICES
#  define TI_MAX_VOICES 10
#endif

/// Additional voices which are only used to fade out stolen or re-struck
/// notes within a few milliseconds, so that they do not click
#ifndef TI_FADE_VOICES
#  define TI_FADE_VOICES 2
#endif

/// Number of frames which are rendered in one block
#ifndef TI_BLOCK_SIZE
#  define TI_BLOCK_SIZE 64
#endif

/// Number of midi events which can be queued between two render calls
#ifndef TI_EVENT_QUEUE_SIZE
#  define TI_EVENT_QUEUE_SIZE 64
#endif

/// Number of midi channels
#define TI_MIDI_CHANNELS 16

/// The Arduino cores compile with -Os (optimize for size), which makes the
/// audio rendering much slower: the time critical code is compiled with -O2
#ifndef TI_OPTIMIZE
#  if defined(__GNUC__) && !defined(__clang__) && defined(__OPTIMIZE_SIZE__)
#    define TI_OPTIMIZE 1
#  else
#    define TI_OPTIMIZE 0
#  endif
#endif

#if TI_OPTIMIZE
#  define TI_OPTIMIZE_BEGIN _Pragma("GCC push_options") _Pragma("GCC optimize(\"O2\")")
#  define TI_OPTIMIZE_END _Pragma("GCC pop_options")
#else
#  define TI_OPTIMIZE_BEGIN
#  define TI_OPTIMIZE_END
#endif
