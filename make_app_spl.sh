gcc -g -Wall -D_REENTRANT -D_GNU_SOURCE -fPIC -DAST_MODULE=\"app_vbmixmonitor\" -c -o app_vbmixmonitor.o app_vbmixmonitor.c -lcurl
gcc -shared -o app_vbmixmonitor.so -Xlinker app_vbmixmonitor.o -lcurl
