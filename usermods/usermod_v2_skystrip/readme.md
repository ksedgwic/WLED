# SkyStrip

This usermod displays the weather forecast on several parallel LED strips.
It currently includes Temperature, Wind, and Cloud views. Cloud view shades
day/night colors by cloud cover and blends in precipitation colors scaled by
probability of precipitation.

## Installation

Add the following to your active environment in `platformio_override.ini`:
```
custom_usermods = usermod_v2_skystrip
```
