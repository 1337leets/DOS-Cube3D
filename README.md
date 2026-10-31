# DOS Cube3D

A shaded 3D cube that bounces around the screen in VGA mode 13h, written in C for 16-bit MS-DOS and built with Open Watcom. I ran it under MS-DOS in a Windows 3.11 virtual machine.

![The cube in bounce mode](images/cube.png)

## How It Works

The program draws into a 64000-byte back buffer in far memory and copies it to video memory at `A000:0000` after each vertical retrace, so frames do not flicker. The palette holds 16 brightness levels for each of the six face colors.

Each frame rotates the eight corners around three axes and projects them to the screen. Back faces are skipped, and each visible face is split into two filled triangles and given a brightness from the angle between its normal and a fixed light.

The cube collides with the screen edges using the real bounds of its projected corners, not a fixed radius, so it touches the edge exactly as it appears at any rotation or distance. The depth axis has fixed world limits, because the screen has no edge in that direction.

## Modes

| Mode | Gravity | Restitution | Indicator |
|------|---------|-------------|-----------|
| Bounce | -7.5 | 0.90 | Red square |
| Float | 0 | 1.00 | Cyan square |

## Controls

| Key | Action |
|-----|--------|
| Space or M | Switch mode |
| Esc or Q | Quit |

## Build and Run

Run `cube3d.exe` under MS-DOS or in DOSBox. To rebuild it with Open Watcom:

```
wcl -bt=dos -ml -3 -fp3 -ox cube3d.c
```

## License

MIT
