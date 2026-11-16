# Loading Sample Sets from Files / PSRAM

Instead of compiling a sample set into the program (`#include "samples/X.h"`),
it can be loaded at runtime. The program stays small and the instrument can
be changed by replacing a file. On an ESP32 with PSRAM, several instruments
can be loaded at the same time.

The `data` folder of the library contains all sample sets as binary files:
the full, small and octave versions of all instruments (20 files, 150 - 700 KB
each). See [Versions and sizes](instruments.md#versions-and-sizes) for the list.

## DynamicSampleSet

`DynamicSampleSet` reads a sample set from any object which provides
`readBytes(char*, size_t)`: an Arduino `Stream` or `File` (LittleFS, SD,
SPIFFS), a network client, or `StdioFileStream` on the desktop. The memory is
allocated in PSRAM if available, otherwise on the heap.

```cpp
#include <LittleFS.h>
#include "TinyInstrumentsAudioTools.h"

DynamicSampleSet samples;
Instrument piano;

void setup() {
  LittleFS.begin();
  File file = LittleFS.open("/GrandPiano.bin", "r");
  if (!samples.begin(file)) {
    Serial.println(samples.errorText());
  }
  piano = PianoInstrument(samples.sampleSet());
  synth.setInstrument(-1, piano);
  ...
}
```

The `DynamicSampleSet` must exist as long as the instrument is played. Call
`end()` to release the memory.

See the [file-psram-serial-midi](../examples/file-psram-serial-midi) example.
The [all-instruments](../examples/all-instruments) example loads all sample
sets (4.9 MB) into the 8 MB PSRAM of an ESP32-S3: this was tested on an
ESP32-S3 N16R8 (3.4 MB of PSRAM stay free).

To create a LittleFS image with all sample sets without the Arduino IDE plugin
(16 MB flash with the `partitions.csv` of the example):

```
mklittlefs -c data -b 4096 -p 256 -s 0xCE0000 littlefs.bin
esptool --chip esp32s3 --port /dev/ttyUSB0 write_flash 0x310000 littlefs.bin
```

## Copying from flash to PSRAM

`copyFrom()` copies a compiled-in sample set into PSRAM:

```cpp
DynamicSampleSet grand;
grand.copyFrom(GrandPiano());
```

On the ESP32-S3 this does not make the playback faster: the rendering is
limited by the CPU, not by the memory (see [Performance](performance.md)).

## File format

Binary, little endian:

| Part | Content |
|---|---|
| header | `"TISS"`, u16 version (1, or 2 with pitch marks), u16 zone count, u32 sample rate, u32 data size, u8 name length, name |
| zones | 32 bytes each: u32 length, u32 loop start, u32 loop end, i16 loop predictor, u8 loop index, u8 root key, u32 decay start, f32 decay ln, u8 lo key, hi key, lo vel, hi vel, group, off by, i8 tune, u8 reserved |
| pitch marks | version 2 only: u16 mark count per zone, then the marks of all zones (u16: distance to the previous mark, the first one: position) |
| data | the IMA ADPCM data of all zones, in the same order |

The files are created by the converter (`tools/sfz2header.py --binary`) or from
an existing header (`tools/header2bin.py src/samples/X.h data/X.bin`).
