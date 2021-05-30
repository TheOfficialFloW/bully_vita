#!/usr/bin/env python2
from sys import argv, exit
import re
import struct
import os

HLSL_COMPILER = 'HLSLCompiler.exe'
PSP2CGC = 'psp2cgc.exe'

SHADER_MARKER = '//######################_==_YOYO_SHADER_MARKER_==_######################@~\n'

VARYING_SEARCH = '^varying .* (vec[0-4]) (.*);$'

CG_SUB_DICT = {
  '0.69999999': '0.7',
  '([^.[ ]+)(\.?([w-z]*)) = input\.([^.[;]+)(\.?([w-z]*))': '\g<1>\g<2> = input.\g<1>\g<5>',
  'output\.([^.[ ]+)(\.?([w-z]*)) = ([^.[;]+)(\.?([w-z]*))': 'output.\g<4>\g<2> = \g<4>\g<5>',
  'struct PS_INPUT\n{[^{]*\n};' :
'''struct PS_INPUT
{
    float4 gl_FragCoord : TEXCOORD0;
    float4 _In_custom1 : TEXCOORD1;
    float2 _In_uv1 : TEXCOORD2;
    float2 _In_uv2 : TEXCOORD3;
    float3 _In_eyedirection : TEXCOORD4;
    float3 _In_normal : TEXCOORD5;
    float4 _In_vertexcolor : COLOR0;
    float3 _In_lighting : COLOR1;
};''',
  'struct PS_OUTPUT\n{[^{]*\n};' :
'''struct PS_OUTPUT
{
    float4 gl_Color : COLOR0;
};''',
  'struct VS_OUTPUT\n{[^{]*\n};' :
'''struct VS_OUTPUT
{
    float4 gl_Position : POSITION;
    float4 gl_FragCoord : TEXCOORD0;
    float4 _In_custom1 : TEXCOORD1;
    float2 _In_uv1 : TEXCOORD2;
    float2 _In_uv2 : TEXCOORD3;
    float3 _In_eyedirection : TEXCOORD4;
    float3 _In_normal : TEXCOORD5;
    float4 _In_vertexcolor : COLOR0;
    float3 _In_lighting : COLOR1;
};''',
}

def main():
  if len(argv) != 2:
    print('Usage: generate.py input-dir')
    return -1

  try:
    os.mkdir('gxp')
  except OSError:
    pass
  try:
    os.mkdir('cg')
  except OSError:
    pass

  for file in os.listdir(argv[1]):
    with open('{}/{}'.format(argv[1], file), 'r') as f:
      lines = f.readlines()

    is_vp = False
    varyings = []
    for line in lines:
      if 'gl_Position' in line:
        is_vp = True
      if 'varying' in line:
        varyings.append(line)

    with open('temp.glsl', 'w') as f:
      if is_vp:
        f.writelines(lines)
        f.write(SHADER_MARKER)
        f.write('precision mediump float;')
        for varying in varyings:
          f.write(varying)
        f.write('void main() {\n')
        f.write('gl_FragColor = vec4(0, 0, 0, 0);\n')
        for varying in varyings:
          varying_search = re.search(VARYING_SEARCH, varying)
          vec = varying_search.group(1)
          name = varying_search.group(2)
          if vec == 'vec4':
            f.write('gl_FragColor += vec4({});\n'.format(name))
          elif vec == 'vec3':
            f.write('gl_FragColor += vec4({}, 0);\n'.format(name))
          elif vec == 'vec2':
            f.write('gl_FragColor += vec4({}, 0, 0);\n'.format(name))
        f.write('}\n')
      else:
        for varying in varyings:
          f.write(varying)
        f.write('void main() {\n')
        for varying in varyings:
          varying_search = re.search(VARYING_SEARCH, varying)
          vec = varying_search.group(1)
          name = varying_search.group(2)
          if vec == 'vec4':
            f.write('{} = vec4(0, 0, 0, 0);'.format(name))
          elif vec == 'vec3':
            f.write('{} = vec3(0, 0, 0);'.format(name))
          elif vec == 'vec2':
            f.write('{} = vec2(0, 0);'.format(name))
        f.write('}\n')
        f.write(SHADER_MARKER)
        f.writelines(lines)

    os.system('{} -shader temp.glsl -name shader -out out'.format(HLSL_COMPILER))

    profile = 'sce_vp_psp2' if is_vp else 'sce_fp_psp2'
    shader_file = 'out/vout.shader' if is_vp else 'out/fout.shader'

    with open(shader_file, 'r+') as f:
      code = f.read()
      for x, y in CG_SUB_DICT.items():
        code = re.sub(x, y, code)
      f.seek(0)
      f.write(code)
      f.truncate()

    os.system('{} -O4 -fastprecision -profile {} {}'.format(PSP2CGC, profile, shader_file))

    gxp = 'gxp/{}'.format(file.replace('.glsl', '.gxp'))
    cg = 'cg/{}'.format(file.replace('.glsl', '.cg'))

    try:
      os.remove(gxp)
    except OSError:
      pass
    try:
      os.remove(cg)
    except OSError:
      pass

    os.rename('{}.gxp'.format(shader_file.replace('.', '_')), gxp)
    os.rename(shader_file, cg)

if __name__ == '__main__':
  exit(main())
