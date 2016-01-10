//
//  types.c
//  icejpeg
//
//  Created by Matthias Gruen on 10/01/16.
//  Copyright © 2016 Matthias Gruen. All rights reserved.
//

#include "types.h"

byte clip(int x)
{
    if (x > 255)
        return 255;
    if (x < 0)
        return 0;
    
    return x;
}