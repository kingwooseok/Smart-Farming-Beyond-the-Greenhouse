"""Build the four Uno sketches using an existing Arduino AVR core and toolchain."""
import argparse
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--avr-bin', type=pathlib.Path, required=True)
parser.add_argument('--arduino-core', type=pathlib.Path, required=True)
parser.add_argument('--variant', type=pathlib.Path, required=True)
parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'build')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)

def tool(name):
    return str(args.avr_bin / (name + ('.exe' if __import__('os').name == 'nt' else '')))

flags = ['-Os', '-Wall', '-Wextra', '-mmcu=atmega328p', '-DF_CPU=16000000UL',
         '-DARDUINO=10819', '-DARDUINO_AVR_UNO', '-DARDUINO_ARCH_AVR',
         '-ffunction-sections', '-fdata-sections', '-I' + str(args.arduino_core),
         '-I' + str(args.variant)]
core_object = args.output / 'wiring.o'
subprocess.run([tool('avr-gcc'), *flags, '-c', str(args.arduino_core / 'wiring.c'),
                '-o', str(core_object)], check=True)

for sketch in sorted((ROOT / 'firmware').glob('*/*.ino')):
    text = sketch.read_text(encoding='utf-8')
    # Equivalent to the Arduino sketch preprocessor for these ordinary functions.
    prototypes = re.findall(r'^((?:bool|void|uint16_t)\s+\w+\([^)]*\))\s*\{', text, re.M)
    cpp = args.output / (sketch.stem + '.cpp')
    cpp.write_text('#include <Arduino.h>\n' + '\n'.join(p + ';' for p in prototypes) +
                   '\n' + text + '\nint main() { init(); setup(); for (;;) loop(); }\n', encoding='utf-8')
    elf = args.output / (sketch.stem + '.elf')
    subprocess.run([tool('avr-g++'), *flags, '-std=gnu++11', '-fno-exceptions',
                    '-I' + str(sketch.parent), str(cpp), str(core_object),
                    '-Wl,--gc-sections', '-lm', '-o', str(elf)], check=True)
    subprocess.run([tool('avr-objcopy'), '-O', 'ihex', '-R', '.eeprom', str(elf),
                    str(elf.with_suffix('.hex'))], check=True)
    subprocess.run([tool('avr-size'), str(elf)], check=True)

test = ROOT / 'tests' / 'control_checks.cpp'
if test.exists():
    subprocess.run([tool('avr-g++'), *flags, '-std=gnu++11', '-c', str(test),
                    '-o', str(args.output / 'control_checks.o')], check=True)
print('All four sketches compiled and linked successfully.')
