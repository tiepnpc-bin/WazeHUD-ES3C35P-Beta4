from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 binary=Path(tmp)/'hud_host_tests'
 args=['g++','-std=c++17','-fsanitize=address,undefined','-g',
       '-I'+str(root/'tests/host_stubs'),'-I'+str(root/'main/hud'),'-I'+str(root/'tests/vendor/cJSON'),
       str(root/'tests/hud_host_tests.cpp'),str(root/'main/hud/protocol/hlp_core.c'),
       str(root/'main/hud/protocol/hlp_decoder.cpp'),str(root/'tests/vendor/cJSON/cJSON.c'),'-o',str(binary)]
 subprocess.run(args,check=True)
 env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=0'
 # LeakSanitizer's /proc walker is unavailable in the hosted container;
 # AddressSanitizer and UndefinedBehaviorSanitizer remain active.
 subprocess.run([str(binary)],check=True,env=env)
