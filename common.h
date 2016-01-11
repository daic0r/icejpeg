//
//  common.h
//  icejpeg
//
//  Created by Matthias Gruen on 07/01/16.
//  Copyright © 2016 Matthias Gruen. All rights reserved.
//

#ifndef common_h
#define common_h

#define CF(x) clip((((x) + 64) >> 7))

typedef unsigned char byte;
typedef unsigned short word;

struct jpeg_component
{
    // we can store this globally since we only support baseline
    // jpegs with only one scan. otherwise this must be stored
    // per scan.
    byte id_dht;
    int width, height;
    int stride;
    int sx, sy;
    byte qt_table;
    byte *pixels;
    int prev_dc;
};

byte clip(int x);

#endif /* common_h */
