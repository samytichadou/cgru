#!/usr/bin/env python
# -*- coding: utf-8 -*-

import json
import os
import sys
import subprocess

import cgruutils

def processMovie( i_file):

    out = dict()

    out['infile'] = i_file

    if not os.path.isfile( out['infile']):
        out['error'] = 'Input file does not exist.'
        return out

    params = {}
    params['frame_count'] = 'FrameCount'
    params['fps'] = 'FrameRate'
    params['codec'] = 'Codec'
    params['width'] = 'Width'
    params['height'] = 'Height'

    inform = ''
    for key in params:
        if len( inform): inform += ','
        inform += '"%s":"%%%s%%"' % ( key, params[key])
    inform = '--inform=Video;{' + inform + '}'

    data = subprocess.check_output(['mediainfo', inform, out['infile']])
    data = cgruutils.toStr( data)

    inform = None
    try:
        inform = json.loads( data)
    except:
        inform = None
        out['data'] = data
        out['error'] = 'JSON load error'

    if inform:
        for key in inform:
            if inform[key].isdigit():
                inform[key] = int(inform[key])

        out['mediainfo'] = inform

    return out


if __name__ == '__main__':

    if len(sys.argv) < 2:
        print('Input file is not specified.')
        sys.exit(1)

    out = processMovie( sys.argv[1])
    print( json.dumps( out, indent=4))

