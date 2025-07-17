# generate_compile_commands.py
import os
Import("env")
from pathlib import Path
env.Replace(COMPILATIONDB_INCLUDE_TOOLCHAIN=True)
env.Append( CPPPATH=[str(Path(env["PROJECT_DIR"])/".pio/libdeps/bartdepart/ESPAsyncUDP/src")] )
env.Append( CPPPATH=[str(Path(env["PROJECT_DIR"])/"lib/ESP8266PWM/src")] )
env.Append( CPPPATH=[str(Path(env["PROJECT_DIR"])/".pio/libdeps/bartdepart/FastLED/src")] )
env.Append( CPPPATH=[str(Path(env["PROJECT_DIR"])/".pio/libdeps/bartdepart/IRremoteESP8266/src")] )
env.Append( CPPPATH=[str(Path(env["PROJECT_DIR"])/".pio/libdeps/bartdepart/NeoPixelBus/src")] )
env.Append( CPPPATH=[str(Path(env["PROJECT_DIR"])/".pio/libdeps/bartdepart/ESPAsyncWebServerWLED/src")] )
env.Append( CPPPATH=[str(Path(env["PROJECT_DIR"])/".pio/libdeps/bartdepart/QuickEspNow/src")] )
env.Append( CPPPATH=[str(Path(env["PROJECT_DIR"])/"usermods/usermod_v2_bart_depart")] )
env.Append( CPPPATH=[str(Path(env["PROJECT_DIR"])/"wled00")] )
env.Append( CPPPATH=[str("/home/user/.platformio/packages/framework-arduinoespressif8266/cores/esp8266")] )
env.Append( CPPPATH=[str("/home/user/.platformio/packages/framework-arduinoespressif8266/tools/sdk/include")] )
env.Append( CPPPATH=[str("/home/user/.platformio/packages/framework-arduinoespressif8266/libraries/ESP8266WiFi/src")] )
env.Replace(COMPILATIONDB_PATH=os.path.join("$BUILD_DIR", "compile_commands.json"))
