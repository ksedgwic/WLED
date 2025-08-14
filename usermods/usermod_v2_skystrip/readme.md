# SkyStrip

This usermod displays the weather forecast on several parallel LED strips.
It currently includes Temperature, Wind, Delta, Cloud, and TestPattern views.
Cloud view shades day/night colors by cloud cover and blends in precipitation colors scaled by
 probability of precipitation. It also marks sunrise and sunset. Temperature view
 adds orientation cues with black markers at local midnight and local noon across
 the two-day timeline, blending them across adjacent pixels for sub-pixel
 precision.
 Delta view visualizes temperature changes over the
previous 24 hours with brightness scaling from dark when neither temperature nor
humidity changes to full intensity for large shifts. Color saturation still
indicates drying or moistening air.

## Installation

Add the following to your active environment in `platformio_override.ini`:
```
custom_usermods = usermod_v2_skystrip
```
