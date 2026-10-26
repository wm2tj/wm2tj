#!/usr/bin/env python3
# Multi-monitor test of wm2tj + wm2tj-bar on a dummy Xorg (DISPLAY=:8).
# usage: run.py <wm2tj> <wm2tj-bar>
import os, re, signal, subprocess, sys, time

WM, BAR = sys.argv[1], sys.argv[2]
ENV = dict(os.environ, DISPLAY=':8')
T = os.path.dirname(os.path.abspath(__file__))
fails = []

def sh(cmd, check=False):
    r = subprocess.run(cmd, shell=True, env=ENV, capture_output=True, text=True)
    return r.stdout

def monitors():
    out = []
    for line in sh('xrandr --listmonitors').splitlines()[1:]:
        m = re.match(r'\s*\d+: ([+*]*)(\S+) (\d+)/\d+x(\d+)/\d+\+(\d+)\+(\d+)', line)
        if m:
            out.append(dict(primary='*' in m.group(1), name=m.group(2),
                            w=int(m.group(3)), h=int(m.group(4)),
                            x=int(m.group(5)), y=int(m.group(6))))
    return out

def layout(fb, mons):
    for m in monitors():
        sh(f"xrandr --delmonitor '{m['name']}'")
    sh(f'xrandr --fb {fb}')
    for i, (name, w, h, x, y, prim) in enumerate(mons):
        out = 'DUMMY0' if i == 0 else 'none'
        sh(f"xrandr --setmonitor '{'*' if prim else ''}{name}' {w}/300x{h}/200+{x}+{y} {out}")
    time.sleep(1.0)

def bars():
    out = []
    for line in sh('xwininfo -root -tree').splitlines():
        if '"wm2tj-bar": ("wm2tj-bar"' in line:
            m = re.search(r'(0x[0-9a-f]+).*\s(\d+)x(\d+)\+(-?\d+)\+(-?\d+)\s', line)
            if m:
                out.append(dict(id=m.group(1), w=int(m.group(2)), h=int(m.group(3)),
                                x=int(m.group(4)), y=int(m.group(5))))
    return sorted(out, key=lambda b: (b['y'], b['x']))

def geom(wid):
    o = sh(f'xwininfo -id {wid}')
    g = lambda k: int(re.search(k + r':\s+(-?\d+)', o).group(1))
    return dict(x=g('Absolute upper-left X'), y=g('Absolute upper-left Y'),
                w=g('Width'), h=g('Height'))

def check(cond, what):
    print(('  ok   ' if cond else '  FAIL ') + what)
    if not cond: fails.append(what)

def inside(r, m):
    return (r['x'] >= m['x'] and r['y'] >= m['y'] and
            r['x'] + r['w'] <= m['x'] + m['w'] and r['y'] + r['h'] <= m['y'] + m['h'])

def newxlogo():
    before = set(sh('xdotool search --class xlogo').split())
    p = subprocess.Popen(['xlogo', '-geometry', '400x300'], env=ENV)
    for _ in range(50):
        time.sleep(0.1)
        now = set(sh('xdotool search --onlyvisible --class xlogo').split()) - before
        if now:
            time.sleep(0.3)
            return p, now.pop()
    return p, None

def windowTests(mons, barOn):
    for m in mons:
        cx, cy = m['x'] + m['w'] // 2, m['y'] + m['h'] // 2
        sh(f'xdotool mousemove {cx} {cy}')
        p, wid = newxlogo()
        if not wid:
            check(False, f"{m['name']}: xlogo appeared"); p.kill(); continue
        g = geom(wid)
        check(inside(g, m), f"{m['name']}: new window on the pointer's monitor {g}")
        sh(f'{T}/netstate {wid} add MAXIMIZED_VERT MAXIMIZED_HORZ')
        time.sleep(0.5)
        g = geom(wid)
        top = m['y'] + (34 if m['name'] in barOn else 0)
        check(inside(g, m) and g['y'] >= top and g['w'] > m['w'] * 0.9 and g['h'] > m['h'] * 0.8,
              f"{m['name']}: maximized within the monitor, below its bar {g}")
        p.kill(); time.sleep(0.3)

