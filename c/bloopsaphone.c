//
// bloopsaphone.c
// the chiptune maker for portaudio
// (with bindings for ruby)
//
// (c) 2009 why the lucky stiff
// See COPYING for the license
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <portaudio.h>
#include <sndfile.h>
#include <unistd.h>
#include "bloopsaphone.h"

#define SAMPLE_RATE 44100
#define FRAMES_PER_BUFFER 512
#define rnd(n) (rand() % (n + 1))
#define tempo2frames(tempo) ((float)SAMPLE_RATE / (tempo / 60.0f))
#define PI 3.14159265f

static int bloops_open = 0;

float
frnd(float range)
{
  return (float)rnd(10000) / 10000 * range;
}

void
bloops_ready(bloops *B, bloopsatrack *A, unsigned char init)
{
  A->period = 100.0 / (A->P->freq * A->P->freq + 0.001);
  A->maxperiod = 100.0 / (A->P->limit * A->P->limit + 0.001);
  A->slide = 1.0 - pow((double)A->P->slide, 3.0) * 0.01;
  A->dslide = -pow((double)A->P->dslide, 3.0) * 0.000001;
  A->square = 0.5f - A->P->square * 0.5f;
  A->sweep = -A->P->sweep * 0.00005f;
  if (A->P->arp >= 0.0f)
    A->arp = 1.0 - pow((double)A->P->arp, 2.0) * 0.9;
  else
    A->arp = 1.0 + pow((double)A->P->arp, 2.0) * 10.0;
  A->atime = 0;
  A->alimit = (int)(pow(1.0f - A->P->aspeed, 2.0f) * 20000 + 32);
  if (A->P->aspeed == 1.0f)
    A->alimit = 0;

  if (init)
  {
    int i = 0;
    A->phase = 0;
    A->filter[0] = 0.0f;
    A->filter[1] = 0.0f;
    A->filter[2] = pow(A->P->lpf, 3.0f) * 0.1f;
    A->filter[3] = 1.0f + A->P->lsweep * 0.0001f;
    A->filter[4] = 5.0f / (1.0f + pow(A->P->resonance, 2.0f) * 20.0f) * (0.01f + A->filter[2]);
    if (A->filter[4] > 0.8f) A->filter[4] = 0.8f;
    A->filter[5] = 0.0f;
    A->filter[6] = pow(A->P->hpf, 2.0f) * 0.1f;
    A->filter[7] = 1.0 + A->P->hsweep * 0.0003f;

    A->vibe = 0.0f;
    A->vspeed = pow(A->P->vspeed, 2.0f) * 0.01f;
    A->vdelay = A->P->vibe * 0.5f;

    A->volume = 0.0f;
    A->stage = 0;
    A->time = 0;
    A->length[0] = (int)(A->P->attack * A->P->attack * 100000.0f);
    A->length[1] = (int)(A->P->sustain * A->P->sustain * 100000.0f);
    A->length[2] = (int)(A->P->decay * A->P->decay * 100000.0f);

    A->fphase = pow(A->P->phase, 2.0f) * 1020.0f;
    if (A->P->phase < 0.0f) A->fphase = -A->fphase;
    A->dphase = pow(A->P->psweep, 2.0f) * 1.0f;
    if (A->P->psweep < 0.0f) A->dphase = -A->dphase;
    A->iphase = abs((int)A->fphase);
    A->phasex = 0;

    memset(A->phaser, 0, 1024 * sizeof(float));
    for (i = 0; i < 32; i++)
      A->noise[i] = frnd(2.0f) - 1.0f;

    A->repeat = 0;
    A->limit = (int)(pow(1.0f - A->P->repeat, 2.0f) * 20000 + 32);
    if (A->P->repeat == 0.0f)
      A->limit = 0;
    A->playing = BLOOPS_PLAY;
  }
}

void
bloops_clear(bloops *B)
{
  int i;
  for (i = 0; i < BLOOPS_MAX_TRACKS; i++)
    B->tracks[i] = NULL;
}

void
bloops_tempo(bloops *B, int tempo)
{
  B->tempo = tempo;
}

void
bloops_track_at(bloops *B, bloopsatrack *track, int num)
{
  B->tracks[num] = track;
}

int
bloops_is_done(bloops *B)
{
  return B->play == BLOOPS_STOP;
}

