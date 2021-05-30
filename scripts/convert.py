#!/usr/bin/env python2
from sys import argv, exit
import os
import struct

PVR_TEX_TOOL_CLI = 'PVRTexToolCLI.exe'

def main():
  if len(argv) != 3:
    print('Usage: convert.py input-dir output-dir')
    return -1

  try:
    os.mkdir(argv[2])
  except OSError as e:
    pass

  for file in os.listdir(argv[1]):
    os.system('{} -i {}/{} -o {}/{} -f r4g4b4a4,UBN,sRGB -m 7'.format(PVR_TEX_TOOL_CLI, argv[1], file, argv[2], file))

    pvr = '{}/{}.pvr'.format(argv[2], file)

    with open(pvr, 'rb') as f:
      tex = f.read()

    with open(pvr.replace('.png.pvr', '.tex'), 'wb') as f:
      f.write(struct.pack('I', 0x07))
      f.write(struct.pack('I', 0x02))
      f.write(struct.pack('I', 0x6E))
      f.write(struct.pack('I', 0x18))
      f.write(struct.pack('I', 0x04))
      f.write(struct.pack('I', 0x1C)) # offset
      f.write(struct.pack('I', 0x00)) # size

      f.write(struct.pack('I', 0x04))
      f.write(struct.pack('I', 0x40)) # width
      f.write(struct.pack('I', 0x40)) # height
      f.write(struct.pack('I', 0x07)) # numberofmipmaps
      f.write(struct.pack('I', 0x2AAA)) # totalsize

      f.write(tex[0x44:])

    os.remove(pvr)

if __name__ == '__main__':
  exit(main())
