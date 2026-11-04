"""Compile the delivered sketches against fake I/O and execute native tests."""
import argparse,importlib.util,json,pathlib,re,shutil,subprocess,sys

def generated_source(source):
    prototypes=re.findall(r'^((?:void|bool|int|uint\w+|const __FlashStringHelper \*|const char \*)\s*\w+\([^\n]*?\))\s*\{',source,re.M)
    pos=source.find('// ============================================================================')
    if pos<0: pos=list(re.finditer(r'^#include[^\n]*\n',source,re.M))[-1].end()
    return '#include <Arduino.h>\n'+source[:pos]+'\n'.join(p+';' for p in prototypes)+'\n'+source[pos:]

def run(args):
    subprocess.run([str(a) for a in args],check=True)

def main():
    default=pathlib.Path(__file__).resolve().parents[1]
    p=argparse.ArgumentParser();p.add_argument('--root',type=pathlib.Path,default=default)
    p.add_argument('--tests',type=pathlib.Path);p.add_argument('--build',type=pathlib.Path)
    p.add_argument('--compiler');p.add_argument('--zig',action='store_true')
    p.add_argument('--extra',action='append',default=[]);a=p.parse_args()
    root=a.root.resolve();tests=(a.tests or root/'tests').resolve();build=(a.build or root/'build/native-tests').resolve()
    build.mkdir(parents=True,exist_ok=True)
    compiler=a.compiler or shutil.which('clang++') or shutil.which('g++')
    if not compiler: raise SystemExit('Use --compiler <clang++, g++, or zig> (and --zig for Zig).')
    command=[compiler]+(['c++'] if a.zig else [])
    for stem,harness,generated in [('AirPurifier','firmware_tests','FirmwareUnderTest'),('UiEepromProgrammer','programmer_tests','ProgrammerUnderTest')]:
        sketch=root/stem
        unit=generated_source((sketch/(stem+'.ino')).read_text(encoding='utf-8-sig'))
        unit+='\n'+''.join('#include "'+p.name+'"\n' for p in sorted(sketch.glob('*.cpp')))
        (build/(generated+'.cpp')).write_text(unit)
        exe=build/(harness+('.exe' if sys.platform=='win32' else ''))
        run([*command,'-std=c++17','-O0','-g0',*a.extra,'-Wno-nullability-completeness','-I'+str(tests/'fakes'),'-I'+str(sketch),'-I'+str(build),tests/(harness+'.cpp'),'-o',exe])
        run([exe,*([root/'ui-assets.bin'] if stem=='AirPurifier' else [])])
    generator=root/'tools/generate_ui_assets.py'
    if not generator.exists(): generator=pathlib.Path(__file__).with_name('generate_ui_assets.py')
    spec=importlib.util.spec_from_file_location('assets',generator);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    texts=json.loads((root/'ui-texts.json').read_text())
    image,checksum=module.make_image(texts)
    assert image==(root/'ui-assets.bin').read_bytes()
    assert f'0x{checksum:04X}' in (root/'AirPurifier/UiAssets.h').read_text()
    assert len(image)+256<=4096
    for invalid in [[],['a'*32],['bad\0text'],['é'],['x']*120]:
        try: module.make_image(invalid)
        except (ValueError,UnicodeError): pass
        else: raise AssertionError('Invalid catalogue accepted')
    assert len(module.make_image(['x']*119)[0])+256<=4096
    print('PASS: asset generation, capacity, format and checksum tests')

if __name__=='__main__': main()