def start(cfg):
    os.makedirs(f'{T}/home/.config/wm2tj-bar', exist_ok=True)
    open(f'{T}/home/.config/wm2tj-bar/config', 'w').write(cfg + '\n')
    env = dict(ENV, HOME=f'{T}/home', XDG_CONFIG_HOME=f'{T}/home/.config')
    wm = subprocess.Popen([WM], env=env, stderr=subprocess.DEVNULL)
    time.sleep(1.0)
    bar = subprocess.Popen([BAR], env=env, stderr=subprocess.DEVNULL)
    time.sleep(2.0)
    return wm, bar

def stop(*ps):
    for p in ps:
        p.terminate()
    for p in ps:
        try: p.wait(3)
        except Exception: p.kill()
    time.sleep(0.5)

LAYOUTS = {
    # name: (framebuffer, [(name, w, h, x, y, primary)])
    'two stacked (dual-screen laptop), primary below': ('2256x3008',
        [('U', 2256, 1504, 0, 0, False), ('L', 2256, 1504, 0, 1504, True)]),
    'three side by side, mixed sizes, primary in the middle': ('5760x1440',
        [('A', 1920, 1080, 0, 0, False), ('B', 2560, 1440, 1920, 0, True),
         ('C', 1280, 1024, 4480, 0, False)]),
    'L-shape (dead corner top right), primary bottom right': ('3840x2160',
        [('TL', 1920, 1080, 0, 0, False), ('BL', 1920, 1080, 0, 1080, False),
         ('BR', 1920, 1080, 1920, 1080, True)]),
    '2x2 grid, no primary': ('3840x2160',
        [('Q1', 1920, 1080, 0, 0, False), ('Q2', 1920, 1080, 1920, 0, False),
         ('Q3', 1920, 1080, 0, 1080, False), ('Q4', 1920, 1080, 1920, 1080, False)]),
    'portrait next to landscape, bottoms aligned': ('3000x1920',
        [('P', 1080, 1920, 0, 0, True), ('W', 1920, 1080, 1080, 840, False)]),
    'mirrored (two monitors, same place)': ('1920x1080',
        [('M1', 1920, 1080, 0, 0, True), ('M2', 1920, 1080, 0, 0, False)]),
}

for title, (fb, mons) in LAYOUTS.items():
    layout(fb, mons)
    ms = monitors()
    names = {m['name'] for m in ms}
    prim = [m for m in ms if m['primary']]
    for cfg in ('monitors primary', 'monitors all'):
        print(f'== {title} [{cfg}]  ' + ' '.join(f"{m['name']}{'*' if m['primary'] else ''}={m['w']}x{m['h']}+{m['x']}+{m['y']}" for m in ms))
        wm, bar = start(cfg)
        bs = bars()
        uniq = {(m['x'], m['y'], m['w'], m['h']) for m in ms}
        if cfg == 'monitors all':
            want = sorted(uniq, key=lambda r: (r[1], r[0]))
        else:
            h = prim[0] if prim else ms[0]
            want = [(h['x'], h['y'], h['w'], h['h'])]
        got = [(b['x'], b['y'], b['w']) for b in bs]
        check(sorted(got) == sorted((x, y, w) for x, y, w, _ in want),
              f'bars at the top of {[(x, y, w) for x, y, w, _ in want]}: got {got}')
        barOn = {m['name'] for m in ms if (m['x'], m['y'], m['w']) in got}
        windowTests([m for m in ms if (m['x'], m['y'], m['w'], m['h']) in uniq and
                     m is next(n for n in ms if (n['x'], n['y'], n['w'], n['h']) == (m['x'], m['y'], m['w'], m['h']))], barOn)
        stop(bar, wm)