static void
bloops_synth(bloops *B, int length, float* buffer)
{
  int t, i, si;

  while (length--)
  {
    int samplecount = 0;
    int moreframes = 0;
    float allsample = 0.0f;

    for (t = 0; t < BLOOPS_MAX_TRACKS; t++)
    {
      bloopsatrack *A = B->tracks[t];
      if (A == NULL)
        continue;

      if (A->notes)
      {
        int frames = 0;
        for (i = 0; i < A->nlen; i++)
        {
          bloopsanote *note = &A->notes[i];
          if (A->frames == frames)
          {
            float freq = bloops_note_freq(note->tone, (int)note->octave);
            if (freq == 0.0f) {
              A->period = 0.0f;
              A->playing = BLOOPS_STOP;
            } else {
              bloops_ready(B, A, 1);
              A->period = 100.0 / (freq * freq + 0.001);
            }
          }
          else if (A->frames < frames)
            break;
          frames += (int)(tempo2frames(B->tempo) * (4.0f / note->duration));
        }

        if (A->frames <= frames)
          moreframes++;
      }
      else
      {
        moreframes++;
      }

      A->frames++;

      if (A->playing == BLOOPS_STOP)
        continue;

      samplecount++;
      A->repeat++;
      if (A->limit != 0 && A->repeat >= A->limit)
      {
        A->repeat = 0;
        bloops_ready(B, A, 0);
      }

      A->atime++;
      if (A->alimit != 0 && A->atime >= A->alimit)
      {
        A->alimit = 0;
        A->period *= A->arp;
      }

      A->slide += A->dslide;
      A->period *= A->slide;
      if (A->period > A->maxperiod)
      {
        A->period = A->maxperiod;
        if (A->P->limit > 0.0f)
          A->playing = BLOOPS_STOP;
      }

      float rfperiod = A->period;
      if (A->vdelay > 0.0f)
      {
        A->vibe += A->vspeed;
        rfperiod = A->period * (1.0 + sin(A->vibe) * A->vdelay);
      }

      int period = (int)rfperiod;
      if (period < 8) period = 8;
      A->square += A->sweep;
      if(A->square < 0.0f) A->square = 0.0f;
      if(A->square > 0.5f) A->square = 0.5f;

      A->time++;
      if (A->time > A->length[A->stage])
      {
        A->time = 0;
        A->stage++;
        if (A->stage == 3)
          A->playing = BLOOPS_STOP;
      }

      switch (A->stage) {
        case 0:
          A->volume = (float)A->time / A->length[0];
        break;
        case 1:
          A->volume = 1.0f + pow(1.0f - (float)A->time / A->length[1], 1.0f) * 2.0f * A->P->punch;
        break;
        case 2:
          A->volume = 1.0f - (float)A->time / A->length[2];
        break;
      }

      A->fphase += A->dphase;
      A->iphase = abs((int)A->fphase);
      if (A->iphase > 1023) A->iphase = 1023;

      if (A->filter[7] != 0.0f)
      {
        A->filter[6] *= A->filter[7];
        if (A->filter[6] < 0.00001f) A->filter[6] = 0.00001f;
        if (A->filter[6] > 0.1f)     A->filter[6] = 0.1f;
      }

      float ssample = 0.0f;
      for (si = 0; si < 8; si++)
      {
        float sample = 0.0f;
        A->phase++;
        if (A->phase >= period)
        {
          A->phase %= period;
          if (A->P->type == BLOOPS_NOISE)
            for (i = 0; i < 32; i++)
              A->noise[i] = frnd(2.0f) - 1.0f;
        }

        float fp = (float)A->phase / period;
        switch (A->P->type)
        {
          case BLOOPS_SQUARE:
            if (fp < A->square)
              sample = 0.5f;
            else
              sample = -0.5f;
          break;
          case BLOOPS_SAWTOOTH:
            sample = 1.0f - fp * 2;
          break;
          case BLOOPS_SINE:
            sample = (float)sin(fp * 2 * PI);
          break;
          case BLOOPS_NOISE:
            sample = A->noise[A->phase * 32 / period];
          break;
        }

        float pp = A->filter[0];
        A->filter[2] *= A->filter[3];
        if (A->filter[2] < 0.0f) A->filter[2] = 0.0f;
        if (A->filter[2] > 0.1f) A->filter[2] = 0.1f;
        if (A->P->lpf != 1.0f)
        {
          A->filter[1] += (sample - A->filter[0]) * A->filter[2];
          A->filter[1] -= A->filter[1] * A->filter[4];
        }
        else
        {
          A->filter[0] = sample;
          A->filter[1] = 0.0f;
        }
        A->filter[0] += A->filter[1];

        A->filter[5] += A->filter[0] - pp;
        A->filter[5] -= A->filter[5] * A->filter[6];
        sample = A->filter[5];

        A->phaser[A->phasex & 1023] = sample;
        sample += A->phaser[(A->phasex - A->iphase + 1024) & 1023];
        A->phasex = (A->phasex + 1) & 1023;

        ssample += sample * A->volume;
      }
      ssample = ssample / 8 * B->volume;
      ssample *= 2.0f * A->P->volume;

      if (ssample > 1.0f)  ssample = 1.0f;
      if (ssample < -1.0f) ssample = -1.0f;
      allsample += ssample;
    }

    if (moreframes == 0)
      B->play = BLOOPS_STOP;
    *buffer++ = allsample;
  }
}

