#pragma once
/**
 * TinyInstruments: header only, sample based instruments for microcontrollers.
 *
 * The core does not depend on Arduino. For the integration with the
 * arduino-audio-tools and arduino-midi libraries include
 * TinyInstrumentsAudioTools.h and TinyInstrumentsMidi.h.
 */
#include "TinyInstruments/Config.h"
#include "TinyInstruments/SampleSet.h"
#include "TinyInstruments/ImaAdpcm.h"
#include "TinyInstruments/Psola.h"
#include "TinyInstruments/SamplerVoice.h"
#include "TinyInstruments/EventQueue.h"
#include "TinyInstruments/Instrument.h"
#include "TinyInstruments/InstrumentBank.h"
#include "TinyInstruments/SampleSynthesizer.h"
#include "TinyInstruments/DynamicSampleSet.h"
#include "TinyInstruments/Biquad.h"
#include "TinyInstruments/ChannelEffect.h"
#include "TinyInstruments/GuitarAmp.h"

#ifndef TI_NO_NAMESPACE
using namespace tinyinstruments;
#endif
