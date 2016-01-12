//
//  common.c
//  icejpeg
//
//  Created by Matthias Gruen on 10/01/16.
//  Copyright © 2016 Matthias Gruen. All rights reserved.
//

#include "common.h"

byte clip(int x)
{
    if (x > 255)
        return 255;
    if (x < 0)
        return 0;
    
    return x;
}

int round(float x)
{
	if (x < 0.0f)
		return -round(-x, 0.5);
	return (int)(x + 0.5f);
}