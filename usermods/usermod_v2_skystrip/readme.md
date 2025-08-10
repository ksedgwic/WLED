# SkyStrip

This usermod displays the weather forecast on several parallel LED strips.
It currently includes Temperature, Wind, Delta, and Cloud views. Cloud view shades
day/night colors by cloud cover and blends in precipitation colors scaled by
probability of precipitation. Delta view visualizes temperature changes over the
previous 24 hours with brightness scaling from dark when neither temperature nor
humidity changes to full intensity for large shifts. Color saturation still
indicates drying or moistening air.

## Installation

Add the following to your active environment in `platformio_override.ini`:
```
custom_usermods = usermod_v2_skystrip
```
