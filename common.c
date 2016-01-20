//
//  common.c
//  icejpeg
//
//  Created by Matthias Gruen on 10/01/16.
//  Copyright © 2016 Matthias Gruen. All rights reserved.
//

#include "common.h"

int rnd(float x)
{
	if (x < 0.0f)
		return -rnd(-x);
	return (int)(x + 0.5f);
}