# hotplug: bar and wm2tj follow a monitor change while running
print('== hotplug: two side by side, then a third added below')
layout('3840x2160', [('A', 1920, 1080, 0, 0, True), ('B', 1920, 1080, 1920, 0, False)])
wm, bar = start('monitors all')
check(len(bars()) == 2, f'two bars: {[(b["x"], b["y"]) for b in bars()]}')
sh("xrandr --setmonitor C 1920/300x1080/200+0+1080 none"); time.sleep(1.5)
check(sorted((b['x'], b['y']) for b in bars()) == [(0, 0), (0, 1080), (1920, 0)],
      f'three bars after adding C: {[(b["x"], b["y"]) for b in bars()]}')
windowTests([m for m in monitors() if m['name'] == 'C'], {'C'})
stop(bar, wm)

# rotated while running: two portrait panels side by side become two
# landscape ones stacked. wm2tj applies the bar's new width after the bar has
# asked for it, so the bar's shape must follow too, or the right end (clock,
# status icons) stays cut off at the old width. On a real machine wm2tj is
# often that slow; here it is stopped while the monitors change, to be sure.
print('== rotated: two portrait side by side, then landscape stacked')
layout('2400x1920', [('R1', 1200, 1920, 0, 0, True), ('R2', 1200, 1920, 1200, 0, False)])
wm, bar = start('monitors primary')
wm.send_signal(signal.SIGSTOP)
layout('1920x2400', [('R1', 1920, 1200, 0, 0, True), ('R2', 1920, 1200, 0, 1200, False)])
wm.send_signal(signal.SIGCONT)
time.sleep(1.5)
bs = bars()
check([(b['x'], b['y'], b['w']) for b in bs] == [(0, 0, 1920)],
      f'bar across the rotated primary: {[(b["x"], b["y"], b["w"]) for b in bs]}')
for b in bs:
    m = re.search(r'Window shape extents:\s+(\d+)x', sh(f"xwininfo -shape -id {b['id']}"))
    check(m is not None and int(m.group(1)) == b['w'],
          f"bar's shape as wide as the bar: {m.group(1) if m else 'none'} of {b['w']}")
stop(bar, wm)

# side panels: a dock down the left of the middle monitor (no strut can say
# that), one at the screen's right edge and one at the bottom (with struts)
print('== side panels: three side by side, docks left of B (no strut), right of C, bottom of A')
layout('5760x1080', [('A', 1920, 1080, 0, 0, False), ('B', 1920, 1080, 1920, 0, True),
                     ('C', 1920, 1080, 3840, 0, False)])
wm, bar = start('monitors primary')
docks = [subprocess.Popen([f'{T}/dock', '1920', '34', '60', '1046'], env=ENV),
         subprocess.Popen([f'{T}/dock', '5700', '0', '60', '1080',
                           '0', '60', '0', '0', '0', '0', '0', '1079', '0', '0', '0', '0'], env=ENV),
         subprocess.Popen([f'{T}/dock', '0', '1040', '1920', '40',
                           '0', '0', '0', '40', '0', '0', '0', '0', '0', '0', '0', '1919'], env=ENV)]
time.sleep(1.0)
for name, test in (('B', lambda g: g['x'] >= 1980 and g['y'] >= 34),
                   ('C', lambda g: g['x'] + g['w'] <= 5700 and g['y'] < 34),
                   ('A', lambda g: g['y'] + g['h'] <= 1040 and g['y'] < 34)):
    m = next(m for m in monitors() if m['name'] == name)
    sh(f"xdotool mousemove {m['x'] + 900} {m['y'] + 500}")
    p, wid = newxlogo()
    sh(f'{T}/netstate {wid} add MAXIMIZED_VERT MAXIMIZED_HORZ'); time.sleep(0.5)
    g = geom(wid)
    check(inside(g, m) and test(g) and g['w'] > 1700, f'{name}: maximized clear of its panels {g}')
    p.kill(); time.sleep(0.3)
for d in docks: d.kill()
stop(bar, wm)

