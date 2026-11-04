"""Reproduce Arduino AVR 1.8.8 Leonardo compile/link flags without CLI discovery."""
import argparse, hashlib, json, pathlib, re, subprocess
ROOT = pathlib.Path(__file__).resolve().parents[1]
TC = ROOT / 'toolchain/data/packages/arduino'
BIN = TC / 'tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin'
PLATFORM = TC / 'hardware/avr/1.8.8'
CORE = PLATFORM / 'cores/arduino'
WIRE = PLATFORM / 'libraries/Wire/src'
LCD = ROOT / 'toolchain/user/libraries/LiquidCrystal_I2C'

def run(args):
    p = subprocess.run([str(a) for a in args], capture_output=True, text=True)
    if p.stderr: print(p.stderr, end='')
    if p.returncode: raise RuntimeError('Command failed: ' + ' '.join(map(str,args)))
    return p.stdout

def build(sketch, out, extra):
    sketch, out = pathlib.Path(sketch).resolve(), pathlib.Path(out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    includes = [CORE, PLATFORM/'variants/leonardo', WIRE, LCD, sketch]
    common = ['-mmcu=atmega32u4', '-DF_CPU=16000000L', '-DARDUINO=10607',
              '-DARDUINO_AVR_LEONARDO', '-DARDUINO_ARCH_AVR', '-DUSB_VID=0x2341',
              '-DUSB_PID=0x8036', '-DUSB_MANUFACTURER="Unknown"', '-DUSB_PRODUCT="Arduino Leonardo"']
    common += ['-I'+str(p) for p in includes]
    flags = ['-Os','-g','-ffunction-sections','-fdata-sections','-flto'] + extra
    cache = ROOT / 'build' / ('core-' + hashlib.sha256(str(flags).encode()).hexdigest()[:10])
    cache.mkdir(parents=True,exist_ok=True)
    objects=[]
    sources = sorted(CORE.glob('*.c')) + sorted(CORE.glob('*.cpp')) + sorted(CORE.glob('*.S'))
    sources += sorted(WIRE.rglob('*.c')) + sorted(WIRE.rglob('*.cpp')) + sorted(LCD.glob('*.cpp'))
    for src in sources:
        obj=cache/(src.name+'.o')
        cpp=src.suffix=='.cpp'
        lang = ['-std=gnu++11','-fpermissive','-fno-exceptions','-fno-threadsafe-statics','-Wno-error=narrowing'] if cpp else (['-x','assembler-with-cpp'] if src.suffix=='.S' else ['-std=gnu11','-fno-fat-lto-objects'])
        if not obj.exists(): run([BIN/('avr-g++.exe' if cpp else 'avr-gcc.exe'),'-c',*flags,*lang,*common,src,'-o',obj])
        objects.append(obj)
    archive=cache/'core.a'
    run([BIN/'avr-gcc-ar.exe','rcs',archive,*objects])
    stem=sketch.name
    source=(sketch/(stem+'.ino')).read_text(encoding='utf-8-sig')
    # Equivalent to Arduino's generated forward declarations for this sketch.
    prototypes=re.findall(r'^((?:void|bool|int|uint\w+|const __FlashStringHelper \*|const char \*)\s*\w+\([^\n]*?\))\s*\{', source, re.M)
    insertion=source.find('// ============================================================================')
    if insertion<0: insertion=list(re.finditer(r'^#include[^\n]*\n',source,re.M))[-1].end()
    generated=out/(stem+'.cpp')
    generated.write_text('#include <Arduino.h>\n'+source[:insertion]+'\n'.join(p+';' for p in prototypes)+'\n'+source[insertion:],encoding='utf-8')
    obj=out/(stem+'.o')
    run([BIN/'avr-g++.exe','-c',*flags,'-std=gnu++11','-fpermissive','-fno-exceptions','-fno-threadsafe-statics','-Wno-error=narrowing','-Wall','-Wextra',*common,generated,'-o',obj])
    sketch_objects=[obj]
    for cpp in sorted(sketch.glob('*.cpp')):
        cpp_obj=out/(cpp.stem+'.o')
        run([BIN/'avr-g++.exe','-c',*flags,'-std=gnu++11','-fpermissive','-fno-exceptions','-fno-threadsafe-statics','-Wno-error=narrowing','-Wall','-Wextra',*common,cpp,'-o',cpp_obj])
        sketch_objects.append(cpp_obj)
    elf=out/(stem+'.elf')
    run([BIN/'avr-gcc.exe',*flags,'-fuse-linker-plugin','-Wl,--gc-sections','-Wl,-Map='+str(out/(stem+'.map')),'-mmcu=atmega32u4','-o',elf,*sketch_objects,archive,'-L'+str(out),'-lm'])
    run([BIN/'avr-objcopy.exe','-O','ihex','-R','.eeprom',elf,out/(stem+'.hex')])
    sizes=run([BIN/'avr-size.exe','-A',elf]); print(sizes)
    sections={name:int(size) for name,size in re.findall(r'^(\.\w+)\s+(\d+)',sizes,re.M)}
    summary={'flash':sections.get('.text',0)+sections.get('.data',0),'ram':sum(sections.get(x,0) for x in ['.data','.bss','.noinit']), 'flash_available':28672,'ram_available':2560,'flags':flags}
    (out/'size.json').write_text(json.dumps(summary,indent=2))
    (out/'symbols.txt').write_text(run([BIN/'avr-nm.exe','--size-sort','--print-size','--demangle',elf]))
    print(json.dumps(summary))
    return summary

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('sketch');p.add_argument('out');p.add_argument('--extra',action='append',default=[])
    p.add_argument('--arduino-data',type=pathlib.Path);p.add_argument('--lcd-library',type=pathlib.Path)
    p.add_argument('--max-flash',type=int,default=28672)
    a=p.parse_args()
    if a.arduino_data:
        TC=a.arduino_data.resolve()/'packages/arduino'
        BIN=TC/'tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin'
        PLATFORM=TC/'hardware/avr/1.8.8';CORE=PLATFORM/'cores/arduino';WIRE=PLATFORM/'libraries/Wire/src'
    if a.lcd_library: LCD=a.lcd_library.resolve()
    summary=build(a.sketch,a.out,a.extra)
    if summary['flash']>a.max_flash: raise SystemExit('Flash budget exceeded')
    if summary['ram']>2560: raise SystemExit('Static RAM exceeds board capacity')
