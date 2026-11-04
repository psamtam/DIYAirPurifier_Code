"""Count production-renderer bus operations with fake I/O; no device is accessed."""
import argparse,json,pathlib,re,shutil,subprocess,sys
def preprocess(source):
    protos=re.findall(r'^((?:void|bool|int|uint\w+|const __FlashStringHelper \*|const char \*)\s*\w+\([^\n]*?\))\s*\{',source,re.M)
    pos=source.find('// ============================================================================')
    if pos<0:pos=list(re.finditer(r'^#include[^\n]*\n',source,re.M))[-1].end()
    return '#include <Arduino.h>\n'+source[:pos]+'\n'.join(p+';' for p in protos)+'\n'+source[pos:]
def main():
    p=argparse.ArgumentParser();p.add_argument('--root',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[1]);p.add_argument('--tests',type=pathlib.Path)
    p.add_argument('--build',type=pathlib.Path);p.add_argument('--compiler',required=True);p.add_argument('--zig',action='store_true')
    p.add_argument('--compare',action='append',required=True,help='name=sketch-directory');p.add_argument('--output',type=pathlib.Path)
    p.add_argument('--extra',action='append',default=[])
    a=p.parse_args();root=a.root.resolve();tests=(a.tests or root/'tests').resolve();build=(a.build or root/'build/display-bench').resolve();build.mkdir(parents=True,exist_ok=True)
    results={}
    for item in a.compare:
        label,directory=item.split('=',1);sketch=pathlib.Path(directory).resolve();unit=build/label;unit.mkdir(exist_ok=True)
        source=preprocess((sketch/'AirPurifier.ino').read_text())
        source+='\n'+''.join('#include "'+p.name+'"\n' for p in sorted(sketch.glob('*.cpp')))
        (unit/'FirmwareUnderTest.cpp').write_text(source)
        harness=unit/('bench_'+label+'.cpp');shutil.copyfile(tests/'display_bench.cpp',harness)
        exe=unit/('bench_'+label+('.exe' if sys.platform=='win32' else ''))
        command=[a.compiler]+(['c++'] if a.zig else [])
        subprocess.run([*command,'-std=c++17','-O0','-g0',*a.extra,'-Wno-nullability-completeness','-I'+str(tests/'fakes'),'-I'+str(sketch),'-I'+str(unit),str(harness),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe),str(root/'ui-assets.bin')],check=True,text=True,capture_output=True)
        results[label]=json.loads(result.stdout)
    text=json.dumps(results,indent=2);print(text)
    if a.output:a.output.write_text(text+'\n')
if __name__=='__main__':main()
