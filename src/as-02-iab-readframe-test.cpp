/*
Copyright (c) 2011-2021, Robert Scheler, Heiko Sparenberg Fraunhofer IIS,
John Hurst, Pierre-Anthony Lemieux

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the author may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*! \file    as-02-iab-readframe-test.cpp
    \version $Id$
    \brief   Writes IA frames to an IMF IAB track file and checks that
             AS_02::IAB::MXFReader::ReadFrame(ui32_t, Frame&) returns them unchanged.

  ReadFrame(ui32_t, Frame&) starts from an empty buffer and grows it while it
  reads the preamble and the frame, so this fails if the bytes already read are
  lost when the buffer is reallocated.
*/

#include <AS_02_IAB.h>
#include <Metadata.h>
#include <KM_fileio.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace ASDCP;

// Builds one IA frame: preamble (tag, 4-byte length, payload) followed by the
// IA frame (tag, 4-byte length, payload). The payloads are filled with non-zero
// bytes that differ per frame, so zeroed or stale data cannot match.
static std::vector<ui8_t>
make_frame(ui8_t seed, ui32_t preamble_len, ui32_t frame_len)
{
  std::vector<ui8_t> frame;

  frame.push_back(0x01);
  for ( int shift = 24; shift >= 0; shift -= 8 )
    frame.push_back((preamble_len >> shift) & 0xff);
  for ( ui32_t i = 0; i < preamble_len; ++i )
    frame.push_back(static_cast<ui8_t>(seed + 1 + (i % 251)));

  frame.push_back(0x02);
  for ( int shift = 24; shift >= 0; shift -= 8 )
    frame.push_back((frame_len >> shift) & 0xff);
  for ( ui32_t i = 0; i < frame_len; ++i )
    frame.push_back(static_cast<ui8_t>(seed + 100 + (i % 241)));

  return frame;
}

int
main(int argc, const char** argv)
{
  const std::string filename = (argc > 1) ? argv[1] : "as-02-iab-readframe-test.mxf";
  const ASDCP::Dictionary* dict = &ASDCP::DefaultSMPTEDict();

  std::vector<std::vector<ui8_t> > frames;
  frames.push_back(make_frame(1, 12, 40));
  frames.push_back(make_frame(2, 300, 5000));  // larger than the first, so the buffer grows again
  frames.push_back(make_frame(3, 0, 7));       // no preamble payload

  // write
  {
    AS_02::IAB::MXFWriter writer;
    WriterInfo info;
    info.LabelSetType = LS_MXF_SMPTE;
    Kumu::GenRandomUUID(info.AssetUUID);
    ASDCP::MXF::IABSoundfieldLabelSubDescriptor subdescriptor(dict);
    std::vector<ASDCP::UL> conforms_to;
    conforms_to.push_back(dict->ul(MDD_IMF_IABTrackFileLevel0));

    Result_t result = writer.OpenWrite(filename, info, subdescriptor, conforms_to, Rational(24, 1));
    for ( size_t i = 0; ASDCP_SUCCESS(result) && i < frames.size(); ++i )
      result = writer.WriteFrame(&frames[i][0], static_cast<ui32_t>(frames[i].size()));
    if ( ASDCP_SUCCESS(result) )
      result = writer.Finalize();

    if ( ASDCP_FAILURE(result) )
      {
        fprintf(stderr, "Cannot write %s: %s\n", filename.c_str(), result.Message());
        return 2;
      }
  }

  // read back and compare
  Kumu::FileReaderFactory factory;
  AS_02::IAB::MXFReader reader(factory);

  Result_t result = reader.OpenRead(filename);
  if ( ASDCP_FAILURE(result) )
    {
      fprintf(stderr, "Cannot read %s: %s\n", filename.c_str(), result.Message());
      return 2;
    }

  int failures = 0;

  for ( size_t i = 0; i < frames.size(); ++i )
    {
      AS_02::IAB::MXFReader::Frame frame;
      result = reader.ReadFrame(static_cast<ui32_t>(i), frame);

      if ( ASDCP_FAILURE(result) )
        {
          fprintf(stderr, "Frame %zu: ReadFrame failed: %s\n", i, result.Message());
          ++failures;
        }
      else if ( frame.first != frames[i].size() )
        {
          fprintf(stderr, "Frame %zu: got %zu bytes, expected %zu\n", i, frame.first, frames[i].size());
          ++failures;
        }
      else if ( memcmp(frame.second, &frames[i][0], frames[i].size()) != 0 )
        {
          size_t offset = 0;
          while ( frame.second[offset] == frames[i][offset] )
            ++offset;
          fprintf(stderr, "Frame %zu: first difference at byte %zu (got 0x%02x, expected 0x%02x)\n",
                  i, offset, frame.second[offset], frames[i][offset]);
          ++failures;
        }
    }

  reader.Close();

  if ( failures == 0 )
    puts("as-02-iab-readframe-test: OK");

  return failures == 0 ? 0 : 1;
}