# a window dragged against the outer left / right edge of the whole picture
# goes to the desktop on that side (edge_flip_delay, 500 ms); the seam between
# two monitors side by side only moves it to the other monitor
print('== edge flip: two side by side, 3 desktops; a window held at the seam and at the outer edges')
layout('3840x1080', [('A', 1920, 1080, 0, 0, True), ('B', 1920, 1080, 1920, 0, False)])
os.makedirs(f'{T}/home', exist_ok=True)

def num(cmd):
    m = re.search(r'= (\d+)', sh(cmd))
    return int(m.group(1)) if m else None

def drag(wid, tx, ty, hold):
    sh(f'xdotool windowmove {wid} 900 400')
    for _ in range(20):                         # wait until wm2tj has moved it
        time.sleep(0.1)
        g = geom(wid)
        if abs(g['x'] - 900) < 100: break
    x0, y0 = g['x'] - 12, g['y'] + 30            # on the tab
    sh(f'xdotool mousemove {x0} {y0} mousedown 1'); time.sleep(0.2)
    for f in (0.5, 1.0):
        sh(f'xdotool mousemove {int(x0 + (tx - x0) * f)} {int(y0 + (ty - y0) * f)}')
        time.sleep(0.05)
    time.sleep(hold)
    sh('xdotool mouseup 1'); time.sleep(0.5)

for rc, flips in (('desktops 3', True), ('desktops 3\nedge_flip_delay 0', False),
                  ('desktops 1\ndynamic_desktops on', 'dynamic')):
    open(f'{T}/home/.wm2rc', 'w').write(rc + '\n')
    wm, bar = start('monitors primary')
    sh('xdotool mousemove 900 500')
    p, wid = newxlogo()
    cur = lambda: num('xprop -root _NET_CURRENT_DESKTOP')
    wdesk = lambda: num(f'xprop -id {wid} _NET_WM_DESKTOP')
    shown = lambda: wid in sh('xdotool search --onlyvisible --class xlogo').split()
    if not flips:
        drag(wid, 3839, 500, 1.0)
        check(cur() == 0 and wdesk() == 0, f'edge_flip_delay 0: no flip ({cur()}, {wdesk()})')
    elif flips == 'dynamic':
        # another window stays behind on desktop 0, so a desktop is added after
        p2, wid2 = newxlogo()
        drag(wid, 3839, 500, 0.8)
        n = num('xprop -root _NET_NUMBER_OF_DESKTOPS')
        check(cur() == 1 and wdesk() == 1 and shown() and n == 3,
              f'dynamic_desktops: onto the empty desktop 1, an empty one added ({cur()}, {wdesk()}, {n} desktops)')
        p2.kill()
    else:
        drag(wid, 3839, 500, 0.2)
        check(cur() == 0 and wdesk() == 0, f'a short touch of the outer edge: no flip ({cur()}, {wdesk()})')
        drag(wid, 1919, 500, 1.0)
        check(cur() == 0 and wdesk() == 0 and geom(wid)['x'] >= 1900,
              f'held at the seam: still desktop 0, on monitor B ({cur()}, {wdesk()}, x {geom(wid)["x"]})')
        drag(wid, 3839, 500, 0.8)
        check(cur() == 1 and wdesk() == 1 and shown(), f'outer right edge: window and view on desktop 1 ({cur()}, {wdesk()})')
        drag(wid, 3839, 500, 1.3)
        check(cur() == 2 and wdesk() == 2 and shown(), f'held longer: on to desktop 2, the last ({cur()}, {wdesk()})')
        drag(wid, 0, 500, 0.8)
        check(cur() == 1 and wdesk() == 1 and shown(), f'outer left edge: back to desktop 1 ({cur()}, {wdesk()})')
        drag(wid, 0, 500, 1.3)
        check(cur() == 0 and wdesk() == 0 and shown(), f'held at the left on desktop 0: no wrapping ({cur()}, {wdesk()})')
    p.kill(); time.sleep(0.3)
    stop(bar, wm)
os.remove(f'{T}/home/.wm2rc')

print()
print('FAILED: %d' % len(fails) if fails else 'ALL PASSED')
for f in fails: print('  - ' + f)
