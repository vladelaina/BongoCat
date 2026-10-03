"""Convert the reference's SVG icons into static C polylines (no runtime SVG)."""
from pathlib import Path
import argparse, re, math, xml.etree.ElementTree as ET
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('reference', type=Path, help='Path to the reference index.html')
parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] /
                    'src/ui/dial/dial_icons.c')
args = parser.parse_args()
html=args.reference.read_text(encoding='utf-8')
icon_block = re.search(r'const\s+icons\s*=\s*\{(.*?)\n\s*\};', html, re.S)
if not icon_block:
    parser.error('Reference does not contain the SVG icons object')
icons=dict(re.findall(r"^    (\w+): '([^']+)'",icon_block.group(1),re.M))
order=['settings','hide','pass','pin','size','opacity','motion','expression','audio','model','exit','close','passOn','pinOn']
def arc(x,y,rx,ry,rotation,large,sweep,nx,ny):
    rx,ry=abs(rx),abs(ry)
    if not rx or not ry or (x==nx and y==ny): return [(nx,ny)]
    phi=math.radians(rotation); co,si=math.cos(phi),math.sin(phi)
    dx,dy=(x-nx)/2,(y-ny)/2
    xp,yp=co*dx+si*dy,-si*dx+co*dy
    k=xp*xp/(rx*rx)+yp*yp/(ry*ry)
    if k>1: rx*=math.sqrt(k); ry*=math.sqrt(k)
    q=max(0,(rx*rx*ry*ry-rx*rx*yp*yp-ry*ry*xp*xp)/(rx*rx*yp*yp+ry*ry*xp*xp))
    factor=(-1 if bool(large)==bool(sweep) else 1)*math.sqrt(q)
    cxp,cyp=factor*rx*yp/ry,-factor*ry*xp/rx
    cx,cy=co*cxp-si*cyp+(x+nx)/2,si*cxp+co*cyp+(y+ny)/2
    a=math.atan2((yp-cyp)/ry,(xp-cxp)/rx)
    end=math.atan2((-yp-cyp)/ry,(-xp-cxp)/rx)
    delta=(end-a)%(2*math.pi)
    if not sweep: delta-=2*math.pi
    steps=max(2,math.ceil(abs(delta)*max(rx,ry)/0.5))
    return [(cx+co*rx*math.cos(a+delta*j/steps)-si*ry*math.sin(a+delta*j/steps),cy+si*rx*math.cos(a+delta*j/steps)+co*ry*math.sin(a+delta*j/steps)) for j in range(1,steps+1)]
def path(data):
    tokens=re.findall(r'[A-Za-z]|[-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?',data)
    contours=[]; points=[]; x=y=0; start=(0,0); i=0; cmd=None; last_control=(0,0); previous=None
    while i<len(tokens):
        if tokens[i].isalpha(): cmd=tokens[i]; i+=1
        c=cmd.upper(); rel=cmd.islower()
        if c=='Z':
            points.append(start); contours.append(points); points=[]; x,y=start; cmd=None; continue
        n={'M':2,'L':2,'H':1,'V':1,'A':7,'C':6,'Q':4,'S':4}[c]
        v=list(map(float,tokens[i:i+n])); i+=n
        if c in ('M','L'):
            nx,ny=v; nx+=x if rel else 0; ny+=y if rel else 0
            if c=='M':
                if points: contours.append(points)
                points=[]; start=(nx,ny); cmd='l' if rel else 'L'
            elif not points: points=[(x,y)]
            points.append((nx,ny))
        elif c in ('H','V'):
            nx,ny=(v[0]+(x if rel else 0),y) if c=='H' else (x,v[0]+(y if rel else 0))
            if not points: points=[(x,y)]
            points.append((nx,ny))
        elif c=='A':
            rx,ry,rot,large,sweep,nx,ny=v; nx+=x if rel else 0; ny+=y if rel else 0
            points+=arc(x,y,rx,ry,rot,large,sweep,nx,ny)
        else:
            if c=='S':
                cx,cy=(2*x-last_control[0],2*y-last_control[1]) if previous in ('C','S') else (x,y)
                v=[cx-(x if rel else 0),cy-(y if rel else 0)]+v
                c='C'
            coords=[(v[j]+(x if rel else 0),v[j+1]+(y if rel else 0)) for j in range(0,len(v),2)]
            nx,ny=coords[-1]; last_control=coords[-2]
            for j in range(1,13):
                t=j/12; u=1-t
                if c=='C':
                    (ax,ay),(bx,by),_=coords
                    points.append((u**3*x+3*u*u*t*ax+3*u*t*t*bx+t**3*nx,u**3*y+3*u*u*t*ay+3*u*t*t*by+t**3*ny))
                else:
                    (ax,ay),_=coords
                    points.append((u*u*x+2*u*t*ax+t*t*nx,u*u*y+2*u*t*ay+t*t*ny))
        x,y=nx,ny; previous=c
    if points: contours.append(points)
    return contours
lines=['/* Static vector contours transcribed from the supplied index.html. */','#include "dial_internal.h"','typedef struct IconContour { unsigned short offset, count; } IconContour;']
allpoints=[]; contours=[]; ranges=[]
for name in order:
    first=len(contours)
    for el in ET.fromstring('<svg>'+icons[name]+'</svg>'):
        a=el.attrib
        if el.tag=='path': paths=path(a['d'])
        elif el.tag=='circle':
            cx,cy,r=map(float,[a['cx'],a['cy'],a['r']]); paths=[[(cx+r*math.cos(j*math.pi/24),cy+r*math.sin(j*math.pi/24)) for j in range(49)]]
        elif el.tag=='line': paths=[[(float(a['x1']),float(a['y1'])),(float(a['x2']),float(a['y2']))]]
        else:
            v=list(map(float,re.findall(r'[-+]?[\d.]+',a['points']))); pts=list(zip(v[::2],v[1::2])); paths=[pts+[pts[0]] if el.tag=='polygon' else pts]
        for pts in paths:
            contours.append((len(allpoints),len(pts))); allpoints+=pts
    if len(contours) == first:
        raise ValueError(f'Icon {name!r} has no drawable contours')
    ranges.append((first,len(contours)-first))
lines.append('static const DialPoint points[] = {')
for j in range(0,len(allpoints),4): lines.append('    '+', '.join('{%.4ff, %.4ff}'%p for p in allpoints[j:j+4])+',')
lines+=['};','static const IconContour contours[] = {']
for j in range(0,len(contours),6): lines.append('    '+', '.join('{%d, %d}'%p for p in contours[j:j+6])+',')
lines+=['};','static const IconContour icons[] = {','    '+', '.join('{%d, %d}'%p for p in ranges),'};', '''
void dial_icon(Dial *d, int icon, float x, float y, float size, uint32_t color) {
    if (icon < 0 || icon >= (int)(sizeof(icons) / sizeof(icons[0]))) return;
    IconContour range = icons[icon];
    for (int i = 0; i < range.count; ++i) {
        IconContour contour = contours[range.offset + i];
        DialPoint transformed[512];
        if (contour.count > 512) continue;
        for (int j = 0; j < contour.count; ++j) {
            DialPoint p = points[contour.offset + j];
            transformed[j] = (DialPoint){x + (p.x - 12) * size / 24,
                y + (p.y - 12) * size / 24};
        }
        dial_stroke(d, transformed, contour.count, 1.95f * size / 24, color, true);
    }
}
''']
assert max(n for _, n in contours) <= 512
args.output.write_text('\n'.join(lines),encoding='utf-8')
print('Generated',len(allpoints),'icon points;',max(n for _,n in contours),'max contour points')
