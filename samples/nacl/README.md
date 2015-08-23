Start by building the nacl-target of the fletch vm:

    ninja -C out/ReleaseNacl fletch-nacl-all

The resulting executable must then get finalized:

   <NaCl-sdk>/bin/pnacl-finalize out/ReleaseNacl/fletch-nacl-all

Finally copy the result into this directory:

    cp out/ReleaseNacl/fletch-nacl-all samples/nacl/nacl.pexe

Start a google-chrome from the command-line so that you can see printfs:

    google-chrome --user-data-dir=/tmp/nacl_chrome

In a different console, start a web server serving the sample:

    cd samples/nacl && python -m SimpleHTTPServer

Connect to the page:

    http://localhost:8000

Watch the printf in the console where chrome was launched.
