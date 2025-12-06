# SkyStrip Interpretation Guide

This FAQ explains how to read the various HSV-based views of the
`usermod_v2_skystrip` module. Each view maps weather data onto hue,
saturation, and value (brightness) along the LED strip.


## Cloud View (CV)

Markers for sunrise or sunset show as orange pixels. During
precipitation, hue denotes type—deep blue for rain, lavender for snow,
and indigo for mixed—while value scales with probability. In the
absence of precipitation, hue differentiates day from night: daylight
clouds appear pale yellow, nighttime clouds desaturate toward
white. Cloud coverage now dithers along forecast time using a
triangle-wave mask: the central band of each wave lights up, and its
width matches the cloud fraction. Sparse clouds show as occasional
dots, roughly half cover alternates on/off bands, and 100% cover stays
solid. Adjust band spacing with the `CloudWaveHalfPx` setting
(half-cycle in pixels; default 2.2). Thus, a bright blue pixel highlights
likely rain, whereas a striped soft yellow glow marks daytime cloud
cover.


## Wind View (WV)

The hue encodes wind direction around the compass: blue (240°) points
north, orange (~30°) east, yellow (~60°) south, and green (~120°)
west, with intermediate shades for diagonal winds. Saturation rises
with gustiness—calm breezes stay washed out while strong gusts drive
the color toward full intensity. Value scales with wind strength,
boosting brightness as the highest of sustained speed or gust
approaches 50 mph (or equivalent). For example, a saturated blue pixel
indicates gusty north winds, while a dim pastel green suggests a
gentle westerly breeze.

The mapping between wind direction and hue can be approximated as:

| Direction | Hue (°) | Color  |
|-----------|---------|--------|
| N         | 240     | Blue   |
| NE        | 300     | Purple |
| E         | 30      | Orange |
| SE        | 45      | Gold   |
| S         | 60      | Yellow |
| SW        | 90      | Lime   |
| W         | 120     | Green  |
| NW        | 180     | Cyan   |
| N         | 240     | Blue   |

Note: Hues wrap at 360°, so “N” repeats at the boundary.


## Temperature View (TV)

Hue comes from a configurable `ColorMap` string of `center:hue` pairs
separated by `|` (hue is 0–359 degrees or a name: `magenta, purple, blue, cyan, green, yellow, orange, red`). Saturation still tracks dew‑point spread (muggy = desaturated, dry = vivid), value is fixed mid‑brightness, time markers dim pixels briefly at 3‑hour intervals, and hue is linearly interpolated between centers.

Default 15 °F rotation (with short wraps at the ends): `-30:yellow|-15:orange|0:red|15:magenta|30:purple|45:blue|60:cyan|75:green|90:yellow|105:orange|120:red|135:magenta|150:purple|165:blue`. The palette wraps instead of clamping at extremes.

Primary rotation reference:

| Center (°F) | Hue name | Hue (°) |
|-------------|----------|---------|
| -30         | yellow   | 60      |
| -15         | orange   | 30      |
| 0           | red      | 0       |
| 15          | magenta  | 300     |
| 30          | purple   | 275     |
| 45          | blue     | 220     |
| 60          | cyan     | 185     |
| 75          | green    | 130     |
| 90          | yellow   | 60      |
| 105         | orange   | 30      |
| 120         | red      | 0       |
| 135         | magenta  | 300     |
| 150         | purple   | 275     |
| 165         | blue     | 220     |


## 24-Hour Delta View (DV)

Shows how much warmer or colder it is compared to the same time
yesterday. Small changes stay dark so quiet days don’t flicker.
Humidity changes are ignored.

Default thresholds are 5 / 10 / 15 °F (configurable with
`DeltaThresholds`). Colors run from cold on the left to warm on the
right, with “no change” in the middle:

| Change vs 24h prior  | Strip color | Brightness  |
|----------------------|-------------|-------------|
| More than 15° colder | Purple      | Very Strong |
| 10–15° colder        | Indigo      | Strong      |
| 5–10° colder         | Cyan-blue   | Medium      |
| Less than 5° change  | Off (blank) | Off         |
| 5–10° warmer         | Yellow      | Medium      |
| 10–15° warmer        | Orange      | Strong      |
| More than 15° warmer | Red         | Very Strong |


## Test Pattern View (TP)

This diagnostic view simply interpolates hue, saturation, and value
between configured start and end points along the segment. Hue shifts
steadily from the starting hue to the ending hue, with saturation and
brightness following the same linear ramp. It carries no weather
meaning; a common example is a gradient from black to white to verify
LED orientation.