static int bloops_port_callback(const void *inputBuffer, void *outputBuffer,
  unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo,
  PaStreamCallbackFlags statusFlags, void *data)
{
  int i;
  float *out = (float*)outputBuffer;
  bloops *B = (bloops *)data;

  if (B->play == BLOOPS_PLAY) {
    bloops_synth(B, framesPerBuffer, out);
  }
  else
    for(i = 0; i < framesPerBuffer; i++)
      *out++ = 0.0f;

  return 0;
}

void
bloops_prep(bloops *B)
{
  int i;
  for (i = 0; i < BLOOPS_MAX_TRACKS; i++)
    if (B->tracks[i] != NULL) {
      bloops_ready(B, B->tracks[i], 1);
      B->tracks[i]->frames = 0;
    }
  B->play = BLOOPS_PLAY;
}

void
bloops_play(bloops *B)
{
  bloops_prep(B);

  if (B->stream == NULL) {
    if (!bloops_open++)
    {
      srand(time(NULL));
      Pa_Initialize();
    }

    Pa_OpenDefaultStream(&B->stream, 0, 1, paFloat32,
      SAMPLE_RATE, FRAMES_PER_BUFFER, bloops_port_callback, B);
    Pa_StartStream(B->stream);
  }
}

int
bloops_record(bloops *B, char *path)
{
  // setup sndfile
  SF_INFO info;
  info.samplerate = SAMPLE_RATE;
  info.channels = 1;
  info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

  SNDFILE *sndfile = sf_open(path, SFM_WRITE, &info);
  if (sndfile) {
    bloops_prep(B);

    float *buffer = (float *)malloc(sizeof(float)*FRAMES_PER_BUFFER);
    while(!bloops_is_done(B)) {
      bloops_synth(B, FRAMES_PER_BUFFER, buffer);
      sf_write_float(sndfile, buffer, FRAMES_PER_BUFFER);
    }
    sf_close(sndfile);
    free(buffer);
    return 0;
  }
  return 1;
}

bloopsaphone *
bloops_square()
{
  bloopsaphone *P = (bloopsaphone *)calloc(sizeof(bloopsaphone), 1);
  P->type = BLOOPS_SQUARE;
  P->volume = 0.5f;
  P->sustain = 0.3f;
  P->decay = 0.4f;
  P->freq = 0.3f;
  P->lpf = 1.0f;
  return P;
}

bloopsaphone *
bloops_load(char* filename)
{
  bloopsaphone *P = NULL;
  FILE* file = fopen(filename, "rb");
  if (!file) return NULL;

  int version = 0;
  fread(&version, 1, sizeof(int), file);
  if (version != 102)
    return NULL;

  P = (bloopsaphone *)malloc(sizeof(bloopsaphone));
  fread(&P->type,    1, sizeof(int), file);

  P->volume = 0.5f;
  fread(&P->volume,  1, sizeof(float), file);
  fread(&P->freq,    1, sizeof(float), file);
  fread(&P->limit,   1, sizeof(float), file);
  fread(&P->slide,   1, sizeof(float), file);
  fread(&P->dslide,  1, sizeof(float), file);
  fread(&P->square,  1, sizeof(float), file);
  fread(&P->sweep,   1, sizeof(float), file);

  fread(&P->vibe,    1, sizeof(float), file);
  fread(&P->vspeed,  1, sizeof(float), file);
  fread(&P->vdelay,  1, sizeof(float), file);

  fread(&P->attack,  1, sizeof(float), file);
  fread(&P->sustain, 1, sizeof(float), file);
  fread(&P->decay,   1, sizeof(float), file);
  fread(&P->punch,   1, sizeof(float), file);

  fread(&P->resonance, 1, sizeof(float), file);
  fread(&P->lpf,     1, sizeof(float), file);
  fread(&P->lsweep,  1, sizeof(float), file);
  fread(&P->hpf,     1, sizeof(float), file);
  fread(&P->hsweep,  1, sizeof(float), file);

  fread(&P->phase,   1, sizeof(float), file);
  fread(&P->psweep,  1, sizeof(float), file);

  fread(&P->repeat,  1, sizeof(float), file);
  fread(&P->arp,     1, sizeof(float), file);
  fread(&P->aspeed,  1, sizeof(float), file);

  fclose(file);
  return P;
}

bloops *
bloops_new(char *path)
{
  bloops *B = (bloops *)malloc(sizeof(bloops));
  B->volume = 0.10f;
  B->tempo = 120;
  B->play = BLOOPS_STOP;
  bloops_clear(B);

  return B;
}

void
bloops_destroy(bloops *B)
{
  if (B->stream != NULL) {
    Pa_StopStream(B->stream);
    Pa_CloseStream(B->stream);
  }

  free((void *)B);

  if (!--bloops_open)
    Pa_Terminate();
}

void
bloops_track_destroy(bloopsatrack *track)
{
  if (track->notes != NULL)
    free(track->notes);
  free(track);
}
