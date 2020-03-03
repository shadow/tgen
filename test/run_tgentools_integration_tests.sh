source build/toolsenv/bin/activate
tgentools parse -p build build/tgen.client-web.log
tgentools plot --counter-cdfs --prefix build/test -d build/tgen.analysis.json.xz test
