#ifndef _UPSAMPLE_H
#define _UPSAMPLE_H

#include "common.h"

void upsampleBicubicH(struct jpeg_component *component);
void upsampleBicubicV(struct jpeg_component *component);

void upsampleLanczosH(struct jpeg_component *component);
void upsampleLanczosV(struct jpeg_component *component);

#endif