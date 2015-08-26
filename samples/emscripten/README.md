Start by building the nacl-target of the fletch vm:

    ninja -C out/ReleaseEmscripten fletch-emscripten-all.asm.js

Copy the result into this directory:

    cp out/ReleaseEmscripten/fletch-emscripten-all.asm.js samples/emscripten/fletch.asm.js
    cp out/ReleaseEmscripten/pthread-main.js samples/emscripten/pthread-main.js

Start a nightly firefox:

    ./firefox-bin -profile /tmp/fire_profile -no-remote -new-instance

Currently that's it. It won't work and if you abort the script it will be caught somewhere
inside pthread.
