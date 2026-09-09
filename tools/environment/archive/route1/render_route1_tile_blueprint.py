"""Render the saved logical map as a small, inspectable SVG blueprint."""
import argparse
import json
from pathlib import Path


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('layout',type=Path)
    parser.add_argument('output',type=Path)
    parser.add_argument('--grass-scale',type=float,default=1.0)
    parser.add_argument('--south-grass-depth-scale',type=float,default=.5)
    parser.add_argument('--south-grass-pivot-y',type=float,default=3.275)
    args=parser.parse_args()
    cells=json.loads(args.layout.read_text())['cells']
    x0,x1,z0,z1=13,29,-15,0
    step,ox,oy=42,70,92
    svg=['<svg xmlns="http://www.w3.org/2000/svg" width="1110" height="830" viewBox="0 0 1110 830">',
         '<rect width="1110" height="830" fill="#14251f"/>',
         '<g font-family="Segoe UI,Arial,sans-serif" fill="#edf6e8">',
         '<text x="42" y="40" font-size="25" font-weight="600">Route 1 · southern entrance tile blueprint</text>',
         '<text x="42" y="67" font-size="15" fill="#b7cbbd">Recovered cell layout, rebuilt with independent Blender terrain · north is up</text>']
    for c in cells:
        if not (x0<=c['x']<=x1 and z0<=c['z']<=z1): continue
        px,py=ox+(c['x']-x0)*step,oy+(c['z']-z0)*step
        color='#cda665' if c['surface']==1 else ('#48836b' if c['surface']==2 else '#94b877')
        svg.append(f'<rect x="{px}" y="{py}" width="42" height="42" fill="{color}" stroke="#385f44" stroke-width=".7"/>')
        text='↑' if c['ramp']==1 else str(c['height'])
        svg.append(f'<text x="{px+21}" y="{py+26}" text-anchor="middle" font-size="14" fill="#244232">{text}</text>')
    # The fixed board registration is a separate overlay on the terrain plan.
    svg.append(f'<rect x="{ox+(17-x0)*step}" y="{oy+(-10-z0)*step}" width="{8*step}" height="{8*step}" fill="none" stroke="#ffffff" stroke-width="3"/>')
    for z in (-11,-2):
        svg.append(f'<rect x="{ox+(17-x0)*step}" y="{oy+(z-z0)*step}" width="{8*step}" height="42" fill="none" stroke="#ffffff" stroke-width="2" stroke-dasharray="5 4"/>')
    hook=[(-2,1),(-1,1),(0,-1),(0,0),(0,1),(1,-1),(2,-1)]
    svg.append(f'<defs><clipPath id="map"><rect x="{ox}" y="{oy}" width="{(x1-x0+1)*step}" height="{(z1-z0+1)*step}"/></clipPath></defs><g clip-path="url(#map)">')
    beds=[(16.5,6.5,hook,1),(24.5,6.5,hook,1),(20.5,args.south_grass_pivot_y,[(x,z) for x in (-1,0) for z in (-1,0,1)],args.south_grass_depth_scale),(23.5,15.5,[(x,z) for x in (-1,0,1) for z in (-1,0,1)],1)]
    scale=args.grass_scale
    for bx,by,core,depth in beds:
        for cx,cz in core:
            wx,wz=bx+cx*scale,-by+cz*scale*depth
            px,py=ox+(wx-x0)*step,oy+(wz-z0)*step
            svg.append(f'<rect x="{px:.2f}" y="{py:.2f}" width="{step*scale}" height="{step*scale*depth}" fill="#16765d" fill-opacity=".8" stroke="#123e34"/>')
    svg.append('</g>')
    for x in range(x0,x1+1):
        svg.append(f'<text x="{ox+(x-x0+.5)*step}" y="{oy-9}" text-anchor="middle" font-size="11" fill="#b7cbbd">{x}</text>')
    for z in range(z0,z1+1):
        svg.append(f'<text x="{ox-12}" y="{oy+(z-z0+.6)*step}" text-anchor="end" font-size="11" fill="#b7cbbd">{z}</text>')
    labels=[('Each square','1 metre'),('Number','Height in 0.5 m steps'),('↑','Ramp rises north'),('Tan','Dirt tiles'),('Light / dark lawn','Route / enclosed banks'),('Deep green blocks','Encounter-grass cores'),('White outline','8 × 8 board'),('Dashed outline','Reserve rows')]
    for i,(title,detail) in enumerate(labels):
        py=120+i*65
        svg.extend([f'<text x="825" y="{py}" font-size="16" font-weight="600">{title}</text>',f'<text x="825" y="{py+23}" font-size="14" fill="#b7cbbd">{detail}</text>'])
    svg.extend(['<text x="825" y="695" font-size="14" fill="#b7cbbd">Brush outlines show the cores;</text>',
                '<text x="825" y="718" font-size="14" fill="#b7cbbd">blades extend beyond them.</text>',
                '<text x="42" y="802" font-size="14" fill="#b7cbbd">Visual authoring only · camouflage and one-way jumping remain future mechanics.</text>', '</g></svg>'])
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text('\n'.join(svg),encoding='utf-8')


if __name__=='__main__': main()
